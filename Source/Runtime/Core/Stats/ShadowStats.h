#pragma once

#include "LightweightStats.h"

namespace ShadowStats
{
    // 타깃 준비·Depth 명령 제출·정보 업로드에 걸린 CPU 시간이다.
    DECLARE_CYCLE_STAT("Shadow Renderer CPU", STAT_RenderCPU);
    // 분할·Frustum·투영 계산과 Caster 수집에 걸린 CPU 시간이다.
    DECLARE_CYCLE_STAT("Shadow Setup CPU", STAT_SetupCPU);

    // 현재 프레임의 유효 그림자 작업을 기준으로 집계한다.
    DECLARE_COUNTER_STAT("Shadow Directional Lights", STAT_DirectionalLights);
    DECLARE_COUNTER_STAT("Shadow Point Lights", STAT_PointLights);
    DECLARE_COUNTER_STAT("Shadow Spot Lights", STAT_SpotLights);
    DECLARE_COUNTER_STAT("Shadow Maps", STAT_Maps);
    DECLARE_COUNTER_STAT("Shadow Bound Textures", STAT_BoundTextures);
    DECLARE_COUNTER_STAT("Shadow Draw Calls", STAT_DrawCalls);
    DECLARE_COUNTER_STAT("Shadow Map Updates", STAT_MapUpdates);
    // 단일 맵과 CSM을 함께 비교할 수 있도록 실제 Directional 작업을 집계한다.
    // Directional View·Projection은 타깃을 생략한 빈 논리 작업도 포함한다.
    DECLARE_COUNTER_STAT("Shadow Directional Views", STAT_DirectionalViews);
    DECLARE_COUNTER_STAT("Shadow Directional Projections", STAT_DirectionalProjections);
    // 실제 확보한 GPU 자원의 내용 크기를 바이트로 기록한다.
    DECLARE_MEMORY_STAT("Shadow Depth Memory", STAT_DepthMemory);
    DECLARE_MEMORY_STAT("Shadow Parameter Memory", STAT_ParameterMemory);

    DECLARE_COUNTER_STAT("Shadow Local Lights Outside Views", STAT_LocalLightsOutsideViews);    // 광원이 뷰 바깥에있음
    DECLARE_COUNTER_STAT("Shadow Local Lights Without Receivers", STAT_LocalLightsWithoutReceivers);    // 내부에 가시 Receiver가 없음
    DECLARE_COUNTER_STAT("Shadow Empty Caster Depth Skips", STAT_EmptyCasterDepthSkips);;    // 빈 Caster Queue
    DECLARE_COUNTER_STAT("Shadow Small Caster Skips", STAT_SmallCasterSkips);   // 임계값 미만의 크기의 Caster
    DECLARE_COUNTER_STAT("Shadow Caster Sphere Skips", STAT_CasterSphereSkips); // 지역광 Sphere 검사
    DECLARE_COUNTER_STAT("Shadow Receiver Projection Skips", STAT_ReceiverProjectionSkips); // Receiver 없는 면·Cascade
    
    enum EGroup : uint32
    {
        Single,
        CSM,
        Local,
        GroupCount
    };
    inline constexpr const char* GroupNames[GroupCount] =
    {
        "Directional Single", "Directional CSM", "Local"
    };

    // 작업이 일부 생략되어도 남은 Map 수가 아니라 논리 분할 개수로 구분한다.
    inline EGroup GetGroup(bool bDirectional, uint32 LogicalCascadeCount)
    {
        if (!bDirectional)
            return Local;

        return LogicalCascadeCount > 1 ? CSM : Single;
    }

    // 투영 계산·Receiver 범위 계산·Caster 수집에 사용한 CPU 시간이다.
    DECLARE_CYCLE_STAT("Single Setup CPU", STAT_SingleSetupCPU);
    DECLARE_CYCLE_STAT("CSM Setup CPU", STAT_CSMSetupCPU);
    DECLARE_CYCLE_STAT("Local Setup CPU", STAT_LocalSetupCPU);

    // 큐 준비·GPU 상태 설정·Depth Draw 제출에 사용한 CPU 시간이다.
    DECLARE_CYCLE_STAT("Single Depth Submit CPU", STAT_SingleDepthCPU);
    DECLARE_CYCLE_STAT("CSM Depth Submit CPU", STAT_CSMDepthCPU);
    DECLARE_CYCLE_STAT("Local Depth Submit CPU", STAT_LocalDepthCPU);

