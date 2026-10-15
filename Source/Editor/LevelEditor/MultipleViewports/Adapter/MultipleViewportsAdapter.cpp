#include "EnginePCH.h"

#include "Editor/LevelEditor/MultipleViewports/Adapter/MultipleViewportsAdapter.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/ParticleSubUVComponent.h"
#include "Editor/Rendering/GridRenderer.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/World.h"
#include "Input/InputSystem.h"
#include "Asset/LOD/StaticMeshLODSelector.h"

// View 입력에서 사용하는 ImGui API를 직접 포함한다.
#include "imgui.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace
{
	constexpr float Pi = 3.14159265358979323846f;

	// 양·음 방향 키 상태 차이로 -1~1 축 입력을 만든다.
	float AxisValue(const EKeyCode Positive, const EKeyCode Negative)
	{
		return (FInputSystem::IsKeyDown(Positive) ? 1.0f : 0.0f) - (FInputSystem::IsKeyDown(Negative) ? 1.0f : 0.0f);
	}

} // 익명 네임스페이스

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

// Single 대상의 검증과 저장은 레이아웃 소유자에게 맡긴다.
void FMultipleViewportsAdapter::SetSingleViewIndex(const int32 ViewIndex)
{
	Layout.SetSingleViewIndex(ViewIndex);
}

// View 인덱스를 검증하고 카메라 적용을 해당 Client에 맡긴다.
void FMultipleViewportsAdapter::SetViewCamera(const int32 ViewIndex, const FViewCamera& Camera)
{
	assert(ViewIndex >= 0 && ViewIndex < FLevelViewportLayout::ViewCount);
	ViewportClients[ViewIndex].SetCamera(Camera, GetViewRect(ViewIndex));
}

// 속성 편집 시 프리셋 유지 여부는 카메라를 소유한 Client가 결정한다.
void FMultipleViewportsAdapter::ApplyCameraProperties(const int32 ViewIndex,
	const FViewCamera& Camera, const bool bRotationChanged)
{
	assert(ViewIndex >= 0 && ViewIndex < FLevelViewportLayout::ViewCount);
	ViewportClients[ViewIndex].ApplyCameraProperties(Camera, bRotationChanged, GetViewRect(ViewIndex));
}

// 외부 카메라의 위치·방향·투영을 Override로 보관한다. 저장 카메라와 프리셋은 바꾸지 않는다.
void FMultipleViewportsAdapter::SetViewCameraOverride(const int32 ViewIndex, const FVector& Location,
	const FVector& Forward, const FCameraProjection& Projection)
{
	assert(ViewIndex >= 0 && ViewIndex < MaxViewCount);
	FViewCamera& Camera = CameraOverrides[ViewIndex];
	Camera.Transform.Location = Location;
	const FVector Direction = FEditorViewportClient::NormalizedOrZero(Forward);
	Camera.Transform.Rotation = FEditorViewportClient::MakeCameraRotation(
		std::atan2(Direction.Y, Direction.X) * 180.0f / Pi,
		std::asin(FMath::Clamp(Direction.Z, -1.0f, 1.0f)) * 180.0f / Pi);
	Camera.Projection = Projection;
	FEditorViewportClient::ConstrainOrthographicWidth(Camera.Projection, GetViewRect(ViewIndex));
	bHasCameraOverride[ViewIndex] = true;
}

// Override를 해제해 저장 카메라로 되돌린다.
void FMultipleViewportsAdapter::ClearViewCameraOverride(const int32 ViewIndex)
{
	assert(ViewIndex >= 0 && ViewIndex < MaxViewCount);
	bHasCameraOverride[ViewIndex] = false;
}

// 범위를 검사한 뒤 지정 View 카메라의 const 참조를 반환한다.
const FViewCamera& FMultipleViewportsAdapter::GetViewCamera(const int32 ViewIndex) const
{
	assert(ViewIndex >= 0 && ViewIndex < FLevelViewportLayout::ViewCount);
	return ViewportClients[ViewIndex].GetCamera();
}

