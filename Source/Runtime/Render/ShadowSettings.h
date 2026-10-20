#pragma once
#include "Core/Types.h"
#include "Core/EngineString.h"

class FShadowSettings
{
public:
	static bool ExecCommand(const FString& CommandLine, FString& OutMessage);
	static bool IsWholeSceneCacheEnabled() { return bCacheWholeSceneShadows; }

	static uint64 GetCacheGeneration() { return CacheGeneration; }
	static void InvalidateAllCaches() { ++CacheGeneration; }

	// 전역 품질 단계는 라이트별 요청 해상도의 상한으로만 적용한다.
	static bool SetMaxResolution(int32 Resolution);
	static int32 GetMaxResolution() { return MaxResolution; }
	static int32 GetEffectiveResolution(int32 RequestedResolution)
	{
		return RequestedResolution < MaxResolution ? RequestedResolution : MaxResolution;
	}

private:
	inline static bool bCacheWholeSceneShadows = true;
	inline static uint64 CacheGeneration = 1;
	inline static int32 MaxResolution = 1024;
};
