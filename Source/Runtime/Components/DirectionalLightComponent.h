#pragma once

#include "LightComponent.h"

class UDirectionalLightComponent :public ULightComponent
{
	DECLARE_CLASS(UDirectionalLightComponent, ULightComponent)
	REFLECT_START(UDirectionalLightComponent)
		PROPERTY(ShadowDistance)
		PROPERTY(bUseCSM)
		PROPERTY(CascadeCount)
		PROPERTY(CascadeDistributionExponent)
		PROPERTY(CascadeTransitionFraction)
		PROPERTY(SourceAngle)
	REFLECT_END()
public:
	UDirectionalLightComponent();
	virtual ~UDirectionalLightComponent() override;

	virtual class FLightSceneProxy* CreateSceneProxy() const override;

	ELightType GetLightType() const override { return ELightType::Directional; }

	// 입력 설정의 유효 범위는 Component에서 보장한다.
	float GetShadowDistance() const { return FMath::Clamp(ShadowDistance, 1.0f, 100000.0f); }
	// 전체 각지름(도). 셰이더에는 라디안 단위 각반경으로 변환해 전달한다.
	float GetSourceAngle() const { return FMath::Clamp(SourceAngle, 0.0f, 178.0f); }
	void SetSourceAngle(float InAngle) { SourceAngle = FMath::Clamp(InAngle, 0.0f, 178.0f); }

	// 현재 지원하는 View별 Directional Cascade 개수의 상한이다.
	static constexpr int32 MaxShadowCascades = 4;

	// CSM 사용 여부와 편집된 Cascade 개수는 별도로 보관한다.
	bool IsCSMEnabled() const { return bUseCSM; }
	void SetCSMEnabled(bool bEnabled) { bUseCSM = bEnabled; }
	int32 GetCascadeCount() const { return FMath::Clamp(CascadeCount, 1, MaxShadowCascades); }

	// 분포 1은 균등 분할이며, 값이 커질수록 가까운 구간이 짧아진다.
	float GetCascadeDistributionExponent() const
	{
		return FMath::Clamp(CascadeDistributionExponent, 1.0f, 10.0f);
	}
	float GetCascadeTransitionFraction() const
	{
		return FMath::Clamp(CascadeTransitionFraction, 0.0f, 0.5f);
	}
	void SetCascadeCount(int32 InCount)
	{
		CascadeCount = FMath::Clamp(InCount, 1, MaxShadowCascades);
	}
	// 편집한 값도 실제 계산에 사용하는 범위로 맞춘다.
	void PostEditChangeProperty(const FProperty& Property) override;
private:
	// 기존 각반경 0.01 rad의 외형을 유지하는 전체 각지름(도).
	float SourceAngle = 1.1459156f;

	// 카메라 전방에서 그림자를 제공할 최대 Recevier 깊이이다.
	float ShadowDistance = 100.0f;

	// 기존 Scene은 단일 Shadow Map 설정으로 시작한다.
	bool bUseCSM = false;

	// CSM을 꺼도 사용자가 지정한 개수는 유지한다.
	int32 CascadeCount = 4;

	// 각 구간 길이의 가중치를 1, E, E² 순서로 증가시킨다.
	float CascadeDistributionExponent = 3.0f;

	// Cascade 경계에서 두 그림자 결과를 섞을 구간의 비율이다.
	float CascadeTransitionFraction = 0.1f;
};
