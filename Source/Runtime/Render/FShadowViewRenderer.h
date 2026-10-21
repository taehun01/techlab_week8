#pragma once

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Texture2D.h"
#include "TextureLayout.h"
#include "PipelineState.h"

// 같은 라이트의 투영과 View별 그림자 작업을 서로 다른 타일에 연결한다.
struct FShadowAllocationKey
{
	uint32 LightUUID = 0;
	uint32 ProjectionIndex = 0;
	uint32 ViewId = 0;

	bool operator==(const FShadowAllocationKey& Other) const
	{
		return LightUUID == Other.LightUUID && ProjectionIndex == Other.ProjectionIndex && ViewId == Other.ViewId;
	}
};

// 현재 TMap과 TSet이 사용하는 표준 해시에 세 식별자를 함께 반영한다.
template<>
struct std::hash<FShadowAllocationKey>
{
	size_t operator()(const FShadowAllocationKey& Key) const noexcept
	{
		size_t Hash = std::hash<uint32>{}(Key.LightUUID);
		Hash ^= std::hash<uint32>{}(Key.ProjectionIndex) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
		Hash ^= std::hash<uint32>{}(Key.ViewId) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
		return Hash;
	}
};

// Shadow Caching 판정에 쓰이는 key. VP 행렬과 hash값, Generation으로 구분.
struct FShadowCacheKey
{
	FMatrix LightViewProjection = FMatrix::Identity;
	uint64 CasterHash = 0u;
	uint64 CacheGeneration = 0u;

	bool operator==(const FShadowCacheKey& Other) const
	{
		return memcmp(LightViewProjection.M, Other.LightViewProjection.M, sizeof(LightViewProjection.M)) == 0 &&
			CasterHash == Other.CasterHash && CacheGeneration == Other.CacheGeneration;
	}
};

// 캐시 판정 결과. Hit가 아니면 이번 프레임에 깊이를 다시 그린 이유다.
enum class EShadowCacheResult : uint8
{
	Hit,
	Disabled,        // r.shadow.CacheWholeSceneShadows 0
	NewTile,         // 새 할당·재할당이라 내용이 없음
	Generation,      // 전체 무효화(콘솔 토글 등)
	ViewProjection,  // 라이트 이동·회전·Cone·Cascade 변화
	Casters,         // Caster 집합 또는 ShadowVersion 변화
	Count
};

inline const char* GetShadowCacheResultName(EShadowCacheResult Result)
{
	switch (Result)
	{
	case EShadowCacheResult::Hit:            return "Hit";
	case EShadowCacheResult::Disabled:       return "Cache disabled";
	case EShadowCacheResult::NewTile:        return "New tile";
	case EShadowCacheResult::Generation:     return "Generation changed";
	case EShadowCacheResult::ViewProjection: return "View projection changed";
	case EShadowCacheResult::Casters:        return "Casters changed";
	default:                                 return "Unknown";
	}
}

// 논리 그림자 작업의 물리 위치와 기존 Depth 캐시 상태를 함께 보관한다.
struct FShadowData
{
	uint32 AtlasIndex = 0;
	uint32 BaseX = 0;
	uint32 BaseY = 0;
	uint32 Resolution = 0;

	// Shadow Caching
	bool bIsValid = false;
	FShadowCacheKey CacheKey = FShadowCacheKey{};
	// 마지막으로 깊이를 그린 Shadow 프레임 번호 (디버그 표시용)
	uint64 LastUpdatedFrame = 0;
};

// 페이지 하나가 텍스처와 그 텍스처의 공간 배치기를 소유한다.
struct FShadowAtlas
{
	explicit FShadowAtlas(uint32 InSize)
		: Layout(InSize)
	{
	}

	TUniquePtr<FTexture2D> Texture;
	FTextureLayout Layout;
};

// 통합 페이지, 키별 배정, Depth 캐시를 관리한다. 실제 Depth 렌더링은 FRenderer가 수행한다.
class FShadowViewRenderer
{
public:
	static constexpr uint32 AtlasSize = 8192;
	static constexpr uint32 MaxAtlasPages = 16;

	bool Init();

	const FShadowData* AllocateShadowInfo(const FShadowAllocationKey& Key, uint32 Resolution);
	void ReleaseShadowInfo(const FShadowAllocationKey& Key);
	void ReleaseUnusedShadowInfos(const TSet<FShadowAllocationKey>& ActiveKeys);

	// 렌더링과 통계에서는 할당 상태를 변경하지 않고 위치와 자원을 조회한다.
	const FShadowData* FindShadowInfo(const FShadowAllocationKey& Key) const { return ShadowInfoMap.Find(Key); }
	const TArray<FShadowAtlas>& GetAtlases() const { return ShadowAtlasArray; }
	uint32 GetAllocationCount() const { return ShadowInfoMap.Num(); }

	void UpdateShadowCache(const FShadowAllocationKey& Key, const FShadowCacheKey& CacheKey, uint64 Frame);

	// BeginRenderPass로 타일 DSV와 뷰포트가 설정된 후 호출해야한다.
	void ClearBoundTile();

private:
	bool AddNewAtlas();
	const FShadowData* AddShadowInfo(const FShadowAllocationKey& Key,
		uint32 AtlasIndex, uint32 BaseX, uint32 BaseY, uint32 Resolution);

	TMap<FShadowAllocationKey, FShadowData> ShadowInfoMap;
	TArray<FShadowAtlas> ShadowAtlasArray;

	// 각 뷰포트의 DSV를 Clear하기 위한 파이프라인. Shadow Cache가 Miss인 경우 해당 뷰포트만 clear한다.
	FPipelineState ClearPipelineState{};
};