    // Directional은 View별 작업이며, Local은 World 공유 작업이다.
    DECLARE_COUNTER_STAT("Single Views", STAT_SingleViews);
    DECLARE_COUNTER_STAT("CSM Views", STAT_CSMViews);

    // 유효한 Depth 타깃을 가진 투영 작업 수다.
    DECLARE_COUNTER_STAT("Single Maps", STAT_SingleMaps);
    DECLARE_COUNTER_STAT("CSM Maps", STAT_CSMMaps);
    DECLARE_COUNTER_STAT("Local Maps", STAT_LocalMaps);

    // 실제 Depth Draw와 갱신 실행 횟수다.
    DECLARE_COUNTER_STAT("Single Draw Calls", STAT_SingleDrawCalls);
    DECLARE_COUNTER_STAT("CSM Draw Calls", STAT_CSMDrawCalls);
    DECLARE_COUNTER_STAT("Local Draw Calls", STAT_LocalDrawCalls);
    DECLARE_COUNTER_STAT("Single Updates", STAT_SingleUpdates);
    DECLARE_COUNTER_STAT("CSM Updates", STAT_CSMUpdates);
    DECLARE_COUNTER_STAT("Local Updates", STAT_LocalUpdates);

    // 활성 타일의 Depth 내용량이며, Atlas 페이지 전체 할당량과 구분한다.
    DECLARE_MEMORY_STAT("Single Active Tile Memory", STAT_SingleTileMemory);
    DECLARE_MEMORY_STAT("CSM Active Tile Memory", STAT_CSMTileMemory);
    DECLARE_MEMORY_STAT("Local Active Tile Memory", STAT_LocalTileMemory);

    // 기존 Stat ID를 배열로 연결해 집계·표시 코드를 공유한다.
    inline constexpr TStatId SetupCPUStats[GroupCount] =
    {
        STAT_SingleSetupCPU, STAT_CSMSetupCPU, STAT_LocalSetupCPU
    };
    inline constexpr TStatId DepthCPUStats[GroupCount] =
    {
        STAT_SingleDepthCPU, STAT_CSMDepthCPU, STAT_LocalDepthCPU
    };
    inline constexpr TStatId MapStats[GroupCount] =
    {
        STAT_SingleMaps, STAT_CSMMaps, STAT_LocalMaps
    };
    inline constexpr TStatId DrawStats[GroupCount] =
    {
        STAT_SingleDrawCalls, STAT_CSMDrawCalls, STAT_LocalDrawCalls
    };
    inline constexpr TStatId UpdateStats[GroupCount] =
    {
        STAT_SingleUpdates, STAT_CSMUpdates, STAT_LocalUpdates
    };
    inline constexpr TStatId TileMemoryStats[GroupCount] =
    {
        STAT_SingleTileMemory, STAT_CSMTileMemory, STAT_LocalTileMemory
    };

    // Shadow Map Caching
    DECLARE_COUNTER_STAT("Shadow Cache Hits", STAT_CacheHits);
    DECLARE_COUNTER_STAT("Shadow Cache Hits Directional", STAT_CacheHitsDirectional);
    DECLARE_COUNTER_STAT("Shadow Cache Hits Point", STAT_CacheHitsPoint);
    DECLARE_COUNTER_STAT("Shadow Cache Hits Spot", STAT_CacheHitsSpot);
    DECLARE_COUNTER_STAT("Shadow Cache Miss Disabled", STAT_CacheMissDisabled);
    DECLARE_COUNTER_STAT("Shadow Cache Miss NewTile", STAT_CacheMissNewTile);
    DECLARE_COUNTER_STAT("Shadow Cache Miss Generation", STAT_CacheMissGeneration);
    DECLARE_COUNTER_STAT("Shadow Cache Miss ViewProjection", STAT_CacheMissViewProjection);
    DECLARE_COUNTER_STAT("Shadow Cache Miss Casters", STAT_CacheMissCasters);
    DECLARE_COUNTER_STAT("Shadow Tile Clears", STAT_TileClears);
    // 가변 크기 배치의 활성 배정 수와 실제 예약 블록 면적을 따로 기록한다.
    DECLARE_COUNTER_STAT("Shadow Atlas Allocations", STAT_AtlasAllocations);
    DECLARE_COUNTER_STAT("Shadow Atlas Pages", STAT_AtlasPages);
    DECLARE_MEMORY_STAT("Shadow Atlas Reserved Memory", STAT_AtlasReservedMemory);
}
