#include "EnginePCH.h"
#include "Renderer.h"
#include "FShadowViewRenderer.h"
#include "ShadowSettings.h"
#include "Shader.h"
#include "Mesh.h"

#include "Core/EngineTimer.h"
#include "Core/Stats/LightweightStats.h"
#include "Core/Stats/ShadowStats.h"
#include "Containers/Set.h"

#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/SceneManagement.h"

#include "RenderCommand.h"
#include "RenderResourceManager.h"

#include "Camera/CameraComponent.h"
#include "Asset/AssetManager.h"

#include "Engine/LightSceneProxy.h"
#include "ProjectedShadowInfo.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <bit>

DECLARE_CYCLE_STAT("Draw Render Packets", STAT_DrawRenderPackets);
DECLARE_CYCLE_STAT("Render Queue Sorting", STAT_RenderQueueSorting);
DECLARE_CYCLE_STAT("Upload Per-Object CB", STAT_UploadPerObjectCB);

float GMinScreenRadiusForShadowCaster = 0.01f;

// 실제 해상도는 투영 계산과 공유하는 Runtime 설정에서 조회한다.
bool FRenderer::SetShadowMapResolution(int32 Resolution)
{
	return FShadowSettings::SetMaxResolution(Resolution);
}

int32 FRenderer::GetShadowMapResolution() const
{
	return FShadowSettings::GetMaxResolution();
}

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
	// FireBallLightCB = RenderCommand::CreateConstantBuffer(sizeof(FFireBallLightConstants));
	LightingCB = RenderCommand::CreateConstantBuffer(sizeof(FLightingConstants));
	LightCullingCB = RenderCommand::CreateConstantBuffer(sizeof(FLightCullingConstants));
	GridLightingCB = RenderCommand::CreateConstantBuffer(sizeof(FLightGridConstants));
	GridLightingFallbackCB = RenderCommand::CreateConstantBuffer(sizeof(FLightGridConstants));

	LocalLightBuffer = RenderCommand::CreateDynamicStructuredBuffer(sizeof(FLightRenderParameters), MaxLocalLights);
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
		WritePerObjectConstants(Dest + static_cast<size_t>(Index) * ObjectSlotBytes, Model, WorldToLocal, P.Proxy && P.Proxy->IsShadowReceiver());
		P.Slot = Index;
	}

	RenderCommand::Unmap(PerObjectSlotCB.get());
}

bool FRenderer::EnsureShadowParameterCapacity(uint32 RequiredCount)
{
	const uint32 CurrentCapacity = ShadowParameterBuffer ? ShadowParameterBuffer->GetMaxCount() : 0;

	if (CurrentCapacity >= RequiredCount)
	{
		return true;
	}

	// 최초에는 필요한 만큼, 이후에는 기존 용량의 두 배 이상으로 확장한다.
	const uint32 NewCapacity = std::max(RequiredCount, CurrentCapacity * 2);
	TUniquePtr<FStructuredBuffer> NewBuffer = RenderCommand::CreateDynamicStructuredBuffer(sizeof(FShadowShaderParameters), NewCapacity);

	// 생성 실패 검증은 이 지점에서 담당한다.
	if (!NewBuffer->GetBuffer() || !NewBuffer->GetSRV())
	{
		return false;
	}

	ShadowParameterBuffer = std::move(NewBuffer);
	return true;
}

