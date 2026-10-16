#include "EnginePCH.h"
#include "SpotLightSceneProxy.h"

#include "Components/SpotLightComponent.h"

namespace
{
    FMatrix MakeLookAtView(const FVector& Eye, const FVector& InForward)
    {
        const FVector Forward = InForward.GetSafeNormal();

        const FVector WorldUp = std::abs(Forward.Z) > 0.99f ? FVector(1.f, 0.f, 0.f) : FVector(0.f, 0.f, 1.f);
        const FVector Right = FVector::Cross(WorldUp, Forward).GetSafeNormal();
        const FVector Up = FVector::Cross(Forward, Right).GetSafeNormal();

        return FMatrix{
            Forward.X, Right.X, Up.X, 0.f,
            Forward.Y, Right.Y, Up.Y, 0.f,
            Forward.Z, Right.Z, Up.Z, 0.f,
            -Eye.Dot(Forward), -Eye.Dot(Right), -Eye.Dot(Up), 1.f,
        };
    }

    FMatrix MakeShadowPerspective(float HalfFovRad, float Aspect, float NearZ, float FarZ)
    {
        const float YScale = 1.f / std::tan(HalfFovRad);
        const float XScale = YScale / Aspect;
        const float ZScale = FarZ / (FarZ - NearZ);
        const float ZOffset = -NearZ * FarZ / (FarZ - NearZ);
        return FMatrix{
            0.f, 0.f, ZScale, 1.f,
            XScale, 0.f, 0.f, 0.f,
            0.f, YScale, 0.f, 0.f,
            0.f, 0.f, ZOffset, 0.f
        };
    }

    constexpr float ShadowConeMarginDeg = 2.f;
    constexpr float MaxShadowFovDeg = 80.f;
}

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

bool FSpotLightSceneProxy::GetShadowView(uint32 ViewIndex, FShadowViewInfo& Out) const
{
    assert(ViewIndex == 0);

    const auto* Spot = static_cast<const USpotLightComponent*>(LightComponent);

    FTransform Transform;
    Transform.Rotation = Spot->GetWorldRotation();
    FVector Location = Spot->GetWorldLocation();
    FVector Direction = Spot->GetTransform().GetForward();

    const float Outer = FMath::Clamp(Spot->GetOuterConeAngle(), 0.f, 89.f);
    const float HalfFovDeg = std::min(Outer + ShadowConeMarginDeg, MaxShadowFovDeg);

    Out.FarZ = std::max(Spot->GetAttenuationRadius(), 0.01f);
    Out.NearZ = std::max(Out.FarZ * 0.01f, 0.01f);

    Out.View = MakeLookAtView(Location, Direction);
    Out.Projection = MakeShadowPerspective(FMath::DegreesToRadians(HalfFovDeg), 1.f, Out.NearZ, Out.FarZ);
    Out.ViewProjection = Out.View * Out.Projection;
    Out.Frustum = ExtractFrustumPlanes(Out.ViewProjection);

    return true;
}