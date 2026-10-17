#pragma once

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Math/Matrix.h"
#include "Texture2D.h"

struct FShadowData
{
	FMatrix LightSpaceMatrix = FMatrix::Identity;
	uint32 AtlasIndex = 0;
	uint32 TileIndex = 0;
};

struct FShadowAtlas
{
	TUniquePtr<FTexture2D> Texture;
	bool UsedTiles[4] = {};
	uint32 TileResolution = 1024;
};

// Atlas 생성과 UUID별 타일 배정을 관리한다. 깊이 그리기는 main의 FRenderer가 수행한다.
class FShadowViewRenderer
{
public:
	static constexpr uint32 TilesPerRow = 2;
	static constexpr uint32 TilesPerAtlas = 4;

	bool Init();
	bool AddNewAtlas(uint32 TileResolution = 1024);
	void AddShadowInfo(uint32 UUID, uint32 AtlasIndex, uint32 TileIndex, const FMatrix& LightSpaceMatrix);
	bool AllocateShadowInfo(uint32 UUID, const FMatrix& LightSpaceMatrix, uint32 TileResolution);
	void ReleaseShadowInfo(uint32 UUID);
	void ReleaseUnusedShadowInfos(const TSet<uint32>& ActiveLightUUIDs);

	TMap<uint32, FShadowData> ShadowInfoMap;
	TArray<FShadowAtlas> ShadowAtlasArray;
};
