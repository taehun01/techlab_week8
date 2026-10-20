#pragma once

#include "Editor/LevelEditor/MultipleViewports/Adapter/MultipleViewportsAdapterTypes.h"
#include "Containers/Array.h"
#include "Math/Frustum.h"
#include "Render/ViewMode.h"
#include "Render/EngineShowFlags.h"


struct FLODViewContext;

// 한 View의 카메라·표시 상태와 프리셋·이동·렌더 계산을 관리한다.
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

    // 저장 카메라와 프리셋을 보존하고 실제 View에 적용할 카메라만 교체한다.
    void SetCameraOverride(const FVector& Location, const FVector& Forward,
        const FCameraProjection& Projection, const FRect& Rect);
    void ClearCameraOverride() { bHasCameraOverride = false; }
    bool HasCameraOverride() const { return bHasCameraOverride; }

    // 렌더·컬링·피킹은 Override가 반영된 논리 카메라를 사용한다.
    const FViewCamera& GetEffectiveCamera() const
    {
        return bHasCameraOverride ? CameraOverride : Camera;
    }

    // 패널과 외부 PIE View의 표시 설정을 동일하게 보관한다.
    void SetWireframe(bool Value) { ShowFlags.SetEnabled(EEngineShowFlag::Wireframe, Value); }
    bool IsWireframe() const { return ShowFlags.IsEnabled(EEngineShowFlag::Wireframe); }
    const FEngineShowFlags& GetShowFlags() const { return ShowFlags; }
    void SetShowFlags(const FEngineShowFlags& InFlags) { ShowFlags = InFlags; }
    void SetViewMode(EViewMode Value) { ViewMode = Value; }
    EViewMode GetViewMode() const { return ViewMode; }

    // 축 정렬 프리셋과 실제 카메라 방향으로 Grid 평면을 선택한다.
    EGridPlane GetGridPlane() const;

    // View 크기가 변경되면 현재 카메라의 직교 폭을 보정한다.
    void OnViewportResized(const FRect& Rect)
    {
        ConstrainOrthographicWidth(Camera.Projection, Rect);
    }

    // RawInput은 키 축·마우스 픽셀·원시 휠 값이며 기존 프리셋을 유지해 카메라를 갱신한다.
    void UpdateCamera(float DeltaTime, const FCameraMoveInput& RawInput, const FRect& Rect,
        float MoveSpeed, float MouseSensitivity, bool bCameraCaptured);

    // 유효한 View 영역과 선택된 카메라로 렌더용 행렬을 계산한다.
    static void CalcViewMatrices(const FViewCamera& InCamera, const FRect& Rect,
        FMatrix& OutView, FMatrix& OutProjection);

    // 같은 VP로 Frustum도 필요할 때 함께 계산한다.
    static FMatrix CalcViewProjection(const FViewCamera& InCamera, const FRect& Rect,
        FFrustumPlanes* OutFrustum = nullptr);

    // 실제 카메라와 렌더 영역으로 공통 LOD·컬링 입력을 구성한다.
    static FLODViewContext CalcLODViewContext(const FViewCamera& InCamera, const FRect& Rect);

    // 그림자 거리 안의 Receiver 절두체 꼭짓점 8개를 기존 목록에 추가한다.
    // 직교 View는 논리 원점의 앞뒤를 사용하며, 유효한 깊이 구간이 없으면 목록을 유지한다.
    static bool AppendShadowFrustumCorners(const FViewCamera& InCamera, const FRect& Rect,
        float ShadowDistance, TArray<FVector>& OutCorners);

    // 논리 카메라의 전방 선형 거리 구간을 월드 꼭짓점 8개로 변환한다.
    // 원근은 카메라 클립 범위로 제한하고, 직교는 음수 거리도 허용한다.
    // 성공하면 기존 목록에 추가하며, 실패하면 목록을 변경하지 않는다.
    static bool AppendShadowFrustumCorners(const FViewCamera& InCamera, const FRect& Rect,
        float NearDistance, float FarDistance, TArray<FVector>& OutCorners);

    // View 원점을 뺀 화면 좌표를 렌더 카메라 기준으로 역투영한다.
    static FRay DeprojectView(const FViewCamera& InCamera, const FRect& Rect,
        FVector2 ViewLocalPosition);

    // 선택한 논리 카메라로 렌더·피킹에 공통인 Billboard 행렬을 계산한다.
    static FMatrix BuildBillboardMatrix(const FViewCamera& InCamera,
        const FVector& WorldPosition, float Width, float Height);

    // Override와 렌더 경로에서도 사용하는 기존 카메라 계산 구현을 한곳에 둔다.
    static FVector NormalizedOrZero(FVector Value);

private:
    // 논리 카메라는 보존하고 직교 렌더링에 필요한 깊이만 보정한다.
    static FViewCamera BuildRenderCamera(const FViewCamera& InCamera, const FRect& Rect);

    // 투영 방식에 따라 원근 이동·회전 또는 직교 이동·줌을 계산한다.
    void UpdatePerspectiveCamera(float DeltaTime, const FCameraMoveInput& MoveInput,
        FVector2 MouseDelta, float MouseSensitivity, bool bCameraCaptured);
    void UpdateOrthographicCamera(float DeltaTime, const FCameraMoveInput& MoveInput,
        FVector2 MouseDelta, float WheelDelta, const FRect& Rect, bool bCameraCaptured);

    // 기존 카메라 이동·회전·직교 투영 계산의 상수와 보조 함수다.
    static constexpr float Pi = 3.14159265358979323846f;
    static constexpr float MaximumCameraPitchDegrees = 89.0f;
    static constexpr float MaximumRotationDegreesPerFrame = 45.0f;
    static constexpr float MouseWheelDeltaPerStep = 120.0f;
    static constexpr float OrthographicZoomFactorPerStep = 0.9f;
    static constexpr float PerspectiveWheelSpeed = 0.1f;
    static constexpr float MinimumOrthographicWidth = 0.01f;
    // 양방향 깊이 보정 뒤에도 절두체 평면 법선이 Core epsilon보다 충분히 크게 유지되게 한다.
    static constexpr float MaximumOrthographicSpan = 80000.0f;
    static float CameraForwardZ(const FQuat& Rotation);
    static float CameraYawDegrees(const FQuat& Rotation);
    static float CameraPitchDegrees(const FQuat& Rotation);
    static FQuat MakeCameraRotation(float YawDegrees, float PitchDegrees);
    static void ConstrainOrthographicWidth(FCameraProjection& Projection, const FRect& Rect);

    FViewCamera Camera{};
    EMultipleViewportsCameraPreset CameraPreset = EMultipleViewportsCameraPreset::Perspective;

    // Override 설정·해제는 저장 Camera와 CameraPreset을 변경하지 않는다.
    FViewCamera CameraOverride{};
    bool bHasCameraOverride = false;

    // 모든 Client가 같은 표시 기본값으로 시작한다.
    FEngineShowFlags ShowFlags{ EShowFlagScope::View };
    EViewMode ViewMode = EViewMode::Lit_Lambert;
};
