#include "EnginePCH.h"
#include "PointLightSceneProxy.h"

#include "Components/PointLightComponent.h"

void FPointLightSceneProxy::GetLightShaderParameters(FLightRenderParameters& Out) const
{
    const auto* PointLightComponent = static_cast<const UPointLightComponent*>(LightComponent);
    const FLinearColor Color(PointLightComponent->GetLightColor());
    constexpr float NearClip = 0.1f;
    const float FarClip = PointLightComponent->GetAttenuationRadius();

    Out = FLightRenderParameters{};
    Out.Type = static_cast<uint32>(ELightType::Point);
    Out.Position = PointLightComponent->GetWorldLocation();
    Out.Radius = PointLightComponent->GetAttenuationRadius();
    Out.Color = FVector(Color.R, Color.G, Color.B);
    Out.Intensity = PointLightComponent->GetIntensity();
    Out.FalloffExponent = PointLightComponent->GetFalloff();
    // 그림자를 만들 수 없는 짧은 영향 거리에서도 조명 파라미터에 NaN을 넣지 않는다.
    if (FarClip > NearClip)
    {
        Out.DepthScale = FarClip / (FarClip - NearClip);
        Out.DepthOffset = -NearClip * FarClip / (FarClip - NearClip);
    }
    Out.SourceRadius = PointLightComponent->GetSourceRadius();
}

bool FPointLightSceneProxy::GetShadowProjection(FShadowProjection& OutShadowProjection) const
{
    if (!ShouldCastShadow())
        return false;

    const auto* Point = static_cast<const UPointLightComponent*>(LightComponent);

    // Near는 기초 구현의 고정값이며, Far는 실제 조명 영향 거리와 맞춘다.
    constexpr float NearClip = 0.1f;
    const float FarClip = Point->GetAttenuationRadius();
    const float HalfFOV = 46.0f;

    // 원근 투영을 만들 수 없는 설정은 이 지점에서만 걸러낸다.
    if (FarClip <= NearClip)
        return false;

    // 기존 CameraComponent처럼 위치·회전만 역변환한다.
    // 라이트의 Component Scale은 그림자 시야에 반영하지 않는다.
    const FVector ViewOrigin = Point->GetWorldLocation();

    OutShadowProjection.ViewOrigin = ViewOrigin;

    const FMatrix Translation = FMatrix::MakeTranslation(ViewOrigin * -1.0f);
    // const FMatrix Rotation = Point->GetWorldRotation().Quaternion().ToFMatrix();

    FMatrix Rotation0 = FMatrix::Identity;
    FMatrix Rotation1 = FMatrix::Identity;
    Rotation1[0][0] *= -1; Rotation1[1][1] *= -1; Rotation1[2][2] *= 1;

    FMatrix Rotation2 = FMatrix::Identity;
    Rotation2[0][0] = 0; Rotation2[0][1] = 1; Rotation2[0][2] = 0;
    Rotation2[1][0] = -1; Rotation2[1][1] = 0; Rotation2[1][2] = 0;
    Rotation2[2][0] = 0; Rotation2[2][1] = 0; Rotation2[2][2] = 1;
    FMatrix Rotation3 = FMatrix::Identity;
    Rotation3[0][0] = 0; Rotation3[0][1] = -1; Rotation3[0][2] = 0;
    Rotation3[1][0] = 1; Rotation3[1][1] = 0; Rotation3[1][2] = 0;
    Rotation3[2][0] = 0; Rotation3[2][1] = 0; Rotation3[2][2] = 1;

    FMatrix Rotation4 = FMatrix::Identity;
    Rotation4[0][0] = 0; Rotation4[0][1] = 0; Rotation4[0][2] = 1;
    Rotation4[1][0] = 0; Rotation4[1][1] = 1; Rotation4[1][2] = 0;
    Rotation4[2][0] = -1; Rotation4[2][1] = 0; Rotation4[2][2] = 0;
    FMatrix Rotation5 = FMatrix::Identity;
    Rotation5[0][0] = 0; Rotation5[0][1] = 0; Rotation5[0][2] = -1;
    Rotation5[1][0] = 0; Rotation5[1][1] = 1; Rotation5[1][2] = 0;
    Rotation5[2][0] = 1; Rotation5[2][1] = 0; Rotation5[2][2] = 0;

    FMatrix View0 = Translation * Rotation0.GetTransposed();
    FMatrix View1 = Translation * Rotation1.GetTransposed();
    FMatrix View2 = Translation * Rotation2.GetTransposed();
    FMatrix View3 = Translation * Rotation3.GetTransposed();
    FMatrix View4 = Translation * Rotation4.GetTransposed();
    FMatrix View5 = Translation * Rotation5.GetTransposed();

    const float Scale = 1.0f / std::tan(FMath::DegreesToRadians(HalfFOV));
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

    OutShadowProjection.ViewProjections.Add(View0 * Projection);
    OutShadowProjection.ViewProjections.Add(View1 * Projection);
    OutShadowProjection.ViewProjections.Add(View2 * Projection);
    OutShadowProjection.ViewProjections.Add(View3 * Projection);
    OutShadowProjection.ViewProjections.Add(View4 * Projection);
    OutShadowProjection.ViewProjections.Add(View5 * Projection);

    return true;
}
