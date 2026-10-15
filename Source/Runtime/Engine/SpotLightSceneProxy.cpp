#include "EnginePCH.h"
#include "SpotLightSceneProxy.h"

#include "Components/SpotLightComponent.h"

void FSpotLightSceneProxy::GetLightShaderParameters(FLightRenderParameters& Out) const
{
    FPointLightSceneProxy::GetLightShaderParameters(Out);

    const auto* Spot = static_cast<const USpotLightComponent*>(LightComponent);
    FTransform Transform;
    Transform.Rotation = Spot->GetWorldRotation();
    const float Outer = FMath::Clamp(Spot->GetOuterConeAngle(), 0.0f, 89.0f);
    const float Inner = FMath::Clamp(Spot->GetInnerConeAngle(), 0.0f, Outer);

    Out.Type = static_cast<uint32>(ELightType::Spot);
    Out.Direction = Transform.GetForward();
    Out.InnerConeCos = std::cos(FMath::DegreesToRadians(Inner));
    Out.OuterConeCos = std::cos(FMath::DegreesToRadians(Outer));
}
