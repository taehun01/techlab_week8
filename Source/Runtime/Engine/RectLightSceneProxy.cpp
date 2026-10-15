#include "EnginePCH.h"
#include "RectLightSceneProxy.h"

#include "Components/RectLightComponent.h"

void FRectLightSceneProxy::GetLightShaderParameters(FLightRenderParameters& Out) const
{
    const auto* RectLightComponent = static_cast<const URectLightComponent*>(LightComponent);
    const FLinearColor Color(RectLightComponent->GetLightColor());

    FTransform Transform;
    Transform.Rotation = RectLightComponent->GetWorldRotation();

    Out = FLightRenderParameters{};
    Out.Type = static_cast<uint32>(ELightType::Rect);
    Out.Position = RectLightComponent->GetWorldLocation();
    Out.Radius = RectLightComponent->GetAttenuationRadius();
    Out.Color = FVector(Color.R, Color.G, Color.B);
    Out.Intensity = RectLightComponent->GetIntensity();
    Out.FalloffExponent = 1.0f;                 // Rect는 Falloff 속성이 없어 선형 감쇠
    Out.Direction = Transform.GetForward();     // 빛이 나가는 방향
    Out.Right = Transform.GetRight();
    Out.HalfWidth = RectLightComponent->GetWidth() * 0.5f;
    Out.HalfHeight = RectLightComponent->GetHeight() * 0.5f;

    const float AngleRad = FMath::DegreesToRadians(FMath::Clamp(RectLightComponent->GetBarnDoorAngle(), 0.0f, 90.0f));
    Out.BarnDoorCos = std::cos(AngleRad);
    Out.BarnDoorSin = std::sin(AngleRad);
    Out.BarnDoorLength = std::max(RectLightComponent->GetBarnDoorLength(), 0.0f);
}
