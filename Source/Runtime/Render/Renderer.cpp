#include "EnginePCH.h"
#include "Renderer.h"
#include "Shader.h"
#include "Mesh.h"

#include "Core/EngineTimer.h"
#include "Core/Stats/LightweightStats.h"

#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/SceneManagement.h"

#include "RenderCommand.h"
#include "RenderResourceManager.h"

#include "Camera/CameraComponent.h"
#include "Asset/AssetManager.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <bit>

DECLARE_CYCLE_STAT("Draw Render Packets", STAT_DrawRenderPackets);
DECLARE_CYCLE_STAT("Render Queue Sorting", STAT_RenderQueueSorting);
DECLARE_CYCLE_STAT("Upload Per-Object CB", STAT_UploadPerObjectCB);
DECLARE_CYCLE_STAT("Shadow Depth Pass", STAT_ShadowDepthPass);

namespace
{
	// VSSetConstantBuffers1의 오프셋은 16개 상수(256바이트) 단위여야 하므로 오브젝트마다 256바이트 칸을 쓴다.
	constexpr uint32 PerObjectSlotConstants = 16;
	static_assert(sizeof(FPerObjectConstants) <= ObjectSlotBytes);

	constexpr uint32 MinPerObjectSlots = 1024;

	const FMatrix& GetPacketWorld(const FRenderPacket& Packet)
	{
		if (Packet.Proxy) return Packet.Proxy->GetLocalToWorld();
		return Packet.Model ? *Packet.Model : FMatrix::Identity;
	}

	uint64 MakeSortKey(const FRenderPacket& Packet)
	{
		if (Packet.Material->BlendState != EBlendState::Opaque)
		{
			const uint32 DistanceBits = std::bit_cast<uint32>(Packet.CameraToParticleDistance);
			return (1ull << 63) | static_cast<uint64>(~DistanceBits);
		}

		return (static_cast<uint64>(Packet.Material->SortID) << 47)
			| (static_cast<uint64>(Packet.Mesh->SortID) << 31)
			| (static_cast<uint64>(Packet.LODIndex & 0x3) << 29);
	}

	// 불투명 패킷과 같은 규칙의 키 (묶음은 전부 불투명)
	uint64 MakeGroupKey(const FStaticDrawGroup& Group)
	{
		return (static_cast<uint64>(Group.Material->SortID) << 47)
			| (static_cast<uint64>(Group.Mesh->SortID) << 31)
			| (static_cast<uint64>(Group.LODIndex & 0x3) << 29);
	}

	void ExtractDepthRange(const FMatrix& P, float& OutNear, float& OutFar, bool& bOutOrtho)
	{
		const float ZScale = P.M[0][2];
		const float ZOffset = P.M[3][2];
		bOutOrtho = P.M[0][3] < 0.5f;                 // 원근: w = X (M[0][3] = 1), 직교: w = 1
		OutNear = -ZOffset / ZScale;
		OutFar = bOutOrtho ? OutNear + 1.0f / ZScale
			: ZOffset / (1.0f - ZScale);
	}
}

bool FRenderer::Init()
{
	bUsePerObjectSlots = RenderCommand::SupportsConstantBufferOffsets();
	PerObjectCB = RenderCommand::CreateConstantBuffer(sizeof(FPerObjectConstants));
	ViewCB = RenderCommand::CreateConstantBuffer(sizeof(FViewConstants));
	ShadowViewCB = RenderCommand::CreateConstantBuffer(sizeof(FViewConstants));
	// FireBallLightCB = RenderCommand::CreateConstantBuffer(sizeof(FFireBallLightConstants));
	LightingCB = RenderCommand::CreateConstantBuffer(sizeof(FLightingConstants));
	LightCullingCB = RenderCommand::CreateConstantBuffer(sizeof(FLightCullingConstants));
	GridLightingCB = RenderCommand::CreateConstantBuffer(sizeof(FLightGridConstants));
	GridLightingFallbackCB = RenderCommand::CreateConstantBuffer(sizeof(FLightGridConstants));

	LocalLightBuffer = RenderCommand::CreateDynamicStructuredBuffer(sizeof(FLightRenderParameters), MaxLocalLights);
	ShadowDataBuffer = RenderCommand::CreateDynamicStructuredBuffer(sizeof(FShadowShaderData), MaxSpotShadowSlices);
	const FLightGridConstants GridConstants{};
	RenderCommand::UpdateBufferData(GridLightingCB.get(), &GridConstants, sizeof(FLightGridConstants));
	RenderCommand::UpdateBufferData(GridLightingFallbackCB.get(), &GridConstants, sizeof(FLightGridConstants));

	GPUOcclusion.Init();   // 실패해도 오클루전만 못 쓸 뿐 렌더링은 된다

	return true;
}

// 필요한 칸 수가 용량을 넘을 때만 두 배씩 키워 재할당을 드물게 한다.
void FRenderer::EnsurePerObjectSlotCapacity(uint32 SlotCount)
{
	if (SlotCount <= PerObjectSlotCapacity)
		return;

	uint32 NewCapacity = std::max(PerObjectSlotCapacity * 2, MinPerObjectSlots);
	while (NewCapacity < SlotCount)
		NewCapacity *= 2;

	PerObjectSlotCB = RenderCommand::CreateConstantBuffer(NewCapacity * ObjectSlotBytes);
	if (!PerObjectSlotCB || !PerObjectSlotCB->GetBuffer())
	{
		HTR_LOG(Warning, "Per-object constant buffer ({} slots) creation failed. Falling back to per-draw updates.", NewCapacity);
		PerObjectSlotCB.reset();
		PerObjectSlotCapacity = 0;
		return;
	}
	PerObjectSlotCapacity = NewCapacity;
}

