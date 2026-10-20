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

private:
	inline static bool bCacheWholeSceneShadows = true;
	inline static uint64 CacheGeneration = 1;
};