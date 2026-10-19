#include "EnginePCH.h"
#include "FShadowViewRenderer.h"
#include "Components/LightComponentBase.h"
#include "RenderCommand.h"

// 타일 네 개가 같은 해상도를 사용하는 기존 2x2 Depth 페이지를 만든다.
bool FShadowViewRenderer::AddNewAtlas(FShadowAtlasPool& Pool, uint32 TileResolution)
{
	D3D11_TEXTURE2D_DESC Desc{};
	Desc.Width = TileResolution * TilesPerRow;
	Desc.Height = TileResolution * TilesPerRow;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_DEFAULT;
	Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

	FShadowAtlas Atlas;
	Atlas.Texture = RenderCommand::CreateTexture2D(Desc);
	if (!Atlas.Texture || !Atlas.Texture->GetDSV() || !Atlas.Texture->GetSRV())
	{
		HTR_LOG(Error, "Shadow Atlas creation failed: {}x{}", Desc.Width, Desc.Height);
		return false;
	}
	Atlas.TileResolution = TileResolution;
	Pool.ShadowAtlasArray.Add(std::move(Atlas));
	return true;
}

// 타일 점유와 키의 위치 기록을 한 경로에서 함께 갱신한다.
const FShadowData* FShadowViewRenderer::AddShadowInfo(FShadowAtlasPool& Pool, const FShadowAllocationKey& Key, uint32 AtlasIndex, uint32 TileIndex)
{
	Pool.ShadowAtlasArray[AtlasIndex].UsedTiles[TileIndex] = true;
	FShadowData& Data = Pool.ShadowInfoMap[Key];
	Data = FShadowData{};
	Data.AtlasIndex = AtlasIndex;
	Data.TileIndex = TileIndex;
	Pool.ShadowInfoMap[Key] = Data;
	return &Data;
}

const FShadowData* FShadowViewRenderer::AllocateShadowInfo(ELightType Type, const FShadowAllocationKey& Key, uint32 TileResolution)
{
	// 현재 생성 경로가 있는 라이트만 허용하고, 이후 배정은 같은 알고리즘을 사용한다.
	switch (Type)
	{
	case ELightType::Directional:
	case ELightType::Spot:
		break;
	default:
		return nullptr;
	}

	FShadowAtlasPool& Pool = AtlasPools[Type];
	if (const FShadowData* Data = Pool.ShadowInfoMap.Find(Key))
	{
		if (Pool.ShadowAtlasArray[Data->AtlasIndex].TileResolution == TileResolution)
		{
			// 같은 작업과 해상도는 기존 위치를 유지한다.
			return Data;
		}
		// 해상도가 바뀐 작업만 이전 타일을 반환하고 다시 배정한다.
		ReleaseShadowInfo(Type, Key);
	}

	for (int32 AtlasIndex = 0; AtlasIndex < Pool.ShadowAtlasArray.Num(); ++AtlasIndex)
	{
		FShadowAtlas& Atlas = Pool.ShadowAtlasArray[AtlasIndex];
		// 페이지 내부의 타일 해상도는 모두 같아야 한다.
		if (Atlas.TileResolution != TileResolution)
			continue;

		for (uint32 TileIndex = 0; TileIndex < TilesPerAtlas; ++TileIndex)
		{
			if (!Atlas.UsedTiles[TileIndex])
			{
				return AddShadowInfo(Pool, Key, AtlasIndex, TileIndex);
			}
		}
	}

	if (!AddNewAtlas(Pool, TileResolution))
		return nullptr;

	const uint32 AtlasIndex = Pool.ShadowAtlasArray.Num() - 1;
	return AddShadowInfo(Pool, Key, AtlasIndex, 0);
}

// 존재하지 않는 풀은 만들지 않고, 사용하던 타일만 반환한다.
void FShadowViewRenderer::ReleaseShadowInfo(ELightType Type, const FShadowAllocationKey& Key)
{
	FShadowAtlasPool* Pool = AtlasPools.Find(Type);
	if (!Pool)
		return;

	if (const FShadowData* Data = Pool->ShadowInfoMap.Find(Key))
	{
		Pool->ShadowAtlasArray[Data->AtlasIndex].UsedTiles[Data->TileIndex] = false;
		Pool->ShadowInfoMap.Remove(Key);
	}
}

// 활성 목록에 종류 자체가 없으면 해당 풀의 모든 할당을 반환한다.
void FShadowViewRenderer::ReleaseUnusedShadowInfos(const TMap<ELightType, TSet<FShadowAllocationKey>>& ActiveAllocations)
{
	for (auto& [Type, Pool] : AtlasPools)
	{
		const TSet<FShadowAllocationKey>* ActiveKeys = ActiveAllocations.Find(Type);
		TArray<FShadowAllocationKey> RemovedKeys;
		for (const auto& Entry : Pool.ShadowInfoMap)
		{
			if (!ActiveKeys || !ActiveKeys->Contains(Entry.first))
				RemovedKeys.Add(Entry.first);
		}
		// 맵 순회가 끝난 뒤 제거해 반복자를 무효화하지 않는다.
		for (const FShadowAllocationKey& Key : RemovedKeys)
			ReleaseShadowInfo(Type, Key);
	}
	// TODO(ShadowAtlas): 빈 페이지를 회수할 때 남은 할당의 AtlasIndex 갱신도 함께 처리한다.
}

// shadow cache 확인 
bool FShadowViewRenderer::CheckShadowCache(ELightType InType, const FShadowAllocationKey& InAllocKey, const FShadowCacheKey& InCacheKey) const
{
	const FShadowAtlasPool* Pool = AtlasPools.Find(InType);
	if (!Pool)
		return false;

	if (const FShadowData* Data = Pool->ShadowInfoMap.Find(InAllocKey))
	{
		return Data->bIsValid && Data->CacheKey == InCacheKey;
	}
	return false;
}

// shadow cache 갱신
void FShadowViewRenderer::UpdateShadowCache(ELightType InType, const FShadowAllocationKey& InAllocKey, const FShadowCacheKey& InCacheKey)
{
	FShadowAtlasPool* Pool = AtlasPools.Find(InType);
	if (!Pool)
		return;

	if (FShadowData* Data = Pool->ShadowInfoMap.Find(InAllocKey))
	{
		Data->CacheKey = InCacheKey;
		Data->bIsValid = true;
	}
}