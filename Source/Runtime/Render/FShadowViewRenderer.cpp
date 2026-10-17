#include "EnginePCH.h"

#include "FShadowViewRenderer.h"

#include "EnginePCH.h"
#include "RenderCommand.h"
#include "RenderResourceManager.h"
#include "Render/Buffer.h"


bool FShadowViewRenderer::Init()
{
	Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/ShadowViewShader.hlsl");
	if (!Shader)
	{
		HTR_LOG(Error, "[ShadowMap] shader not found");
		return false;
	}

	ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FShadowViewConstants));

	PipelineState.Shader = Shader;
	return true;
}

void FShadowViewRenderer::OnRender(FTexture2D* depthTexture, const FMatrix& LightSpaceMatrix, const FMatrix& WorldMatrix)
{
	FShadowViewConstants Constants;
	Constants.LightSpaceMatrix = LightSpaceMatrix;
	Constants.WorldMatrix = WorldMatrix;

	RenderCommand::BindPipelineState(PipelineState);
	RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Constants);
	RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Vertex);
}


void FShadowViewRenderer::AddShadowInfo(int32 index, TUniquePtr<FTexture2D> ShadowMap, FMatrix LightSpaceMatrix)
{
	FShadowData ShadowInfo;
	ShadowInfo.LightSpaceMatrix = LightSpaceMatrix;
	ShadowInfo.ShadowTextureMap = std::move(ShadowMap);
	ShadowTextureMap[index] = std::move(ShadowInfo);
}

