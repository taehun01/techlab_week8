#pragma once

#include "Editor/LevelEditor/MultipleViewports/Adapter/MultipleViewportsAdapterTypes.h"
#include "Editor/LevelViewportLayout.h"
#include "Editor/HitoriEd/EditorViewportClient.h"

#include "Collision/Ray.h"
#include "Math/Matrix.h"
#include "Render/ViewMode.h"

#include <cassert>

class UWorld;
class FRenderQueue;
struct FLODViewContext;

// 패널 Layout·Client와 엔진의 입력·렌더 수집 경로를 연결한다.
class FMultipleViewportsAdapter
{
public:
    // 패널의 네 View(0~3) 밖에서 그리는 View(PIE 별도 창 등). 레이아웃·입력·피킹에는 참여하지 않고
    // 카메라 Override와 SetExternalViewSize로만 활성화되며 렌더·컬링 함수에서만 쓴다.
    static constexpr int32 ExternalViewIndex = FLevelViewportLayout::ViewCount;
    static constexpr int32 MaxViewCount = ExternalViewIndex + 1;
    // 외부 View의 렌더 크기를 지정한다. 0이면 비활성이다.
    void SetExternalViewSize(uint32 Width, uint32 Height);

    // 엔진 메인 카메라 설정을 읽고 네 View의 초기 상태와 프리셋을 구성한다.
    void InitializeFromWorld(UWorld& World);

    // 현재 Single·Quad 레이아웃 모드를 설정한다.
    void SetLayoutMode(ELayoutMode Mode) { Layout.SetLayoutMode(Mode); }
    // 현재 레이아웃 모드를 반환한다.
    ELayoutMode GetLayoutMode() const { return Layout.GetLayoutMode(); }
    // Single 대상 검증은 Layout이 담당한다.
    void SetSingleViewIndex(int32 ViewIndex) { Layout.SetSingleViewIndex(ViewIndex); }
    // Single 레이아웃에서 선택된 View 인덱스를 반환한다.
    int32 GetSingleViewIndex() const { return Layout.GetSingleViewIndex(); }
    // 속성 창의 변경을 반영하되 위치·투영 수치만 바뀌면 기존 축 정렬 프리셋을 유지한다.
    void ApplyCameraProperties(int32 ViewIndex, const FViewCamera& Camera, bool bRotationChanged);
    // 지정 View의 Native 카메라 상태를 반환한다.
    const FViewCamera& GetViewCamera(int32 ViewIndex) const;
    // 선택한 프리셋의 위치·방향·투영 모드를 지정 View에 적용한다.
    void ApplyCameraPreset(int32 ViewIndex, EMultipleViewportsCameraPreset Preset);
    // 저장 카메라는 그대로 두고 렌더·컬링·피킹에만 외부 카메라(PIE 게임 카메라 등)를 쓴다.
    // Forward는 정규화되어 있어야 하며 Roll은 버린다.
    void SetViewCameraOverride(int32 ViewIndex, const FVector& Location, const FVector& Forward, const FCameraProjection& Projection);
    void ClearViewCameraOverride(int32 ViewIndex);
    // 지정 View에 적용된 카메라 프리셋을 반환한다.
    EMultipleViewportsCameraPreset GetCameraPreset(int32 ViewIndex) const;
    // 프리셋 또는 카메라 Forward 지배축으로 표시할 Grid 평면을 고른다.
    EGridPlane GetGridPlane(int32 ViewIndex) const;

    // 레이아웃을 갱신하고 카메라 투영과 숨겨진 View의 입력 상태를 보정한다.
    void UpdateLayout(FVector2 WindowSize);
    // Split 비율 계산과 제한은 Layout이 담당한다.
    void ApplySplitterDrag(EDragAxis Axis, float DeltaPixels, FVector2 WindowSize)
    {
        Layout.ApplySplitterDrag(Axis, DeltaPixels, WindowSize);
    }
    // 우클릭 Capture View에 이동·Euler Yaw/Pitch·줌 입력을 적용한다.
    void UpdateInput(float DeltaTime, FVector2 LocalMousePosition, float MoveSpeed, float MouseSensitivity);
    // 레이아웃과 Rect 상태를 기준으로 지정 View의 활성 여부를 반환한다.
    bool IsViewActive(int32 ViewIndex) const;
    // 현재 입력을 소비할 활성 View 인덱스를 반환한다.
    int32 GetActiveViewIndex() const { return ActiveViewIndex; }
    // 입력 영역 밖에서도 마지막 편집 대상을 유지하며 Single에서는 확대 View를 반환한다.
    int32 GetEditorViewIndex() const { return Layout.GetLayoutMode() == ELayoutMode::Single ? Layout.GetSingleViewIndex() : EditorViewIndex; }
    // 속성 창에서 선택한 View를 공통 편집 대상으로 지정한다.
    void SetEditorViewIndex(int32 Index) { if (IsPanelViewIndex(Index)) EditorViewIndex = Index; }
    // View별 장면 래스터라이저 모드를 저장하고 조회한다.
    void SetViewWireframe(int32 Index, bool Value)
    {
        if (IsViewIndex(Index))
            ViewportClients[Index].SetWireframe(Value);
    }
    bool IsViewWireframe(int32 Index) const
    {
        return IsViewIndex(Index) && ViewportClients[Index].IsWireframe();
    }
    const FEngineShowFlags& GetViewShowFlags(int32 Index) const
    {
        assert(IsViewIndex(Index));
        return ViewportClients[Index].GetShowFlags();
    }

