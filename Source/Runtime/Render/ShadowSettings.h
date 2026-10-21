#pragma once
#include "Core/Types.h"
#include "Core/EngineString.h"

// Shadow Depth 패스에서 어느 면을 컬링할지 정한다.
enum class EShadowCullMode : uint8
{
	Back,	// 기본값: 앞면을 기록하고 Bias로 Acne를 줄인다.
	Front,	// 뒷면을 기록한다. 닫힌 메시에서 Acne가 줄지만 단면 메시는 그림자를 드리우지 않는다.
};

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

	// 컬링 모드가 바뀌면 캐시된 Shadow Map을 모두 다시 그린다.
	static void SetCullMode(EShadowCullMode Mode);
	static EShadowCullMode GetCullMode() { return CullMode; }
	static int32 GetEffectiveResolution(int32 RequestedResolution)
	{
		return RequestedResolution < MaxResolution ? RequestedResolution : MaxResolution;
	}

private:
	inline static bool bCacheWholeSceneShadows = true;
	inline static uint64 CacheGeneration = 1;
	inline static int32 MaxResolution = 4096;
	inline static EShadowCullMode CullMode = EShadowCullMode::Back;
};
