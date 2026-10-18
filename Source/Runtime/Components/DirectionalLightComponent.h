#pragma once

#include "LightComponent.h"

class UDirectionalLightComponent :public ULightComponent
{
	DECLARE_CLASS(UDirectionalLightComponent, ULightComponent)
	REFLECT_START(UDirectionalLightComponent)
		PROPERTY(ShadowDistance)
	REFLECT_END()
public:
	UDirectionalLightComponent();
	virtual ~UDirectionalLightComponent() override;

	virtual class FLightSceneProxy* CreateSceneProxy() const override;

	ELightType GetLightType() const override { return ELightType::Directional; }

	// 입력 설정의 유효 범위는 Component에서 보장한다.
	float GetShadowDistance() const { return FMath::Clamp(ShadowDistance, 1.0f, 100000.0f); }

	// 편집한 값도 실제 계산에 사용하는 범위로 맞춘다.
	void PostEditChangeProperty(const FProperty& Property) override;
private:
	float ShadowDistance = 100.0f;
};