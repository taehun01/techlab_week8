#pragma once

#include "LightComponentBase.h"
#include "Render/LineBatcher.h"

class ULightComponent : public ULightComponentBase
{
	DECLARE_CLASS(ULightComponent, ULightComponentBase)
	REFLECT_START(ULightComponent)
		PROPERTY(ShadowResolutionScale)
		PROPERTY(ShadowBias)
		PROPERTY(ShadowSlopeBias)
		PROPERTY(ShadowSharpen)
	REFLECT_END()
public:
	ULightComponent();
	virtual ~ULightComponent() override;

	virtual ELightType GetLightType() const = 0;
	virtual void DrawDebug(FLineBatcher* LineBatcher) const;

	virtual class FLightSceneProxy* CreateSceneProxy() const
	{
		return NULL;
	}
	FLightSceneProxy* SceneProxy = nullptr;

	float GetShadowResolutionScale() const { return FMath::Clamp(ShadowResolutionScale, 0.125f, 4.0f); }
	float GetShadowBias() const { return std::max(ShadowBias, 0.0f); }
	float GetShadowSlopeBias() const { return std::max(ShadowSlopeBias, 0.0f); }
	float GetShadowSharpen() const { return FMath::Clamp(ShadowSharpen, 0.0f, 1.0f); }

protected:
	virtual bool ShouldCreateRenderState() const override
	{ return true; }

	virtual void CreateRenderState() override;
	virtual void DestroyRenderState() override;

	float ShadowResolutionScale = 1.f;
	float ShadowBias = 0.5f;
	float ShadowSlopeBias = 0.5f;
	float ShadowSharpen = 0.5f;

private:


};