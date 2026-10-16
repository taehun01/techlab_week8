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

class FShadowViewRenderer
{
public:
	FShadowViewRenderer() = default;
	~FShadowViewRenderer() = default;

	FShadowViewRenderer(const FShadowViewRenderer&) = delete;
	FShadowViewRenderer operator=(const FShadowViewRenderer&) = delete;

	bool Init();
	void OnRender(FTexture2D* depthTexture, const FMatrix& ViewProjection, const FMatrix& WorldMatrix);
	TUniquePtr<FTexture2D>& GetShadowTarget() { return CurrentShadowMap; }
	void AddShadowMap(int32 index, FTexture2D* ShadowMap);
	void ResetCurrentShadowMap();

private:
	FPipelineState PipelineState;
	FShaderProgram* Shader = nullptr;
	TUniquePtr<FConstantBuffer> ConstantBuffer;
	TMap<int32, FTexture2D> ShadowTextureMap;
	TUniquePtr<FTexture2D> CurrentShadowMap;
};