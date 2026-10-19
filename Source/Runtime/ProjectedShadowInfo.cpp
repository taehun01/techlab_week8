#include "EnginePCH.h"
#include "ProjectedShadowInfo.h"


// 새 투영을 설정하면 이전 Caster와 타깃 연결은 더 이상 사용하지 않는다.
void FProjectedShadowInfo::SetupProjection(const FLightSceneProxy& InLightProxy, const FVector& InViewOrigin,
    const FMatrix& InLightViewProjection, uint32 InProjectionIndex, uint32 InViewId)
{
    // 기존 Spot은 Depth VP에서 추출한 6평면을 그대로 사용한다.
    SetupProjection(InLightProxy, InViewOrigin, InLightViewProjection,
        ExtractFrustumPlanes(InLightViewProjection), FrustumAllPlanesMask, InProjectionIndex, InViewId);
}

// 명시한 Caster 범위와 투영 식별 정보를 같은 작업에 설정한다.
void FProjectedShadowInfo::SetupProjection(const FLightSceneProxy& InLightProxy,
    const FVector& InViewOrigin, const FMatrix& InLightViewProjection,
    const FFrustumPlanes& InCasterFrustum, uint32 InCasterPlaneMask,
    uint32 InProjectionIndex, uint32 InViewId)
{
    // 이전 Caster 큐와 타깃 참조의 초기화는 Info가 담당한다.
    Reset();

    LightProxy = &InLightProxy;
    ProjectionIndex = InProjectionIndex;
    ViewId = InViewId;
    ShadowViewOrigin = InViewOrigin;
    LightViewProjection = InLightViewProjection;
    CasterFrustum = InCasterFrustum;
    CasterPlaneMask = InCasterPlaneMask;
}

// CastShadow·Frustum·메시 적합성 판정은 World가 수행한 상태로 전달한다.
void FProjectedShadowInfo::AddCasterPacket(const FRenderPacket& Packet)
{
    CasterQueue.Add(Packet);
}

// GPU 자원은 유지하고 이번 작업의 CPU 상태와 비소유 참조만 비운다.
void FProjectedShadowInfo::Reset()
{
    LightProxy = nullptr;
    ProjectionIndex = 0;
    LogicalCascadeCount = 1;
    ViewId = 0;
    ShadowViewOrigin = FVector::ZeroVector;
    LightViewProjection = FMatrix::Identity;
    CasterFrustum = {};
    CasterPlaneMask = FrustumAllPlanesMask;
    RenderingInfo = FRenderingInfo{};
    CasterQueue.Reset();
}
