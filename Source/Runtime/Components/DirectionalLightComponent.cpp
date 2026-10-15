#include "EnginePCH.h"
#include "DirectionalLightComponent.h"

#include "Engine/DirectionalLightSceneProxy.h"

UDirectionalLightComponent::UDirectionalLightComponent()
{

}

UDirectionalLightComponent::~UDirectionalLightComponent()
{
}

FLightSceneProxy* UDirectionalLightComponent::CreateSceneProxy() const
{
    return new FDirectionalLightSceneProxy(this);
}
