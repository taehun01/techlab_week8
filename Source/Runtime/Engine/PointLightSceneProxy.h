#pragma once

#include "LocalLightSceneProxy.h"

class FPointLightSceneProxy : public FLocalLightSceneProxy
{
public:
    using FLocalLightSceneProxy::FLocalLightSceneProxy;
    void GetLightShaderParameters(FLightRenderParameters& Out) const override;

    // +X, -X, +Y, -Y, +Z, -Z 순서로 그림자 투영 여섯 개를 계산한다.
    virtual bool GetShadowProjection(FShadowProjection& OutShadowProjection) const override;
};
