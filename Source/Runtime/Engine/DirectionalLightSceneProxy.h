#pragma once

#include "LightSceneProxy.h"

class FProjectedShadowInfo;

class FDirectionalLightSceneProxy : public FLightSceneProxy
{
public:
	using FLightSceneProxy::FLightSceneProxy;
	void GetLightShaderParameters(FLightRenderParameters& Out) const override;

	// Receiver 범위로 Projection을 계산하고 지정한 View·Cascade 작업을 설정한다.
	bool SetupShadowProjection(const TArray<FVector>& ReceiverCorners,
		FProjectedShadowInfo& OutInfo, uint32 ProjectionIndex, uint32 ViewId) const;

	// Perspective View에 대해, Cascade를 위한 경계값을 리턴한다.
	bool CalculateCascadeSplitDistances(float ViewNear, float ViewFar, TArray<float>& OutSplitDistances) const;
};
