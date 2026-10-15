#pragma once

#include "Core/Types.h"

// 뷰포트 조명 표시 모드. UberLit.hlsl을 어떤 매크로로 컴파일한 변형을 쓸지 고른다.
enum class EViewMode : uint8
{
	Lit_Gouraud,
	Lit_Lambert,
	Lit_BlinnPhong,
	Unlit,
	WorldNormal,
	Count
};

inline const char* GetViewModeName(EViewMode Mode)
{
	switch (Mode)
	{
	case EViewMode::Lit_Gouraud: return "Lit (Gouraud)";
	case EViewMode::Lit_Lambert: return "Lit (Lambert)";
	case EViewMode::Lit_BlinnPhong: return "Lit (Blinn-Phong)";
	case EViewMode::Unlit:       return "Unlit";
	case EViewMode::WorldNormal: return "World Normal";
	default:                     return "Unknown";
	}
}
