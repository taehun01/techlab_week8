#pragma once

#include "LightSceneProxy.h"

class FProjectedShadowInfo;

class FDirectionalLightSceneProxy : public FLightSceneProxy
{
public:
	using FLightSceneProxy::FLightSceneProxy;
	void GetLightShaderParameters(FLightRenderParameters& Out) const override;

	// Receiver 범위로 Depth VP를 계산하고 광원 쪽으로 열린 Caster 범위를 설정한다.
	bool SetupShadowProjection(const TArray<FVector>& ReceiverCorners, FProjectedShadowInfo& OutInfo) const;

	// Perspective View에 대해, Cascade를 위한 경계값을 리턴한다.
	bool CalculateCascadeSplitDistances(float ViewNear, float ViewFar, TArray<float>& OutSplitDistances) const;
};
