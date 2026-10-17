#pragma once

#include "EnginePCH.h"

#include "Buffer.h"
#include "PipelineState.h"
#include "Texture2D.h"

struct FShadowViewConstants
{
	FMatrix LightSpaceMatrix;
	FMatrix WorldMatrix;
};

struct FShadowData
{
	FMatrix LightSpaceMatrix;
	uint32 AtlasIndex;
	uint32 TileIndex;
};

struct FShadowAtlas
{
	TUniquePtr<FTexture2D> Texture;
	bool UsedTiles[4] = {false, false, false, false};
};

class FShadowViewRenderer
{
public:
	FShadowViewRenderer() = default;
	~FShadowViewRenderer() = default;

	FShadowViewRenderer(const FShadowViewRenderer&) = delete;
	FShadowViewRenderer operator=(const FShadowViewRenderer&) = delete;

	bool Init();
	void OnRender(FTexture2D* depthTexture, const FMatrix& ViewProjection, const FMatrix& WorldMatrix);
	void AddShadowInfo(int32 UUID, int32 Atlasindex, int32 Tileindex, FMatrix LightSpaceMatrix);
	void ResetCurrentShadowMap();
	void AddNewAtlas();

	TMap<int32, FShadowData> ShadowInfoMap;
	TArray<FShadowAtlas> ShadowAtlasArray;
private:
	FPipelineState PipelineState;
	FShaderProgram* Shader = nullptr;
	TUniquePtr<FConstantBuffer> ConstantBuffer;
	
};