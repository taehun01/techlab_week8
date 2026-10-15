#pragma once

#include "Editor/LevelEditor/MultipleViewports/Adapter/MultipleViewportsAdapterTypes.h"

// 한 에디터 View의 카메라 상태와 프리셋 적용 규칙을 관리한다.
class FEditorViewportClient
{
public:
    // 외부 카메라를 적용하고 투영 방식에 맞는 일반 프리셋을 선택한다.
    void SetCamera(const FViewCamera& InCamera, const FRect& Rect);
    const FViewCamera& GetCamera() const { return Camera; }

    // 위치·투영 수치만 변경하면 기존 축 정렬 프리셋을 유지한다.
    void ApplyCameraProperties(const FViewCamera& InCamera, bool bRotationChanged, const FRect& Rect);

    // 선택한 프리셋의 방향과 투영 방식을 적용한다.
    void ApplyCameraPreset(EMultipleViewportsCameraPreset InPreset, const FRect& Rect);
    EMultipleViewportsCameraPreset GetCameraPreset() const { return CameraPreset; }

    // View 크기가 변경되면 현재 카메라의 직교 폭을 보정한다.
    void OnViewportResized(const FRect& Rect)
    {
        ConstrainOrthographicWidth(Camera.Projection, Rect);
    }

    // 이동 입력의 계산 결과를 저장하며 기존 프리셋을 유지한다.
    void ApplyNavigation(const FViewCamera& UpdatedCamera)
    {
        Camera = UpdatedCamera;
    }

    // 입력과 Override에서도 사용하는 기존 카메라 계산 구현을 한곳에 둔다.
    static FVector NormalizedOrZero(FVector Value);
    static float CameraYawDegrees(const FQuat& Rotation);
    static float CameraPitchDegrees(const FQuat& Rotation);
    static FQuat MakeCameraRotation(float YawDegrees, float PitchDegrees);
    static void ConstrainOrthographicWidth(FCameraProjection& Projection, const FRect& Rect);

    // 입력의 줌 계산과 투영 보정에서 같은 최솟값을 사용한다.
    static constexpr float MinimumOrthographicWidth = 0.01f;

private:
    // 기존 카메라 회전·직교 투영 계산의 상수와 보조 함수다.
    static constexpr float Pi = 3.14159265358979323846f;
    // 양방향 깊이 보정 뒤에도 절두체 평면 법선이 Core epsilon보다 충분히 크게 유지되게 한다.
    static constexpr float MaximumOrthographicSpan = 80000.0f;
    static float CameraForwardZ(const FQuat& Rotation);

    FViewCamera Camera{};
    EMultipleViewportsCameraPreset CameraPreset = EMultipleViewportsCameraPreset::Perspective;
};
