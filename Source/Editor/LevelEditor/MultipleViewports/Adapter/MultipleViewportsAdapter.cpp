#include "EnginePCH.h"

#include "Editor/LevelEditor/MultipleViewports/Adapter/MultipleViewportsAdapter.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "Input/InputSystem.h"
#include "Asset/LOD/StaticMeshLODSelector.h"
#include "Render/RenderPacket.h"

// View 입력에서 사용하는 ImGui API를 직접 포함한다.
#include "imgui.h"

#include <algorithm>
#include <cassert>

// 외부 View Rect는 원점 기준 렌더 타깃 크기와 같다.
void FMultipleViewportsAdapter::SetExternalViewSize(const uint32 Width, const uint32 Height)
{
	ExternalViewRect = { 0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height) };
}

// 메인 카메라 투영값을 공유하고 네 View의 기본 프리셋 상태를 만든다.
void FMultipleViewportsAdapter::InitializeFromWorld(UWorld& World)
{
	UCameraComponent* MainCamera = World.GetMainCamera() ? World.GetMainCamera()->GetCameraComponent() : nullptr;
	assert(MainCamera != nullptr);

	const FCameraProjection Perspective{
		EProjectionMode::Perspective,
		MainCamera->GetFieldOfView(),
		MainCamera->GetOrthoWidth(),
		MainCamera->GetNearZ(),
		MainCamera->GetFarZ() };
	Layout.SetLayoutMode(ELayoutMode::QuadSplit);
	// 최신 trace의 구도를 사용해 화면 정면은 +X, 화면 오른쪽은 +Y가 되도록 시작한다.
	const FViewCamera InitialCamera{ {{-11.665390f, 6.117728f, 9.921079f},
		{0.081251f, 0.256328f, -0.291035f, 0.918146f}}, Perspective };
	for (int32 ViewIndex = 0; ViewIndex < FLevelViewportLayout::ViewCount; ++ViewIndex)
		ViewportClients[ViewIndex].SetCamera(InitialCamera, GetViewRect(ViewIndex));
	ViewportClients[1].ApplyCameraPreset(EMultipleViewportsCameraPreset::Top, GetViewRect(1));
	ViewportClients[2].ApplyCameraPreset(EMultipleViewportsCameraPreset::Front, GetViewRect(2));
	ViewportClients[3].ApplyCameraPreset(EMultipleViewportsCameraPreset::Left, GetViewRect(3));
}

// 속성 편집 시 프리셋 유지 여부는 카메라를 소유한 Client가 결정한다.
void FMultipleViewportsAdapter::ApplyCameraProperties(const int32 ViewIndex,
	const FViewCamera& Camera, const bool bRotationChanged)
{
	assert(IsPanelViewIndex(ViewIndex));
	ViewportClients[ViewIndex].ApplyCameraProperties(Camera, bRotationChanged, GetViewRect(ViewIndex));
}

// 패널과 외부 PIE의 카메라 Override를 해당 Client에 전달한다.
void FMultipleViewportsAdapter::SetViewCameraOverride(const int32 ViewIndex, const FVector& Location,
	const FVector& Forward, const FCameraProjection& Projection)
{
	assert(IsViewIndex(ViewIndex));
	ViewportClients[ViewIndex].SetCameraOverride(Location, Forward, Projection, GetViewRect(ViewIndex));
}

// Override를 해제해 저장 카메라로 되돌린다.
void FMultipleViewportsAdapter::ClearViewCameraOverride(const int32 ViewIndex)
{
	assert(IsViewIndex(ViewIndex));
	ViewportClients[ViewIndex].ClearCameraOverride();
}

// 범위를 검사한 뒤 지정 View 카메라의 const 참조를 반환한다.
const FViewCamera& FMultipleViewportsAdapter::GetViewCamera(const int32 ViewIndex) const
{
	assert(IsPanelViewIndex(ViewIndex));
	return ViewportClients[ViewIndex].GetCamera();
}

// 선택한 View의 프리셋 적용을 해당 Client에 맡긴다.
void FMultipleViewportsAdapter::ApplyCameraPreset(const int32 ViewIndex, const EMultipleViewportsCameraPreset Preset)
{
	assert(IsPanelViewIndex(ViewIndex));
	ViewportClients[ViewIndex].ApplyCameraPreset(Preset, GetViewRect(ViewIndex));
}

