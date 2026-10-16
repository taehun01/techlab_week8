#pragma once

#include "PointLightSceneProxy.h"

class FSpotLightSceneProxy : public FPointLightSceneProxy
{
public:
    using FPointLightSceneProxy::FPointLightSceneProxy;
    virtual void GetLightShaderParameters(FLightRenderParameters& Out) const override;

    uint32 GetNumShadowViews() const override { return 1; }
    bool GetShadowView(uint32 ViewIndex, FShadowViewInfo& Out) const override;
};