// 원래 패킷 순서로 World 행렬을 올린다. 정렬 목록은 원래 패킷 번호의 칸을 바인딩한다.
void FRenderer::UploadPerObjectConstants()
{
	SCOPE_CYCLE_COUNTER(STAT_UploadPerObjectCB);

	const uint32 Count = static_cast<uint32>(RenderPackets.size());
	if (!bUsePerObjectSlots || Count == 0)
		return;

	EnsurePerObjectSlotCapacity(Count);
	if (!PerObjectSlotCB)
	{
		bUsePerObjectSlots = false;
		return;
	}

	uint8* Dest = static_cast<uint8*>(RenderCommand::MapWriteDiscard(PerObjectSlotCB.get()));
	if (!Dest)
	{
		bUsePerObjectSlots = false;
		return;
	}

	// 매핑된 메모리는 write-combined라 순차 쓰기만 하고 읽지 않는다.
	for (uint32 Index = 0; Index < Count; ++Index)
	{
		FRenderPacket& P = RenderPackets[Index];
		const FMatrix& Model = GetPacketWorld(P);
		const FMatrix WorldToLocal = P.Proxy ? P.Proxy->GetWorldToLocal() : Model.Inverse();
		WritePerObjectConstants(Dest + static_cast<size_t>(Index) * ObjectSlotBytes, Model, WorldToLocal);
		P.Slot = Index;
	}

	RenderCommand::Unmap(PerObjectSlotCB.get());
}

// 카메라의 ViewProjection을 공통 렌더 경로로 전달한다.
void FRenderer::RenderAll(FRenderQueue& InQueue, UCameraComponent* CameraComponent)
{
	RenderAll(InQueue, CameraComponent->GetViewProjectionMatrix(), CameraComponent->GetWorldLocation());
}

// 불투명 우선·반투명 거리순으로 정렬해 View 행렬과 Section 범위로 그린다.
void FRenderer::RenderAll(FRenderQueue& InQueue, const FMatrix& ViewProjection, const FVector& CameraWorldPos)
{
	RenderQueueSorting(InQueue, ViewProjection, CameraWorldPos);
	RenderOpaque(ViewProjection);
	RenderTranslucent(ViewProjection);
}

void FRenderer::RenderOpaque(const FMatrix& ViewProjection)
{
	RenderCommand::BindConstantBuffer(4, LightingCB.get(), EShaderBindFlagBits::Pixel | EShaderBindFlagBits::Vertex);
	RenderCommand::BindShaderResource(10, LocalLightBuffer.get(), EShaderBindFlagBits::Pixel | EShaderBindFlagBits::Vertex);
	RenderCommand::BindShaderResource(12, TileIndexBuffer.get(), EShaderBindFlagBits::Pixel);
	RenderCommand::BindConstantBuffer(5, GridLightingCB.get(), EShaderBindFlagBits::Pixel | EShaderBindFlagBits::Vertex);
	BindShadowResources();

	DrawStaticGroups();
	DrawPackets(0, FirstTranslucentIndex, ViewProjection);
}

// RenderOpaque가 남긴 반투명 패킷을 먼 것부터 그린다.
void FRenderer::RenderTranslucent(const FMatrix& ViewProjection)
{
	RenderCommand::BindConstantBuffer(4, LightingCB.get(), EShaderBindFlagBits::Pixel | EShaderBindFlagBits::Vertex);
	RenderCommand::BindShaderResource(10, LocalLightBuffer.get(), EShaderBindFlagBits::Pixel | EShaderBindFlagBits::Vertex);
	RenderCommand::BindShaderResource(12, TileIndexBuffer.get(), EShaderBindFlagBits::Pixel);
	// 2.5D 목록은 불투명 깊이 범위만 담으므로, 불투명보다 앞에 있는 반투명은 전체 순회로 그린다.
	FConstantBuffer* GridCB = LightCullingMode == ELightCullingMode::Tiled25D ? GridLightingFallbackCB.get() : GridLightingCB.get();
	RenderCommand::BindConstantBuffer(5, GridCB, EShaderBindFlagBits::Pixel | EShaderBindFlagBits::Vertex);
	BindShadowResources();

	DrawPackets(FirstTranslucentIndex, SortEntries.Num(), ViewProjection);
	RenderPackets.Reset();
	FirstTranslucentIndex = 0;
	StaticGroups.clear();   // 묶음 메모리는 World 것이므로 이번 프레임이 끝나면 놓는다
}

