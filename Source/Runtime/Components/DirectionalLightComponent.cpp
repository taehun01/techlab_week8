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

    // Getter와 같은 범위를 사용해 편집값과 실제 계산값을 맞춘다.
    if (Property.Name == "ShadowDistance")
        ShadowDistance = GetShadowDistance();
    else if (Property.Name == "CascadeCount")
        CascadeCount = GetCascadeCount();
    else if (Property.Name == "CascadeDistributionExponent")
        CascadeDistributionExponent = GetCascadeDistributionExponent();
    else if (Property.Name == "CascadeTransitionFraction")
        CascadeTransitionFraction = GetCascadeTransitionFraction();
    else if (Property.Name == "SourceAngle")
        SourceAngle = GetSourceAngle();
}
