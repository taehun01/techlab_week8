#include "EnginePCH.h"
#include "ShadowMapManager.h"
#include "RenderCommand.h"
#include "Math/EngineMath.h"
#include "Core/EngineLog.h"

void FShadowMapManager::BeginFrame(uint32 NumRequests)
{
	// 매 프레임 0번부터 재시작
	NextSlice = 0;
	// 필요한 장 수 개산. 최대 최소로 clamp
	const uint32 Needed = std::min(std::max(NumRequests, 1u), MaxSpotShadowSlices);

	// 지금 텍스쳐가 가진 슬라이스의 개수
	const uint32 Capacity = SpotShadowMaps ? SpotShadowMaps->GetSliceCount() : 0;
	// 해상도가 바뀌었는지 여부 체크
	const bool bResolutionChanged = SpotShadowMaps && SpotShadowMaps->GetResolution() != BaseResolution;

	// 다시 만들어야 하는 경우
	if (!SpotShadowMaps || bResolutionChanged || Needed > Capacity)
	{
		const uint32 NewCapacity = bResolutionChanged ? Needed : std::max(Capacity, Needed);
		// 4장 단위로 반올림
		const uint32 Rounded = std::min((NewCapacity + 3u) & ~3u, MaxSpotShadowSlices);

		// ShadowMap 재생성
		SpotShadowMaps = MakeUnique<FShadowMapArray>(RenderCommand::GetDevice(), BaseResolution, Rounded);
		//HTR_LOG(Info, "[Shadow] Spot shadow array {}x{} x {} ({} MB)",
		//	BaseResolution, BaseResolution, Rounded, GetMemoryBytes() / (1024 * 1024));
	}
}

bool FShadowMapManager::Allocate(float ResolutionScale, FShadowAllocation& Out)
{
	if (!SpotShadowMaps || NextSlice >= SpotShadowMaps->GetSliceCount())
	{
		return false;
	}

	const uint32 Base = SpotShadowMaps->GetResolution();
	const float Scale = FMath::Clamp(ResolutionScale, 0.f, 1.f);

	Out.Slice = NextSlice++;
	Out.Resolution = FMath::Clamp(static_cast<uint32>(Base * Scale), MinShadowResolution, Base);
	Out.UVScale = static_cast<float>(Out.Resolution) / Base;

	return true;
}