void FRenderer::PrepareShadowParameters()
{
	const uint32 Count = static_cast<uint32>(ProjectedShadows.Num());
	if (Count == 0 || !EnsureShadowParameterCapacity(Count))
	{
		return;
	}

	// 텍스처 바인딩 한도와 무관하게 모든 작업의 정보를 만든다.
	TArray<FShadowShaderParameters> Parameters;
	Parameters.SetNum(static_cast<int32>(Count));

	for (uint32 Index = 0; Index < Count; ++Index)
	{
		const FProjectedShadowInfo& ShadowInfo = ProjectedShadows[Index];
		FShadowShaderParameters& Parameter = Parameters[Index];
		Parameter.LightViewProjection = ShadowInfo.LightViewProjection;
		// Receiver 생략으로 Caster를 미수집한 작업은 빈 Depth로 확정하지 않는다.
		Parameter.IsKnownEmpty = ShadowInfo.HasReceivers() && !ShadowInfo.HasCasters() ? 1u : 0u;
		// 실패한 작업은 기본 AtlasPageIndex=-1을 유지한다.
		if (!ShadowInfo.HasDepthTarget())
		{
			continue;
		}

		FTexture2D* DepthTexture = ShadowInfo.RenderingInfo.DepthStencil.Texture;
		Parameter.Bias = ShadowInfo.LightProxy->GetLightComponent()->GetShadowBias();
		const FViewportSettings& Viewport = ShadowInfo.RenderingInfo.ViewportSetting;
		Parameter.ShadowPCFKernelSize = ShadowInfo.LightProxy->GetLightComponent()->GetShadowPCFKernelSize();
		Parameter.Resolution = Viewport.Width;
		// 콘텐츠 해상도와 예약 블록 크기를 구분하고 실제 페이지 좌표를 전달한다.
		Parameter.AtlasOffset[0] = Viewport.StartX;
		Parameter.AtlasOffset[1] = Viewport.StartY;
		Parameter.InvAtlasSize = 1.0f / static_cast<float>(DepthTexture->GetWidth());

		// 같은 페이지의 타일들은 SRV 하나를 공유한다.
		int32 PageIndex = ShadowDepthBindings.Find(DepthTexture);
		if (PageIndex == INDEX_NONE && ShadowDepthBindings.Num() < static_cast<int32>(MaxBoundShadowAtlases))
		{
			PageIndex = static_cast<int32>(ShadowDepthBindings.Add(DepthTexture));
		}
		Parameter.AtlasPageIndex = PageIndex;
		Parameter.ShadowViewSize = ShadowInfo.ShadowViewSize;
		// 수신 필터 변경은 Depth 캐시와 독립적으로 매 프레임 전달한다.
		Parameter.UsePCSS = ShadowInfo.LightProxy->GetLightComponent()->IsPCSSEnabled() ? 1u : 0u;
	}

	// 업로드 실패 시 이전 GPU 정보를 현재 작업에 사용하지 않는다.
	void* Dest = RenderCommand::MapWriteDiscard(ShadowParameterBuffer.get());
	if (!Dest)
	{
		ShadowDepthBindings.Reset();
		HTR_LOG(Error, "Shadow parameter buffer map failed.");
		return;
	}

	std::memcpy(Dest, Parameters.GetData(), sizeof(FShadowShaderParameters) * Count);
	RenderCommand::Unmap(ShadowParameterBuffer.get());

	// 업로드 성공 후에만 현재 프레임의 유효 개수를 공개한다.
	NumShadowParameters = Count;

}

void FRenderer::BindShadowResources()
{
	// 이번 단계의 그림자 평가 대상은 Lambert·Blinn-Phong이다.
	if (CurrentViewMode != EViewMode::Lit_Lambert && CurrentViewMode != EViewMode::Lit_BlinnPhong)
	{
		return;
	}

	// StructuredBuffer 바인딩 API는 nullptr이면 해당 SRV를 해제한다.
	FStructuredBuffer* Parameters = NumShadowParameters > 0 ? ShadowParameterBuffer.get() : nullptr;
	RenderCommand::BindShaderResource(ShadowParameterSlot, Parameters, EShaderBindFlagBits::Pixel);

	// 타일 수와 무관하게 각 Atlas 페이지의 SRV를 한 번만 연결한다.
	for (uint32 Index = 0; Index < MaxBoundShadowAtlases; ++Index)
	{
		FTexture2D* Texture = nullptr;
		if (NumShadowParameters > 0 && Index < static_cast<uint32>(ShadowDepthBindings.Num()))
		{
			Texture = ShadowDepthBindings[Index];
		}

		// Texture 바인딩 API는 nullptr를 받지 않으므로 별도로 해제한다.
		if (Texture)
		{
			RenderCommand::BindShaderResource(ShadowDepthTextureSlot + Index, Texture, EShaderBindFlagBits::Pixel);
		}
		else
		{
			RenderCommand::UnbindShaderResource(ShadowDepthTextureSlot + Index, EShaderBindFlagBits::Pixel);
		}
	}
}

