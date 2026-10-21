#include "EnginePCH.h"
#include "ProjectedShadowInfo.h"

namespace ShadowCasterHashPrivate
{
    // Caster 식별자와 변경 버전을 순서에 무관하게 누적할 수 있도록 혼합한다.
    uint64 MixHash64(uint64 X)
    {
        X += 0x9E3779B97F4A7C15ull;
        X = (X ^ (X >> 30)) * 0xBF58476D1CE4E5B9ull;
        X = (X ^ (X >> 27)) * 0x94D049BB133111EBull;
        return X ^ (X >> 31);
    }
}


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
    // World가 선별한 패킷을 보관하고 수집 결과를 기록한다.
    CasterQueue.Add(Packet);
    bHasCasters = true;
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
    ShadowViewSize = FVector::ZeroVector;
    CasterFrustum = {};
    CasterPlaneMask = FrustumAllPlanesMask;
    RenderingInfo = FRenderingInfo{};
    CasterQueue.Reset();
    bHasCasters = false;
    // 새 투영에서 이전 Receiver 생략 결과를 재사용하지 않는다.
    bHasReceivers = true;
    CasterHash = 0u;
    CasterCount = 0u;
    bShouldRenderShadow = true;
    CacheKey = FShadowCacheKey{};
    CacheResult = EShadowCacheResult::NewTile;
    LastUpdatedFrame = 0;
}

// Shadow Caching 할 때 필요한 헤시값을 저장한다.
void FProjectedShadowInfo::AddCasterKey(uint32 UUID, uint64 Version)
{
    uint64 HashKey = (Version << 32) + static_cast<uint64>(UUID);
    CasterHash += ShadowCasterHashPrivate::MixHash64(HashKey);
    CasterCount++;
}
