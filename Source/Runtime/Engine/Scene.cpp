#include "EnginePCH.h"
#include "Scene.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Render/Material.h"

void FScene::AddPrimitive(UPrimitiveComponent* Component)
{
	if (!Component || Component->SceneProxy) return;

	FPrimitiveSceneProxy* Proxy = Component->CreateSceneProxy();
	Proxy->Scene = this;
	Proxy->PackedIndex = PrimitiveSceneProxies.Num();
	Component->SceneProxy = Proxy;
	MarkDirty(Proxy);
	MarkRenderStateDirty(Proxy);

	PrimitiveSceneProxies.Add(Proxy);
	PrimitiveBounds.Add(FAABB{});
	PrimitiveFlags.Add(0);

	bElementListChanged = true;
}

void FScene::RemovePrimitive(UPrimitiveComponent* Component)
{
	if (!Component || !Component->SceneProxy) return;

	FPrimitiveSceneProxy* Proxy = Component->SceneProxy;
	if (!Proxy) return;

	const uint32 Index = static_cast<uint32>(Proxy->PackedIndex);

	PrimitiveSceneProxies.RemoveAtSwap(Index);
	PrimitiveBounds.RemoveAtSwap(Index);
	PrimitiveFlags.RemoveAtSwap(Index);

	if (Index < static_cast<uint32>(PrimitiveSceneProxies.Num()))
		PrimitiveSceneProxies[Index]->PackedIndex = static_cast<int32>(Index);

	if (Proxy->bQueuedForUpdate)
	{
		const int32 Found = DirtyProxies.Find(Proxy);   // 프로젝트 TArray의 Find 이름에 맞게
		if (Found != INDEX_NONE) DirtyProxies.RemoveAtSwap(Found);
	}

	if (Proxy->bRenderStateQueued)
	{
		const int32 Found = RenderStateDirtyProxies.Find(Proxy);
		if (Found != INDEX_NONE) RenderStateDirtyProxies.RemoveAtSwap(Found);
	}

	Component->SceneProxy = nullptr;
	delete Proxy;

	bElementListChanged = true;
}

void FScene::RemoveAllPrimitives()
{
	// 하나씩 RemovePrimitive하면 대기열 검색(Find)이 물체 수만큼 반복되므로 통째로 비운다.
	for (FPrimitiveSceneProxy* Proxy : PrimitiveSceneProxies)
	{
		if (UPrimitiveComponent* Component = Proxy->GetComponent())
			Component->SceneProxy = nullptr;
		delete Proxy;
	}
	PrimitiveSceneProxies.Reset();
	PrimitiveBounds.Reset();
	PrimitiveFlags.Reset();
	DirtyProxies.Reset();
	RenderStateDirtyProxies.Reset();
	BVH.Clear();
	bElementListChanged = true;
}

void FScene::AddLight(ULightComponent* Component)
{
	if (!Component || Component->SceneProxy) return;

	FLightSceneProxy* Proxy = Component->CreateSceneProxy();
	if (!Proxy)
	{
		if (USkyLightComponent* Sky = Cast<USkyLightComponent>(Component))
			if (SkyLights.Find(Sky) == INDEX_NONE) SkyLights.Add(Sky);
		return;
	}

	Proxy->Scene = this;
	Component->SceneProxy = Proxy;

	if (Component->GetLightType() == ELightType::Directional)
	{
		Proxy->PackedIndex = DirectionalLightProxies.Num();
		DirectionalLightProxies.Add(static_cast<FDirectionalLightSceneProxy*>(Proxy));
	}
	else
	{
		Proxy->PackedIndex = LocalLightProxies.Num();
		LocalLightProxies.Add(static_cast<FLocalLightSceneProxy*>(Proxy));
		LocalLightParameters.Add(FLightRenderParameters{});
	}
}

