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

void UDirectionalLightComponent::PostEditChangeProperty(const FProperty& Property)
{
    // 부모의 해상도 등 기존 그림자 설정 편집 처리를 유지한다.
    Super::PostEditChangeProperty(Property);

    if (Property.Name == "ShadowDistance")
        ShadowDistance = GetShadowDistance();
}