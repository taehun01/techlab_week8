#pragma once

#include "LocalLightSceneProxy.h"

class FPointLightSceneProxy : public FLocalLightSceneProxy
{
public:
    using FLocalLightSceneProxy::FLocalLightSceneProxy;
    void GetLightShaderParameters(FLightRenderParameters& Out) const override;
};