// 선택한 View의 프리셋 적용을 해당 Client에 맡긴다.
void FMultipleViewportsAdapter::ApplyCameraPreset(const int32 ViewIndex, const EMultipleViewportsCameraPreset Preset)
{
	assert(ViewIndex >= 0 && ViewIndex < FLevelViewportLayout::ViewCount);
	ViewportClients[ViewIndex].ApplyCameraPreset(Preset, GetViewRect(ViewIndex));
}

// 범위를 검사한 뒤 지정 View의 프리셋을 반환한다.
EMultipleViewportsCameraPreset FMultipleViewportsAdapter::GetCameraPreset(const int32 ViewIndex) const
{
	assert(ViewIndex >= 0 && ViewIndex < FLevelViewportLayout::ViewCount);
	return ViewportClients[ViewIndex].GetCameraPreset();
}

// 축 정렬 프리셋은 고정 평면을, Current는 Forward 지배축에 수직인 평면을 고른다.
EGridPlane FMultipleViewportsAdapter::GetGridPlane(const int32 ViewIndex) const
{
	assert(ViewIndex >= 0 && ViewIndex < 4);
	switch (ViewportClients[ViewIndex].GetCameraPreset())
	{
	case EMultipleViewportsCameraPreset::Top:
	case EMultipleViewportsCameraPreset::Bottom:
		return EGridPlane::XY;
	case EMultipleViewportsCameraPreset::Front:
	case EMultipleViewportsCameraPreset::Back:
		return EGridPlane::YZ;
	case EMultipleViewportsCameraPreset::Left:
	case EMultipleViewportsCameraPreset::Right:
		return EGridPlane::XZ;
	case EMultipleViewportsCameraPreset::Perspective:
		return EGridPlane::XY;
	case EMultipleViewportsCameraPreset::OrthographicView:
		break;
	}

	const FVector Forward = GetEffectiveCamera(ViewIndex).Transform.Rotation.GetForwardVector();
	const float AbsX = std::fabs(Forward.X);
	const float AbsY = std::fabs(Forward.Y);
	const float AbsZ = std::fabs(Forward.Z);
	if (AbsZ >= AbsX && AbsZ >= AbsY) return EGridPlane::XY;
	if (AbsY >= AbsX) return EGridPlane::XZ;
	return EGridPlane::YZ;
}