// 범위를 검사한 뒤 지정 View의 프리셋을 반환한다.
EMultipleViewportsCameraPreset FMultipleViewportsAdapter::GetCameraPreset(const int32 ViewIndex) const
{
	assert(IsPanelViewIndex(ViewIndex));
	return ViewportClients[ViewIndex].GetCameraPreset();
}

// Grid를 표시하는 패널 View의 평면 선택을 Client에 맡긴다.
EGridPlane FMultipleViewportsAdapter::GetGridPlane(const int32 ViewIndex) const
{
	assert(IsPanelViewIndex(ViewIndex));
	return ViewportClients[ViewIndex].GetGridPlane();
}

// 배치는 Layout에 맡기고 기존 카메라 보정과 입력 대상 유지 규칙을 적용한다.
void FMultipleViewportsAdapter::UpdateLayout(const FVector2 WindowSize)
{
	Layout.Update(WindowSize, SplitterThickness);
	// 창 크기와 Single/Quad 전환으로 Aspect가 바뀌어도 저장값과 실제 투영값을 함께 보정한다.
	for (int32 ViewIndex = 0; ViewIndex < FLevelViewportLayout::ViewCount; ++ViewIndex)
	{
		const FRect& Rect = Layout.GetViewRect(ViewIndex);
		if (IsViewRectValid(Rect))
			ViewportClients[ViewIndex].OnViewportResized(Rect);
	}
	// Hover 추적은 비활성화한다. 클릭 위치 판정 함수는 입력 전환에서 계속 사용한다.
	InputState.HoveredViewIndex = InvalidViewIndex;
	if (InputState.CapturedViewIndex != InvalidViewIndex && !IsViewActive(InputState.CapturedViewIndex))
		InputState = EndCapture(InputState);
	// Capture가 없으면 마지막 선택을 유지하고, 숨겨진 View만 레이아웃에 맞게 보정한다.
	if (InputState.CapturedViewIndex != InvalidViewIndex)
		ActiveViewIndex = InputState.CapturedViewIndex;
	if (!IsViewActive(ActiveViewIndex))
	{
		const int32 SingleViewIndex = Layout.GetSingleViewIndex();
		ActiveViewIndex = Layout.GetLayoutMode() == ELayoutMode::Single && IsViewActive(SingleViewIndex)
			? SingleViewIndex : InvalidViewIndex;
	}
}

// Capture와 입력 대상을 결정하고 키·마우스·휠 입력을 해당 Client에 전달한다.
void FMultipleViewportsAdapter::UpdateInput(
	const float DeltaTime,
	const FVector2 LocalMousePosition,
	const float MoveSpeed,
	const float MouseSensitivity)
{
	const int WheelDelta = FInputSystem::GetWheelDelta(); // 이 Hook은 프레임마다 정확히 한 번만 호출한다.
	if (ImGui::IsKeyReleased(ImGuiKey_MouseRight))
		InputState = EndCapture(InputState);
	const int32 PointerViewIndex = Layout.FindViewAt(LocalMousePosition);
	// 우클릭 시작 위치를 Capture하고, Capture 밖에서는 좌클릭으로만 선택을 바꾼다.
	if (InputState.CapturedViewIndex == InvalidViewIndex && IsViewActive(PointerViewIndex))
	{
		if (FInputSystem::IsMousePressed(EMouseButton::Right))
			InputState = BeginCapture(InputState, PointerViewIndex);
		else if (FInputSystem::IsMousePressed(EMouseButton::Left))
			ActiveViewIndex = PointerViewIndex;
	}
	if (InputState.CapturedViewIndex != InvalidViewIndex)
		ActiveViewIndex = InputState.CapturedViewIndex;
	if (!IsViewActive(ActiveViewIndex)) return;

	// 키와 마우스 드래그는 우클릭으로 캡처한 View에만 전달한다.
	const bool bCameraCaptured = FInputSystem::IsMouseDown(EMouseButton::Right)
		&& InputState.CapturedViewIndex == ActiveViewIndex;
	FCameraMoveInput RawInput{};
	if (bCameraCaptured)
	{
		// 키 상태를 읽는 규칙은 이 입력 처리 구간에서만 사용한다.
		const auto AxisValue = [](EKeyCode Positive, EKeyCode Negative)
		{
			return (FInputSystem::IsKeyDown(Positive) ? 1.0f : 0.0f)
				- (FInputSystem::IsKeyDown(Negative) ? 1.0f : 0.0f);
		};
		RawInput.MoveAxis = {
			AxisValue(EKeyCode::W, EKeyCode::S),
			AxisValue(EKeyCode::D, EKeyCode::A),
			AxisValue(EKeyCode::E, EKeyCode::Q)
		};
		RawInput.MouseDelta = {
			static_cast<float>(FInputSystem::GetMouseDeltaX()),
			static_cast<float>(FInputSystem::GetMouseDeltaY())
		};
	}

	// 이미 한 번 읽은 휠 값을 대상 View에만 전달한다.
	RawInput.ZoomDelta = bCameraCaptured || PointerViewIndex == ActiveViewIndex
		? static_cast<float>(WheelDelta) : 0.0f;
	ViewportClients[ActiveViewIndex].UpdateCamera(
		DeltaTime, RawInput, GetViewRect(ActiveViewIndex),
		MoveSpeed, MouseSensitivity, bCameraCaptured);
}