// 논리 그림자 작업 수와 물리 자원 메모리를 분리해 집계한다.
void FRenderer::UpdateShadowStats()
{
	uint64 DirectionalLights = 0;
	uint64 PointLights = 0;
	uint64 SpotLights = 0;
	uint64 MapCount = 0;
	// 라이트 개수와 View별 Directional 투영 작업 수는 별도로 집계한다.
	uint64 DirectionalProjections = 0;
	TSet<uint32> DirectionalViewIds;

	// 매 프레임 새로 집계하므로 사용하지 않는 그룹도 0으로 기록된다.
	uint64 GroupMapCounts[ShadowStats::GroupCount]{};
	uint64 GroupTileBytes[ShadowStats::GroupCount]{};
	TSet<uint32> SingleViewIds;
	TSet<uint32> CSMViewIds;

	// Point의 여러 면이나 CSM Cascade에서 같은 라이트를 중복 계산하지 않는다.
	TSet<const FLightSceneProxy*> CountedLights;
	CountedLights.Reserve(ProjectedShadows.Num());

	for (const FProjectedShadowInfo& ShadowInfo : ProjectedShadows)
	{
		const ELightType LightType = ShadowInfo.LightProxy->GetLightComponent()->GetLightType();
		const auto Group = ShadowStats::GetGroup(LightType == ELightType::Directional, ShadowInfo.GetLogicalCascadeCount());

		// 논리 투영과 View는 빈 작업을 포함해 집계한다.
		if (LightType == ELightType::Directional)
		{
			++DirectionalProjections;
			if (ShadowInfo.ViewId != 0)
			{
				DirectionalViewIds.Add(ShadowInfo.ViewId);
				if (Group == ShadowStats::CSM)
					CSMViewIds.Add(ShadowInfo.ViewId);
				else
					SingleViewIds.Add(ShadowInfo.ViewId);
			}
		}

		// Prepared Light는 실제 Map 개수와 독립적으로 중복 제거한다.
		if (CountedLights.Add(ShadowInfo.LightProxy))
		{
			switch (LightType)
			{
			case ELightType::Directional:
				++DirectionalLights;
				break;
			case ELightType::Point:
				++PointLights;
				break;
			case ELightType::Spot:
				++SpotLights;
				break;
			default:
				break;
			}
		}

		// Map과 활성 타일 메모리는 실제 타깃이 있는 작업만 집계한다.
		if (!ShadowInfo.HasDepthTarget())
			continue;

		++MapCount;
		++GroupMapCounts[Group];

		const FViewportSettings& Viewport = ShadowInfo.RenderingInfo.ViewportSetting;
		const FTexture2D* Target = ShadowInfo.RenderingInfo.DepthStencil.Texture;
		GroupTileBytes[Group] += static_cast<uint64>(Viewport.Width) * Viewport.Height * FormatToBytes(Target->GetFormat());
	}

	uint64 DepthBytes = 0;
	uint64 ReservedBytes = 0;
	// 빈 페이지를 포함한 실제 메모리와 활성 POT 블록의 예약 면적을 구분한다.
	for (const FShadowAtlas& Atlas : ShadowViewRenderer->GetAtlases())
	{
		const FTexture2D* Target = Atlas.Texture.get();
		const uint32 BytesPerTexel = FormatToBytes(Target->GetFormat());
		DepthBytes += static_cast<uint64>(Target->GetWidth()) * Target->GetHeight() * BytesPerTexel;
		ReservedBytes += Atlas.Layout.GetUsedArea() * BytesPerTexel;
	}

	// 실제 사용 항목 수가 아니라 확보한 버퍼 용량이다.
	const uint64 ParameterBytes = ShadowParameterBuffer
		? ShadowParameterBuffer->GetBufferSize() : 0;

	FStatRegistry::SetCounter(ShadowStats::STAT_DirectionalLights, DirectionalLights);
	FStatRegistry::SetCounter(ShadowStats::STAT_PointLights, PointLights);
	FStatRegistry::SetCounter(ShadowStats::STAT_SpotLights, SpotLights);
	FStatRegistry::SetCounter(ShadowStats::STAT_Maps, MapCount);
	// ViewId는 프레임 전체에서 같은 View 슬롯을 식별한다.
	FStatRegistry::SetCounter(ShadowStats::STAT_DirectionalViews, DirectionalViewIds.Num());
	FStatRegistry::SetCounter(ShadowStats::STAT_DirectionalProjections, DirectionalProjections);
	FStatRegistry::SetCounter(ShadowStats::STAT_BoundTextures, ShadowDepthBindings.Num());

	FStatRegistry::SetMemory(ShadowStats::STAT_DepthMemory, DepthBytes);
	FStatRegistry::SetMemory(ShadowStats::STAT_ParameterMemory, ParameterBytes);

	// 메모리는 EndFrame에서 초기화되지 않으므로 작업이 없는 그룹도 갱신한다.
	for (uint32 Group = 0; Group < ShadowStats::GroupCount; ++Group)
	{
		FStatRegistry::SetCounter(ShadowStats::MapStats[Group], GroupMapCounts[Group]);
		FStatRegistry::SetMemory(ShadowStats::TileMemoryStats[Group], GroupTileBytes[Group]);
	}

	// Local은 World 공유 작업이므로 Directional 그룹의 View 수만 기록한다.
	FStatRegistry::SetCounter(ShadowStats::STAT_SingleViews, SingleViewIds.Num());
	FStatRegistry::SetCounter(ShadowStats::STAT_CSMViews, CSMViewIds.Num());
	FStatRegistry::SetCounter(ShadowStats::STAT_AtlasAllocations, ShadowViewRenderer->GetAllocationCount());
	FStatRegistry::SetCounter(ShadowStats::STAT_AtlasPages, ShadowViewRenderer->GetAtlases().Num());
	FStatRegistry::SetMemory(ShadowStats::STAT_AtlasReservedMemory, ReservedBytes);
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
	RenderCommand::BindSamplerState(1, ESamplerState::ShadowPCF,EShaderBindFlagBits::Pixel);
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
				UpdatePerObjectConstants(Item.Proxy->GetLocalToWorld(), Item.Proxy->IsShadowReceiver());
			}
			RenderCommand::DrawIndexed(Item.IndexCount, Item.StartIndex);
		}
	}

	// 뒤따르는 DrawPackets가 처음부터 다시 바인딩하도록 기록을 비운다.
	LastMesh = nullptr;
	LastMaterial = nullptr;
}

