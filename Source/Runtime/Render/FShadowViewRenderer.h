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
	void AddShadowMap(int32 index, TUniquePtr<FTexture2D> ShadowMap);
	FTexture2D* GetShadowMapbyUUID(int32 UUID) { return ShadowTextureMap[UUID].get(); }
	void ResetCurrentShadowMap();

	TMap<int32, TUniquePtr<FTexture2D>> ShadowTextureMap;
private:
	FPipelineState PipelineState;
	FShaderProgram* Shader = nullptr;
	TUniquePtr<FConstantBuffer> ConstantBuffer;
};