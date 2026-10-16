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
	REFLECT_END()
public:
	ULightComponent();
	virtual ~ULightComponent() override;

	virtual ELightType GetLightType() const = 0;
	virtual void DrawDebug(FLineBatcher* LineBatcher) const;

	bool CastsShadows() const { return bCastShadows; }
	int32 GetShadowMapResolution() const { return FMath::Clamp(ShadowMapResolution, 128, 4096); }
	float GetShadowBias() const { return FMath::Max(ShadowBias, 0.0f); }

	void SetCastShadows(bool bInCastShadows) { bCastShadows = bInCastShadows; }
	void SetShadowMapResolution(int32 InResolution) { ShadowMapResolution = InResolution; }
	void SetShadowBias(float InBias) { ShadowBias = InBias; }

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

};