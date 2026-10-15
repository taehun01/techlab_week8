#pragma once
#include "PointLightComponent.h"

class UFireBallComponent : public UPointLightComponent
{
	DECLARE_CLASS(UFireBallComponent, UPointLightComponent)

	REFLECT_START(ClassName)
		PROPERTY(Intensity)
		REFLECT_END()

public:
	UFireBallComponent();
	virtual ~UFireBallComponent() override;

	ELightType GetLightType() const { return ELightType::Fireball; }

private:
};
