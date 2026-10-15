#include "EnginePCH.h"
#include "ToneMapRenderer.h"
#include "RenderCommand.h"
#include "RenderResourceManager.h"
#include "Texture2D.h"

bool FToneMapRenderer::Init()
{
	ToneMapShader = FRenderResourceManager::GetShaderProgram("Resources/Shader/ToneMapping.hlsl");
	if (!ToneMapShader)
	{
		HTR_LOG(Error, "[ToneMapping] shader not found");
		return false;
	}

	ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FToneMapConstants));

	PipelineState.Shader = ToneMapShader;
	PipelineState.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	// 결과로 덮어쓰는 후처리라 블렌딩,깊이 테스트가 필요 없음
	PipelineState.BlendState = EBlendState::Opaque;
	PipelineState.DepthStencilState = EDepthStencilState::Disabled;
	return true;
}

// HDR(선형) 입력을 톤 매핑·sRGB 인코딩해 현재 바인딩된 LDR 타깃에 쓴다.
void FToneMapRenderer::OnRender(FTexture2D* InputTexture, EToneMapMode Mode)
{
	if (!InputTexture || !ConstantBuffer)
		return;
	if (InputTexture->GetWidth() == 0 || InputTexture->GetHeight() == 0)
		return;

	FToneMapConstants Constants{};
	Constants.Exposure = Exposure;
	Constants.Mode = static_cast<uint32>(Mode);

	RenderCommand::BindPipelineState(PipelineState);
	RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Constants);
	RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(0, InputTexture, EShaderBindFlagBits::Pixel);

	RenderCommand::Draw(3);
}