// 패널 View는 Layout이, 외부 PIE View는 별도 크기와 카메라가 활성 여부를 결정한다.
bool FMultipleViewportsAdapter::IsViewActive(const int32 ViewIndex) const
{
	// 외부 View는 레이아웃과 무관하게 크기와 카메라가 주어졌을 때만 활성이다.
	if (ViewIndex == ExternalViewIndex)
		return ViewportClients[ViewIndex].HasCameraOverride() && IsViewRectValid(ExternalViewRect);
	return Layout.IsViewActive(ViewIndex);
}

// 외부 PIE 영역을 구분하고 패널 인덱스 검증은 Layout에 맡긴다.
const FRect& FMultipleViewportsAdapter::GetViewRect(const int32 ViewIndex) const
{
	return ViewIndex == ExternalViewIndex ? ExternalViewRect : Layout.GetViewRect(ViewIndex);
}

// 활성 View의 카메라를 선택하고 VP 계산을 Client에 맡긴다.
FMatrix FMultipleViewportsAdapter::GetEngineViewProjection(const int32 ViewIndex) const
{
	assert(IsViewActive(ViewIndex));
	return FEditorViewportClient::CalcViewProjection(
		GetEffectiveCamera(ViewIndex), GetViewRect(ViewIndex));
}

FLODViewContext FMultipleViewportsAdapter::GetLODViewContext(int32 ViewIndex) const
{
	assert(IsViewActive(ViewIndex));
	return FEditorViewportClient::CalcLODViewContext(GetEffectiveCamera(ViewIndex), GetViewRect(ViewIndex));

}

// 활성 View의 Override·외부 PIE 크기를 반영하고 절두체 계산은 Client에 맡긴다.
bool FMultipleViewportsAdapter::AppendShadowFrustumCorners(
    int32 ViewIndex, float ShadowDistance, TArray<FVector>& OutCorners) const
{
    if (!IsViewActive(ViewIndex))
        return false;

    return FEditorViewportClient::AppendShadowFrustumCorners(
        GetEffectiveCamera(ViewIndex), GetViewRect(ViewIndex), ShadowDistance, OutCorners);
}
// View 선택은 Adapter가, 카메라 구간의 역투영은 Client가 담당한다.
bool FMultipleViewportsAdapter::AppendShadowFrustumCorners(int32 ViewIndex, float NearDistance, float FarDistance, TArray<FVector>& OutCorners) const
{
	if (!IsViewActive(ViewIndex))
		return false;

	return FEditorViewportClient::AppendShadowFrustumCorners(GetEffectiveCamera(ViewIndex), GetViewRect(ViewIndex),
		NearDistance, FarDistance, OutCorners);

}

// Native 카메라 위치를 엔진 FVector 그대로 반환한다.
FVector FMultipleViewportsAdapter::GetEngineCameraLocation(const int32 ViewIndex) const
{
	return GetEffectiveCamera(ViewIndex).Transform.Location;
}

// Native 회전에서 정규화한 Forward를 엔진 FVector로 반환한다.
FVector FMultipleViewportsAdapter::GetEngineCameraForward(const int32 ViewIndex) const
{
	return FEditorViewportClient::NormalizedOrZero(GetEffectiveCamera(ViewIndex).Transform.Rotation.GetForwardVector());
}

