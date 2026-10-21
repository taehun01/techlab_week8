#include "EnginePCH.h"
#include "Editor/Viewports/ViewportsPanel.h"
#include "Editor/LevelEditor/MultipleViewports/Adapter/MultipleViewportsAdapter.h"

#include "Core/StatOverlay.h"
#include "Core/Stats/LightweightStats.h"
#include "Render/RenderCommand.h"
#include "Editor/EditorUI/ShowFlagUI.h"
#include <algorithm>
#include <cassert>
#include <format>

#include "Core/Stats/EditorStats.h"
#include "Core/Stats/ShadowStats.h"
#include "Render/ShadowSettings.h"

namespace
{
constexpr ImU32 SplitterColor = IM_COL32(55, 55, 55, 230);
constexpr ImU32 SplitterHoverColor = IM_COL32(255, 192, 0, 255);
constexpr const char* CameraPresetLabels[] = {
	"Perspective", "Ortho (Current)", "Top", "Bottom", "Front", "Back", "Left", "Right"};

// Stat Overlay
constexpr float StatOverlayMargin = 8.0f;
constexpr float StatOverlayPadding = 6.0f;
constexpr ImU32 StatOverlayBackgroundColor = IM_COL32(0, 0, 0, 140);
constexpr ImU32 TitleColor = IM_COL32(255, 210, 60, 255);
constexpr ImU32 ValueColor = IM_COL32(235, 235, 235, 255);
}

// 네 View의 렌더 타깃을 최소 크기로 초기화한다.
bool FViewportsPanel::Init()
{
	for (FViewSlot& Slot : Slots)
		ResizeSlot(Slot, 1, 1);
	return true;
}

// 패널의 프레임 갱신 인터페이스이며 별도 계산은 하지 않는다.
void FViewportsPanel::Tick(float DeltaTime)
{
	(void)DeltaTime;
}

// Core Rect에 맞춰 View 활성 상태와 타깃 크기를 갱신한다.
void FViewportsPanel::SetView(const int32 ViewIndex, const FRect& Rect, const bool bActive)
{
	assert(ViewIndex >= 0 && ViewIndex < 4);
	FViewSlot& Slot = Slots[ViewIndex];
	Slot.Rect = Rect;
	Slot.bActive = bActive && Rect.Width > 0.0f && Rect.Height > 0.0f;
	if (!Slot.bActive)
		return;

	const uint32 Width = static_cast<uint32>(std::max(1.0f, Rect.Width));
	const uint32 Height = static_cast<uint32>(std::max(1.0f, Rect.Height));
	if (Width != Slot.Width || Height != Slot.Height)
		ResizeSlot(Slot, Width, Height);
}

// 인덱스를 검사해 해당 View의 렌더 정보를 반환한다.
const FRenderingInfo& FViewportsPanel::GetRenderingInfo(const int32 ViewIndex) const
{
	assert(ViewIndex >= 0 && ViewIndex < 4);
	return Slots[ViewIndex].RenderingInfo;
}

FTexture2D* FViewportsPanel::GetFxaaTarget(const int32 ViewIndex) const
{
	assert(ViewIndex >= 0 && ViewIndex < 4);
	return Slots[ViewIndex].FxaaTarget.get();
}

FTexture2D* FViewportsPanel::GetHDRTarget(const int32 ViewIndex) const
{
	assert(ViewIndex >= 0 && ViewIndex < 4);
	return Slots[ViewIndex].HDRTarget.get();
}

// 마우스 위치에서 패널 원점을 빼 로컬 좌표로 바꾼다.
FVector2 FViewportsPanel::GetLocalMousePosition() const
{
	const ImVec2 Mouse = ImGui::GetMousePos();
	return {Mouse.x - ContentOrigin.x, Mouse.y - ContentOrigin.y};
}

// 엔진 입력 처리는 ImGui NewFrame보다 먼저 실행되므로 현재 OS 커서로 버튼 영역을 검사한다.
bool FViewportsPanel::IsHovered() const
{
	if (bStatResetButtonVisible && FStatOverlay::IsAnyEnabled())
	{
		POINT Cursor{};
		if (::GetCursorPos(&Cursor) &&
			Cursor.x >= StatResetButtonMin.x && Cursor.x < StatResetButtonMax.x &&
			Cursor.y >= StatResetButtonMin.y && Cursor.y < StatResetButtonMax.y)
			return false;
	}
	return bHovered;
}

