#pragma once

#include "Core/Types.h"
#include "Render/ViewSettingsList.h"

// 기존 엔진의 전역·View별 적용 범위를 유지한다.
enum class EShowFlagScope : uint8
{
    Global,
    View
};

// 기존 Settings 섹션과 View 툴바의 표시 위치를 구분한다.
enum class EShowFlagGroup : uint8
{
    Rendering,
    Grid,
    Viewport
};

// 공통 목록에서 실제 비트 값을 생성한다.
enum class EEngineShowFlag : uint32
{
#define MAKE_SHOW_FLAG_ENUM(Symbol, Bit, Label, Default, Scope, Group) Symbol = Bit,
    SHOW_FLAG_LIST(MAKE_SHOW_FLAG_ENUM)
#undef MAKE_SHOW_FLAG_ENUM
};

// 기본값 설정과 UI 생성에서 함께 사용하는 정적 메타데이터다.
struct FShowFlagEntry
{
    EEngineShowFlag Flag;
    const char* Label;
    bool bDefaultEnabled;
    EShowFlagScope Scope;
    EShowFlagGroup Group;
};

inline constexpr FShowFlagEntry ShowFlagEntries[] =
{
#define MAKE_SHOW_FLAG_ENTRY(Symbol, Bit, Label, Default, Scope, Group) \
    { EEngineShowFlag::Symbol, Label, Default, EShowFlagScope::Scope, EShowFlagGroup::Group },
    SHOW_FLAG_LIST(MAKE_SHOW_FLAG_ENTRY)
#undef MAKE_SHOW_FLAG_ENTRY
};

// 실제 표시 상태는 Flags 하나에만 저장한다.
struct FEngineShowFlags
{
    // 소유자의 적용 범위에 해당하는 기본값만 활성화한다.
    explicit constexpr FEngineShowFlags(EShowFlagScope Scope)
    {
        for (const FShowFlagEntry& Entry : ShowFlagEntries)
        {
            if (Entry.Scope == Scope && Entry.bDefaultEnabled)
                Flags |= static_cast<uint32>(Entry.Flag);
        }
    }

    constexpr bool IsEnabled(EEngineShowFlag Flag) const
    {
        return (Flags & static_cast<uint32>(Flag)) != 0;
    }

    constexpr void SetEnabled(EEngineShowFlag Flag, bool bEnabled)
    {
        const uint32 Mask = static_cast<uint32>(Flag);
        if (bEnabled)
            Flags |= Mask;
        else
            Flags &= ~Mask;
    }

private:
    uint32 Flags = 0;
};