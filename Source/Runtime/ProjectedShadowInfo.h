#pragma once

#include "Render/RenderPacket.h"
#include "Render/RenderingInfo.h"
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

    // World가 라이트 기준으로 Caster를 수집할 때 사용한다
    FFrustumPlanes CasterFrustum{};

    // Spot은 6평면, Directional은 Near를 제외한 5평면을 사용한다.
    uint32 CasterPlaneMask = FrustumAllPlanesMask;

    // GPU 타깃을 소유하지 않고 기존 패스 설정으로 참조한다.
    FRenderingInfo RenderingInfo{};

    // Caster 패킷과 큐 내부에서 사용하는 임시 행렬 저장소를 소유한다.
    FRenderQueue CasterQueue;
};
