#pragma once

#include "Editor/LevelEditor/MultipleViewports/Core/MultipleViewports.h"

#include <algorithm>
#include <cassert>

// 에디터 네 View의 화면 배치와 좌표 판정을 관리한다.
class FLevelViewportLayout
{
public:
    static constexpr int32 ViewCount = 4;

    // 배치 모드는 다음 Update에서 Rect에 반영한다.
    void SetLayoutMode(ELayoutMode Mode) { LayoutMode = Mode; }
    ELayoutMode GetLayoutMode() const { return LayoutMode; }

    // Single로 확대할 View는 패널의 네 View 중에서 선택한다.
    void SetSingleViewIndex(int32 ViewIndex)
    {
        assert(ViewIndex >= 0 && ViewIndex < ViewCount);
        SingleViewIndex = ViewIndex;
    }
    int32 GetSingleViewIndex() const { return SingleViewIndex; }

    // 외부 설정은 기존 Core의 허용 범위로 제한한다.
    void SetSplitRatio(const FSplitRatio& Ratio) { SplitRatio = ClampSplitRatio(Ratio, MinimumSplitRatio); }
    const FSplitRatio& GetSplitRatio() const { return SplitRatio; }

    // 두 축의 Drag를 모두 적용한 뒤 Update에서 화면 배치를 계산한다.
    void ApplySplitterDrag(EDragAxis Axis, float DeltaPixels, FVector2 WindowSize)
    {
        SplitRatio = ::ApplySplitterDrag(SplitRatio, Axis, DeltaPixels, WindowSize, MinimumSplitRatio);
    }

    // Single은 선택한 View만 확대하고, Quad는 Splitter 여백을 적용한다.
    void Update(FVector2 WindowSize, float GutterWidth)
    {
        ComputeViewRects(SplitRatio, WindowSize, ViewRects);
        if (LayoutMode == ELayoutMode::Single)
        {
            for (int32 ViewIndex = 0; ViewIndex < ViewCount; ++ViewIndex)
            {
                ViewRects[ViewIndex] = ViewIndex == SingleViewIndex
                    ? FRect{ 0.0f, 0.0f, WindowSize.X, WindowSize.Y }
                    : FRect{};
            }
        }
        else
        {
            ApplySplitterGutter(WindowSize, GutterWidth);
        }
    }

    // Single에서는 선택된 View만, Quad에서는 유효한 Rect를 가진 View를 활성화한다.
    bool IsViewActive(int32 ViewIndex) const
    {
        return ViewIndex >= 0 && ViewIndex < ViewCount && IsViewRectValid(ViewRects[ViewIndex])
            && (LayoutMode == ELayoutMode::QuadSplit || ViewIndex == SingleViewIndex);
    }

    // 패널 Rect만 판정하므로 Splitter와 외부 PIE 창은 입력 대상에 포함하지 않는다.
    int32 FindViewAt(FVector2 Position) const { return DetermineHoveredView(Position, ViewRects); }

    // 내부 호출자는 유효한 패널 View 인덱스를 전달한다.
    const FRect& GetViewRect(int32 ViewIndex) const
    {
        assert(ViewIndex >= 0 && ViewIndex < ViewCount);
        return ViewRects[ViewIndex];
    }

private:
    // 기존 중앙 경계에서 양쪽 Rect를 절반씩 줄여 실제 Splitter 여백을 만든다.
    void ApplySplitterGutter(FVector2 WindowSize, float GutterWidth)
    {
        const float SplitX = ViewRects[0].Width;
        const float SplitY = ViewRects[0].Height;
        const float HalfThickness = GutterWidth * 0.5f;
        const float HalfGapX = std::min({ HalfThickness, SplitX, WindowSize.X - SplitX });
        const float HalfGapY = std::min({ HalfThickness, SplitY, WindowSize.Y - SplitY });
        const float RightX = SplitX + HalfGapX;
        const float BottomY = SplitY + HalfGapY;
        const float LeftWidth = SplitX - HalfGapX;
        const float TopHeight = SplitY - HalfGapY;
        const float RightWidth = WindowSize.X - RightX;
        const float BottomHeight = WindowSize.Y - BottomY;

        ViewRects[0] = { 0.0f, 0.0f, LeftWidth, TopHeight };
        ViewRects[1] = { RightX, 0.0f, RightWidth, TopHeight };
        ViewRects[2] = { 0.0f, BottomY, LeftWidth, BottomHeight };
        ViewRects[3] = { RightX, BottomY, RightWidth, BottomHeight };
    }

    static constexpr float MinimumSplitRatio = 0.1f;
    ELayoutMode LayoutMode = ELayoutMode::Single;
    FSplitRatio SplitRatio{};
    FRect ViewRects[ViewCount]{};
    int32 SingleViewIndex = 0;
};