// 지정 View의 논리 카메라로 렌더·피킹에 공통인 Billboard 계산을 호출한다.
FMatrix FMultipleViewportsAdapter::BuildEngineBillboardMatrix(const int32 ViewIndex, const FVector& WorldPosition, const float Width, const float Height) const
{
	return FEditorViewportClient::BuildBillboardMatrix(
		GetEffectiveCamera(ViewIndex), WorldPosition, Width, Height);
}

// 지정 View 카메라의 투영 모드가 Orthographic인지 검사한다.
bool FMultipleViewportsAdapter::IsOrthographic(const int32 ViewIndex) const
{
	return GetEffectiveCamera(ViewIndex).Projection.Mode == EProjectionMode::Orthographic;
}

// 피킹할 View를 판정하고 View 내부 좌표의 역투영을 Client에 맡긴다.
bool FMultipleViewportsAdapter::TryGetActiveViewRay(const FVector2 LocalMousePosition, FRay& OutRay) const
{
	if (ActiveViewIndex == InvalidViewIndex || !IsViewActive(ActiveViewIndex))
		return false;
	// Splitter gutter나 다른 View 위의 좌표를 이전 Active View의 Ray로 잘못 해석하지 않는다.
	if (Layout.FindViewAt(LocalMousePosition) != ActiveViewIndex)
		return false;
	const FRect& Rect = GetViewRect(ActiveViewIndex);
	const FVector2 ViewLocal{ LocalMousePosition.X - Rect.X, LocalMousePosition.Y - Rect.Y };
	OutRay = FEditorViewportClient::DeprojectView(
		GetEffectiveCamera(ActiveViewIndex), Rect, ViewLocal);
	return true;
}

// View 입력만 준비하고 정적·동적 패킷 수집은 World의 공통 경로에 맡긴다.
void FMultipleViewportsAdapter::BuildRenderQueue(UWorld& World, const int32 ViewIndex,
    FRenderQueue& OutQueue, FRenderQueue& OutIconQueue, FRenderQueue& OutArrowQueue)
{
    OutQueue.Reset();
    OutIconQueue.Reset();
    OutArrowQueue.Reset();
    if (!IsViewActive(ViewIndex)) return;

    const FViewCamera& ViewCamera = GetEffectiveCamera(ViewIndex);
    // Runtime은 이 동기 호출 동안만 논리 카메라 Context를 사용한다.
    const auto ResolveBillboard = [](const FVector& Position, float Width, float Height,
        const void* Context) -> FMatrix
    {
        const auto& Camera = *static_cast<const FViewCamera*>(Context);
        return FEditorViewportClient::BuildBillboardMatrix(Camera, Position, Width, Height);
    };
  
	FLODViewContext LODContext = GetLODViewContext(ViewIndex);
	FFrustumPlanes Frustum = ExtractFrustumPlanes(LODContext.ViewProjection);

	// Renderer를 넘기지 않아 기존 에디터의 GPU 오클루전·상수 업로드 정책을 유지한다.
	World.GatherRenderPackets(OutQueue, &LODContext, &Frustum, nullptr, 
		ResolveBillboard, &ViewCamera, &OutIconQueue, &OutArrowQueue);
}

// 라이트 컬링 등에 사용하는 렌더 View 행렬을 반환한다.
FMatrix FMultipleViewportsAdapter::GetEngineView(int32 ViewIndex) const
{
	assert(IsViewActive(ViewIndex));
	FMatrix View;
	FMatrix Projection;
	FEditorViewportClient::CalcViewMatrices(
		GetEffectiveCamera(ViewIndex), GetViewRect(ViewIndex), View, Projection);
	return View;
}

// 깊이 보정까지 반영된 렌더 Projection 행렬을 반환한다.
FMatrix FMultipleViewportsAdapter::GetEngineProjection(int32 ViewIndex) const
{
	assert(IsViewActive(ViewIndex));
	FMatrix View;
	FMatrix Projection;
	FEditorViewportClient::CalcViewMatrices(
		GetEffectiveCamera(ViewIndex), GetViewRect(ViewIndex), View, Projection);
	return Projection;
}
