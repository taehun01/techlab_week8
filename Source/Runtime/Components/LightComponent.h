#pragma once

#include "LightComponentBase.h"
#include "Render/LineBatcher.h"

class ULightComponent : public ULightComponentBase
{
	DECLARE_CLASS(ULightComponent, ULightComponentBase)
	REFLECT_START(ULightComponent)
		PROPERTY(bCastShadows)
		PROPERTY(ShadowMapResolution)
		PROPERTY(ShadowBias)
		PROPERTY(ShadowPCFKernelSize)
		PROPERTY(bUsePCSS)
	REFLECT_END()
public:
	ULightComponent();
	virtual ~ULightComponent() override;

	virtual ELightType GetLightType() const = 0;
	virtual void DrawDebug(FLineBatcher* LineBatcher) const;
	void PostEditChangeProperty(const FProperty& Property) override;
	bool CastsShadows() const { return bCastShadows; }
	int32 GetShadowMapResolution() const { return FMath::Clamp(ShadowMapResolution, 128, 4096); }
	float GetShadowBias() const { return FMath::Max(ShadowBias, 0.0f); }
	int32 GetShadowPCFKernelSize() const { return FMath::Max(ShadowPCFKernelSize, 0); }
	bool IsPCSSEnabled() const { return bUsePCSS; }

	void SetCastShadows(bool bInCastShadows) { bCastShadows = bInCastShadows; }
	void SetShadowMapResolution(int32 InResolution) { ShadowMapResolution = InResolution; }
	void SetShadowBias(float InBias) { ShadowBias = InBias; }
	void SetShadowPCFKernelSize(int32 InKernelSize) { ShadowPCFKernelSize = InKernelSize; }
	void SetPCSSEnabled(bool bEnabled) { bUsePCSS = bEnabled; }

	virtual class FLightSceneProxy* CreateSceneProxy() const
	{
		return NULL;
	}
	FLightSceneProxy* SceneProxy = nullptr;
protected:
	virtual bool ShouldCreateRenderState() const override
	{ return true; }

	virtual void CreateRenderState() override;
	virtual void DestroyRenderState() override;
private:
	bool bCastShadows = true;
	// 해상도
	int32 ShadowMapResolution = 1024;
	// Receiver의 깊이 비교에 사용할 Bias
	float ShadowBias = 0.001f;

	// 한 축의 샘플 수. 0은 Hard Shadow, 1은 중심의 하드웨어 PCF 한 번이다.
	int32 ShadowPCFKernelSize = 2;
	// 기존 씬의 PCF를 유지하고, 선택한 라이트에서 PCSS를 사용할 수 있다.
	bool bUsePCSS = false;

};
