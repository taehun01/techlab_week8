#pragma once

#include "Core/Types.h"
#include "Render/ViewSettingsList.h"

// 뷰포트 조명 표시 모드. UberLit.hlsl을 어떤 매크로로 컴파일한 변형을 쓸지 고른다.
enum class EViewMode : uint8
{
#define MAKE_VIEW_MODE_ENUM(Symbol, Label) Symbol,
	VIEW_MODE_LIST(MAKE_VIEW_MODE_ENUM)
#undef MAKE_VIEW_MODE_ENUM
	Count
};

inline constexpr const char* ViewModeNames[] =
{
#define MAKE_VIEW_MODE_NAME(Symbol, Label) Label,
	VIEW_MODE_LIST(MAKE_VIEW_MODE_NAME)
#undef MAKE_VIEW_MODE_NAME
};


inline const char* GetViewModeName(EViewMode Mode)
{
	const uint32 Index = static_cast<uint32>(Mode);
	return Index < static_cast<uint32>(EViewMode::Count)? ViewModeNames[Index] : "Unknown";
}