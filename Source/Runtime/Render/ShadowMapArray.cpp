#include "EnginePCH.h"
#include "ShadowMapArray.h"
#include "Core/EngineLog.h"

FShadowMapArray::FShadowMapArray(ID3D11Device* Device, uint32 InResolution, uint32 InSliceCount)
{
	Width = Height = InResolution;
	Depth = InSliceCount;
	Format = DXGI_FORMAT_R32_TYPELESS;
	Dimension = ETextureDimension::Texture2DArray;

    D3D11_TEXTURE2D_DESC Desc{};
    Desc.Width = Desc.Height = InResolution;
    Desc.MipLevels = 1;
    Desc.ArraySize = InSliceCount;
    Desc.Format = DXGI_FORMAT_R32_TYPELESS;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = Device->CreateTexture2D(&Desc, nullptr, (ID3D11Texture2D**)Texture.GetAddressOf());
    if (FAILED(hr)) 
    { 
        //HTR_LOG(ELogVerbosity::Error, "[ShadowMapArray] CreateTexture2D failed (hr=0x{:08X})", (uint32)hr); 
        return; 
    }

    // 배열 전체 SRV — 셰이더의 Texture2DArray<float>
    D3D11_SHADER_RESOURCE_VIEW_DESC SrvDesc{};
    SrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    SrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    SrvDesc.Texture2DArray.MipLevels = 1;
    SrvDesc.Texture2DArray.FirstArraySlice = 0;
    SrvDesc.Texture2DArray.ArraySize = InSliceCount;
    Device->CreateShaderResourceView(Texture.Get(), &SrvDesc, SRV.GetAddressOf());

    // 슬라이스별 DSV — 5단계에서 라이트 하나를 그릴 때 이것만 묶는다
    SliceDSVs.SetNum(InSliceCount);
    for (uint32 Slice = 0; Slice < InSliceCount; ++Slice)
    {
        D3D11_DEPTH_STENCIL_VIEW_DESC DsvDesc{};
        DsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        DsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        DsvDesc.Texture2DArray.MipSlice = 0;
        DsvDesc.Texture2DArray.FirstArraySlice = Slice;
        DsvDesc.Texture2DArray.ArraySize = 1;
        Device->CreateDepthStencilView(Texture.Get(), &DsvDesc, SliceDSVs[Slice].GetAddressOf());
    }
}
