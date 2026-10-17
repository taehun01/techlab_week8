#pragma once
#include "Texture.h"
#include "Core/Types.h"
#include "Containers/Array.h"

#include <d3d11.h>
#include <wrl/client.h>


class FShadowMapArray : public FTexture
{
public:
	FShadowMapArray(ID3D11Device* Device, uint32 InResolution, uint32 InSliceCount);

	uint32 GetResolution() const { return Width; }
	uint32 GetSliceCount() const { return Depth; }
	ID3D11DepthStencilView* GetSliceDSV(uint32 Slice) const { return Slice < static_cast<uint32>(SliceDSVs.Num()) ? SliceDSVs[Slice].Get() : nullptr; }
	uint64 GetMemoryBytes() const { return uint64(Width) * uint64(Height) * 4ull * Depth; }

private:
	TArray<Microsoft::WRL::ComPtr<ID3D11DepthStencilView>> SliceDSVs;
};