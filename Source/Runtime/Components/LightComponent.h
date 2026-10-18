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
		PROPERTY(ShadowPCFRadius)
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
	float GetShadowPCFRadius() const { return FMath::Max(ShadowPCFRadius, 0.0f); }

	void SetCastShadows(bool bInCastShadows) { bCastShadows = bInCastShadows; }
	void SetShadowMapResolution(int32 InResolution) { ShadowMapResolution = InResolution; }
	void SetShadowBias(float InBias) { ShadowBias = InBias; }
	void SetShadowPCFRadius(float InPCFRadius) { ShadowPCFRadius = InPCFRadius; }

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
	// PCF에 사용되는 Radius 값
	float ShadowPCFRadius = 0.1f;

};