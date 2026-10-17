#pragma once
#include "Core/Types.h"
#include "Texture2D.h"
#include "Components/LightComponentBase.h"
#include "Engine/Scene.h"

class FLightSceneProxy;
class FSpotLightSceneProxy;

enum EShadowMapType
{
	Spot,
	Point,
	Directional,

	Size
};

struct FShadowView
{
	FMatrix View;
	FMatrix Projection;
	FMatrix ViewProjection;

	uint32 TextureSlice;
	uint32 Resolution;
};

struct FShadowRequest
{
	FLightSceneProxy* Light = nullptr;

	int32 PackedLightIndex;
	ELightType LightType;

	float Priority;
	uint32 RequestedResolution;
};

struct FShadowCacheEntry
{
	uint64 LightId;

	TUniquePtr<FTexture2D> DepthTexture;

	uint32 Resolution;
	uint32 LastUsedFrame;

	bool bDirty;
};

struct FShadowData
{
	FMatrix LightVP;
	uint32 TextureSlice;
	float Bias;
	float Padding[2]{};
};

struct FShadowLightEntry
{
	FLightSceneProxy* Light = nullptr;

	// 일반 조명 배열에서 몇 번째 광원인지
	uint32 PackedLightIndex;

	// Shadow GPU 데이터 배열에서 몇 번째인지
	uint32 ShadowDataIndex;

	EShadowMapType Type;

	// Directional/Spot은 1개, Point는 6개
	TArray<FShadowView> Views;

	FShadowCacheEntry* CacheEntry = nullptr;
};

class FShadowMapRenderer
{
public:
	FShadowMapRenderer() = default;
	~FShadowMapRenderer() = default;

	bool Init();

	void PrepareShadows(FScene& Scene);
	void RenderShadows(UWorld* World, FRenderer* Renderer);

	void UploadShadowData();

	void BindShadows();
	void UnbindShadows();

	void AddShadowRequest(FLocalLightSceneProxy* Light);

	FTexture2D* GetTexture() { return SpotShadowDepth.get(); }

private:
	TArray<FShadowRequest> SelectedLights;
	TUniquePtr<FTexture2D> SpotShadowDepth;

	FSpotLightSceneProxy* SelectedSpotLight;

	TArray<FShadowView> ShadowViews;
	TArray<FShadowData> ShadowDatas;

	FMatrix LightView;
	FMatrix LightProjection;
	FMatrix LightViewProjection;

	TUniquePtr<FConstantBuffer> ShadowConstants;

	TUniquePtr<FStructuredBuffer> ShadowBuffer;


	// 광원이 다음 프레임에도 선택됐을 때 리소스 재사용
	// std::unordered_map<uint64, FShadowCacheEntry> ShadowCache;
};