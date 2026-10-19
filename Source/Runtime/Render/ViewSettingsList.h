#pragma once

// ViewMode의 enum과 UI 이름을 생성한다. 기존 enum 순서를 유지한다.
#define VIEW_MODE_LIST(X)                   \
    X(Lit_Gouraud,    "Lit (Gouraud)")      \
    X(Lit_Lambert,    "Lit (Lambert)")      \
    X(Lit_BlinnPhong, "Lit (Blinn-Phong)")  \
    X(Unlit,          "Unlit")              \
    X(WorldNormal,    "World Normal")       


// Symbol, 비트, UI 이름, 기본값, 적용 범위, UI 그룹.
// 표시 항목을 추가할 때 수정하는 공통 목록이다.
#define SHOW_FLAG_LIST(X)                                                          \
    X(Primitives,     1u << 0, "Draw Primitives",        true,  Global, Rendering) \
    X(Bounds,         1u << 1, "Draw Bounding Box",      false, Global, Rendering) \
    X(UUID,           1u << 2, "Show Object UUID",       false, Global, Rendering) \
    X(FXAA,           1u << 3, "FXAA",                   true,  Global, Rendering) \
    X(DepthView,      1u << 4, "Depth View",             false, Global, Rendering) \
    X(Fog,            1u << 5, "Exponential Height Fog", true,  Global, Rendering) \
    X(Grid,           1u << 6, "Draw Batch Line / Grid", true,  Global, Grid)      \
    X(PSGrid,         1u << 7, "Draw PS Grid",           false, Global, Grid)      \
    X(Wireframe,      1u << 8, "Wireframe",              false, View,   Viewport)  \
    X(DynamicShadows, 1u << 9, "Dynamic Shadows",        true,  Global, Rendering) \
    X(ShadowCascades, 1u << 10, "Shadow Cascades", false, View,   Viewport)