// 배치는 Layout에 맡기고 기존 카메라 보정과 입력 대상 유지 규칙을 적용한다.
void FMultipleViewportsAdapter::UpdateLayout(const FVector2 WindowSize, const FVector2 LocalMousePosition)
{
	Layout.Update(WindowSize, SplitterThickness);
	// 창 크기와 Single/Quad 전환으로 Aspect가 바뀌어도 저장값과 실제 투영값을 함께 보정한다.
	for (int32 ViewIndex = 0; ViewIndex < 4; ++ViewIndex)
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

// Drag는 비율만 바꾸며 호출자가 UpdateLayout에서 배치와 입력 상태를 함께 갱신한다.
void FMultipleViewportsAdapter::ApplySplitterDrag(const EDragAxis Axis, const float DeltaPixels, const FVector2 WindowSize)
{
	Layout.ApplySplitterDrag(Axis, DeltaPixels, WindowSize);
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
	// 외부 카메라가 그리는 View는 에디터 카메라 조작을 받지 않는다.
	if (bHasCameraOverride[ActiveViewIndex]) return;

	// 키와 마우스 드래그는 우클릭으로 캡처한 View에만 전달한다.
	const bool bCameraCaptured = FInputSystem::IsMouseDown(EMouseButton::Right)
		&& InputState.CapturedViewIndex == ActiveViewIndex;
	FCameraMoveInput RawInput{};
	if (bCameraCaptured)
	{
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

// 현재 World의 가시 컴포넌트에서 경계만 캡처한다. Mesh·삼각형은 복사하지 않는다.
void FMultipleViewportsAdapter::CaptureWorld(UWorld& World)
{
	RenderObjects.Reset();
	for (auto& Entry : PrimitiveById) Entry.second.bCaptured = false;
	bCapturedBillboard = false;
	bCapturedParticle = false;

	const FScene& Scene = World.GetScene();
	const int32 Count = Scene.PrimitiveSceneProxies.Num();
	for (int32 i = 0; i < Count;++i)
	{
		if (!Scene.PrimitiveFlags[i]) continue;
		UPrimitiveComponent* Primitive = Scene.PrimitiveSceneProxies[i]->GetComponent();
		if (!Primitive || !Primitive->IsVisible() || !Primitive->GetOwner() ||
			Primitive->GetOwner()->GetWorld() != &World) continue;
		const ObjectId Id = Primitive->GetUUID();
		if (Id == InvalidObjectId) continue;

		FRenderableObject RenderObject{};
		RenderObject.Id = Id;
		const FPrimitiveSceneProxy* Proxy = Primitive->GetSceneProxy();
		RenderObject.WorldBounds = Proxy && Proxy->GetMesh()
			? Proxy->GetBounds() : MakeWorldBounds(Primitive->CalcBounds());
		RenderObjects.Add(RenderObject);
		PrimitiveSnapshot& Snapshot = PrimitiveById[Id];
		Snapshot.Primitive = Primitive;
		Snapshot.bCaptured = true;
		Snapshot.bParticlesPrepared = false;
		if (Cast<UParticleSubUVComponent>(Primitive)) bCapturedParticle = true;
		else if (Cast<UBillboardComponent>(Primitive)) bCapturedBillboard = true;
	}

	for (auto Iterator = PrimitiveById.begin(); Iterator != PrimitiveById.end();)
	{
		const auto Current = Iterator++;
		if (!Current->second.bCaptured) PrimitiveById.Remove(Current->first);
	}
}

// 패널 View는 Layout이, 외부 PIE View는 별도 크기와 카메라가 활성 여부를 결정한다.
bool FMultipleViewportsAdapter::IsViewActive(const int32 ViewIndex) const
{
	// 외부 View는 레이아웃과 무관하게 크기와 카메라가 주어졌을 때만 활성이다.
	if (ViewIndex == ExternalViewIndex)
		return bHasCameraOverride[ViewIndex] && IsViewRectValid(ExternalViewRect);
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

// Native 카메라 위치를 엔진 FVector 그대로 반환한다.
FVector FMultipleViewportsAdapter::GetEngineCameraLocation(const int32 ViewIndex) const
{
	assert(ViewIndex >= 0 && ViewIndex < MaxViewCount);
	return GetEffectiveCamera(ViewIndex).Transform.Location;
}

// Native 회전에서 정규화한 Forward를 엔진 FVector로 반환한다.
FVector FMultipleViewportsAdapter::GetEngineCameraForward(const int32 ViewIndex) const
{
	assert(ViewIndex >= 0 && ViewIndex < MaxViewCount);
	return FEditorViewportClient::NormalizedOrZero(GetEffectiveCamera(ViewIndex).Transform.Rotation.GetForwardVector());
}

// 위치·크기·View 카메라로 엔진 규약의 Billboard 행렬을 계산한다.
FMatrix FMultipleViewportsAdapter::BuildEngineBillboardMatrix(const int32 ViewIndex, const FVector& WorldPosition, const float Width, const float Height) const
{
	assert(ViewIndex >= 0 && ViewIndex < MaxViewCount);
	const FViewCamera& ViewCamera = GetEffectiveCamera(ViewIndex);
	if (ViewCamera.Projection.Mode == EProjectionMode::Orthographic)
	{
		// 직교 투영의 모든 시선은 평행하므로 카메라 위치가 아니라 고정된 화면 기저를 사용한다.
		// 위치 기반 LookAt을 사용하면 평면 이동 시 오브젝트-카메라 벡터가 달라져 Billboard가 회전한다.
		const FVector Facing = FEditorViewportClient::NormalizedOrZero(ViewCamera.Transform.Rotation.GetForwardVector()) * -1.0f;
		const FVector Right = FEditorViewportClient::NormalizedOrZero(ViewCamera.Transform.Rotation.GetRightVector());
		const FVector Up = FEditorViewportClient::NormalizedOrZero(ViewCamera.Transform.Rotation.GetUpVector());

		FMatrix EngineMatrix;
		EngineMatrix.SetIdentity();
		EngineMatrix.M[0][0] = Facing.X; EngineMatrix.M[0][1] = Facing.Y; EngineMatrix.M[0][2] = Facing.Z;
		EngineMatrix.M[1][0] = Right.X * Width; EngineMatrix.M[1][1] = Right.Y * Width; EngineMatrix.M[1][2] = Right.Z * Width;
		EngineMatrix.M[2][0] = Up.X * Height; EngineMatrix.M[2][1] = Up.Y * Height; EngineMatrix.M[2][2] = Up.Z * Height;
		EngineMatrix.M[3][0] = WorldPosition.X; EngineMatrix.M[3][1] = WorldPosition.Y; EngineMatrix.M[3][2] = WorldPosition.Z;
		return EngineMatrix;
	}

	const FBillboardTransform Result = ComputeBillboardTransform(
		{ WorldPosition, {Width, Height} },
		ViewCamera.Transform);
	FMatrix EngineMatrix = Result.WorldMatrix;

	// 팀 엔진의 ParticleQuad는 Core의 Billboard 오른쪽 축과 반대 와인딩을 사용한다.
	// 오른쪽 축만 뒤집어 기존 렌더 경로와 같은 앞면이 카메라를 향하게 한다.
	EngineMatrix.M[1][0] = -EngineMatrix.M[1][0];
	EngineMatrix.M[1][1] = -EngineMatrix.M[1][1];
	EngineMatrix.M[1][2] = -EngineMatrix.M[1][2];
	return EngineMatrix;
}

// 지정 View 카메라의 투영 모드가 Orthographic인지 검사한다.
bool FMultipleViewportsAdapter::IsOrthographic(const int32 ViewIndex) const
{
	assert(ViewIndex >= 0 && ViewIndex < MaxViewCount);
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

// 범위를 검사한 뒤 지정 View의 재사용 가시 ID 버퍼 크기를 반환한다.
std::size_t FMultipleViewportsAdapter::GetVisibleObjectCount(const int32 ViewIndex) const
{
	assert(ViewIndex >= 0 && ViewIndex < MaxViewCount);
	return VisibleIds[ViewIndex].Num();
}

// 가시 ID를 컴포넌트로 역매핑해 큐를 구성한다. 생존 목록·상수는 프레임 공통, Billboard·정렬 거리는 View별이다.
void FMultipleViewportsAdapter::BuildRenderQueue(const int32 ViewIndex, FRenderQueue& OutQueue, FRenderQueue& OutIconQueue, FRenderQueue& OutArrowQueue)
{
    OutQueue.Reset();
    OutIconQueue.Reset();
    OutArrowQueue.Reset();
    
    PendingStaticMeshes.Reset();
    LODInputs.Reset();
    if (!IsViewActive(ViewIndex)) return;
    const FRect& Rect = GetViewRect(ViewIndex);
    const FViewCamera& ViewCamera = GetEffectiveCamera(ViewIndex);
    // 컬링과 LOD가 이번 호출에서 계산한 동일한 VP를 사용한다.
    FFrustumPlanes Frustum{};
    const FMatrix ViewProjection = FEditorViewportClient::CalcViewProjection(
        ViewCamera, Rect, &Frustum);
    CullForView(RenderObjects, Frustum, VisibleIds[ViewIndex]);

    // 직교 깊이 보정은 Projection의 화면 X/Y 배율을 바꾸지 않는다.
    const FMatrix Projection = GetEngineProjection(ViewIndex);
    const float ScaleX = Projection.M[1][0];
    const float ScaleY = Projection.M[2][1];

	FLODViewContext LODContext{
		static_cast<uint32>(Rect.Width),
		static_cast<uint32>(Rect.Height)
	};
	// 거리와 NearZ는 기존처럼 논리 카메라를 기준으로 유지한다.
	LODContext.CameraPosition = ViewCamera.Transform.Location;
	LODContext.ViewProjection = ViewProjection;
	LODContext.CameraForward = GetEngineCameraForward(ViewIndex);
	LODContext.ProjectionScaleSquared = std::max(ScaleX * ScaleX, ScaleY * ScaleY);
	LODContext.NearZ = ViewCamera.Projection.NearClip;
	LODContext.bOrthographic = ViewCamera.Projection.Mode == EProjectionMode::Orthographic;
	LODContext.Prepare();

	for (const ObjectId Id : VisibleIds[ViewIndex])
	{
		const auto Found = PrimitiveById.Find(Id);
		if (!Found || !Found->Primitive)
			continue;

        UPrimitiveComponent* Primitive = Found->Primitive;
        if (UParticleSubUVComponent* ParticleComponent = Cast<UParticleSubUVComponent>(Primitive))
        {
            const TArray<FParticle>& Particles = ParticleComponent->GetParticlesForView();
            PrimitiveSnapshot& Snapshot = *Found;
            if (!Snapshot.bParticlesPrepared)
            {
                Snapshot.AliveParticleIndices.Reset();
                Snapshot.AliveParticleIndices.Reserve(Particles.Num());
                for (int32 Index = 0; Index < Particles.Num(); ++Index)
                    if (Particles[Index].bAlive) Snapshot.AliveParticleIndices.Add(Index);
                ParticleComponent->BeginViewSubmission();
                Snapshot.bParticlesPrepared = true;
            }
            // 반투명은 모든 emitter를 합친 최종 Renderer만 정렬한다. 불투명은 Core 정렬을 유지하며 동률은 인덱스 순서다.
            const FVector CameraLocation = GetEngineCameraLocation(ViewIndex);
            const bool bOpaque = ParticleComponent->UsesOpaqueMaterial();
            if (bOpaque)
            {
                SortInputs.Reset();
                for (const int32 Index : Snapshot.AliveParticleIndices)
                    SortInputs.Add({static_cast<ObjectId>(Index + 1), Particles[Index].Location});
                SortParticlesByCameraDistance(SortInputs, ViewCamera.Transform.Location, SortedParticleIds);
            }
            for (int32 Order = 0; Order < Snapshot.AliveParticleIndices.Num(); ++Order)
            {
                const int32 ParticleIndex = bOpaque ? static_cast<int32>(SortedParticleIds[Order] - 1) : Snapshot.AliveParticleIndices[Order];
                const FParticle& Particle = Particles[ParticleIndex];
                const FMatrix ParticleWorld = BuildEngineBillboardMatrix(ViewIndex, Particle.Location, Particle.Scale, Particle.Scale);
                const FVector Delta = Particle.Location - CameraLocation;
                ParticleComponent->SubmitParticleToRenderQueue(OutQueue, ParticleIndex, ParticleWorld, Delta.Dot(Delta));
            }
        }
        else if (UBillboardComponent* Billboard = Cast<UBillboardComponent>(Primitive))
        {
            const FVector Scale = Billboard->GetWorldScale3D();

            // Editor Icon 이면 IconQueue로 빼기
            FRenderQueue& Target = Billboard->IsEditorIcon()? OutIconQueue : OutQueue;

            Billboard->SubmitToRenderQueue(
                Target,
                BuildEngineBillboardMatrix(ViewIndex, Billboard->GetWorldLocation(), Scale.Y, Scale.Z));
        }
        else if (auto* StaticComponent = Cast<UStaticMeshComponent>(Primitive))
        {
            UStaticMesh* Mesh = StaticComponent->GetStaticMesh();
            if (!Mesh)
                continue;
            
            // 빛 방향 Arrow 이면 ArrowQueue로 빼기
            if (StaticComponent->IsEditorIcon())
            {
                StaticComponent->SubmitToRenderQueue(OutArrowQueue, 0);
                continue;
            }

			const FPrimitiveSceneProxy* Proxy = StaticComponent->GetSceneProxy();
			if (Proxy)
			{
				PendingStaticMeshes.Add(StaticComponent);
				LODInputs.Add({ Proxy->GetLODSphere(), Proxy->GetRenderState() });
			}
		}
		else
		{
			Primitive->SubmitToRenderQueue(OutQueue);
		}
	}
	SelectLODs(LODInputs, LODContext, SelectedLODs);
	for (uint32 I = 0; I < static_cast<uint32>(PendingStaticMeshes.Num()); ++I)
		PendingStaticMeshes[I]->SubmitToRenderQueue(OutQueue, SelectedLODs[I]);
}

// Config에서 받은 비율의 검증과 저장은 Layout이 담당한다.
void FMultipleViewportsAdapter::SetSplitRatio(const FSplitRatio& Value)
{
	Layout.SetSplitRatio(Value);
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