void FScene::RemoveLight(ULightComponent* Component)
{
	if (!Component) return;

	FLightSceneProxy* Proxy = Component->SceneProxy;
	if (!Proxy)
	{
		if (USkyLightComponent* Sky = Cast<USkyLightComponent>(Component))
		{
			const int32 Found = SkyLights.Find(Sky);
			if (Found != INDEX_NONE) SkyLights.RemoveAtSwap(Found);
		}
		return;
	}

	const int32 Index = Proxy->PackedIndex;
	if (Component->GetLightType() == ELightType::Directional)
	{
		DirectionalLightProxies.RemoveAtSwap(Index);
		if (Index < DirectionalLightProxies.Num())
			DirectionalLightProxies[Index]->PackedIndex = Index;
	}
	else
	{
		LocalLightProxies.RemoveAtSwap(Index);
		LocalLightParameters.RemoveAtSwap(Index);      // 두 배열을 같이 움직여야 순서가 맞음
		if (Index < LocalLightProxies.Num())
			LocalLightProxies[Index]->PackedIndex = Index;
	}

	Component->SceneProxy = nullptr;
	delete Proxy;
}

void FScene::RemoveAllLights()
{
	for (FLightSceneProxy* Proxy : DirectionalLightProxies) delete Proxy;
	for (FLightSceneProxy* Proxy : LocalLightProxies)       delete Proxy;
	DirectionalLightProxies.Reset();
	LocalLightProxies.Reset();
	LocalLightParameters.Reset();
	SkyLights.Reset();
}

// 등록된 라이트를 셰이더용 상수로 모아 Renderer에 올린다. 컴포넌트 값을 매 프레임 직접 읽으므로 별도 갱신 경로가 필요 없다.
void FScene::UpdateLights(FRenderer* Renderer)
{
	if (!Renderer) return;

	FLightingConstants Constants{};
	Constants.Ambient.Color = FVector(1, 1, 1);
	Constants.Ambient.Intensity = 0.0f;

	for (USkyLightComponent* Sky : SkyLights)            // 기존 Sky 코드 그대로
	{
		if (!Sky->IsVisible()) continue;
		const FLinearColor Color(Sky->GetLightColor());
		Constants.Ambient.Color = FVector(Color.R, Color.G, Color.B);
		Constants.Ambient.Intensity = Sky->GetIntensity();
		break;
	}

	for (FDirectionalLightSceneProxy* Proxy : DirectionalLightProxies)
	{
		if (!Proxy->IsVisible()) continue;
		FLightRenderParameters P;
		Proxy->GetLightShaderParameters(P);
		Constants.Directional.Direction = P.Direction;
		Constants.Directional.Color = P.Color;
		Constants.Directional.Intensity = P.Intensity;
		break;                                            // 셰이더는 하나만 받음
	}

	for (FLocalLightSceneProxy* Proxy : LocalLightProxies)
	{
		FLightRenderParameters& P = LocalLightParameters[Proxy->PackedIndex];
		Proxy->GetLightShaderParameters(P);
		if (!Proxy->IsVisible())
		{
			P.Radius = 0.0f; // 칸은 유지하고 영향만 없앤다
		}
	}

	ShadowRequests.Reset();
	for (FLocalLightSceneProxy* Proxy : LocalLightProxies)
	{
		if (!Proxy->IsVisible() || !Proxy->IsCastShadows()) continue;
		if (Proxy->GetNumShadowViews() != 1) continue;
		const FLightRenderParameters& P = LocalLightParameters[Proxy->PackedIndex];
		if (P.Intensity <= 0 || P.Radius <= 0) continue;

		FShadowRequest& Request = ShadowRequests.AddDefaulted_GetRef();
		Request.Proxy = Proxy;
		Request.LightIndex = Proxy->PackedIndex;
		Proxy->GetShadowView(0, Request.View);
	}

	FShadowMapManager& Shadows = Renderer->GetShadowMapManager();
	Shadows.BeginFrame(ShadowRequests.Num());
	for (int32 i = 0; i < ShadowRequests.Num();)
	{
		FShadowRequest& Request = ShadowRequests[i];
		if (Shadows.Allocate(Request.Proxy->GetLightComponent()->GetShadowResolutionScale(), Request.Allocation))
		{
			++i;
		}
		else
		{
			ShadowRequests.RemoveAt(i);
		}
	}

	GatherShadowCasters();

	// 셰이더가 라이트 번호로 자기 Shadow Map을 찾도록 슬라이스 번호를 써 넣는다.
	// 칸 배정에 실패해 빠진 라이트는 프록시가 채운 기본값 -1(그림자 없음)로 남는다.
	for (FShadowRequest& Request : ShadowRequests)
	{
		LocalLightParameters[Request.LightIndex].ShadowIndex = static_cast<int32>(Request.Allocation.Slice);

		const ULightComponent* Light = Request.Proxy->GetLightComponent();
		Request.DepthBias = Light->GetShadowBias();
		Request.SlopeBias = Light->GetShadowSlopeBias();
	}

	// ShadowIndex가 들어간 LocalLightParameters를 SetLighting이 올리기 전에 그림자 데이터도 맞춰 둔다.
	Renderer->SetShadowData(ShadowRequests);
	Renderer->SetLighting(Constants, LocalLightParameters);
}