// 스태틱 메시 묶음을 정렬 키 순서로 그린다. 바인딩은 묶음마다 한 번, 항목마다는 칸 바인딩과 드로우만 한다.
void FRenderer::DrawStaticGroups()
{
	if (StaticGroups.empty())
		return;

	SCOPE_CYCLE_COUNTER(STAT_DrawRenderPackets);
	RenderCommand::BindConstantBuffer(0, ViewCB.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);

	UMaterial* BoundMaterial = nullptr;
	for (const FStaticDrawGroup* Group : StaticGroups)
	{
		RenderCommand::BindMesh(Group->Mesh, Group->LODIndex);
		if (Group->Material != BoundMaterial)
		{
			BoundMaterial = Group->Material;
			BindMaterial(BoundMaterial);
			FRenderPacket MaterialOnly;           // 머티리얼 파라미터 갱신은 패킷을 받으므로 머티리얼만 채워 넘긴다
			MaterialOnly.Material = BoundMaterial;
			UpdateMaterialParams(MaterialOnly);
		}

		for (const FStaticDrawItem& Item : Group->Items)
		{
			if (bUsePerObjectSlots && Item.Slot != InvalidObjectSlot)
			{
				RenderCommand::BindConstantBufferRange(2, PerObjectSlotCB.get(), Item.Slot * PerObjectSlotConstants, PerObjectSlotConstants, EShaderBindFlagBits::Vertex);
			}
			else
			{
				RenderCommand::BindConstantBuffer(2, PerObjectCB.get(), EShaderBindFlagBits::Vertex);
				UpdatePerObjectConstants(Item.Proxy->GetLocalToWorld());
			}
			RenderCommand::DrawIndexed(Item.IndexCount, Item.StartIndex);
		}
	}

	// 뒤따르는 DrawPackets가 처음부터 다시 바인딩하도록 기록을 비운다.
	LastMesh = nullptr;
	LastMaterial = nullptr;
}

void FRenderer::RenderQueueSorting(FRenderQueue& InQueue, const FMatrix& ViewProjection, const FVector& CameraWorldPos)
{
	FViewConstants ViewConstants;
	ViewConstants.ViewProjection = ViewProjection;
	ViewConstants.CameraWorldPos = CameraWorldPos;
	RenderCommand::UpdateBufferData(ViewCB.get(), &ViewConstants);
	{
		SCOPE_CYCLE_COUNTER(STAT_RenderQueueSorting);

		std::swap(RenderPackets, InQueue);
		InQueue.Reset();

		// ① 패킷을 한 번 훑으며 키를 만든다. 그릴 수 없는 패킷은 목록에 넣지 않는다.
		SortEntries.Reset();
		SortEntries.Reserve(RenderPackets.Num());
		for (uint32 i = 0; i < RenderPackets.Num(); ++i)
		{
			const FRenderPacket& P = RenderPackets[i];
			if (!P.Mesh || !P.Material) continue;
			SortEntries.Add({ MakeSortKey(P), i });
		}

		// ② 16B 항목만 정렬
		std::sort(SortEntries.begin(), SortEntries.end(),
			[](const FSortEntry& A, const FSortEntry& B) { return A.Key < B.Key; });

		// ③ 반투명 시작 위치 = 최상위 비트가 처음 1인 곳
		FirstTranslucentIndex = 0;
		while (FirstTranslucentIndex < SortEntries.Num() && !(SortEntries[FirstTranslucentIndex].Key >> 63))
			++FirstTranslucentIndex;

		// ④ 스태틱 메시 묶음: 빈 묶음을 빼고 키 순으로 정렬한다. 조각마다 같은 키 묶음이 있으므로 정렬하면 서로 붙는다.
		std::erase_if(StaticGroups, [](const FStaticDrawGroup* Group) { return Group->Items.empty(); });
		std::sort(StaticGroups.begin(), StaticGroups.end(),
			[](const FStaticDrawGroup* A, const FStaticDrawGroup* B) { return MakeGroupKey(*A) < MakeGroupKey(*B); });
	}
	// Gather uploads group and packet slots together. Other queues need an upload here.
	if (!bObjectConstantsPrepared)
		UploadPerObjectConstants();
	bObjectConstantsPrepared = false;
}

void FRenderer::RenderDepth(const FMatrix& ViewProjection)
{
	// 불투명 두 경로(스태틱 묶음, 일반 패킷)를 PS 없이 그린다. 반투명은 깊이를 쓰지 않으므로 뺀다.
	bDepthOnly = true;
	DrawStaticGroups();
	DrawPackets(0, FirstTranslucentIndex, ViewProjection);
	bDepthOnly = false;

	// 본 패스가 머티리얼을 처음부터 다시 바인딩해 PS가 돌아오게 한다.
	LastMesh = nullptr;
	LastMaterial = nullptr;
}

// 정렬된 패킷 중 [Begin, End) 범위를 View 행렬과 Section 범위로 그린다.
void FRenderer::DrawPackets(uint32 Begin, uint32 End, const FMatrix& ViewProjection)
{
	SCOPE_CYCLE_COUNTER(STAT_DrawRenderPackets);

	LastMesh = nullptr;
	LastMaterial = nullptr;
	uint8 LastLODIndex = 0;

	RenderCommand::BindConstantBuffer(0, ViewCB.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);


	for (uint32 k = Begin; k < End; ++k)          // k = 정렬된 위치
	{
		const uint32 PacketIndex = SortEntries[k].PacketIndex;
		const FRenderPacket& RenderPacket = RenderPackets[PacketIndex];
		//if (RenderPacket.Mesh == nullptr || RenderPacket.Material == nullptr) continue;
		if (RenderPacket.Mesh != LastMesh || RenderPacket.LODIndex != LastLODIndex) {
			RenderCommand::BindMesh(RenderPacket.Mesh, RenderPacket.LODIndex);
		}
		if (RenderPacket.Material != LastMaterial) {
			BindMaterial(RenderPacket.Material);
		}
		if (RenderPacket.Material != LastMaterial || RenderPacket.MaterialParamData)
			UpdateMaterialParams(RenderPacket);
		if (bUsePerObjectSlots && RenderPacket.Slot != InvalidObjectSlot)
		{
			RenderCommand::BindConstantBufferRange(2, PerObjectSlotCB.get(), RenderPacket.Slot * PerObjectSlotConstants, PerObjectSlotConstants, EShaderBindFlagBits::Vertex);
		}
		else
		{
			RenderCommand::BindConstantBuffer(2, PerObjectCB.get(), EShaderBindFlagBits::Vertex);
			UpdatePerObjectConstants(RenderPacket, ViewProjection);
		}

		RenderCommand::DrawIndexed(
			RenderPacket.IndexCount ? RenderPacket.IndexCount : RenderPacket.Mesh->GetIndexBuffer(RenderPacket.LODIndex)->GetIndexCount(),
			RenderPacket.StartIndex
		);
		LastMaterial = RenderPacket.Material;
		LastMesh = RenderPacket.Mesh;
		LastLODIndex = RenderPacket.LODIndex;
	}
}

