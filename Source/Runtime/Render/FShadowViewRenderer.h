#pragma once

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Texture2D.h"

enum class ELightType;

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

// 투영 행렬은 작업 Info가 보관하며, 할당 결과에는 풀 내부 위치만 남긴다.
struct FShadowData
{
	uint32 AtlasIndex = 0;
	uint32 TileIndex = 0;
};

// 한 페이지가 실제 Depth 자원과 네 타일의 점유 상태를 소유한다.
struct FShadowAtlas
{
	TUniquePtr<FTexture2D> Texture;
	bool UsedTiles[4] = {};
	uint32 TileResolution = 1024;
};

// 라이트 종류마다 페이지와 할당 수명을 분리하고 배정 알고리즘은 공유한다.
struct FShadowAtlasPool
{
	TMap<FShadowAllocationKey, FShadowData> ShadowInfoMap;
	TArray<FShadowAtlas> ShadowAtlasArray;
};

// Atlas 자원과 타일 배정만 관리하며, 실제 Depth 렌더링은 FRenderer가 수행한다.
class FShadowViewRenderer
{
public:
	static constexpr uint32 TilesPerRow = 2;
	static constexpr uint32 TilesPerAtlas = 4;

	// 지원 종류를 검사한 뒤 필요할 때만 풀과 페이지를 만든다. 해상도 범위는 호출자가 보장한다.
	const FShadowData* AllocateShadowInfo(ELightType Type, const FShadowAllocationKey& Key, uint32 TileResolution);
	void ReleaseShadowInfo(ELightType Type, const FShadowAllocationKey& Key);
	void ReleaseUnusedShadowInfos(const TMap<ELightType, TSet<FShadowAllocationKey>>& ActiveAllocations);

	// 렌더링과 통계는 할당 상태를 변경하지 않고 종류별 자원을 조회한다.
	const FShadowAtlasPool* FindPool(ELightType Type) const { return AtlasPools.Find(Type); }
	const TMap<ELightType, FShadowAtlasPool>& GetAtlasPools() const { return AtlasPools; }

private:
	bool AddNewAtlas(FShadowAtlasPool& Pool, uint32 TileResolution);
	const FShadowData* AddShadowInfo(FShadowAtlasPool& Pool, const FShadowAllocationKey& Key, uint32 AtlasIndex, uint32 TileIndex);

	TMap<ELightType, FShadowAtlasPool> AtlasPools;
};
