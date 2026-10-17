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
	Desc.Width = 2048;
	Desc.Height = 2048;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_DEFAULT;
	Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	TUniquePtr<FTexture2D> ShadowAtlasTexture = RenderCommand::CreateTexture2D(Desc);
	FShadowAtlas ShadowAtlas;
	ShadowAtlas.Texture = std::move(ShadowAtlasTexture);
	ShadowAtlasArray.Add(std::move(ShadowAtlas));


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


void FShadowViewRenderer::AddShadowInfo(int32 UUID, int32 Atlasindex, int32 Tileindex, FMatrix LightSpaceMatrix)
{
	FShadowData ShadowInfo;
	ShadowInfo.LightSpaceMatrix = LightSpaceMatrix;
	ShadowInfo.AtlasIndex = Atlasindex;
	ShadowInfo.TileIndex = Tileindex;
	ShadowInfoMap[UUID] = ShadowInfo;
}

void FShadowViewRenderer::AddNewAtlas()
{
	D3D11_TEXTURE2D_DESC Desc{};
	Desc.Width = 2048;
	Desc.Height = 2048;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_DEFAULT;
	Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	TUniquePtr<FTexture2D> ShadowAtlasTexture = RenderCommand::CreateTexture2D(Desc);
	FShadowAtlas ShadowAtlas;
	ShadowAtlas.Texture = std::move(ShadowAtlasTexture);
	ShadowAtlasArray.Add(std::move(ShadowAtlas));
}

