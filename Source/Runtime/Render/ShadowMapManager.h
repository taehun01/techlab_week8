#pragma once
#include "Core/Types.h"
#include "ShadowMapArray.h"

constexpr uint32 MaxSpotShadowSlices = 16u;
constexpr uint32 MinShadowResolution = 64u;

struct FShadowAllocation
{
	uint32 Slice = 0; // Texture2DArray의 슬라이스. ShadowIndex.
	uint32 Resolution = 0; // 실제로 그릴 크기 (viewport 크기)
	float UVScale = 1.f; // UV 스케일. Resolution / Slice
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
