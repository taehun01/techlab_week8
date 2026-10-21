#pragma once

#include "Render/RenderPacket.h"
#include "Render/RenderingInfo.h"
#include "Render/FShadowViewRenderer.h"
#include "Math/Frustum.h"

class FLightSceneProxy;

class FProjectedShadowInfo
{
public:
    // 이전 작업을 초기화하고 투영 결과와 Caster Frustum을 함께 설정한다.
    // 입력 VP의 유효성은 라이트별 투영 계산을 수행한 호출자가 보장한다.
    void SetupProjection(const FLightSceneProxy& InLightProxy, const FVector& InViewOrigin,
        const FMatrix& InLightViewProjection, uint32 InProjectionIndex = 0, uint32 InViewId = 0);

    // Directional은 Depth 투영과 독립적인 Caster 범위·평면 마스크를 전달한다.
    void SetupProjection(const FLightSceneProxy& InLightProxy, const FVector& InViewOrigin, const FMatrix& InLightViewProjection,
        const FFrustumPlanes& InCasterFrustum, uint32 InCasterPlaneMask,
        uint32 InProjectionIndex = 0, uint32 InViewId = 0);

    // Renderer가 준비한 타깃·Viewport·Clear/Load 설정을 연결한다.
    void SetRenderingInfo(const FRenderingInfo& InRenderingInfo)
    {
        RenderingInfo = InRenderingInfo;
    }

    // 타깃이 있는지 체크
    bool HasDepthTarget() const { return RenderingInfo.DepthStencil.Texture != nullptr; }
    // Caster가 있는지 체크
    bool HasCasters() const { return bHasCasters; }

    // Receiver 검사로 생략한 작업과 Caster가 없다고 확인한 작업을 구분한다.
    bool HasReceivers() const { return bHasReceivers; }
    // 투영별 검사를 수행한 World가 결과를 기록한다. 미검사 작업은 유지한다.
    void SetHasReceivers(bool bInHasReceivers) { bHasReceivers = bInHasReceivers; }

    // World는 범위와 평면 마스크를 함께 사용해 Caster를 탐색한다.
    const FFrustumPlanes& GetCasterFrustum() const { return CasterFrustum; }
    uint32 GetCasterPlaneMask() const { return CasterPlaneMask; }

    // World가 선별한 정적 메시 패킷을 등록한다.
    // 패킷이 참조하는 Proxy·Mesh·Material은 Depth 렌더 종료까지 유효해야 한다.
    void AddCasterPacket(const FRenderPacket& Packet);

    // 일부 작업이 생략되어도 원래 View의 논리 분할 개수를 유지한다.
    uint32 GetLogicalCascadeCount() const { return LogicalCascadeCount; }
    // 유효한 분할 개수는 투영 구간을 계산한 호출자가 보장한다.
    void SetLogicalCascadeCount(uint32 InCount) { LogicalCascadeCount = InCount; }

    // CPU 작업과 타깃 참조를 초기화한다. GPU 텍스처는 해제하지 않는다.
    void Reset();

    // Shadow Caching 할 때 필요한 헤시값을 저장한다.
    void AddCasterKey(uint32 UUID, uint64 Version);

    // 직교 깊이 복원과 PCSS 반경 변환에 사용할 광원 View의 실제 크기다.
    void SetShadowViewSize(const FVector& Size) { ShadowViewSize = Size; }

private:
    // Renderer는 준비된 상태를 읽고 Caster 큐를 소비한다.
    friend class FRenderer;

    // 그림자를 생성할 라이트 객체. 라이트의 수명을 소유하지 않는다.
    const FLightSceneProxy* LightProxy = nullptr;

    // CSM의 Cascade 번호다. 단일 투영은 0을 사용한다.
    uint32 ProjectionIndex = 0;

    // 단일 투영과 지역광은 1이며, Point의 면 개수와는 독립적이다.
    uint32 LogicalCascadeCount = 1;

    // 0은 월드 공유 작업이며, View별 작업은 안정적인 비영 ID를 사용한다.
    uint32 ViewId = 0;

    // 그림자 View의 원점. Spot에서는 광원 위치를 사용한다.
    FVector ShadowViewOrigin = FVector::ZeroVector;

    // Caster 렌더링과 Receiver의 깊이 비교에 동일한 VP를 사용한다.
    FMatrix LightViewProjection = FMatrix::Identity;

    FVector ShadowViewSize = FVector::ZeroVector;

    // World가 라이트 기준으로 Caster를 수집할 때 사용한다
    FFrustumPlanes CasterFrustum{};

    // Spot은 6평면, Directional은 Near를 제외한 5평면을 사용한다.
    uint32 CasterPlaneMask = FrustumAllPlanesMask;

    // GPU 타깃을 소유하지 않고 기존 패스 설정으로 참조한다.
    FRenderingInfo RenderingInfo{};

    // Caster 패킷과 큐 내부에서 사용하는 임시 행렬 저장소를 소유한다.
    FRenderQueue CasterQueue;

    // 패킷 등록 여부이며, Depth 실행 후 큐가 비어도 변경하지 않는다.
    bool bHasCasters = false;

    // Receiver 검사가 연결되지 않은 기존 투영은 보수적으로 렌더링한다.
    bool bHasReceivers = true;

    // UUID + ShadowVersion으로 만든 헤시값
    uint64 CasterHash = 0u;

    // 이번 투영에 실제로 제출한 Caster 수다.
    uint32 CasterCount = 0u;

    // 캐시 Miss인 작업만 Depth를 다시 생성한다.
    bool bShouldRenderShadow = true;

    // 판정 결과(Stat·Preview용)와 마지막 깊이 갱신 프레임
    EShadowCacheResult CacheResult = EShadowCacheResult::NewTile;
    uint64 LastUpdatedFrame = 0;

    // Shadow Cache Key
    FShadowCacheKey CacheKey{};
};