void FScene::AddFogInfo(uint32 Id, const FFogInfo& FogInfo)
{
	FExponentialHeightFogSceneInfo sceneFogInfo;
	sceneFogInfo.Id = Id;
	sceneFogInfo.FogInfo = FogInfo;


	FogInfos.Add(sceneFogInfo);
}

void FScene::RemoveFogInfo(uint32 Id)
{
	// 렌더러는 FogInfos[0]만 쓰므로, 순서를 유지하려고 RemoveAtSwap 대신 RemoveAt을 쓴다.
	for (int32 i = 0; i < FogInfos.Num(); ++i)
	{
		if (FogInfos[i].Id == Id)
		{
			FogInfos.RemoveAt(i);
			return;
		}
	}
}

void FScene::RemoveAllFogInfo()
{
	FogInfos.Reset();

}

void FScene::UpdateFogInfo(uint32 Id, const FFogInfo& FogInfo)
{
	for (FExponentialHeightFogSceneInfo& SceneFogInfo : FogInfos)
	{
		if (SceneFogInfo.Id == Id)
		{
			SceneFogInfo.FogInfo = FogInfo;
			return;
		}


	}
}

//void FScene::RemoveFogInfo(uint32 Id)
//{
//	for (int32 i = 0; i < FogInfos.Num(); ++i)
//	{
//		if (FogInfos[i].Id == Id)
//		{
//			FogInfos.RemoveAtSwap(i);
//			return;
//		}
//	}
//}


void FScene::UpdateAllTransforms()
{
	for (FPrimitiveSceneProxy* Proxy : RenderStateDirtyProxies)
	{
		Proxy->UpdateRenderState();
		Proxy->bRenderStateQueued = false;
	}
	RenderStateDirtyProxies.Reset();

	for (FPrimitiveSceneProxy* Proxy : DirtyProxies)
	{
		Proxy->UpdateTransform();
		PrimitiveBounds[Proxy->PackedIndex] = Proxy->GetBounds();
		PrimitiveFlags[Proxy->PackedIndex] = Proxy->GetComponent()->IsVisible() ? 1 : 0;
		Proxy->bQueuedForUpdate = false;
	}

	//const int32 Count = Proxies.Num();
	//for (int32 i = 0; i < Count; ++i)
	//{
	//	FPrimitiveSceneProxy* Proxy = Proxies[i];
	//	Proxy->UpdateTransform();

	//	const FAABB& Bounds = Proxy->GetBounds();
	//	if (PrimitiveBounds[i].Center != Bounds.Center || PrimitiveBounds[i].Extent != Bounds.Extent)
	//	{
	//		bBoundsChanged = true;
	//	}

	//	PrimitiveBounds[i] = Proxy->GetBounds();
	//	PrimitiveFlags[i] = Proxy->GetComponent()->IsVisible() ? 1 : 0;
	//}

	const bool bAnyMoved = DirtyProxies.Num() > 0;
	DirtyProxies.Reset();


	if (bElementListChanged)
	{
		BuildBVH();
		bElementListChanged = false;
	}
	else if (bAnyMoved)
	{
		BVH.Refit();
	}
}

