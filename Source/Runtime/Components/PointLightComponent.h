#pragma once

#include "LocalLightComponent.h"

class UPointLightComponent : public ULocalLightComponent
{
	DECLARE_CLASS(UPointLightComponent, ULocalLightComponent)
	REFLECT_START(UPointLightComponent)
		PROPERTY(LightFalloffExponent)
		PROPERTY(SourceRadius)
	REFLECT_END()
public:
	UPointLightComponent();
	virtual ~UPointLightComponent() override;

	virtual FLightSceneProxy* CreateSceneProxy() const override;

	void DrawDebug(FLineBatcher* LineBatcher) const override;
	ELightType GetLightType() const override { return ELightType::Point; }

	float GetFalloff() const { return LightFalloffExponent; }
	void SetFalloff(float InFalloff) { LightFalloffExponent = InFalloff; }
	float GetSourceRadius() const { return FMath::Max(SourceRadius, 0.0f); }
	void SetSourceRadius(float InRadius) { SourceRadius = FMath::Max(InRadius, 0.0f); }
	void PostEditChangeProperty(const FProperty& Property) override;


protected:
	float LightFalloffExponent = 1.0f;
private:
	// 월드 단위 발광 반지름. Spot도 이 속성을 상속한다.
	float SourceRadius = 0.03f;
};
