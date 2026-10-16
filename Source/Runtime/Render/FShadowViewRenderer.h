#pragma once

#include "EnginePCH.h"

#include "Buffer.h"
#include "PipelineState.h"
#include "Texture2D.h"

struct FShadowViewConstants
{
	FMatrix InverseViewProjection;
	FVector CameraPosition;
	float   VisualizeRange;
	FVector CameraForward;
	float   Padding;
};

class FShadowViewRenderer
{
public:
	FShadowViewRenderer() = default;
	~FShadowViewRenderer() = default;

	FShadowViewRenderer(const FShadowViewRenderer&) = delete;
	FShadowViewRenderer operator=(const FShadowViewRenderer&) = delete;

	bool Init();
	void OnRender(FTexture2D* depthTexture, const FMatrix& ViewProjection, const FVector& LightPosition, const FVector& ViewLightForward);
private:
	FPipelineState PipelineState;
	FShaderProgram* Shader = nullptr;
	TUniquePtr<FConstantBuffer> ConstantBuffer;

};