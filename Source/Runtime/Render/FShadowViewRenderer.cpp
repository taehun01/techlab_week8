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

	D3D11_TEXTURE2D_DESC Desc{};
	Desc.Width = 1920;
	Desc.Height = 1080;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_DEFAULT;
	Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL| D3D11_BIND_SHADER_RESOURCE;
	CurrentShadowMap = RenderCommand::CreateTexture2D(Desc);

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

void FShadowViewRenderer::ResetCurrentShadowMap()
{
	CurrentShadowMap = nullptr;
	D3D11_TEXTURE2D_DESC Desc{};
	Desc.Width = 1920;
	Desc.Height = 1080;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_DEFAULT;
	Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	CurrentShadowMap = RenderCommand::CreateTexture2D(Desc);
}

void FShadowViewRenderer::AddShadowMap(int32 index, FTexture2D* ShadowMap)
{
	ShadowTextureMap.Add(index, *ShadowMap);
}

