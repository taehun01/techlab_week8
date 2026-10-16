#include "EnginePCH.h"

#include "FShadowViewRenderer.h"

#include "EnginePCH.h"
#include "RenderCommand.h"
#include "RenderResourceManager.h"
#include "Render/Buffer.h"


bool FShadowViewRenderer::Init()
{
	Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/DepthViewShader.hlsl");
	if (!Shader)
	{
		HTR_LOG(Error, "[ShadowMap] shader not found");
		return false;
	}


	ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FShadowViewConstants));

	PipelineState.Shader = Shader;
	return true;
}

void FShadowViewRenderer::OnRender(FTexture2D* depthTexture, const FMatrix& ViewProjection, const FVector& LightPosition, const FVector& ViewLightForward)
{
	FShadowViewConstants Constants;
	Constants.InverseViewProjection = ViewProjection.Inverse();
	Constants.CameraPosition = LightPosition;
	Constants.VisualizeRange = 50;
	Constants.CameraForward = ViewLightForward;

	RenderCommand::BindPipelineState(PipelineState);
	RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Constants);
	RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Pixel);
}
