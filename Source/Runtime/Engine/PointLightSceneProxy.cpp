#include "EnginePCH.h"
#include "PointLightSceneProxy.h"

#include "Components/PointLightComponent.h"

void FPointLightSceneProxy::GetLightShaderParameters(FLightRenderParameters& Out) const
{
    const auto* PointLightComponent = static_cast<const UPointLightComponent*>(LightComponent);
    const FLinearColor Color(PointLightComponent->GetLightColor());

    Out = FLightRenderParameters{};
    Out.Type = static_cast<uint32>(ELightType::Point);
    Out.Position = PointLightComponent->GetWorldLocation();
    Out.Radius = PointLightComponent->GetAttenuationRadius();
    Out.Color = FVector(Color.R, Color.G, Color.B);
    Out.Intensity = PointLightComponent->GetIntensity();
    Out.FalloffExponent = PointLightComponent->GetFalloff();
}