void FRenderer::RenderQueueSorting(FRenderQueue& InQueue, const FMatrix& ViewProjection,
	const FVector& CameraWorldPos)
{
	FViewConstants ViewConstants{};
	ViewConstants.ViewProjection = ViewProjection;
	ViewConstants.CameraWorldPos = CameraWorldPos;

	RenderQueueSorting(InQueue, ViewConstants);
}

void FRenderer::RenderQueueSorting(FRenderQueue& InQueue, const FViewConstants& ViewConstants)
{

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

namespace ShadowCachePrivate
{
	// operator== 한 번 대신 항목별로 비교해 미스 원인을 구분한다.
	EShadowCacheResult ClassifyShadowCache(const FShadowData& Data, const FShadowCacheKey& Key)
	{
		if (!FShadowSettings::IsWholeSceneCacheEnabled())
			return EShadowCacheResult::Disabled;
		if (!Data.bIsValid)
			return EShadowCacheResult::NewTile;
		if (Data.CacheKey.CacheGeneration != Key.CacheGeneration)
			return EShadowCacheResult::Generation;
		if (std::memcmp(Data.CacheKey.LightViewProjection.M, Key.LightViewProjection.M,
			sizeof(Key.LightViewProjection.M)) != 0)
			return EShadowCacheResult::ViewProjection;
		if (Data.CacheKey.CasterHash != Key.CasterHash)
			return EShadowCacheResult::Casters;
		return EShadowCacheResult::Hit;
	}

	void AddCacheMissStat(EShadowCacheResult Result)
	{
		switch (Result)
		{
		case EShadowCacheResult::Disabled:       FStatRegistry::AddCounter(ShadowStats::STAT_CacheMissDisabled); break;
		case EShadowCacheResult::NewTile:        FStatRegistry::AddCounter(ShadowStats::STAT_CacheMissNewTile); break;
		case EShadowCacheResult::Generation:     FStatRegistry::AddCounter(ShadowStats::STAT_CacheMissGeneration); break;
		case EShadowCacheResult::ViewProjection: FStatRegistry::AddCounter(ShadowStats::STAT_CacheMissViewProjection); break;
		case EShadowCacheResult::Casters:        FStatRegistry::AddCounter(ShadowStats::STAT_CacheMissCasters); break;
		default: break;
		}
	}
}

// 유효한 프로젝션이 설정된 섀도우 맵 타깃을 준비한다.
void FRenderer::PrepareShadowDepthTargets(TArray<FProjectedShadowInfo>& ShadowInfos)
{
	TSet<FShadowAllocationKey> ActiveKeys;
	ActiveKeys.Reserve(ShadowInfos.Num());
	for (FProjectedShadowInfo& ShadowInfo : ShadowInfos)
	{
		// 타깃 재배정 전에 이전 비소유 참조를 비우고 전체 활성 키를 모은다.
		ShadowInfo.SetRenderingInfo(FRenderingInfo{});
		if (!ShadowInfo.HasReceivers() || !ShadowInfo.HasCasters())
		{
			continue;
		}
		const ULightComponent* Light = ShadowInfo.LightProxy->GetLightComponent();
		const FShadowAllocationKey Key{
			Light->GetUUID(), ShadowInfo.ProjectionIndex, ShadowInfo.ViewId
		};
		ActiveKeys.Add(Key);
	}

	// 같은 라이트에서 사라진 Cascade·View의 타일도 반환한다.
	ShadowViewRenderer->ReleaseUnusedShadowInfos(ActiveKeys);

	TArray<int32> AllocationOrder;
	AllocationOrder.Reserve(ShadowInfos.Num());
	for (int32 Index = 0; Index < ShadowInfos.Num(); ++Index)
	{
		const FProjectedShadowInfo& ShadowInfo = ShadowInfos[Index];
		if (!ShadowInfo.HasReceivers() || !ShadowInfo.HasCasters())
		{
			continue;
		}
		const ULightComponent* Light = ShadowInfo.LightProxy->GetLightComponent();
		const uint32 Resolution = static_cast<uint32>(FShadowSettings::GetEffectiveResolution(Light->GetShadowMapResolution()));
		const FShadowAllocationKey Key{ Light->GetUUID(), ShadowInfo.ProjectionIndex, ShadowInfo.ViewId };
		// 해상도 변경으로 반환되는 공간도 신규 배정 전에 모두 확보한다.
		const FShadowData* ExistingData = ShadowViewRenderer->FindShadowInfo(Key);
		if (ExistingData && ExistingData->Resolution != Resolution)
			ShadowViewRenderer->ReleaseShadowInfo(Key);
		AllocationOrder.Add(Index);
	}

	// 논리 작업 배열을 정렬하지 않아 Point 6면과 CSM의 GPU 인덱스를 유지한다.
	std::stable_sort(AllocationOrder.begin(), AllocationOrder.end(),
		[&ShadowInfos](int32 LeftIndex, int32 RightIndex)
		{
			const ULightComponent* LeftLight = ShadowInfos[LeftIndex].LightProxy->GetLightComponent();
			const ULightComponent* RightLight = ShadowInfos[RightIndex].LightProxy->GetLightComponent();
			const uint32 LeftResolution = static_cast<uint32>(FShadowSettings::GetEffectiveResolution(LeftLight->GetShadowMapResolution()));
			const uint32 RightResolution = static_cast<uint32>(FShadowSettings::GetEffectiveResolution(RightLight->GetShadowMapResolution()));
			return FTextureLayout::GetBlockSize(LeftResolution, LeftResolution)
				> FTextureLayout::GetBlockSize(RightResolution, RightResolution);
		});

	for (int32 Index : AllocationOrder)
	{
		FProjectedShadowInfo& ShadowInfo = ShadowInfos[Index];
		const ULightComponent* Light = ShadowInfo.LightProxy->GetLightComponent();
		const uint32 Resolution = static_cast<uint32>(FShadowSettings::GetEffectiveResolution(Light->GetShadowMapResolution()));
		const FShadowAllocationKey AllocKey{
			Light->GetUUID(), ShadowInfo.ProjectionIndex, ShadowInfo.ViewId
		};
		const FShadowData* Data = ShadowViewRenderer->AllocateShadowInfo(AllocKey, Resolution);
		if (!Data)
			continue;

		// AtlasIndex는 물리 페이지 번호이며 Shader의 바인딩 번호와는 다르다.
		FRenderingInfo RenderingInfo{};
		RenderingInfo.ViewportSetting.StartX = Data->BaseX;
		RenderingInfo.ViewportSetting.StartY = Data->BaseY;
		RenderingInfo.ViewportSetting.Width = Data->Resolution;
		RenderingInfo.ViewportSetting.Height = Data->Resolution;
		RenderingInfo.DepthStencil.Texture = ShadowViewRenderer->GetAtlases()[Data->AtlasIndex].Texture.get();
		// 각 타일 패스에서 다른 영역의 깊이를 보존한다.
		RenderingInfo.DepthStencil.LoadOp = ERenderTargetLoadOp::Load;
		RenderingInfo.DepthStencil.StoreOp = ERenderTargetStoreOp::Store;
		ShadowInfo.SetRenderingInfo(RenderingInfo);

		// Shadow Cache Hit 확인
		const FShadowCacheKey CacheKey{
			ShadowInfo.LightViewProjection,
			ShadowInfo.CasterHash,
			FShadowSettings::GetCacheGeneration(),
		};
		const EShadowCacheResult CacheResult = ShadowCachePrivate::ClassifyShadowCache(*Data, CacheKey);
		const bool bIsHit = CacheResult == EShadowCacheResult::Hit;

		ShadowInfo.bShouldRenderShadow = !bIsHit;
		ShadowInfo.CacheKey = CacheKey;
		ShadowInfo.CacheResult = CacheResult;
		// 미스는 이번 프레임에 그리므로 현재 프레임, 히트는 저장된 프레임
		ShadowInfo.LastUpdatedFrame = bIsHit ? Data->LastUpdatedFrame : ShadowFrameIndex;
	}
}

void FRenderer::RenderShadowMaps(TArray<FProjectedShadowInfo>&& InShadows, FShadowViewRenderer& InShadowViewRenderer)
{
	SCOPE_CYCLE_COUNTER(ShadowStats::STAT_RenderCPU);

	// 이전 화면의 SRV를 자원 교체·Depth 기록 전에 해제한다.
	RenderCommand::UnbindShaderResource(ShadowParameterSlot, EShaderBindFlagBits::Pixel);

	// 이전 프레임의 모든 Atlas 페이지 슬롯을 해제한다.
	for (uint32 Index = 0; Index < MaxBoundShadowAtlases; ++Index)
	{
		RenderCommand::UnbindShaderResource(ShadowDepthTextureSlot + Index, EShaderBindFlagBits::Pixel);
	}

	// 타깃이 교체되기 전에 비소유 참조와 이전 업로드 상태를 비운다.
	ShadowDepthBindings.Reset();
	NumShadowParameters = 0;
	ShadowViewRenderer = &InShadowViewRenderer;
	// PrepareShadowDepthTargets 전에 증가시켜 이번 프레임 번호로 판정·기록한다.
	++ShadowFrameIndex;

	ProjectedShadows = std::move(InShadows);
	PrepareShadowDepthTargets(ProjectedShadows);

	for (FProjectedShadowInfo& ShadowInfo : ProjectedShadows)
	{
		// 논리 슬롯은 보존하되 Receiver가 없는 작업은 Depth와 캐시 집계에서 제외한다.
		if (!ShadowInfo.HasReceivers())
		{
			FStatRegistry::AddCounter(ShadowStats::STAT_ReceiverProjectionSkips);
			continue;
		}

		if (!ShadowInfo.HasCasters())
		{
			FStatRegistry::AddCounter(ShadowStats::STAT_EmptyCasterDepthSkips);
			continue;
		}
		// 타일 할당에 실패한 작업은 그리지도, 집계하지도 않는다.
		if (!ShadowInfo.HasDepthTarget())
			continue;

		const ULightComponent* Light = ShadowInfo.LightProxy->GetLightComponent();
		const ELightType Type = Light->GetLightType();
		const FShadowAllocationKey AllocKey{ Light->GetUUID(), ShadowInfo.ProjectionIndex, ShadowInfo.ViewId };

		if (ShadowInfo.bShouldRenderShadow)
		{
			// 갱신 횟수는 실제 Depth 실행 함수에서 한 번만 집계한다.
			RenderShadowDepth(ShadowInfo);
			ShadowCachePrivate::AddCacheMissStat(ShadowInfo.CacheResult);

			ShadowViewRenderer->UpdateShadowCache(AllocKey, ShadowInfo.CacheKey, ShadowFrameIndex);
		}
		else
		{
			FStatRegistry::AddCounter(ShadowStats::STAT_CacheHits);
			if (Type == ELightType::Directional)
				FStatRegistry::AddCounter(ShadowStats::STAT_CacheHitsDirectional);
			else if (Type == ELightType::Point)
				FStatRegistry::AddCounter(ShadowStats::STAT_CacheHitsPoint);
			else if (Type == ELightType::Spot)
				FStatRegistry::AddCounter(ShadowStats::STAT_CacheHitsSpot);
		}
	}
	PrepareShadowParameters();

	UpdateShadowStats();
}

int32 FRenderer::GetShadowIndex(
	const FLightSceneProxy& LightProxy, uint32 ProjectionIndex, uint32 ViewId) const
{
	for (uint32 Index = 0; Index < NumShadowParameters; ++Index)
	{
		// 실제 Proxy와 투영·View를 함께 비교해 다른 작업의 GPU 번호를 반환하지 않는다.
		const FProjectedShadowInfo& ShadowInfo = ProjectedShadows[Index];
		if (ShadowInfo.LightProxy == &LightProxy
			&& ShadowInfo.ProjectionIndex == ProjectionIndex && ShadowInfo.ViewId == ViewId)
			return static_cast<int32>(Index);
	}

	return INDEX_NONE;
}

bool FRenderer::IsShadowKnownEmpty(const FLightSceneProxy& LightProxy, uint32 ProjectionIndex, uint32 ViewId) const
{
	// 기존 조회를 재사용하고 소비된 큐 대신 수집 결과를 확인한다.
	const int32 Index = GetShadowIndex(LightProxy, ProjectionIndex, ViewId);
	// Receiver 생략과 Caster 부재를 미리보기에서도 구분한다.
	return Index != INDEX_NONE && ProjectedShadows[Index].HasReceivers()
		&& !ProjectedShadows[Index].HasCasters();
}

const FTexture2D* FRenderer::GetShadowDepthTexture(
	const FLightSceneProxy& LightProxy, uint32 ProjectionIndex, uint32 ViewId) const
{
	// 전체 작업에서 조회한다. 화면 조명용 텍스처 바인딩 상한과는 독립적이다.
	for (const FProjectedShadowInfo& ShadowInfo : ProjectedShadows)
	{
		// EditorWorld와 PlayWorld의 라이트를 실제 Proxy로 구분한다.
		if (ShadowInfo.LightProxy == &LightProxy
			&& ShadowInfo.ProjectionIndex == ProjectionIndex && ShadowInfo.ViewId == ViewId
			&& ShadowInfo.HasDepthTarget())
		{
			return ShadowInfo.RenderingInfo.DepthStencil.Texture;
		}
	}

	// 이번 프레임에 해당 라이트의 Depth 타깃이 준비되지 않았다.
	return nullptr;
}

bool FRenderer::GetShadowCacheDebugInfo(const FLightSceneProxy& LightProxy, FShadowCacheDebugInfo& OutInfo,
	uint32 ProjectionIndex, uint32 ViewId) const
{
	for (const FProjectedShadowInfo& ShadowInfo : ProjectedShadows)
	{
		if (ShadowInfo.LightProxy == &LightProxy
			&& ShadowInfo.ProjectionIndex == ProjectionIndex && ShadowInfo.ViewId == ViewId
			&& ShadowInfo.HasDepthTarget())
		{
			OutInfo.Result = ShadowInfo.CacheResult;
			OutInfo.FramesSinceUpdate = ShadowFrameIndex - ShadowInfo.LastUpdatedFrame;
			OutInfo.CasterCount = ShadowInfo.CasterCount;
			return true;
		}
	}
	return false;
}

bool FRenderer::GetShadowDepthViewport(const FLightSceneProxy& LightProxy, FViewportSettings& OutViewport,
	uint32 ProjectionIndex, uint32 ViewId) const
{
	OutViewport = FViewportSettings{};
	for (const FProjectedShadowInfo& ShadowInfo : ProjectedShadows)
	{
		if (ShadowInfo.LightProxy == &LightProxy
			&& ShadowInfo.ProjectionIndex == ProjectionIndex && ShadowInfo.ViewId == ViewId
			&& ShadowInfo.HasDepthTarget())
		{
			OutViewport = ShadowInfo.RenderingInfo.ViewportSetting;
			return true;
		}
	}
	return false;
}

void FRenderer::RenderShadowDepth(FProjectedShadowInfo& ShadowInfo)
{
	// 큐 준비부터 상태 복원까지 해당 그룹의 Depth 제출 CPU 시간을 측정한다.
	const ELightType LightType = ShadowInfo.LightProxy->GetLightComponent()->GetLightType();
	const auto Group = ShadowStats::GetGroup(
		LightType == ELightType::Directional, ShadowInfo.GetLogicalCascadeCount());
	FScopeCycleCounter DepthCounter(ShadowStats::DepthCPUStats[Group]);

	// 타깃이 배정된 작업만 실행한다.
	// 이전 카메라의 묶음과 상수 준비 상태를 그림자 작업에 재사용하지 않는다.
	ResetStaticDrawGroups();
	bObjectConstantsPrepared = false;

	// Caster 큐를 소비하고 라이트 기준 View·Object 상수를 준비한다.
	const bool bClampShadowNear = LightType == ELightType::Directional;
	RenderQueueSorting(ShadowInfo.CasterQueue, ShadowInfo.LightViewProjection, ShadowInfo.ShadowViewOrigin);

	// 기존 UberLit의 Unlit VS를 사용해 정점 조명 계산 없이 위치를 변환한다.
	// 실제 화면 View의 Unlit 여부는 호출자가 판단한다.
	const EViewMode SavedViewMode = CurrentViewMode;
	SetViewMode(EViewMode::Unlit);

	// Shadow DSV·Viewport를 연결한다.
	RenderCommand::BeginRenderPass(ShadowInfo.RenderingInfo);
	ShadowViewRenderer->ClearBoundTile();

	// 이전 View의 Wireframe·오버레이 상태가 그림자에 영향을 주지 않게 한다.
	RenderCommand::SetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	// Slope Bias를 위한 전용 RasterizerState를 설정한다.
	// shadow_cull 설정에 따라 Back(기본) 또는 Front Face Culling 상태를 고른다.
	const bool bFrontCull = FShadowSettings::GetCullMode() == EShadowCullMode::Front;

	ERasterizerState ShadowRasterizerState = bFrontCull
		? ERasterizerState::ShadowDepthFrontCull
		: ERasterizerState::ShadowDepth;

	// Directional만 정점을 변형하지 않는 하드웨어 Depth Clamp 상태를 선택한다.
	// LightType은 이 함수 상단에 이미 선언되어 있다.
	if (LightType == ELightType::Directional)
	{
		ShadowRasterizerState = bFrontCull ? ERasterizerState::DirectionalShadowDepthFrontCull : ERasterizerState::DirectionalShadowDepth;
	}

	RenderCommand::SetRasterizerState(ShadowRasterizerState);
	RenderCommand::SetDepthStencilState(EDepthStencilState::Default);

	// 실제 제출된 Draw 수를 전체 합계와 해당 그룹에 함께 기록한다.
	const uint64 DrawStart = RenderCommand::GetDrawIndexedCount();
	RenderDepth(ShadowInfo.LightViewProjection);
	const uint64 DrawCount = RenderCommand::GetDrawIndexedCount() - DrawStart;
	FStatRegistry::AddCounter(ShadowStats::STAT_DrawCalls, DrawCount);
	FStatRegistry::AddCounter(ShadowStats::DrawStats[Group], DrawCount);
	RenderCommand::EndRenderPass(ShadowInfo.RenderingInfo);

	// 실제 Depth 실행을 마친 작업만 갱신 횟수에 포함한다.
	FStatRegistry::AddCounter(ShadowStats::STAT_MapUpdates);
	FStatRegistry::AddCounter(ShadowStats::UpdateStats[Group]);

	// 화면 렌더링에 사용할 모드를 복원하고 소비한 렌더 목록을 정리한다.
	SetViewMode(SavedViewMode);
	RenderPackets.Reset();
	SortEntries.Reset();
	FirstTranslucentIndex = 0;

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
			UpdatePerObjectConstants(*Draw.World, false);
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

void FRenderer::GetShadowViewIds(const FLightSceneProxy& LightProxy, TArray<uint32>& OutViewIds) const
{
	OutViewIds.Reset();
	for (const FProjectedShadowInfo& ShadowInfo : ProjectedShadows)
	{
		if (ShadowInfo.LightProxy == &LightProxy && OutViewIds.Find(ShadowInfo.ViewId) == INDEX_NONE)
		{
			OutViewIds.Add(ShadowInfo.ViewId);
		}
	}
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
	{
		RenderCommand::GetContext()->PSSetShader(nullptr, nullptr, 0);   // 깊이만 쓰므로 조명 PS를 돌리지 않는다
		RenderCommand::SetBlendState(EBlendState::Opaque);
		RenderCommand::SetDepthStencilState(EDepthStencilState::Default);
		return;
	}
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
	if (bDepthOnly) return;
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

	UpdatePerObjectConstants(GetPacketWorld(RenderPacket),	RenderPacket.Proxy && RenderPacket.Proxy->IsShadowReceiver());
}

void FRenderer::UpdatePerObjectConstants(const FMatrix& World, bool bReceiveShadow)
{
	FPerObjectConstants Constants{};
	Constants.World = World;
	Constants.NormalMatrix = World.Inverse().GetTransposed();
	Constants.ReceiveShadow = static_cast<uint32>(bReceiveShadow);
	RenderCommand::UpdateBufferData(PerObjectCB.get(), &Constants);
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