// 누적 가로 Splitter 이동량을 반환하고 초기화한다.
float FViewportsPanel::ConsumeHorizontalDrag()
{
	const float Result = PendingHorizontalDrag;
	PendingHorizontalDrag = 0.0f;
	return Result;
}

// 누적 세로 Splitter 이동량을 반환하고 초기화한다.
float FViewportsPanel::ConsumeVerticalDrag()
{
	const float Result = PendingVerticalDrag;
	PendingVerticalDrag = 0.0f;
	return Result;
}

// Layout·Single 대상·Preset을 UI 표시와 동기화한다.
void FViewportsPanel::SetControlState(const ELayoutMode LayoutMode, const int32 SingleViewIndex, const EMultipleViewportsCameraPreset CameraPresets[4])
{
	CurrentLayoutMode = LayoutMode;
	CurrentSingleViewIndex = SingleViewIndex;
	for (int32 Index = 0; Index < 4; ++Index)
		CurrentCameraPresets[Index] = CameraPresets[Index];
}

// 대기 Layout 요청을 한 번 반환하고 플래그를 지운다.
bool FViewportsPanel::ConsumeLayoutRequest(ELayoutMode& OutMode, int32& OutSingleViewIndex)
{
	if (!bHasLayoutRequest)
		return false;
	OutMode = RequestedLayoutMode;
	OutSingleViewIndex = RequestedSingleViewIndex;
	bHasLayoutRequest = false;
	return true;
}

// 대기 Preset 요청을 한 번 반환하고 지운다.
bool FViewportsPanel::ConsumeCameraPresetRequest(int32& OutViewIndex, EMultipleViewportsCameraPreset& OutPreset)
{
	if (PendingCameraPresetViewIndex == InvalidViewIndex)
		return false;
	OutViewIndex = PendingCameraPresetViewIndex;
	OutPreset = PendingCameraPreset;
	PendingCameraPresetViewIndex = InvalidViewIndex;
	return true;
}

