#include "EnginePCH.h"
#include "ShadowMapRenderer.h"

#include "Renderer.h"
#include "RenderCommand.h"
#include "RenderResourceManager.h"
#include "Engine/World.h"
#include "Engine/LightSceneProxy.h"
#include "Engine/SpotLightSceneProxy.h"
#include "Components/SpotLightComponent.h"

bool FShadowMapRenderer::Init()
{
	ShadowConstants = RenderCommand::CreateConstantBuffer(sizeof(FViewConstants));
	ShadowBuffer = RenderCommand::CreateDynamicStructuredBuffer(sizeof(FLightRenderParameters), MaxLocalLights);

	D3D11_TEXTURE2D_DESC Desc{};
	Desc.Width = 1024;
	Desc.Height = 1024;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_DEFAULT;
	Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

	SpotShadowDepth = RenderCommand::CreateTexture2D(Desc);

	return true;
}

void FShadowMapRenderer::PrepareShadows(FScene& Scene)
{
	for (FLocalLightSceneProxy* Proxy : Scene.LocalLightProxies)
	{
		if (!Proxy->IsVisible())
			continue;

		if (!Proxy->IsCastShadow())
			continue;

		FSpotLightSceneProxy* Spot = dynamic_cast<FSpotLightSceneProxy*>(Proxy);

		if (!Spot)
			continue;

		SelectedSpotLight = Spot;

		FShadowRequest Req;
		Req.Light = Spot;
		Req.LightType = ELightType::Rect;
		Req.PackedLightIndex = Spot->GetIndex();
		Req.Priority;
		Req.RequestedResolution;

		SelectedLights.Add(Req);
		break;
	}

	if (SelectedSpotLight)
	{
		const USpotLightComponent* Spot = static_cast<const USpotLightComponent*>(SelectedSpotLight->GetLightComponent());
		FVector LightPosition = Spot->GetWorldLocation();
		FQuat LightRotation = Spot->GetWorldRotation().Quaternion();
		FMatrix RotationMatrix = LightRotation.ToFMatrix();

		FMatrix InverseTranslation = FMatrix::MakeTranslation(LightPosition * -1.0f);
		FMatrix InverseRotation = RotationMatrix.GetTransposed();

		LightView = InverseTranslation * InverseRotation;
		LightProjection = Spot->CreateProjectionMatrix();
		LightViewProjection = LightView * LightProjection;

		FShadowData Data;
		Data.LightVP = LightViewProjection;
		// Spot 라이트만 고려 1장씩, 포인트면 6장씩 추가하는 로직 추가 필요
		Data.Bias = 0.001f;
		Data.TextureSlice = SelectedLights.Num() - 1;

		ShadowDatas.Add(Data);
	}

}

void FShadowMapRenderer::RenderShadows(UWorld* World, FRenderer* Renderer)
{
	if (!SelectedSpotLight)
		return;

	FRenderQueue ShadowQueue;

	World->GatherShadowCasters(LightViewProjection, ShadowQueue);

	FRenderingInfo PassInfo{};
	PassInfo.DepthStencil.Texture = SpotShadowDepth.get();
	PassInfo.DepthStencil.LoadOp = ERenderTargetLoadOp::Clear;
	PassInfo.ViewportSetting.Width = 1024;
	PassInfo.ViewportSetting.Height = 1024;

	RenderCommand::BeginRenderPass(PassInfo);

	Renderer->RenderShadowDepth(LightViewProjection, ShadowQueue);

	RenderCommand::EndRenderPass(PassInfo);
}

void FShadowMapRenderer::UploadShadowData()
{
	uint32 NumLights = ShadowDatas.Num();

	if (NumLights == 0) return;

	RenderCommand::UpdateBufferData(
		ShadowBuffer.get(),
		ShadowDatas.GetData(),
		NumLights * sizeof(FShadowData)
	);
}

void FShadowMapRenderer::BindShadows()
{
	FViewConstants ViewConstants;
	ViewConstants.ViewProjection = LightViewProjection;
	ViewConstants.CameraWorldPos = FVector(0, 0, 0);
	RenderCommand::UpdateBufferData(ShadowConstants.get(), &ViewConstants);
	RenderCommand::BindConstantBuffer(6, ShadowConstants.get(), EShaderBindFlagBits::Pixel);

	//RenderCommand::BindShaderResource(14, ShadowBuffer.get(), EShaderBindFlagBits::Pixel);

	RenderCommand::BindShaderResource(13, SpotShadowDepth.get(), EShaderBindFlagBits::Pixel);
}

void FShadowMapRenderer::UnbindShadows()
{
	//RenderCommand::UnbindShaderResource(14, EShaderBindFlagBits::Pixel);
	RenderCommand::UnbindShaderResource(13, EShaderBindFlagBits::Pixel);
}


//void FShadowMapRenderer::AddShadowRequest(const FLocalLightSceneProxy* Light)
//{
//	FShadowRequest Req;
//	Req.Light = Light;
//	Req.PackedLightIndex = ;
//	Req.LightType = ;
//	Req.Priority = ;
//	Req.RequestedResolution = ;
//
//	ShadowRequests.Add(Req);
//}