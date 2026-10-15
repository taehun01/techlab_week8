#pragma once

#include "LightComponent.h"

class UDirectionalLightComponent :public ULightComponent
{
	DECLARE_CLASS(UDirectionalLightComponent, ULightComponent)
	REFLECT_START(UDirectionalLightComponent)
	REFLECT_END()
public:
	UDirectionalLightComponent();
	virtual ~UDirectionalLightComponent() override;

	virtual class FLightSceneProxy* CreateSceneProxy() const override;

	ELightType GetLightType() const override { return ELightType::Directional; }


private:
};