#pragma once

#include "LocalLightSceneProxy.h"

class FPointLightSceneProxy : public FLocalLightSceneProxy
{
public:
    using FLocalLightSceneProxy::FLocalLightSceneProxy;
    void GetLightShaderParameters(FLightRenderParameters& Out) const override;

    // 포인트라이트는 ViewProjection 6개를 반환해야함 지금은 하나만 바꾸니까 다른 구조가 필요함
    // FMatrix 배열? 아니면 
    virtual bool GetShadowProjection(FShadowProjection& OutShadowProjection) const override;
};