void FScene::BuildBVH()
{
	BVH.Clear();
	// BillboardComponents.Reset();

	TArray<FPrimitiveSceneProxy*> Elements;
	Elements.Reserve(PrimitiveSceneProxies.Num());
	for (FPrimitiveSceneProxy* Proxy : PrimitiveSceneProxies)
	{
		if (Proxy && Proxy->GetComponent())
			Elements.Add(Proxy);
	}

	BVH.Build(std::span(Elements.GetData(), Elements.Num()));
}

void FScene::MarkDirty(FPrimitiveSceneProxy* Proxy)
{
	if (Proxy->bQueuedForUpdate) return;            // 중복 추가 방지
	Proxy->bQueuedForUpdate = true;
	DirtyProxies.Add(Proxy);
}

void FScene::MarkRenderStateDirty(FPrimitiveSceneProxy* Proxy)
{
	if (!Proxy || Proxy->bRenderStateQueued) return;
	Proxy->bRenderStateQueued = true;
	RenderStateDirtyProxies.Add(Proxy);
}


void FScene::GatherShadowCasters()
{
	ShadowCasters.Reset();   // 용량은 유지

	for (FShadowRequest& Request : ShadowRequests)
	{
		Request.FirstCaster = ShadowCasters.Num();
		const FFrustumPlanes& LightFrustum = Request.View.Frustum;

		BVH.QueryCull(
			FrustumAllPlanesMask,
			[&LightFrustum](const FBox& Bounds, uint32& Mask)
			{
				return static_cast<EBVHCullResult>(ClassifyBoxInFrustum(Bounds, LightFrustum, Mask));
			},
			[this](FPrimitiveSceneProxy* Proxy)
			{
				if (!Proxy->IsVisible() || !Proxy->IsCastShadow())
					return;

				// 메시가 없는 프록시(빌보드·파티클·텍스트)는 View마다 형상이 달라 그림자 대상이 아니다
				UStaticMesh* Mesh = Proxy->GetMesh();
				if (!Mesh || !Proxy->GetRenderState())
					return;

				constexpr uint8 ShadowLOD = 0;
				const FCachedMeshLOD& CachedLOD = Proxy->GetLOD(ShadowLOD);
				for (uint32 i = 0; i < CachedLOD.NumSections; ++i)
				{
					const FCachedMeshSection& Section = Proxy->GetSection(CachedLOD.FirstSection + i);
					// 반투명 섹션은 깊이를 쓰지 않는다 (카메라 RenderDepth와 같은 규칙)
					if (!Section.Material || Section.Material->BlendState != EBlendState::Opaque)
						continue;
					ShadowCasters.Add({ Proxy, Mesh, Section.StartIndex, Section.IndexCount, ShadowLOD });
				}
			});

		Request.NumCasters = ShadowCasters.Num() - Request.FirstCaster;

		// 5단계에서 BindMesh가 덜 바뀌도록 구간 안을 메시·LOD 순으로 정렬
		std::sort(ShadowCasters.begin() + Request.FirstCaster, ShadowCasters.end(),
			[](const FShadowDrawItem& A, const FShadowDrawItem& B)
			{ return A.Mesh != B.Mesh ? A.Mesh < B.Mesh : A.LODIndex < B.LODIndex; });
	}
}