#pragma once
#include "Core/Types.h"
#include "Math/Matrix.h"
#include "Math/Frustum.h"
#include "ShadowMapArray.h"

class FLightSceneProxy;
class FPrimitiveSceneProxy;
class UStaticMesh;

// UberLit에서 Spot 그림자 배열(Texture2DArray)을 읽을 SRV 슬롯. t10(LocalLights), t12(CellLightIndices)와 겹치지 않게 둔다.
// Shadow Depth Pass가 같은 리소스를 DSV로 묶기 전에 이 슬롯을 비운다.
constexpr uint32 ShadowMapSRVSlot = 13u;

// 그림자 Depth pass 한 번(= Shadow Map 한 장)에 필요한 시점 정보
struct FShadowViewInfo
{
	FMatrix View;
	FMatrix Projection;
	FMatrix ViewProjection;
	FFrustumPlanes Frustum;
	float NearZ = 0.f;
	float FarZ = 0.f;
};

constexpr uint32 MaxSpotShadowSlices = 16u;
constexpr uint32 MinShadowResolution = 64u;

struct FShadowAllocation
{
	uint32 Slice = 0; // Texture2DArray의 슬라이스. ShadowIndex.
	uint32 Resolution = 0; // 실제로 그릴 크기 (viewport 크기)
	float UVScale = 1.f; // UV 스케일. Resolution / Slice
};

// 그림자 Depth Pass의 드로우 하나. 머티리얼이 필요 없으니 메시·범위·월드 행렬만 있으면 된다.
struct FShadowDrawItem
{
	const FPrimitiveSceneProxy* Proxy = nullptr;
	UStaticMesh* Mesh = nullptr;
	uint32 StartIndex = 0;
	uint32 IndexCount = 0;
	uint8 LODIndex = 0;
};

// 라이트 하나의 이번 프레임 그림자 요청. Scene이 채우고 Renderer가 그린다.
struct FShadowRequest
{
	FLightSceneProxy* Proxy = nullptr;
	int32 LightIndex = INDEX_NONE;
	FShadowViewInfo View;
	FShadowAllocation Allocation;
	uint32 FirstCaster = 0; // FScene::ShadowCasters 안의 구간 시작
	uint32 NumCasters = 0;
};

class FShadowMapManager
{
public:
	void SetBaseResolution(uint32 InResolution) { BaseResolution = InResolution; }
	void BeginFrame(uint32 NumRequests);
	bool Allocate(float ResolutionScale, FShadowAllocation& Out);

	FShadowMapArray* GetSpotShadowMaps() const { return SpotShadowMaps.get(); }
	uint32 GetNumAllocated() const { return NextSlice; }
	uint64 GetMemoryBytes() const { return SpotShadowMaps ? SpotShadowMaps->GetMemoryBytes() : 0; }

	
private:
	TUniquePtr<FShadowMapArray> SpotShadowMaps;
	uint32 BaseResolution = 1024u;
	uint32 NextSlice = 0u;
};
