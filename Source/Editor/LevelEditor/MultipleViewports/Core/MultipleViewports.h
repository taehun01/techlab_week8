// 다중 뷰포트의 레이아웃·카메라·빌보드 계산을 제공한다.
#pragma once

#include "Core/Types.h"
#include "Math/Vector.h"
#include "Math/Vector2.h"
#include "Math/Quat.h"
#include "Math/Matrix.h"
#include "Collision/Ray.h"

// 좌표 규약: +X Forward, +Y Right, +Z Up.
inline constexpr int32 InvalidViewIndex = -1;
// 행렬은 엔진 FMatrix의 row-vector 규약을 사용하며 이동은 M[3][0..2]에 둔다.

// 화면상의 좌상단 위치와 너비·높이를 담는다.
struct FRect { float X, Y, Width, Height; };

enum class EProjectionMode { Perspective, Orthographic };
enum class ELayoutMode { Single, QuadSplit };

// 카메라의 월드 위치와 회전을 담는다.
struct FCameraTransform
{
    FVector Location;
    FQuat Rotation{0.0f, 0.0f, 0.0f, 0.0f};
};

// 원근·직교 투영에 필요한 모드와 절두체 값을 담는다.
struct FCameraProjection
{
    EProjectionMode Mode;
    float FovDegrees;
    float OrthoWidth;
    float NearClip;
    float FarClip;
};

// 한 View에서 사용하는 카메라 Transform과 투영 설정을 담는다.
struct FViewCamera
{
    FCameraTransform Transform;
    FCameraProjection Projection;
};

// 한 프레임의 카메라 회전·이동·줌 입력을 담는다.
struct FCameraMoveInput
{
    FVector2 MouseDelta;
    FVector MoveAxis;
    float ZoomDelta;
};

// 카메라의 월드 축을 직교기저로 변환해 View 행렬을 만든다.
FMatrix BuildViewMatrix(const FCameraTransform& Transform);
// 투영 모드에 따라 원근 또는 직교 Projection 행렬을 만든다.
FMatrix BuildProjectionMatrix(const FCameraProjection& Projection, float AspectRatio);
// 화면 좌표를 카메라 기저와 투영값으로 역투영해 월드 Ray를 만든다.
FRay Deproject(const FViewCamera& Camera, FVector2 ScreenPos, FVector2 ViewportSize);
// 로컬 이동과 Yaw·Pitch 입력을 현재 카메라에 적분한다.
FViewCamera ApplyCameraMovement(const FViewCamera& Current, const FCameraMoveInput& Input, float DeltaTime);

// Rect의 너비와 높이가 모두 양수인지 검사한다.
bool IsViewRectValid(const FRect& Rect);

// 현재 Hover View와 우클릭으로 Capture한 View 인덱스를 담는다.
struct FViewInputState
{
    int32 HoveredViewIndex = InvalidViewIndex;
    int32 CapturedViewIndex = InvalidViewIndex;
};

// 화면 좌표를 포함하는 첫 번째 유효 View Rect를 찾는다.
int32 DetermineHoveredView(FVector2 ScreenPos, const FRect ViewRects[4]);
// 지정한 View를 입력 Capture 대상으로 설정한 새 상태를 반환한다.
FViewInputState BeginCapture(const FViewInputState& Current, int32 ViewIndex);
// 입력 Capture를 해제한 새 상태를 반환한다.
FViewInputState EndCapture(const FViewInputState& Current);

// 가로·세로 Splitter가 차지하는 창의 비율을 담는다.
struct FSplitRatio
{
    float Horizontal = 0.5f;
    float Vertical = 0.5f;
};

enum class EDragAxis { Horizontal, Vertical };

// 픽셀 Drag를 창 크기로 정규화해 해당 Split 비율에 반영한다.
FSplitRatio ApplySplitterDrag(const FSplitRatio& Current, EDragAxis Axis, float DeltaPixels, FVector2 WindowSize, float MinRatio);
// 두 Split 비율을 허용된 최소·최대 범위로 제한한다.
FSplitRatio ClampSplitRatio(const FSplitRatio& Raw, float MinRatio);
// Split 비율과 창 크기로 2x2 View Rect 네 개를 계산한다.
void ComputeViewRects(const FSplitRatio& Ratio, FVector2 WindowSize, FRect OutRects[4]);

// Billboard의 월드 위치와 화면에 보일 너비·높이를 담는다.
struct FBillboardComputeInput
{
    FVector WorldPosition;
    FVector2 Size;
};

// 카메라를 향하도록 계산된 Billboard 월드 행렬을 담는다.
struct FBillboardTransform
{
    FMatrix WorldMatrix{0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0};
};

// 카메라 기저로 Billboard의 Right·Up·Forward 축을 만들어 월드 행렬을 계산한다.
FBillboardTransform ComputeBillboardTransform(const FBillboardComputeInput& Input, const FCameraTransform& ViewCamera);
