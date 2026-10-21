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


bool FSpotLightSceneProxy::GetShadowProjection(FShadowProjection& OutShadowProjection) const
{
    if (!ShouldCastShadow())
        return false;

    const auto* Spot = static_cast<const USpotLightComponent*>(LightComponent);

    // Near는 기초 구현의 고정값이며, Far는 실제 조명 영향 거리와 맞춘다.
    constexpr float NearClip = 0.1f;
    const float FarClip = Spot->GetAttenuationRadius();
    const float OuterAngle = FMath::Clamp(Spot->GetOuterConeAngle(), 0.0f, 89.0f);

    // 원근 투영을 만들 수 없는 설정은 이 지점에서만 걸러낸다.
    if (FarClip <= NearClip || OuterAngle <= 0.0f)
        return false;

    // 기존 CameraComponent처럼 위치·회전만 역변환한다.
    // 라이트의 Component Scale은 그림자 시야에 반영하지 않는다.
    const FVector ViewOrigin = Spot->GetWorldLocation();
    const FMatrix Rotation = Spot->GetWorldRotation().Quaternion().ToFMatrix();
    const FMatrix View = FMatrix::MakeTranslation(ViewOrigin * -1.0f) * Rotation.GetTransposed();

    // OuterConeAngle은 이미 반각이다. 정사각형 타깃이므로 가로·세로 배율이 같다.
    const float Scale = 1.0f / std::tan(FMath::DegreesToRadians(OuterAngle));
    OutShadowProjection.TanHalfFOV = 1.0f / Scale;
    const float DepthScale = FarClip / (FarClip - NearClip);
    const float DepthOffset = -NearClip * FarClip / (FarClip - NearClip);

    // 현재 엔진의 +X 전방·행 벡터·깊이 0~1 규약을 따른다.
    const FMatrix Projection(
        0.0f, 0.0f, DepthScale, 1.0f,
        Scale, 0.0f, 0.0f, 0.0f,
        0.0f, Scale, 0.0f, 0.0f,
        0.0f, 0.0f, DepthOffset, 0.0f
    );

    /*OutViewOrigin = ViewOrigin;
    OutViewProjection = View * Projection;*/

    OutShadowProjection.ViewOrigin = ViewOrigin;
    OutShadowProjection.ViewProjections.Add(View * Projection);

    return true;
}
