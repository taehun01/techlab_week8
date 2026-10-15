#include "EnginePCH.h"
#include "Buffer.h"

FBuffer::FBuffer(ID3D11Device* Device, const D3D11_BUFFER_DESC& Desc, const void* InitialData)
{
	BufferSize = Desc.ByteWidth;

	HRESULT hr;
	if (InitialData)
	{
		D3D11_SUBRESOURCE_DATA Data;
		Data.pSysMem = InitialData;

		hr = Device->CreateBuffer(&Desc, &Data, Buffer.GetAddressOf());
	}
	else
	{
		hr = Device->CreateBuffer(&Desc, nullptr, Buffer.GetAddressOf());
	}
	if (FAILED(hr))
	{
		HTR_LOG(Warning, "Failed To Create Buffer!");
	}
}

FConstantBuffer::FConstantBuffer(ID3D11Device* Device, const D3D11_BUFFER_DESC& Desc)
	:FBuffer(Device, Desc)
{
}

FVertexBuffer::FVertexBuffer(ID3D11Device* Device, const D3D11_BUFFER_DESC& Desc, uint32 InStride, const void* InitialData)
	:FBuffer(Device, Desc, InitialData)
{
	Stride = InStride;
}

FIndexBuffer::FIndexBuffer(ID3D11Device* Device, const D3D11_BUFFER_DESC& Desc, uint32 Count, const void* InitialData, DXGI_FORMAT InFormat)
	:FBuffer(Device, Desc, InitialData), Format(InFormat)
{
	IndexCount = Count;
}

FStructuredBuffer::FStructuredBuffer(ID3D11Device* Device, const D3D11_BUFFER_DESC& Desc, uint32 InStride, uint32 InElementCount, const void* InitialData)
	:FBuffer(Device, Desc, InitialData)
{
	Stride = InStride;
	MaxCount = InElementCount;

	HRESULT hr;
	if (Desc.BindFlags & D3D11_BIND_SHADER_RESOURCE)
	{
		hr = Device->CreateShaderResourceView(Buffer.Get(), nullptr, SRV.GetAddressOf());
		if (FAILED(hr))
			HTR_LOG(Error, "[StructuredBuffer] CreateShaderResourceView failed (hr=0x{:08X})", (uint32)hr);
	}

	if (Desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS)
	{
		hr = Device->CreateUnorderedAccessView(Buffer.Get(), nullptr, UAV.GetAddressOf());
		if (FAILED(hr))
			HTR_LOG(Error, "[StructuredBuffer] CreateUnorderedAccessView failed (hr=0x{:08X})", (uint32)hr);
	}
}
