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

// 투영 방식에 맞는 이동축을 만들고 카메라별 계산으로 분기한다.
void FEditorViewportClient::UpdateCamera(
    float DeltaTime, const FCameraMoveInput& RawInput, const FRect& Rect,
    float MoveSpeed, float MouseSensitivity, bool bCameraCaptured)
{
    const bool bOrthographic = Camera.Projection.Mode == EProjectionMode::Orthographic;
    // 직교 이동은 W/S와 A/D를 화면 평면에 매핑하고 Q/E는 정규화 전에 제외한다.
    const FVector Axis = bOrthographic
        ? FVector(0.0f, RawInput.MoveAxis.Y, RawInput.MoveAxis.X)
        : RawInput.MoveAxis;

    // Core에는 이동 속도만 전달한다. MouseDelta는 0으로 두어 회전 중복을 막는다.
    FCameraMoveInput MoveInput{};
    MoveInput.MoveAxis = NormalizedOrZero(Axis) * MoveSpeed;

    if (bOrthographic)
    {
        UpdateOrthographicCamera(
            DeltaTime, MoveInput, RawInput.MouseDelta, RawInput.ZoomDelta, Rect, bCameraCaptured);
    }
    else
    {
        MoveInput.ZoomDelta = RawInput.ZoomDelta * PerspectiveWheelSpeed;
        UpdatePerspectiveCamera(
            DeltaTime, MoveInput, RawInput.MouseDelta, MouseSensitivity, bCameraCaptured);
    }
}

// 이전 회전으로 이동한 뒤 마우스 회전을 적용한다.
void FEditorViewportClient::UpdatePerspectiveCamera(
    float DeltaTime, const FCameraMoveInput& MoveInput, FVector2 MouseDelta,
    float MouseSensitivity, bool bCameraCaptured)
{
    Camera = ApplyCameraMovement(Camera, MoveInput, DeltaTime);
    if (!bCameraCaptured) return;

    // 마우스 회전은 기존처럼 픽셀 기준이며 DeltaTime을 곱하지 않는다.
    const float YawDelta = FMath::Clamp(
        MouseDelta.X * MouseSensitivity,
        -MaximumRotationDegreesPerFrame, MaximumRotationDegreesPerFrame);
    const float PitchDelta = FMath::Clamp(
        -MouseDelta.Y * MouseSensitivity,
        -MaximumRotationDegreesPerFrame, MaximumRotationDegreesPerFrame);

    const float Yaw = CameraYawDegrees(Camera.Transform.Rotation) + YawDelta;
    const float Pitch = FMath::Clamp(
        CameraPitchDegrees(Camera.Transform.Rotation) + PitchDelta,
        -MaximumCameraPitchDegrees, MaximumCameraPitchDegrees);

    Camera.Transform.Rotation = MakeCameraRotation(Yaw, Pitch);
}

// 직교 카메라는 회전 없이 화면 평면 이동과 폭 줌을 적용한다.
void FEditorViewportClient::UpdateOrthographicCamera(
    float DeltaTime, const FCameraMoveInput& MoveInput, FVector2 MouseDelta,
    float WheelDelta, const FRect& Rect, bool bCameraCaptured)
{
    // 제한된 줌 폭을 이번 프레임의 Pan 거리 계산에도 사용한다.
    const float WheelSteps = WheelDelta / MouseWheelDeltaPerStep;
    Camera.Projection.OrthoWidth = std::max(
        MinimumOrthographicWidth,
        Camera.Projection.OrthoWidth * std::pow(OrthographicZoomFactorPerStep, WheelSteps));
    ConstrainOrthographicWidth(Camera.Projection, Rect);

    Camera = ApplyCameraMovement(Camera, MoveInput, DeltaTime);
    if (!bCameraCaptured) return;

    // 픽셀 드래그는 시간과 무관하게 현재 화면 배율로 월드 이동량을 계산한다.
    const float WorldUnitsPerPixel = Camera.Projection.OrthoWidth / Rect.Width;
    const FVector Right = NormalizedOrZero(Camera.Transform.Rotation.GetRightVector());
    const FVector Up = NormalizedOrZero(Camera.Transform.Rotation.GetUpVector());
    Camera.Transform.Location += Right * (-MouseDelta.X * WorldUnitsPerPixel)
        + Up * (MouseDelta.Y * WorldUnitsPerPixel);
}

// 직교 화면 구도를 유지하면서 카메라 평면 앞뒤를 렌더링할 깊이를 확보한다.
FViewCamera FEditorViewportClient::BuildRenderCamera(
    const FViewCamera& InCamera, const FRect& Rect)
{
    FViewCamera RenderCamera = InCamera;
    if (RenderCamera.Projection.Mode != EProjectionMode::Orthographic)
        return RenderCamera;

    const float AspectRatio = Rect.Width > 0.0f && Rect.Height > 0.0f
        ? Rect.Width / Rect.Height : 1.0f;
    const float HalfWidth = RenderCamera.Projection.OrthoWidth * 0.5f;
    const float HalfHeight = HalfWidth / AspectRatio;
    const FVector Position = RenderCamera.Transform.Location;
    const float Distance = std::sqrt(
        Position.X * Position.X + Position.Y * Position.Y + Position.Z * Position.Z);

    // 기존 FarClip보다 깊이 범위를 줄이지 않는 현재 보정 공식을 유지한다.
    const float Radius = std::max(RenderCamera.Projection.FarClip,
        2.0f * Distance + 4.0f * std::sqrt(HalfWidth * HalfWidth + HalfHeight * HalfHeight));
    const FVector Forward = NormalizedOrZero(
        RenderCamera.Transform.Rotation.GetForwardVector());
    const float Retreat = Radius + RenderCamera.Projection.NearClip;

    RenderCamera.Transform.Location = Position - Forward * Retreat;
    RenderCamera.Projection.FarClip = RenderCamera.Projection.NearClip + 2.0f * Radius;
    return RenderCamera;
}

// 렌더 보정 카메라를 기존 Core 행렬 계산에 전달한다.
void FEditorViewportClient::CalcViewMatrices(
    const FViewCamera& InCamera, const FRect& Rect,
    FMatrix& OutView, FMatrix& OutProjection)
{
    const FViewCamera RenderCamera = BuildRenderCamera(InCamera, Rect);
    OutView = BuildViewMatrix(RenderCamera.Transform);
    OutProjection = BuildProjectionMatrix(
        RenderCamera.Projection, Rect.Width / Rect.Height);
}

// row-vector 순서로 VP를 만들고 요청된 경우에만 절두체를 추출한다.
FMatrix FEditorViewportClient::CalcViewProjection(
    const FViewCamera& InCamera, const FRect& Rect, FFrustumPlanes* OutFrustum)
{
    FMatrix View;
    FMatrix Projection;
    CalcViewMatrices(InCamera, Rect, View, Projection);
    const FMatrix ViewProjection = View * Projection;

    if (OutFrustum)
        *OutFrustum = ExtractFrustumPlanes(ViewProjection);
    return ViewProjection;
}

// 렌더링과 같은 깊이 보정을 적용해 직교 화면 뒤쪽 대상도 피킹한다.
FRay FEditorViewportClient::DeprojectView(
    const FViewCamera& InCamera, const FRect& Rect, FVector2 ViewLocalPosition)
{
    return Deproject(BuildRenderCamera(InCamera, Rect),
        ViewLocalPosition, { Rect.Width, Rect.Height });
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
