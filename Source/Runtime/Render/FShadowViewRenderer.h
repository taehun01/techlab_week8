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
private:
	FPipelineState PipelineState;
	FShaderProgram* Shader = nullptr;
	TUniquePtr<FConstantBuffer> ConstantBuffer;

};