// View Texture와 Splitter·Layout·Preset UI를 그리고 요청을 기록한다.
void FViewportsPanel::OnRender()
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
	ImGui::Begin("Viewports", nullptr,
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoTitleBar);

	ContentOrigin = ImGui::GetCursorScreenPos();
	ContentSize = ImGui::GetContentRegionAvail();
	ContentSize.x = std::max(1.0f, ContentSize.x);
	ContentSize.y = std::max(1.0f, ContentSize.y);
	bHovered = ImGui::IsWindowHovered();
	bStatResetButtonVisible = false;

	// 전체 캔버스를 한 번 확보한 뒤 각 렌더 타깃을 창 DrawList에 직접 그린다.
	// Image 항목 네 개를 따로 배치하면 ImGui 레이아웃과 클리핑 상태가 삽입 순서에
	// 영향을 받아, Core가 올바른 사각형을 줘도 아래쪽 행이 잘릴 수 있다.
	ImGui::Dummy(ContentSize);
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->PushClipRect(ContentOrigin,
		{ContentOrigin.x + ContentSize.x, ContentOrigin.y + ContentSize.y}, true);
	for (int32 ViewIndex = 0; ViewIndex < 4; ++ViewIndex)
	{
		const FViewSlot& Slot = Slots[ViewIndex];
		if (!Slot.bActive || !Slot.ColorTarget)
			continue;

		const ImVec2 ViewMin{ContentOrigin.x + Slot.Rect.X, ContentOrigin.y + Slot.Rect.Y};
		const ImVec2 ViewMax{ViewMin.x + Slot.Rect.Width, ViewMin.y + Slot.Rect.Height};
		DrawList->AddImage(Slot.ColorTarget->GetSRV(), ViewMin, ViewMax);
	}
	DrawList->PopClipRect();

	if (Slots[1].bActive || Slots[2].bActive || Slots[3].bActive)
	{
		// View Rect 사이에 비워 둔 gutter의 중앙에 Splitter 버튼을 배치한다.
		const float SplitX = (Slots[0].Rect.X + Slots[0].Rect.Width + Slots[1].Rect.X) * 0.5f;
		const float SplitY = (Slots[0].Rect.Y + Slots[0].Rect.Height + Slots[2].Rect.Y) * 0.5f;

		const ImVec2 VerticalMin{ContentOrigin.x + SplitX - SplitterThickness * 0.5f, ContentOrigin.y};
		const ImVec2 VerticalMax{VerticalMin.x + SplitterThickness, ContentOrigin.y + ContentSize.y};
		const ImVec2 HorizontalMin{ContentOrigin.x, ContentOrigin.y + SplitY - SplitterThickness * 0.5f};
		const ImVec2 HorizontalMax{ContentOrigin.x + ContentSize.x, HorizontalMin.y + SplitterThickness};

		ImGui::SetCursorScreenPos(VerticalMin);
		ImGui::InvisibleButton("##MultipleViewportsHorizontalSplitter", {SplitterThickness, ContentSize.y});
		ImGui::SetCursorScreenPos(HorizontalMin);
		ImGui::InvisibleButton("##MultipleViewportsVerticalSplitter", {ContentSize.x, SplitterThickness});

		const bool bVerticalHovered = bHovered && ImGui::IsMouseHoveringRect(VerticalMin, VerticalMax);
		const bool bHorizontalHovered = bHovered && ImGui::IsMouseHoveringRect(HorizontalMin, HorizontalMax);
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			bDraggingVerticalSplitter = bVerticalHovered;
			bDraggingHorizontalSplitter = bHorizontalHovered;
		}
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			bDraggingVerticalSplitter = false;
			bDraggingHorizontalSplitter = false;
		}

		const bool bVerticalHighlighted = bDraggingVerticalSplitter || bVerticalHovered;
		const bool bHorizontalHighlighted = bDraggingHorizontalSplitter || bHorizontalHovered;
		DrawList->AddRectFilled(VerticalMin, VerticalMax,
			bVerticalHighlighted ? SplitterHoverColor : SplitterColor);
		DrawList->AddRectFilled(HorizontalMin, HorizontalMax,
			bHorizontalHighlighted ? SplitterHoverColor : SplitterColor);

		if (bDraggingVerticalSplitter)
			PendingHorizontalDrag += ImGui::GetIO().MouseDelta.x;
		if (bDraggingHorizontalSplitter)
			PendingVerticalDrag += ImGui::GetIO().MouseDelta.y;

		if (bVerticalHighlighted && bHorizontalHighlighted)
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
		else if (bVerticalHighlighted)
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		else if (bHorizontalHighlighted)
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
	}
	else
	{
		bDraggingVerticalSplitter = false;
		bDraggingHorizontalSplitter = false;
	}

	// 뷰포트별 반응형 툴바: 현재 줄에 공간이 없으면 다음 줄로 내려 배치한다.
	float ToolbarExtraHeight[4]{};

	for (int32 ViewIndex = 0; ViewIndex < 4; ++ViewIndex)
	{
		if (!Slots[ViewIndex].bActive)
			continue;

		const FViewSlot& Slot = Slots[ViewIndex];

		const float Left = ContentOrigin.x + Slot.Rect.X + 8.0f;
		const float Top = ContentOrigin.y + Slot.Rect.Y + 8.0f;
		const float Width = std::max(1.0f, Slot.Rect.Width - 16.0f);

		ImVec2 Cursor{ Left, Top };

		auto PlaceToolbarItem = [&](float PreferredWidth)
		{
			const float ItemWidth = std::min(PreferredWidth, Width);

			if (Cursor.x > Left && Cursor.x + ItemWidth > Left + Width)
			{
				Cursor.x = Left;
				Cursor.y += ImGui::GetFrameHeightWithSpacing();
			}

			ImGui::SetCursorScreenPos(Cursor);
			Cursor.x += ItemWidth + ImGui::GetStyle().ItemSpacing.x;

			return ItemWidth;
		};

		auto ComboWidth = [](const char* LongestLabel)
		{
			return ImGui::CalcTextSize(LongestLabel).x + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f;
		};

		auto SmallButtonWidth = [](const char* Label)
		{
			return ImGui::CalcTextSize(Label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
		};

		ImGui::PushID(100 + ViewIndex);

		// Camera Preset
		int SelectedPreset = static_cast<int>(CurrentCameraPresets[ViewIndex]);
		ImGui::SetNextItemWidth(PlaceToolbarItem(ComboWidth("Ortho (Current)")));
		if (ImGui::Combo("##CameraPreset", &SelectedPreset, CameraPresetLabels, IM_ARRAYSIZE(CameraPresetLabels)))
		{
			PendingCameraPresetViewIndex = ViewIndex;
			PendingCameraPreset = static_cast<EMultipleViewportsCameraPreset>(SelectedPreset);
		}

		if (ViewportAdapter)
		{
			FEngineShowFlags Flags = ViewportAdapter->GetViewShowFlags(ViewIndex);
			ImGui::SetNextItemWidth(PlaceToolbarItem(ComboWidth("Show Flags")));

			if (ImGui::BeginCombo("##ViewShowFlags", "Show Flags"))
			{
				if (ShowFlagUI::DrawFlags(Flags, EShowFlagScope::View, EShowFlagGroup::Viewport))
				{
					ViewportAdapter->SetViewShowFlags(ViewIndex, Flags);
				}
				ImGui::EndCombo();
			}

			// 뷰모드. 고른 값은 렌더 직전에 Renderer로 넘어가 UberLit 변형을 고른다.
			const EViewMode CurrentViewMode = ViewportAdapter->GetViewMode(ViewIndex);

			// 가장 긴 뷰모드 이름 기준으로 Combo 폭을 잡는다.
			const char* LongestViewModeName = GetViewModeName(CurrentViewMode);
			for (int32 ModeIndex = 0; ModeIndex < static_cast<int32>(EViewMode::Count); ++ModeIndex)
			{
				const char* Name = GetViewModeName(static_cast<EViewMode>(ModeIndex));
				if (ImGui::CalcTextSize(Name).x > ImGui::CalcTextSize(LongestViewModeName).x)
					LongestViewModeName = Name;
			}

			ImGui::SetNextItemWidth(PlaceToolbarItem(ComboWidth(LongestViewModeName)));
			if (ImGui::BeginCombo("##ViewMode", GetViewModeName(CurrentViewMode)))
			{
				for (int32 ModeIndex = 0; ModeIndex < static_cast<int32>(EViewMode::Count); ++ModeIndex)
				{
					const EViewMode Candidate = static_cast<EViewMode>(ModeIndex);
					const bool bSelected = Candidate == CurrentViewMode;
					if (ImGui::Selectable(GetViewModeName(Candidate), bSelected))
						ViewportAdapter->SetViewMode(ViewIndex, Candidate);
					if (bSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}

		// Single / Quad
		if (CurrentLayoutMode == ELayoutMode::QuadSplit)
		{
			PlaceToolbarItem(SmallButtonWidth("Single"));
			if (ImGui::SmallButton("Single"))
			{
				RequestedLayoutMode = ELayoutMode::Single;
				RequestedSingleViewIndex = ViewIndex;
				bHasLayoutRequest = true;
			}
		}
		else if (ViewIndex == CurrentSingleViewIndex)
		{
			PlaceToolbarItem(SmallButtonWidth("Quad"));
			if (ImGui::SmallButton("Quad"))
			{
				RequestedLayoutMode = ELayoutMode::QuadSplit;
				RequestedSingleViewIndex = ViewIndex;
				bHasLayoutRequest = true;
			}
		}

		// Reset Stats
		if (ViewportAdapter && ViewIndex == ViewportAdapter->GetEditorViewIndex() &&
			FStatOverlay::IsAnyEnabled() &&
			(FStatOverlay::IsEnabled(EStatFlags::Profile) ||
				FStatRegistry::Find(EditorStats::STAT_PickingTime)))
		{
			PlaceToolbarItem(SmallButtonWidth("Reset Stats"));
			if (ImGui::SmallButton("Reset Stats"))
				FStatRegistry::Reset();
			StatResetButtonMin = ImGui::GetItemRectMin();
			StatResetButtonMax = ImGui::GetItemRectMax();
			bStatResetButtonVisible = true;
		}

		// 줄바꿈으로 늘어난 툴바 높이만큼 통계 오버레이를 아래로 민다.
		ToolbarExtraHeight[ViewIndex] = Cursor.y - Top;

		ImGui::PopID();
	}

	// 마지막으로 선택된 뷰포트만 오버레이
	for (int32 ViewIndex = 0; ViewIndex < 4; ++ViewIndex)
	{
		if (!ViewportAdapter || ViewIndex != ViewportAdapter->GetEditorViewIndex())
			continue;
		if (!Slots[ViewIndex].bActive)
			continue;
		DrawStatOverlay(DrawList, {
			ContentOrigin.x + Slots[ViewIndex].Rect.X,
			ContentOrigin.y + Slots[ViewIndex].Rect.Y + ToolbarExtraHeight[ViewIndex]});
	}

	ImGui::End();
	ImGui::PopStyleVar();
}

// 항목별 줄을 모아 한 번에 배경과 텍스트를 그린다.
void FViewportsPanel::DrawStatOverlay(ImDrawList* DrawList, const ImVec2& ViewMin) const
{
	if (!DrawList || !FStatOverlay::IsAnyEnabled())
		return;

	constexpr float BytesPerMegabyte = 1024.0f * 1024.0f;

	// 제목은 UE처럼 노란색, 값은 흰색으로 구분한다.
	struct FStatLine
	{
		FString Text;
		ImU32 Color;
	};
	TArray<FStatLine> Lines;

	if (FStatOverlay::IsEnabled(EStatFlags::FPS))
	{
		Lines.Add({"FPS", TitleColor});
		Lines.Add({std::format("  {:.1f} fps", FStatOverlay::GetFPS()), ValueColor});
		Lines.Add({std::format("  {:.2f} ms", FStatOverlay::GetFrameTimeMs()), ValueColor});
	}

	if (FStatOverlay::IsEnabled(EStatFlags::Memory))
	{
		Lines.Add({"Memory", TitleColor});
		Lines.Add({std::format("  Object  {:.2f} MB ({} allocs)",
			static_cast<double>(FStatOverlay::GetObjectAllocationBytes()) / BytesPerMegabyte,
			FStatOverlay::GetObjectAllocationCount()), ValueColor});
		Lines.Add({std::format("  Process {:.2f} MB",
			static_cast<double>(FStatOverlay::GetProcessWorkingSetBytes()) / BytesPerMegabyte), ValueColor});
	}

	// 현재 프레임 전체 World의 그림자 통계를 기존 줄 목록에 추가한다.
	if (FStatOverlay::IsEnabled(EStatFlags::Shadow))
	{
		Lines.Add({ "Shadow / Total", TitleColor });
		// 전체 CPU 시간에는 그룹별 작업 외의 Atlas 준비와 정보 업로드 비용도 포함된다.
		Lines.Add({ std::format("  CPU Setup: {:.3f} ms  Renderer: {:.3f} ms",
			FStatRegistry::GetCurrentFrameMs(ShadowStats::STAT_SetupCPU),
			FStatRegistry::GetCurrentFrameMs(ShadowStats::STAT_RenderCPU)), ValueColor });
		Lines.Add({ std::format("  Prepared lights D/P/S: {}/{}/{}  Bound SRVs: {}",
			FStatRegistry::GetCounter(ShadowStats::STAT_DirectionalLights),
			FStatRegistry::GetCounter(ShadowStats::STAT_PointLights),
			FStatRegistry::GetCounter(ShadowStats::STAT_SpotLights),
			FStatRegistry::GetCounter(ShadowStats::STAT_BoundTextures)), ValueColor });
		Lines.Add({ std::format("  Local lights outside views: {}",
			FStatRegistry::GetCounter(ShadowStats::STAT_LocalLightsOutsideViews)), ValueColor });
		Lines.Add({ std::format("  Local lights without receivers: {}",
			FStatRegistry::GetCounter(ShadowStats::STAT_LocalLightsWithoutReceivers)), ValueColor });
		Lines.Add({ std::format("  Empty caster depth skips: {}",
			FStatRegistry::GetCounter(ShadowStats::STAT_EmptyCasterDepthSkips)), ValueColor });
		Lines.Add({ std::format("  Small caster submission skips: {}",
			FStatRegistry::GetCounter(ShadowStats::STAT_SmallCasterSkips)), ValueColor });
		Lines.Add({ std::format("  Caster sphere skips: {}",
			FStatRegistry::GetCounter(ShadowStats::STAT_CasterSphereSkips)), ValueColor });
		Lines.Add({ std::format("  Receiver projection skips: {}",
			FStatRegistry::GetCounter(ShadowStats::STAT_ReceiverProjectionSkips)), ValueColor });
		Lines.Add({ std::format("  Maps: {}  Draws: {}  Updates: {}",
			FStatRegistry::GetCounter(ShadowStats::STAT_Maps),
			FStatRegistry::GetCounter(ShadowStats::STAT_DrawCalls),
			FStatRegistry::GetCounter(ShadowStats::STAT_MapUpdates)), ValueColor });
		// CSM OFF와 직교 View의 단일 맵도 실제 Directional 투영에 포함한다.
		Lines.Add({ std::format("  Directional views: {}  Projections: {}",
			FStatRegistry::GetCounter(ShadowStats::STAT_DirectionalViews),
			FStatRegistry::GetCounter(ShadowStats::STAT_DirectionalProjections)), ValueColor });
		
		// Directional은 View별로 구분하고, Point와 Spot은 World 공유 그룹으로 표시한다.
		for (uint32 Group = 0; Group < ShadowStats::GroupCount; ++Group)
		{
			if (Group == ShadowStats::Local)
				Lines.Add({ "Local / World shared", TitleColor });
			else
			{
				const TStatId ViewStat = Group == ShadowStats::Single
					? ShadowStats::STAT_SingleViews : ShadowStats::STAT_CSMViews;
				Lines.Add({ std::format("{} / Views: {}", ShadowStats::GroupNames[Group],
					FStatRegistry::GetCounter(ViewStat)), TitleColor });
			}

			Lines.Add({ std::format("  CPU Setup: {:.3f} ms  Depth submit: {:.3f} ms",
				FStatRegistry::GetCurrentFrameMs(ShadowStats::SetupCPUStats[Group]),
				FStatRegistry::GetCurrentFrameMs(ShadowStats::DepthCPUStats[Group])), ValueColor });
			// 활성 타일 내용량은 아래의 전체 Atlas 페이지 할당량과 별개다.
			const double TileMiB = static_cast<double>(
				FStatRegistry::GetMemory(ShadowStats::TileMemoryStats[Group])) / BytesPerMegabyte;
			Lines.Add({ std::format("  Maps: {}  Draws: {}  Updates: {}  Tiles: {:.2f} MiB",
				FStatRegistry::GetCounter(ShadowStats::MapStats[Group]),
				FStatRegistry::GetCounter(ShadowStats::DrawStats[Group]),
				FStatRegistry::GetCounter(ShadowStats::UpdateStats[Group]),
				TileMiB), ValueColor });
		}

		Lines.Add({ "Shadow / Cache", TitleColor });
		Lines.Add({ std::format("  r.shadow.CacheWholeSceneShadows: {}  Generation: {}",
			FShadowSettings::IsWholeSceneCacheEnabled() ? "ON" : "OFF",
			FShadowSettings::GetCacheGeneration()), ValueColor });

		const uint64 CacheHits = FStatRegistry::GetCounter(ShadowStats::STAT_CacheHits);
		const uint64 MapUpdates = FStatRegistry::GetCounter(ShadowStats::STAT_MapUpdates);
		const uint64 CacheTotal = CacheHits + MapUpdates;
		const double HitRate = CacheTotal > 0 ? 100.0 * static_cast<double>(CacheHits) / static_cast<double>(CacheTotal) : 0.0;
		Lines.Add({ std::format("  Cached: {}  Updated: {}  Hit rate: {:.1f}%",
			CacheHits, MapUpdates, HitRate), ValueColor });
		// Point는 라이트 수가 아니라 캐시를 재사용한 면 수로 집계한다.
		Lines.Add({ std::format("  Cached D/P/S: {} / {} / {}",
			FStatRegistry::GetCounter(ShadowStats::STAT_CacheHitsDirectional),
			FStatRegistry::GetCounter(ShadowStats::STAT_CacheHitsPoint),
			FStatRegistry::GetCounter(ShadowStats::STAT_CacheHitsSpot)), ValueColor });
		Lines.Add({ std::format("  Miss Off / New / Gen / VP / Caster: {} / {} / {} / {} / {}",
			FStatRegistry::GetCounter(ShadowStats::STAT_CacheMissDisabled),
			FStatRegistry::GetCounter(ShadowStats::STAT_CacheMissNewTile),
			FStatRegistry::GetCounter(ShadowStats::STAT_CacheMissGeneration),
			FStatRegistry::GetCounter(ShadowStats::STAT_CacheMissViewProjection),
			FStatRegistry::GetCounter(ShadowStats::STAT_CacheMissCasters)), ValueColor });
		// 실제 확보한 페이지 용량 대비 POT 예약 블록의 점유율을 표시한다.
		const uint64 AtlasDepthBytes = FStatRegistry::GetMemory(ShadowStats::STAT_DepthMemory);
		const uint64 ReservedBytes = FStatRegistry::GetMemory(ShadowStats::STAT_AtlasReservedMemory);
		const double Occupancy = AtlasDepthBytes > 0
			? 100.0 * static_cast<double>(ReservedBytes) / static_cast<double>(AtlasDepthBytes)
			: 0.0;
		Lines.Add({ std::format("  Tile clears: {}  Atlas pages: {}  Allocations: {}",
			FStatRegistry::GetCounter(ShadowStats::STAT_TileClears),
			FStatRegistry::GetCounter(ShadowStats::STAT_AtlasPages),
			FStatRegistry::GetCounter(ShadowStats::STAT_AtlasAllocations)), ValueColor });
		Lines.Add({ std::format("  Reserved: {:.2f} MiB  Block occupancy: {:.1f}%",
			static_cast<double>(ReservedBytes) / BytesPerMegabyte, Occupancy), ValueColor });

		const uint64 DepthBytes = FStatRegistry::GetMemory(ShadowStats::STAT_DepthMemory);
		const uint64 ParameterBytes = FStatRegistry::GetMemory(ShadowStats::STAT_ParameterMemory);
		// 빈 타일과 재사용 대기 페이지를 포함해 실제 확보한 자원 크기를 표시한다.
		Lines.Add({ "Shadow / Allocated resources", TitleColor });
		Lines.Add({ std::format("  Depth: {:.2f} MiB  Info: {} B  Total: {:.2f} MiB",
			static_cast<double>(DepthBytes) / BytesPerMegabyte, ParameterBytes,
			static_cast<double>(DepthBytes + ParameterBytes) / BytesPerMegabyte), ValueColor });
	}

	struct FProfileStatLine
	{
		FString Text;
		ImU32 Color;
		FString Values[3];
	};
	TArray<FProfileStatLine> ProfileLines;
	if (FStatOverlay::IsEnabled(EStatFlags::Profile))
	{
		ProfileLines.Add({"CPU Profile (ms)", TitleColor, {"Last", "Avg", "Max"}});

		for (const auto& [Name, Data] : FStatRegistry::GetAll())
		{
			if (TStatId{ Name } == EditorStats::STAT_PickingTime)
				continue;

			FProfileStatLine Line{"  " + FString(Name), ValueColor};
			Line.Values[0] = std::format("{:.2f}", Data.GetLastMs());
			Line.Values[1] = std::format("{:.2f}", Data.GetRecentAverageMs());
			Line.Values[2] = std::format("{:.2f}", Data.GetMaxMs());
			ProfileLines.Add(Line);
		}
	}

	if (const FCycleStatData* PickingData = FStatRegistry::Find(EditorStats::STAT_PickingTime))
	{
		ProfileLines.Add({ "Picking", TitleColor });
		FProfileStatLine Line{"  Time (ms)", ValueColor};
		Line.Values[0] = std::format("Last: {:.2f}", PickingData->GetLastMs());
		Line.Values[1] = std::format("Attempts: {}", PickingData->CallCount);
		Line.Values[2] = std::format("Acc.: {:.2f}", PickingData->GetTotalMs());
		ProfileLines.Add(Line);
	}

	if (Lines.Num() == 0 && ProfileLines.Num() == 0)
		return;

	// 제어 위젯 한 줄 아래에서 시작해 Combo와 겹치지 않게 한다.
	const float LineHeight = ImGui::GetTextLineHeight();
	const ImVec2 Origin{
		ViewMin.x + StatOverlayMargin,
		ViewMin.y + StatOverlayMargin + ImGui::GetFrameHeight() + StatOverlayMargin};

	// 최대 너비 계산
	float MaxWidth = 0.0f;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
		MaxWidth = std::max(MaxWidth, ImGui::CalcTextSize(Lines[Index].Text.c_str()).x);

	const float MinValueWidth = ImGui::CalcTextSize("00000.00").x;
	float NameWidth = 0.0f;
	float ValueWidths[3] = { MinValueWidth, MinValueWidth, MinValueWidth };
	bool bHasColumns = false;

	for (const FProfileStatLine& Line : ProfileLines)
	{
		const float TextWidth = ImGui::CalcTextSize(Line.Text.c_str()).x;
		bHasColumns = true;
		NameWidth = std::max(NameWidth, TextWidth);
		for (int32 Column = 0; Column < 3; ++Column)
			ValueWidths[Column] = std::max(MinValueWidth, ImGui::CalcTextSize(Line.Values[Column].c_str()).x);
	}

	float ColumnRight[3]{};
	if (bHasColumns)
	{
		float Width = NameWidth;
		for (int32 Column = 0; Column < 3; ++Column)
		{
			Width += ImGui::GetFontSize() + ValueWidths[Column];
			ColumnRight[Column] = Origin.x + Width;
		}
		MaxWidth = std::max(MaxWidth, Width);
	}

	const ImVec2 BackgroundMin{Origin.x - StatOverlayPadding, Origin.y - StatOverlayPadding};
	const ImVec2 BackgroundMax{
		Origin.x + MaxWidth + StatOverlayPadding,
		Origin.y + LineHeight * static_cast<float>(Lines.Num() + ProfileLines.Num()) + StatOverlayPadding};
	DrawList->AddRectFilled(BackgroundMin, BackgroundMax, StatOverlayBackgroundColor, 4.0f);

	// Lines 그리기
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		DrawList->AddText(
			{ Origin.x, Origin.y + LineHeight * static_cast<float>(Index) },
			Lines[Index].Color,
			Lines[Index].Text.c_str());
	}

	// ProfileLines 그리기
	for (int32 Index = 0; Index < ProfileLines.Num(); ++Index)
	{
		const FProfileStatLine& Line = ProfileLines[Index];
		const float Y = Origin.y + LineHeight * static_cast<float>(Lines.Num() + Index);
		DrawList->AddText({ Origin.x, Y }, Line.Color, Line.Text.c_str());

		for (int32 Column = 0; Column < 3; ++Column)
		{
			const FString& Text = Line.Values[Column];
			const float TextWidth = ImGui::CalcTextSize(Text.c_str()).x;
			DrawList->AddText({ ColumnRight[Column] - TextWidth, Y }, Line.Color, Text.c_str());
		}
	}
}

// View 크기에 맞춰 Color·Depth Texture와 렌더 정보를 재생성한다.
void FViewportsPanel::ResizeSlot(FViewSlot& Slot, const uint32 Width, const uint32 Height)
{
	D3D11_TEXTURE2D_DESC Desc{};
	Desc.Width = Width;
	Desc.Height = Height;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_DEFAULT;
	Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	Slot.ColorTarget = RenderCommand::CreateTexture2D(Desc);
	// CopyResource로 되돌려 쓰므로 ColorTarget과 같은 Desc로 만든다
	Slot.FxaaTarget = RenderCommand::CreateTexture2D(Desc);

	// Fog 패스가 깊이를 셰이더에서 읽으므로 DSV와 SRV를 둘 다 만들 수 있는 TYPELESS로 만든다
	Desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	Slot.DepthTarget = RenderCommand::CreateTexture2D(Desc);

	// HDR Target
	Desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;	
	Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	Slot.HDRTarget = RenderCommand::CreateTexture2D(Desc);

	Slot.Width = Width;
	Slot.Height = Height;
	Slot.RenderingInfo.ColorRenderTargets.Reset();
	Slot.RenderingInfo.ViewportSetting.Width = Width;
	Slot.RenderingInfo.ViewportSetting.Height = Height;
	FRenderingDesc ColorDesc{};
	ColorDesc.Texture = Slot.ColorTarget.get();
	Slot.RenderingInfo.ColorRenderTargets.Add(ColorDesc);
	Slot.RenderingInfo.DepthStencil.Texture = Slot.DepthTarget.get();
}