FOcclusionMeasureResult FRenderer::MeasureOpaqueOcclusion(const FMatrix& ViewProjection)
{
	FOcclusionMeasureResult Result;

	// 불투명 드로우를 두 경로(스태틱 묶음, 일반 패킷)에서 한 목록으로 모은다. 그린 순서와 같게 묶음 먼저.
	struct FMeasureDraw
	{
		UStaticMesh* Mesh;
		UMaterial* Material;
		uint8 LODIndex;
		uint32 Slot;
		uint32 StartIndex;
		uint32 IndexCount;
		const FMatrix* World;       // 칸이 없을 때만 쓴다
		const void* ObjectKey;      // 같은 물체의 섹션을 한 물체로 센다
		bool bOccludedByGpu;
		const FRenderPacket* Packet;
	};
	std::vector<FMeasureDraw> Draws;
	for (const FStaticDrawGroup* Group : StaticGroups)
		for (const FStaticDrawItem& Item : Group->Items)
			Draws.push_back({ Group->Mesh, Group->Material, Group->LODIndex, Item.Slot, Item.StartIndex, Item.IndexCount,
				&Item.Proxy->GetLocalToWorld(), Item.Proxy, Item.bOccludedByGpu != 0, nullptr });
	for (uint32 k = 0; k < FirstTranslucentIndex; ++k)
	{
		const FRenderPacket& Packet = RenderPackets[SortEntries[k].PacketIndex];
		const uint32 IndexCount = Packet.IndexCount ? Packet.IndexCount : Packet.Mesh->GetIndexBuffer(Packet.LODIndex)->GetIndexCount();
		// 프록시가 없는 패킷(빌보드 등)은 패킷 자체를 한 물체로 센다.
		const void* Key = Packet.Proxy ? static_cast<const void*>(Packet.Proxy) : static_cast<const void*>(&Packet);
		Draws.push_back({ Packet.Mesh, Packet.Material, Packet.LODIndex, Packet.Slot, Packet.StartIndex, IndexCount,
			&GetPacketWorld(Packet), Key, Packet.bOccludedByGpu, &Packet });
	}

	const uint32 Count = static_cast<uint32>(Draws.size());
	if (Count == 0)
		return Result;

	const auto StartTime = std::chrono::high_resolution_clock::now();

	ID3D11Device* Device = RenderCommand::GetDevice();
	ID3D11DeviceContext* Context = RenderCommand::GetContext();

	// 같은 셰이더·같은 행렬로 다시 그리면 깊이가 비트 단위로 같으므로,
	// LESS_EQUAL이면 최종 깊이 버퍼에서 이 물체가 이긴 픽셀만 통과한다.
	if (!DepthLessEqualReadOnly)
	{
		D3D11_DEPTH_STENCIL_DESC Desc{};
		Desc.DepthEnable = TRUE;
		Desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		Desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		if (FAILED(Device->CreateDepthStencilState(&Desc, DepthLessEqualReadOnly.GetAddressOf())))
			return Result;
	}

	while (OcclusionQueries.Num() < Count)
	{
		const D3D11_QUERY_DESC QueryDesc{ D3D11_QUERY_OCCLUSION, 0 };
		ComPtr<ID3D11Query> Query;
		if (FAILED(Device->CreateQuery(&QueryDesc, Query.GetAddressOf())))
			return Result;
		OcclusionQueries.Add(std::move(Query));
	}

	RenderCommand::BindConstantBuffer(0, ViewCB.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	if (!bUsePerObjectSlots)
		RenderCommand::BindConstantBuffer(2, PerObjectCB.get(), EShaderBindFlagBits::Vertex);

	UStaticMesh* BoundMesh = nullptr;
	UMaterial* BoundMaterial = nullptr;
	uint8 BoundLOD = 0;

	// 1) 드로우마다 쿼리를 걸고 다시 그린다. 본 패스와 같은 바인딩(칸 또는 PerObjectCB)을 쓴다.
	for (uint32 Index = 0; Index < Count; ++Index)
	{
		const FMeasureDraw& Draw = Draws[Index];

		if (Draw.Mesh != BoundMesh || Draw.LODIndex != BoundLOD)
			RenderCommand::BindMesh(Draw.Mesh, Draw.LODIndex);
		if (Draw.Material != BoundMaterial)
		{
			BindMaterial(Draw.Material);
			// BindMaterial이 바꾼 상태를 측정용으로 덮어쓴다.
			RenderCommand::SetBlendState(EBlendState::NoColorWrite);
			Context->OMSetDepthStencilState(DepthLessEqualReadOnly.Get(), 0);
		}
		if (Draw.Packet && (Draw.Material != BoundMaterial || Draw.Packet->MaterialParamData))
			UpdateMaterialParams(*Draw.Packet);

		if (bUsePerObjectSlots && Draw.Slot != InvalidObjectSlot)
			RenderCommand::BindConstantBufferRange(2, PerObjectSlotCB.get(),
				Draw.Slot * PerObjectSlotConstants, PerObjectSlotConstants, EShaderBindFlagBits::Vertex);
		else
		{
			RenderCommand::BindConstantBuffer(2, PerObjectCB.get(), EShaderBindFlagBits::Vertex);
			UpdatePerObjectConstants(*Draw.World);
		}

		ID3D11Query* Query = OcclusionQueries[Index].Get();
		Context->Begin(Query);
		RenderCommand::DrawIndexed(Draw.IndexCount, Draw.StartIndex);
		Context->End(Query);

		BoundMesh = Draw.Mesh;
		BoundMaterial = Draw.Material;
		BoundLOD = Draw.LODIndex;
	}

	// 2) 결과를 기다려 모은다. 한 물체가 Section 여러 개로 나뉘면 하나라도 보이면 보이는 것으로 친다.
	std::unordered_map<const void*, bool> ObjectVisible;
	ObjectVisible.reserve(Count);
	for (uint32 Index = 0; Index < Count; ++Index)
	{
		const FMeasureDraw& Draw = Draws[Index];

		UINT64 Samples = 0;
		while (Context->GetData(OcclusionQueries[Index].Get(), &Samples, sizeof(Samples), 0) == S_FALSE) {}

		const uint64 Triangles = Draw.IndexCount / 3;
		const bool bVisible = Samples > 0;

		// Cull을 끄고 판정만 한 드로우: 가렸다고 했는데 최종 화면에 픽셀이 남았으면 잘못 가린 것이다.
		if (Draw.bOccludedByGpu)
		{
			++Result.GPUOccludedDraws;
			if (bVisible)
				++Result.FalseCulls;
		}

		++Result.TotalDraws;
		Result.TotalTriangles += Triangles;
		if (bVisible)
		{
			++Result.VisibleDraws;
			Result.VisibleTriangles += Triangles;
		}

		bool& bObjectVisible = ObjectVisible[Draw.ObjectKey];
		bObjectVisible = bObjectVisible || bVisible;
	}

	Result.TotalObjects = static_cast<uint32>(ObjectVisible.size());
	for (const auto& [Key, bVisible] : ObjectVisible)
		Result.VisibleObjects += bVisible ? 1 : 0;

	// 뒤따르는 Grid·반투명 패스를 위해 상태를 되돌린다.
	RenderCommand::SetBlendState(EBlendState::Opaque);
	RenderCommand::SetDepthStencilState(EDepthStencilState::Default);

	Result.ElapsedMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - StartTime).count();
	Result.bValid = true;
	return Result;
}

