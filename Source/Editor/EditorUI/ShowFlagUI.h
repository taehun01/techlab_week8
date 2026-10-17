#pragma once

#include "imgui.h"
#include "Render/EngineShowFlags.h"

// 두 패널에서 같은 목록과 편집 방식을 사용한다.
namespace ShowFlagUI
{
    inline bool DrawFlags(FEngineShowFlags& Flags, EShowFlagScope Scope, EShowFlagGroup Group)
    {
        bool bChanged = false;
        for (const FShowFlagEntry& Entry : ShowFlagEntries)
        {
            if (Entry.Scope != Scope || Entry.Group != Group)
            {
                continue;
            }

            ImGui::PushID(static_cast<int>(Entry.Flag));

            // ImGui 전달용 임시 값이며 실제 상태는 비트마스크에만 저장한다.
            bool bEnabled = Flags.IsEnabled(Entry.Flag);
            if (ImGui::Checkbox(Entry.Label, &bEnabled))
            {
                Flags.SetEnabled(Entry.Flag, bEnabled);
                bChanged = true;
            }

            ImGui::PopID();
        }
        return bChanged;
    }
}