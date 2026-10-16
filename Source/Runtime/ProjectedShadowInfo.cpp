#include "EnginePCH.h"
#include "ProjectedShadowInfo.h"


// 새 투영을 설정하면 이전 Caster와 타깃 연결은 더 이상 사용하지 않는다.
void FProjectedShadowInfo::SetupProjection(const FLightSceneProxy& InLightProxy, const FVector& InViewOrigin, const FMatrix& InLightViewProjection)
{
    Reset();

    LightProxy = &InLightProxy;
    ShadowViewOrigin = InViewOrigin;
    LightViewProjection = InLightViewProjection;

    // 저장한 VP에서 즉시 추출해 렌더링과 컬링의 투영이 일치하게 한다.
    CasterFrustum = ExtractFrustumPlanes(LightViewProjection);
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
    ShadowViewOrigin = FVector::ZeroVector;
    LightViewProjection = FMatrix::Identity;
    CasterFrustum = {};
    RenderingInfo = FRenderingInfo{};
    CasterQueue.Reset();
}