uint8* FRenderer::BeginObjectConstants(uint32 MaxSlots)
{
	bObjectConstantsPrepared = false;
	if (!bUsePerObjectSlots || MaxSlots == 0) return nullptr;
	EnsurePerObjectSlotCapacity(MaxSlots);
	if (!PerObjectSlotCB) { bUsePerObjectSlots = false; return nullptr; }
	return static_cast<uint8*>(RenderCommand::MapWriteDiscard(PerObjectSlotCB.get()));
}

void FRenderer::EndObjectConstants()
{
	if (PerObjectSlotCB)
	{
		RenderCommand::Unmap(PerObjectSlotCB.get());
		bObjectConstantsPrepared = true;
	}
}

// Material마다 Shader/Texture/Sampler/State 꽂기
void FRenderer::BindMaterial(UMaterial* Material) {
	FShaderProgram* Program{ Material->Shader };
	if (FRenderResourceManager::IsUberLitProgram(Program)) {
		if (FShaderProgram* Variant{ FRenderResourceManager::GetUberLitProgram(CurrentViewMode) }) {
			Program = Variant;
		}
	}

	RenderCommand::BindShaderProgram(Program);
	if (bDepthOnly)
		RenderCommand::GetContext()->PSSetShader(nullptr, nullptr, 0);   // 깊이만 쓰므로 조명 PS를 돌리지 않는다
	RenderCommand::SetBlendState(Material->BlendState);
	// 반투명은 뒤에 그려지는 Grid·다른 반투명을 가리지 않도록 깊이를 쓰지 않는다.
	const bool Translucent{ Material->BlendState != EBlendState::Opaque };
	RenderCommand::SetDepthStencilState(Translucent && Material->DepthStencilState == EDepthStencilState::Default ? EDepthStencilState::ReadOnly : Material->DepthStencilState);

	const int32 TextureCount{ Material->ParamLayout == EMaterialParamLayout::StaticMesh ? std::max(2, Material->Textures.Num()) : Material->Textures.Num() };
	for (int32 Index{}; Index < TextureCount; ++Index) {
		UTexture2D* Texture{ Index < Material->Textures.Num() ? Material->Textures[Index] : nullptr };
		if (!Texture && Material->ParamLayout == EMaterialParamLayout::StaticMesh && Index < 2) {
			Texture = UAssetManager::GetAssetByPath<UTexture2D>(Index == 0 ? "WhiteTexture" : "FlatNormalTexture");
		}
		if (Texture) {
			RenderCommand::BindShaderResource(Index, Texture, EShaderBindFlagBits::Pixel);
		}
	}
	RenderCommand::BindSamplerState(0, Material->SamplerState, EShaderBindFlagBits::Pixel);
}

