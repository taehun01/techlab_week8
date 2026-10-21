#include "EnginePCH.h"
#include "FShadowViewRenderer.h"
#include "RenderCommand.h"
#include "RenderResourceManager.h"
#include "Core/Stats/ShadowStats.h"

#include <cassert>

// 고정 크기 페이지를 필요할 때 한 장씩 만든다. 빈 페이지도 이후 재사용한다.
bool FShadowViewRenderer::AddNewAtlas()
{
	if (ShadowAtlasArray.Num() >= static_cast<int32>(MaxAtlasPages))
		return false;

	D3D11_TEXTURE2D_DESC Desc{};
	Desc.Width = AtlasSize;
	Desc.Height = AtlasSize;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_DEFAULT;
	Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

	FShadowAtlas Atlas(AtlasSize);
	Atlas.Texture = RenderCommand::CreateTexture2D(Desc);
	if (!Atlas.Texture || !Atlas.Texture->GetDSV() || !Atlas.Texture->GetSRV())
	{
		HTR_LOG(Error, "Shadow Atlas creation failed: {}x{}", Desc.Width, Desc.Height);
		return false;
	}
	ShadowAtlasArray.Add(std::move(Atlas));
	return true;
}

// 같은 좌표를 재사용하더라도 이전 작업의 캐시 상태를 계승하지 않는다.
const FShadowData* FShadowViewRenderer::AddShadowInfo(const FShadowAllocationKey& Key,
	uint32 AtlasIndex, uint32 BaseX, uint32 BaseY, uint32 Resolution)
{
	FShadowData& Data = ShadowInfoMap[Key];
	Data = FShadowData{};
	Data.AtlasIndex = AtlasIndex;
	Data.BaseX = BaseX;
	Data.BaseY = BaseY;
	Data.Resolution = Resolution;
	return &Data;
}

bool FShadowViewRenderer::Init()
{
	FShaderProgram* Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/ShadowTileClear.hlsl");
	if (!Shader)
	{
		HTR_LOG(Error, "[ShadowTileClear] shader not found");
		return false;
	}

	ClearPipelineState.Shader = Shader;
	ClearPipelineState.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	ClearPipelineState.RasterizerState = ERasterizerState::SolidNone;
	ClearPipelineState.DepthStencilState = EDepthStencilState::AlwaysWrite;
	return true;
}

// 기존 배정을 유지하고, 기존 페이지에 공간이 없을 때만 페이지를 늘린다.
const FShadowData* FShadowViewRenderer::AllocateShadowInfo(const FShadowAllocationKey& Key, uint32 Resolution)
{
	if (Resolution == 0 || Resolution > AtlasSize)
		return nullptr;

	if (const FShadowData* Data = ShadowInfoMap.Find(Key))
	{
		if (Data->Resolution == Resolution)
			return Data;

		ReleaseShadowInfo(Key);
	}

	uint32 BaseX = 0;
	uint32 BaseY = 0;
	for (int32 AtlasIndex = 0; AtlasIndex < ShadowAtlasArray.Num(); ++AtlasIndex)
	{
		FShadowAtlas& Atlas = ShadowAtlasArray[AtlasIndex];
		if (Atlas.Layout.AddElement(BaseX, BaseY, Resolution, Resolution))
			return AddShadowInfo(Key, static_cast<uint32>(AtlasIndex), BaseX, BaseY, Resolution);
	}

	if (!AddNewAtlas())
		return nullptr;

	const uint32 AtlasIndex = static_cast<uint32>(ShadowAtlasArray.Num() - 1);
	if (!ShadowAtlasArray[AtlasIndex].Layout.AddElement(BaseX, BaseY, Resolution, Resolution))
		return nullptr;

	return AddShadowInfo(Key, AtlasIndex, BaseX, BaseY, Resolution);
}

// 배치기의 반환에 성공했을 때만 키를 지워 두 자료구조의 상태를 맞춘다.
void FShadowViewRenderer::ReleaseShadowInfo(const FShadowAllocationKey& Key)
{
	const FShadowData* Data = ShadowInfoMap.Find(Key);
	if (!Data)
		return;

	FTextureLayout& Layout = ShadowAtlasArray[Data->AtlasIndex].Layout;
	const bool bReleased = Layout.RemoveElement(Data->BaseX, Data->BaseY, Data->Resolution, Data->Resolution);
	// 반환 호출은 Release 빌드에서도 실행해야 하므로 assert 밖에 둔다.
	assert(bReleased);
	if (bReleased)
		ShadowInfoMap.Remove(Key);
}

// 전체 World와 View에서 이번 프레임에 사용하지 않는 할당만 반환한다.
void FShadowViewRenderer::ReleaseUnusedShadowInfos(const TSet<FShadowAllocationKey>& ActiveKeys)
{
	TArray<FShadowAllocationKey> RemovedKeys;
	for (const auto& Entry : ShadowInfoMap)
	{
		if (!ActiveKeys.Contains(Entry.first))
			RemovedKeys.Add(Entry.first);
	}

	// 맵 순회가 끝난 뒤 제거한다. 페이지 배열은 지우거나 재정렬하지 않는다.
	for (const FShadowAllocationKey& Key : RemovedKeys)
		ReleaseShadowInfo(Key);
}

// 실제 Depth 렌더링을 마친 작업에 대해서만 기존 캐시 정보를 갱신한다.
void FShadowViewRenderer::UpdateShadowCache(const FShadowAllocationKey& Key,
	const FShadowCacheKey& CacheKey, uint64 Frame)
{
	if (FShadowData* Data = ShadowInfoMap.Find(Key))
	{
		Data->CacheKey = CacheKey;
		Data->bIsValid = true;
		Data->LastUpdatedFrame = Frame;
	}
}

// 바인딩 된 타일을 클리어
void FShadowViewRenderer::ClearBoundTile()
{
	if (!ClearPipelineState.Shader)
		return;
	RenderCommand::BindPipelineState(ClearPipelineState);
	RenderCommand::Draw(3);
	RenderCommand::SetDepthStencilState(EDepthStencilState::Default);
	// Draw(3)는 DrawIndexed 카운트에 잡히지 않으므로 따로 센다.
	FStatRegistry::AddCounter(ShadowStats::STAT_TileClears);
}
