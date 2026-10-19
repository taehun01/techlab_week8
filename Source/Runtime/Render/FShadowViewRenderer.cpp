#include "EnginePCH.h"
#include "FShadowViewRenderer.h"
#include "RenderCommand.h"

bool FShadowViewRenderer::Init()
{
	return !ShadowAtlasArray.IsEmpty() || AddNewAtlas();
}

bool FShadowViewRenderer::AddNewAtlas(uint32 TileResolution)
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
	// 이번 프레임의 전체 Clear 이후 추가되는 페이지도 초기 깊이를 보장한다.
	RenderCommand::ClearDepthStencil(Atlas.Texture.get(), 1.0f, 0);
	ShadowAtlasArray.Add(std::move(Atlas));
	return true;
}

void FShadowViewRenderer::AddShadowInfo(uint32 UUID, uint32 ProjectionID, uint32 AtlasIndex, uint32 TileIndex, const FMatrix& LightSpaceMatrix)
{
	FShadowKey Key;
	Key.UUID = UUID;
	Key.ProjectionIndex = ProjectionID;

	FShadowData Data;
	Data.LightSpaceMatrix = LightSpaceMatrix;
	Data.AtlasIndex = AtlasIndex;
	Data.TileIndex = TileIndex;
	//ShadowInfoMap[UUID] = Data;
	ShadowInfoMap[Key] = Data;
}

bool FShadowViewRenderer::AllocateShadowInfo(uint32 UUID, uint32 ProjectionID, const FMatrix& LightSpaceMatrix, uint32 TileResolution)
{
	FShadowKey Key;
	Key.UUID = UUID;
	Key.ProjectionIndex = ProjectionID;

	if (FShadowData* Data = ShadowInfoMap.Find(Key))	//Find(UUID)
	{
		if (ShadowAtlasArray[Data->AtlasIndex].TileResolution == TileResolution)
		{
			// 기존 UUID는 위치를 유지하고 이번 프레임의 행렬만 갱신한다.
			Data->LightSpaceMatrix = LightSpaceMatrix;
			return true;
		}
		// 에디터에서 해상도를 변경한 조명만 이전 타일을 반환하고 다시 배정한다.
		ReleaseShadowInfo(UUID, ProjectionID);
	}

	for (int32 AtlasIndex = 0; AtlasIndex < ShadowAtlasArray.Num(); ++AtlasIndex)
	{
		FShadowAtlas& Atlas = ShadowAtlasArray[AtlasIndex];
		if (Atlas.TileResolution != TileResolution)
			continue;

		for (uint32 TileIndex = 0; TileIndex < TilesPerAtlas; ++TileIndex)
		{
			if (!Atlas.UsedTiles[TileIndex])
			{
				Atlas.UsedTiles[TileIndex] = true;
				AddShadowInfo(UUID, ProjectionID, AtlasIndex, TileIndex, LightSpaceMatrix);
				return true;
			}
		}
	}

	if (!AddNewAtlas(TileResolution))
		return false;

	const uint32 AtlasIndex = ShadowAtlasArray.Num() - 1;
	ShadowAtlasArray[AtlasIndex].UsedTiles[0] = true;
	AddShadowInfo(UUID, ProjectionID, AtlasIndex, 0, LightSpaceMatrix);
	return true;
}

// UUID가 아니라 Key로 해제하도록 변경 필요
void FShadowViewRenderer::ReleaseShadowInfo(uint32 UUID, uint32 ProjectionID)
{
	FShadowKey Key;
	Key.UUID = UUID;
	Key.ProjectionIndex = ProjectionID;

	if (const FShadowData* Data = ShadowInfoMap.Find(Key))
	{
		ShadowAtlasArray[Data->AtlasIndex].UsedTiles[Data->TileIndex] = false;
		ShadowInfoMap.Remove(Key);
	}
}

void FShadowViewRenderer::ReleaseUnusedShadowInfos(const TSet<uint32>& ActiveLightUUIDs)
{
	TArray<uint32> RemovedUUIDs;
	for (const auto& Entry : ShadowInfoMap)
	{
		if (!ActiveLightUUIDs.Contains(Entry.first))
			RemovedUUIDs.Add(Entry.first);
	}
	for (uint32 UUID : RemovedUUIDs)
		ReleaseShadowInfo(UUID);
}