// b1 내용 채우고 꽂기
void FRenderer::UpdateMaterialParams(const FRenderPacket& RenderPacket)
{
	switch (RenderPacket.Material->ParamLayout)
	{
	case EMaterialParamLayout::StaticMesh:
	{
		//break; // 라이팅 적용 시 제거

		const float TotalTime = EngineTimer::GetTotalTime();

		FStaticMeshMaterialParams Params{};
		Params.BaseColor = RenderPacket.Material->BaseColor;
		Params.UVOffset = RenderPacket.Material->UVScrollSpeed * TotalTime;
		Params.bOpaque = RenderPacket.Material->BlendState == EBlendState::Opaque ? 1.0f : 0.0f;
		Params.Shininess = RenderPacket.Material->Shininess;

		RenderCommand::UpdateBufferData(RenderPacket.Material->ParamBuffer.get(), &Params, sizeof(FStaticMeshMaterialParams));
		RenderCommand::BindConstantBuffer(1, RenderPacket.Material->ParamBuffer.get(), EShaderBindFlagBits::Pixel);
		break;
	}
	case EMaterialParamLayout::ParticleSubUV:
	{
		if (RenderPacket.Material->ParamBuffer && RenderPacket.MaterialParamData != nullptr)
		{
			RenderCommand::UpdateBufferData(RenderPacket.Material->ParamBuffer.get(), RenderPacket.MaterialParamData, RenderPacket.MaterialParamDataSize);
			RenderCommand::BindConstantBuffer(1, RenderPacket.Material->ParamBuffer.get(), EShaderBindFlagBits::Pixel);
		}
		break;
	}
	case EMaterialParamLayout::None:
	{
		break;
	}
	}
}

// b0 MVP 채우고 꽂기
void FRenderer::UpdatePerObjectConstants(const FRenderPacket& RenderPacket, const FMatrix& ViewProjection)
{
	// rp.Transform 과 Camera VP 행렬 곱
	// 행렬곱의 결과 (MVP Matrix) Constant Buffer 업데이트 필요

	UpdatePerObjectConstants(GetPacketWorld(RenderPacket));
}

void FRenderer::UpdatePerObjectConstants(const FMatrix& World)
{
	FPerObjectConstants Constants;

	Constants.World = World;
	Constants.NormalMatrix = World.Inverse().GetTransposed();

	RenderCommand::UpdateBufferData(PerObjectCB.get(), &Constants);
}


// 그림자 요청마다 슬라이스 DSV 하나를 묶고, 라이트 VP로 Caster를 PS 없이 그린다.
void FRenderer::RenderShadowDepths(const TArray<FShadowRequest>& Requests, const TArray<FShadowDrawItem>& Casters)
{
	SCOPE_CYCLE_COUNTER(STAT_ShadowDepthPass);

	if (!bShadowDepthProgramResolved)
	{
		ShadowDepthProgram = FRenderResourceManager::GetShaderProgram("Resources/Shader/ShadowDepth.hlsl");
		bShadowDepthProgramResolved = true;   // 못 찾아도 매 프레임 에러 로그를 남기지 않도록 한 번만 찾는다
	}

	FShadowMapArray* ShadowMaps = ShadowMapManager.GetSpotShadowMaps();
	if (!ShadowMaps || !ShadowDepthProgram || !ShadowViewCB || Requests.IsEmpty())
		return;

	// ① 같은 리소스가 SRV와 DSV로 동시에 묶이면 D3D가 SRV를 강제로 푼다. 그림자 SRV 슬롯을 먼저 비운다.
	//    6단계부터 Gouraud(VS 조명)를 위해 VS에도 묶으므로 두 스테이지 모두 비운다.
	RenderCommand::UnbindShaderResource(ShadowMapSRVSlot, EShaderBindFlagBits::Pixel | EShaderBindFlagBits::Vertex);

	// ② 파이프라인: 그림자 VS만 쓰고 PS는 끈다 (BindShaderProgram이 PS를 요구해서 빈 mainPS를 바인딩 직후 해제)
	RenderCommand::BindShaderProgram(ShadowDepthProgram);
	RenderCommand::GetContext()->PSSetShader(nullptr, nullptr, 0);
	RenderCommand::SetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	RenderCommand::SetRasterizerState(ERasterizerState::SolidNone);       // 단면 Plane도 그림자를 남기도록. 8단계에서 Bias를 가진 전용 상태로 교체
	RenderCommand::SetDepthStencilState(EDepthStencilState::Default);     // 깊이 쓰기 + LESS
	RenderCommand::SetBlendState(EBlendState::Opaque);
	RenderCommand::BindConstantBuffer(0, ShadowViewCB.get(), EShaderBindFlagBits::Vertex);
	RenderCommand::BindConstantBuffer(2, PerObjectCB.get(), EShaderBindFlagBits::Vertex);

	for (const FShadowRequest& Request : Requests)
	{
		ID3D11DepthStencilView* DSV = ShadowMaps->GetSliceDSV(Request.Allocation.Slice);
		if (!DSV)
			continue;

		// Caster가 0개여도 Clear는 한다. 직전 프레임·다른 월드(PIE)가 그린 깊이가 남아 있기 때문이다.
		const uint32 Resolution = Request.Allocation.Resolution;
		RenderCommand::BeginDepthOnlyPass(DSV, Resolution, Resolution);

		FViewConstants LightView{};
		LightView.ViewProjection = Request.View.ViewProjection;
		RenderCommand::UpdateBufferData(ShadowViewCB.get(), &LightView);

		UStaticMesh* BoundMesh = nullptr;
		uint8 BoundLOD = 0xFF;
		for (uint32 i = 0; i < Request.NumCasters; ++i)
		{
			const FShadowDrawItem& Item = Casters[Request.FirstCaster + i];
			if (Item.Mesh != BoundMesh || Item.LODIndex != BoundLOD)
			{
				RenderCommand::BindMesh(Item.Mesh, Item.LODIndex);
				BoundMesh = Item.Mesh;
				BoundLOD = Item.LODIndex;
			}

			// 프록시가 WorldToLocal을 이미 들고 있어 역행렬 계산 없이 채운다. 셰이더는 World만 읽는다.
			FPerObjectConstants Object;
			WritePerObjectConstants(reinterpret_cast<uint8*>(&Object),
				Item.Proxy->GetLocalToWorld(), Item.Proxy->GetWorldToLocal());
			RenderCommand::UpdateBufferData(PerObjectCB.get(), &Object);

			const uint32 IndexCount = Item.IndexCount ? Item.IndexCount : Item.Mesh->GetIndexBuffer(Item.LODIndex)->GetIndexCount();
			RenderCommand::DrawIndexed(IndexCount, Item.StartIndex);
		}
	}

	// ③ 뒷정리: DSV를 풀어 7단계에서 SRV로 읽을 수 있게 하고, 이후 카메라 패스가 메시·머티리얼을 처음부터 다시 바인딩하게 한다.
	RenderCommand::GetContext()->OMSetRenderTargets(0, nullptr, nullptr);
	LastMesh = nullptr;
	LastMaterial = nullptr;
}