    void SetViewShowFlags(int32 Index, const FEngineShowFlags& Flags)
    {
        assert(IsViewIndex(Index));
        ViewportClients[Index].SetShowFlags(Flags);
    }
    // View별 조명 뷰모드(UberLit 변형)를 저장하고 조회한다.
    void SetViewMode(int32 Index, EViewMode Value)
    {
        if (IsViewIndex(Index))
            ViewportClients[Index].SetViewMode(Value);
    }
    EViewMode GetViewMode(int32 Index) const
    {
        return IsViewIndex(Index)
            ? ViewportClients[Index].GetViewMode() : EViewMode::Lit_Lambert;
    }
    // 우클릭 입력을 Capture 중인 View 인덱스를 반환한다.
    int32 GetCapturedViewIndex() const { return InputState.CapturedViewIndex; }
    // 지정 View의 로컬 화면 Rect를 반환한다.
    const FRect& GetViewRect(int32 ViewIndex) const;
    // Native View·Projection을 row-vector 순서로 합성한 엔진 행렬을 반환한다.
    FMatrix GetEngineViewProjection(int32 ViewIndex) const;
    // 패널·별도 PIE에서 선택된 실제 카메라와 영역을 Client에 전달한다.
    FLODViewContext GetLODViewContext(int32 ViewIndex) const;
    // 패널·별도 PIE의 실제 카메라와 영역으로 그림자 Receiver 꼭짓점을 추가한다.
    bool AppendShadowFrustumCorners(int32 ViewIndex, float ShadowDistance, TArray<FVector>& OutCorners) const;
    // 지정 View의 실제 카메라·렌더 영역으로 Receiver 구간을 계산한다.
    bool AppendShadowFrustumCorners(int32 ViewIndex, float NearDistance, float FarDistance, TArray<FVector>& OutCorners) const;
    // 지정 View 카메라 위치를 엔진 FVector 그대로 반환한다.
    FVector GetEngineCameraLocation(int32 ViewIndex) const;
    // 지정 View의 카메라 Forward를 엔진 FVector로 계산해 반환한다.
    FVector GetEngineCameraForward(int32 ViewIndex) const;
    // Core Billboard 계산 결과를 엔진 월드 행렬로 변환한다.
    FMatrix BuildEngineBillboardMatrix(int32 ViewIndex, const FVector& WorldPosition, float Width, float Height) const;
    // 지정 View가 직교 투영인지 반환한다.
    bool IsOrthographic(int32 ViewIndex) const;
    // 활성 View의 로컬 마우스 좌표를 Core로 역투영해 엔진 Ray로 반환한다.
    bool TryGetActiveViewRay(FVector2 LocalMousePosition, FRay& OutRay) const;
    // View 입력을 준비하고 컬링·LOD·패킷 수집은 World에 맡긴다.
    void BuildRenderQueue(UWorld& World, int32 ViewIndex, FRenderQueue& OutQueue,
        FRenderQueue& OutIconQueue, FRenderQueue& OutArrowQueue);

    // 외부에서 읽은 Split 비율의 검증과 저장은 Layout이 담당한다.
    void SetSplitRatio(const FSplitRatio& Value) { Layout.SetSplitRatio(Value); }
    // 현재 가로·세로 Split 비율을 반환한다.
    const FSplitRatio& GetSplitRatio() const { return Layout.GetSplitRatio(); }

    FMatrix GetEngineView(int32 ViewIndex) const;
    FMatrix GetEngineProjection(int32 ViewIndex) const;

    // 실제 카메라를 조회하는 공통 경로에서 배열 범위를 검증한다.
    const FViewCamera& GetEffectiveCamera(int32 ViewIndex) const
    {
        assert(IsViewIndex(ViewIndex));
        return ViewportClients[ViewIndex].GetEffectiveCamera();
    }
private:
    // 편집 카메라·프리셋·Grid는 패널 View만 대상으로 한다.
    static constexpr bool IsPanelViewIndex(int32 Index)
    {
        return Index >= 0 && Index < FLevelViewportLayout::ViewCount;
    }
    // 렌더·표시 설정·Override는 외부 PIE View까지 대상으로 한다.
    static constexpr bool IsViewIndex(int32 Index)
    {
        return Index >= 0 && Index < MaxViewCount;
    }
    // 패널 배치는 Layout이, 카메라·표시 상태는 패널 4개와 외부 PIE의 Client가 소유한다.
    FLevelViewportLayout Layout;
    FEditorViewportClient ViewportClients[MaxViewCount];
    FViewInputState InputState{};
    // 별도 PIE 창의 렌더 영역은 패널 레이아웃과 무관하다.
    FRect ExternalViewRect{};
    int32 ActiveViewIndex = InvalidViewIndex;
    int32 EditorViewIndex = 0;
};
