#pragma once

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Math/Matrix.h"
#include "Texture2D.h"

struct FShadowKey
{
	uint32 UUID;
	uint32 ProjectionIndex;
};

// 포인트 라이트의 경우 6개의 타일을 가지도록 수정해야함
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
	void AddShadowInfo(uint32 UUID, uint32 ProjectionID, uint32 AtlasIndex, uint32 TileIndex, const FMatrix& LightSpaceMatrix);
	bool AllocateShadowInfo(uint32 UUID, uint32 ProjectionID, const FMatrix& LightSpaceMatrix, uint32 TileResolution);
	void ReleaseShadowInfo(uint32 UUID, uint32 ProjectionID);
	void ReleaseUnusedShadowInfos(const TSet<uint32>& ActiveLightUUIDs);

	// 현재는 UUID : FShadowData -> UUID, ProjectionIndex : FShadowData로 바꿔서 라이트가 같아도 다른 data를 갖도록해야함
	TMap<FShadowKey, FShadowData>ShadowInfoMap;
	//TMap<uint32, FShadowData> ShadowInfoMap;
	TArray<FShadowAtlas> ShadowAtlasArray;
};