void FRenderer::SetShadowData(const TArray<FShadowRequest>& Requests)
{
	const FShadowMapArray* ShadowMaps = ShadowMapManager.GetSpotShadowMaps();
	if (Requests.IsEmpty() || !ShadowMaps || !ShadowDataBuffer)
		return;   // 모든 ShadowIndex가 -1이라 셰이더가 버퍼를 읽지 않는다

	FShadowShaderData Data[MaxSpotShadowSlices]{};
	const float TexelSize = 1.0f / static_cast<float>(ShadowMaps->GetResolution());
	uint32 Count = 0;
	for (const FShadowRequest& Request : Requests)
	{
		const uint32 Slice = Request.Allocation.Slice;
		if (Slice >= MaxSpotShadowSlices)
			continue;

		FShadowShaderData& Out = Data[Slice];   // 인덱스 = 슬라이스
		Out.LightViewProjection = Request.View.ViewProjection;   // 행 벡터 그대로 (셰이더 row_major + mul(v, M))
		Out.UVScale = Request.Allocation.UVScale;
		Out.DepthBias = Request.DepthBias;
		Out.SlopeBias = Request.SlopeBias;
		Out.TexelSize = TexelSize;
		Count = std::max(Count, Slice + 1);
	}

	// 슬라이스는 0부터 연속으로 배정되므로 앞쪽 Count개만 올리면 된다.
	if (Count > 0)
		RenderCommand::UpdateBufferData(ShadowDataBuffer.get(), Data, Count * sizeof(FShadowShaderData));
}

void FRenderer::BindShadowResources()
{
	// Gouraud는 조명을 VS에서 계산하므로 VS에도 묶는다. 7단계에서 VS 그림자를 쓸지 정한다.
	const EShaderBindFlagBits Stages = EShaderBindFlagBits::Pixel | EShaderBindFlagBits::Vertex;
	FShadowMapArray* ShadowMaps = ShadowMapManager.GetSpotShadowMaps();
	RenderCommand::BindShaderResourceView(ShadowMapSRVSlot, ShadowMaps ? ShadowMaps->GetSRV() : nullptr, Stages);
	RenderCommand::BindShaderResource(ShadowDataSRVSlot, ShadowDataBuffer.get(), Stages);
	RenderCommand::BindSamplerState(ShadowSamplerSlot, ESamplerState::ShadowCompare, Stages);
}

void FRenderer::SetViewMode(EViewMode InViewMode)
{
	if (CurrentViewMode == InViewMode)
		return;
	CurrentViewMode = InViewMode;
	// 같은 머티리얼이라도 셰이더가 바뀌므로 다음 드로우에서 다시 바인딩하게 한다.
	LastMaterial = nullptr;
}

void FRenderer::SetLighting(FLightingConstants& LightingConstants, const TArray<FLightRenderParameters>& InLocalLights)
{
	NumLocalLightsThisFrame = std::min<uint32>(InLocalLights.Num(), LocalLightBuffer->GetMaxCount());
	if (NumLocalLightsThisFrame < static_cast<uint32>(InLocalLights.Num()))
		HTR_LOG(Warning, "Local lights exceed capacity: {} / {}", InLocalLights.Num(), NumLocalLightsThisFrame);

	if (NumLocalLightsThisFrame > 0)
		RenderCommand::UpdateBufferData(LocalLightBuffer.get(), InLocalLights.GetData(),
			NumLocalLightsThisFrame * sizeof(FLightRenderParameters));

	LightingConstants.NumLocalLights = NumLocalLightsThisFrame;
	RenderCommand::UpdateBufferData(LightingCB.get(), &LightingConstants, sizeof(FLightingConstants));

	// CullLights 전에는 NumTilesX = 0으로 두어 PS가 셀 버퍼 대신 전체 순회를 쓰게 한다.
	FLightGridConstants GridConstants{};
	GridConstants.LightDebugMode = LightDebugMode;
	RenderCommand::UpdateBufferData(GridLightingCB.get(), &GridConstants, sizeof(FLightGridConstants));
}

