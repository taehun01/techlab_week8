#include "EnginePCH.h"

#include "Editor/HitoriEd/EditorViewportClient.h"

#include <algorithm>
#include <cmath>

// 외부 설정 적용 시 Roll을 제거하고 일반 프리셋으로 갱신한다.
void FEditorViewportClient::SetCamera(const FViewCamera& InCamera, const FRect& Rect)
{
    Camera = InCamera;
    ConstrainOrthographicWidth(Camera.Projection, Rect);

    const bool bPerspective = Camera.Projection.Mode == EProjectionMode::Perspective;
    if (bPerspective)
    {
        Camera.Transform.Rotation = MakeCameraRotation(
            CameraYawDegrees(Camera.Transform.Rotation),
            CameraPitchDegrees(Camera.Transform.Rotation));
    }

    CameraPreset = bPerspective
        ? EMultipleViewportsCameraPreset::Perspective
        : EMultipleViewportsCameraPreset::OrthographicView;
}

// 위치나 투영 수치 편집으로 Top·Front 등의 프리셋이 풀리지 않게 한다.
void FEditorViewportClient::ApplyCameraProperties(
    const FViewCamera& InCamera, bool bRotationChanged, const FRect& Rect)
{
    const EMultipleViewportsCameraPreset PreviousPreset = CameraPreset;
    const bool bKeepPreset = !bRotationChanged
        && Camera.Projection.Mode == InCamera.Projection.Mode;

    SetCamera(InCamera, Rect);
    if (bKeepPreset)
        CameraPreset = PreviousPreset;
}

// 축 정렬 프리셋은 고정 Transform을, 일반 프리셋은 현재 위치를 유지해 적용한다.
void FEditorViewportClient::ApplyCameraPreset(EMultipleViewportsCameraPreset InPreset, const FRect& Rect)
{
    ConstrainOrthographicWidth(Camera.Projection, Rect);
    CameraPreset = InPreset;
    Camera.Projection.Mode = InPreset == EMultipleViewportsCameraPreset::Perspective
        ? EProjectionMode::Perspective
        : EProjectionMode::Orthographic;

    switch (InPreset)
    {
    case EMultipleViewportsCameraPreset::Top:
        Camera.Transform = { {0.0f, 0.0f, 10.0f}, {0.0f, 0.70710678f, 0.0f, 0.70710678f} };
        break;
    case EMultipleViewportsCameraPreset::Bottom:
        Camera.Transform = { {0.0f, 0.0f, -10.0f}, {0.0f, -0.70710678f, 0.0f, 0.70710678f} };
        break;
    case EMultipleViewportsCameraPreset::Front:
        Camera.Transform = { {10.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f} };
        break;
    case EMultipleViewportsCameraPreset::Back:
        Camera.Transform = { {-10.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f} };
        break;
    case EMultipleViewportsCameraPreset::Left:
        Camera.Transform = { {0.0f, -10.0f, 0.0f}, {0.0f, 0.0f, 0.70710678f, 0.70710678f} };
        break;
    case EMultipleViewportsCameraPreset::Right:
        Camera.Transform = { {0.0f, 10.0f, 0.0f}, {0.0f, 0.0f, -0.70710678f, 0.70710678f} };
        break;
    case EMultipleViewportsCameraPreset::Perspective:
        Camera.Transform.Rotation = MakeCameraRotation(
            CameraYawDegrees(Camera.Transform.Rotation),
            CameraPitchDegrees(Camera.Transform.Rotation));
        break;
    case EMultipleViewportsCameraPreset::OrthographicView:
        // 투영 방식만 바꾸는 프리셋은 현재 Transform을 유지한다.
        break;
    }
}

// 길이가 0이면 영벡터를, 아니면 기존 계산 순서대로 정규화한 벡터를 반환한다.
FVector FEditorViewportClient::NormalizedOrZero(FVector Value)
{
    const float LengthSquared = Value.X * Value.X + Value.Y * Value.Y + Value.Z * Value.Z;
    if (LengthSquared <= 0.0f) return {};
    const float InverseLength = 1.0f / std::sqrt(LengthSquared);
    return { Value.X * InverseLength, Value.Y * InverseLength, Value.Z * InverseLength };
}

// quaternion으로 회전된 카메라 Forward의 Z 성분을 계산한다.
float FEditorViewportClient::CameraForwardZ(const FQuat& Rotation)
{
    return 2.0f * (Rotation.X * Rotation.Z - Rotation.W * Rotation.Y);
}

// 카메라 Forward의 XY 방향을 atan2로 바꿔 기존 Euler Yaw를 구한다.
float FEditorViewportClient::CameraYawDegrees(const FQuat& Rotation)
{
    const FVector Forward = NormalizedOrZero(Rotation.GetForwardVector());
    return std::atan2(Forward.Y, Forward.X) * 180.0f / Pi;
}

// 카메라 Forward의 Z 성분을 asin해 기존 부호의 Euler Pitch를 구한다.
float FEditorViewportClient::CameraPitchDegrees(const FQuat& Rotation)
{
    return std::asin(FMath::Clamp(CameraForwardZ(Rotation), -1.0f, 1.0f)) * 180.0f / Pi;
}

// Z-up Yaw와 Right축 Pitch를 합성해 Roll 없는 quaternion을 만든다.
FQuat FEditorViewportClient::MakeCameraRotation(float YawDegrees, float PitchDegrees)
{
    const float HalfYaw = YawDegrees * Pi / 360.0f;
    const float HalfPitch = PitchDegrees * Pi / 360.0f;
    const float SinYaw = std::sin(HalfYaw);
    const float CosYaw = std::cos(HalfYaw);
    const float SinPitch = std::sin(HalfPitch);
    const float CosPitch = std::cos(HalfPitch);

    return { SinPitch * SinYaw, -SinPitch * CosYaw, CosPitch * SinYaw, CosPitch * CosYaw };
}

// 종횡비와 Core 절두체 정밀도에 맞춰 기존 직교 투영 폭의 허용 범위를 유지한다.
void FEditorViewportClient::ConstrainOrthographicWidth(FCameraProjection& Projection, const FRect& Rect)
{
    const float AspectRatio = Rect.Width > 0.0f && Rect.Height > 0.0f
        ? Rect.Width / Rect.Height : 1.0f;
    const float MaximumWidth = std::max(MinimumOrthographicWidth,
        MaximumOrthographicSpan * std::min(1.0f, AspectRatio));
    Projection.OrthoWidth = std::isnan(Projection.OrthoWidth)
        ? 10.0f
        : FMath::Clamp(Projection.OrthoWidth, MinimumOrthographicWidth, MaximumWidth);
}
