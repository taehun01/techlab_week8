#pragma once
#include "Render/PipelineState.h"
#include "Render/Buffer.h"

class FTexture2D;

// ToneMapping.hlsl의 Mode 값과 같아야 한다.
enum class EToneMapMode : uint32
{
	ACES = 0,          // Lit: 노출 → ACES → sRGB 인코딩
	EncodeOnly = 1,    // Unlit: 선형 → sRGB 인코딩만 (텍스처 원본 색)
	Passthrough = 2,   // WorldNormal·Heatmap: 셰이더가 쓴 값 그대로
};

// ToneMapping.hlsl의 cbuffer ToneMapConstants와 바이트 단위로 같아야 한다.
struct alignas(16) FToneMapConstants
{
	float Exposure = 1.0f;
	uint32 Mode = static_cast<uint32>(EToneMapMode::ACES);
	float Padding[2]{};
};
static_assert(sizeof(FToneMapConstants) == 16);

class FToneMapRenderer
{
public:
	FToneMapRenderer() = default;
	~FToneMapRenderer() = default;

	bool Init();
	void OnRender(FTexture2D* InputTexture, EToneMapMode Mode);

	void SetExposure(float InExposure) { Exposure = InExposure; }
	float GetExposure() const { return Exposure; }

	FToneMapRenderer(const FToneMapRenderer&) = delete;
	FToneMapRenderer& operator=(const FToneMapRenderer&) = delete;


private:
	FShaderProgram* ToneMapShader = nullptr;
	FPipelineState PipelineState;
	TUniquePtr<FConstantBuffer> ConstantBuffer;
	float Exposure = 1.0f;
};