void FRenderer::CullLights(const FMatrix& View, const FMatrix& Projection, uint32 Width, uint32 Height, FTexture2D* DepthTexture)
{
	// 반투명(2.5D)과 컬링 전 경로가 쓰는 전체 순회용 b5. 디버그 모드만 반영한다.
	FLightGridConstants FallbackConstants{};
	FallbackConstants.LightDebugMode = LightDebugMode;
	RenderCommand::UpdateBufferData(GridLightingFallbackCB.get(), &FallbackConstants, sizeof(FLightGridConstants));

	if (Width == 0 || Height == 0)
	{
		RenderCommand::UpdateBufferData(GridLightingCB.get(), &FallbackConstants, sizeof(FLightGridConstants));
		return;
	}

	float NearZ, FarZ; bool bOrtho;
	ExtractDepthRange(Projection, NearZ, FarZ, bOrtho);

	// Tiled는 슬라이스 1개짜리 Clustered로 돌린다. 2.5D는 같은 형식(슬라이스 1개)을 깊이 버퍼로 채우는 별도 CS다.
	// 깊이 텍스처가 없으면 2.5D는 Tiled로 대신한다.
	const bool b25D = LightCullingMode == ELightCullingMode::Tiled25D && DepthTexture && DepthTexture->GetSRV();
	const bool bClustered = LightCullingMode == ELightCullingMode::Clustered;
	const uint32 TileSize = bClustered ? ClusteredTileSize : TiledTileSize;
	const uint32 NumSlices = bClustered ? ClusteredSlices : 1;

	const uint32 NumTilesX = (Width + TileSize - 1) / TileSize;
	const uint32 NumTilesY = (Height + TileSize - 1) / TileSize;
	const uint32 NumCells = NumTilesX * NumTilesY * NumSlices;
	const uint32 Required = NumCells * CellStride;

	RenderCommand::UnbindShaderResource(12, EShaderBindFlagBits::Pixel);
	if (!TileIndexBuffer || TileIndexBuffer->GetMaxCount() < Required) {
		TileIndexBuffer = RenderCommand::CreateRWStructuredBuffer(sizeof(uint32), Required);
	}

	FLightCullingConstants CB{};
	CB.View = View;
	CB.Projection = Projection;
	CB.ViewportSize = FVector2(static_cast<float>(Width), static_cast<float>(Height));
	CB.NumTilesX = NumTilesX;
	CB.NumTilesY = NumTilesY;
	CB.NumLocalLights = NumLocalLightsThisFrame;
	CB.NumSlices = NumSlices;
	CB.TileSize = TileSize;
	CB.bOrthographic = bOrtho ? 1u : 0u;
	CB.NearZ = NearZ;
	CB.FarZ = FarZ;
	RenderCommand::UpdateBufferData(LightCullingCB.get(), &CB, sizeof(FLightCullingConstants));

	FComputeShader* CullingShader = FRenderResourceManager::GetComputeShader(b25D
		? "Resources/Shader/TiledLightCulling25D.hlsl"
		: "Resources/Shader/TileBasedLightCulling.hlsl");
	if (!CullingShader)
	{
		// 셰이더 컴파일 실패 시 셀 버퍼를 쓰지 않고 전체 순회로 그린다.
		RenderCommand::UpdateBufferData(GridLightingCB.get(), &FallbackConstants, sizeof(FLightGridConstants));
		return;
	}

	RenderCommand::BindComputeShader(CullingShader);
	RenderCommand::BindConstantBuffer(0, LightCullingCB.get(), EShaderBindFlagBits::Compute);
	RenderCommand::BindShaderResource(10, LocalLightBuffer.get(), EShaderBindFlagBits::Compute);
	if (b25D)
		RenderCommand::BindShaderResource(11, DepthTexture, EShaderBindFlagBits::Compute);
	RenderCommand::BindUnorderedAccess(0, TileIndexBuffer.get());
	RenderCommand::Dispatch(NumTilesX, NumTilesY, NumSlices);
	RenderCommand::UnbindUnorderedAccess(0);
	RenderCommand::UnbindShaderResource(10, EShaderBindFlagBits::Compute);
	// 깊이 텍스처는 곧 본 패스에서 DSV로 다시 묶이므로 SRV를 풀어 둔다.
	if (b25D)
		RenderCommand::UnbindShaderResource(11, EShaderBindFlagBits::Compute);

	// 원근은 로그 분포라 PS가 Slice = log(z) * SliceScale + SliceBias로 바로 구하게 미리 계산한다.
	FLightGridConstants GridCB{};
	GridCB.NumTilesX = NumTilesX;
	GridCB.NumTilesY = NumTilesY;
	GridCB.NumSlices = NumSlices;
	GridCB.LightDebugMode = LightDebugMode;
	GridCB.NearZ = NearZ;
	GridCB.FarZ = FarZ;
	GridCB.SliceScale = bOrtho ? 0.0f : static_cast<float>(NumSlices) / std::log(FarZ / NearZ);
	GridCB.SliceBias = bOrtho ? 0.0f : -std::log(NearZ) * GridCB.SliceScale;
	GridCB.TileSize = TileSize;
	GridCB.bOrthographic = bOrtho ? 1u : 0u;
	RenderCommand::UpdateBufferData(GridLightingCB.get(), &GridCB, sizeof(FLightGridConstants));
	RenderCommand::BindConstantBuffer(5, GridLightingCB.get(), EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(12, TileIndexBuffer.get(), EShaderBindFlagBits::Pixel);


}

void FRenderer::SetLightDebugMode(uint32 Mode)
{
	LightDebugMode = Mode;
}
