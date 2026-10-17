#pragma once

#include "LightweightStats.h"

namespace ShadowStats
{
    // 타깃 준비·Depth 명령 제출·정보 업로드에 걸린 CPU 시간이다.
    DECLARE_CYCLE_STAT("Shadow Renderer CPU", STAT_RenderCPU);

    // 현재 프레임의 유효 그림자 작업을 기준으로 집계한다.
    DECLARE_COUNTER_STAT("Shadow Directional Lights", STAT_DirectionalLights);
    DECLARE_COUNTER_STAT("Shadow Point Lights", STAT_PointLights);
    DECLARE_COUNTER_STAT("Shadow Spot Lights", STAT_SpotLights);
    DECLARE_COUNTER_STAT("Shadow Maps", STAT_Maps);
    DECLARE_COUNTER_STAT("Shadow Bound Textures", STAT_BoundTextures);
    DECLARE_COUNTER_STAT("Shadow Draw Calls", STAT_DrawCalls);
    DECLARE_COUNTER_STAT("Shadow Map Updates", STAT_MapUpdates);

    // 실제 확보한 GPU 자원의 내용 크기를 바이트로 기록한다.
    DECLARE_MEMORY_STAT("Shadow Depth Memory", STAT_DepthMemory);
    DECLARE_MEMORY_STAT("Shadow Parameter Memory", STAT_ParameterMemory);
}