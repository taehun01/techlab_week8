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
    void SetupProjection(const FLightSceneProxy& InLightProxy, const FVector& InViewOrigin, const FMatrix& InLightViewProjection);

    // Renderer가 준비한 타깃·Viewport·Clear/Load 설정을 연결한다.
    void SetRenderingInfo(const FRenderingInfo& InRenderingInfo)
    {
        RenderingInfo = InRenderingInfo;
    }

    // 타깃이 있는지 체크
    bool HasDepthTarget() const { return RenderingInfo.DepthStencil.Texture != nullptr; }

    // World가 Caster를 탐색할 범위를 읽기 전용으로 제공한다.
    const FFrustumPlanes& GetCasterFrustum() const { return CasterFrustum; }

    // World가 선별한 정적 메시 패킷을 등록한다.
    // 패킷이 참조하는 Proxy·Mesh·Material은 Depth 렌더 종료까지 유효해야 한다.
    void AddCasterPacket(const FRenderPacket& Packet);

    // CPU 작업과 타깃 참조를 초기화한다. GPU 텍스처는 해제하지 않는다.
    void Reset();

private:
    // Renderer는 준비된 상태를 읽고 Caster 큐를 소비한다.
    friend class FRenderer;

    // 그림자를 생성할 라이트 객체. 라이트의 수명을 소유하지 않는다.
    const FLightSceneProxy* LightProxy = nullptr;

    // 그림자 View의 원점. SpotLight, PointLight에서는 광원 위치를 사용한다.
    FVector ShadowViewOrigin = FVector::ZeroVector;

    // Caster 렌더링과 Receiver의 깊이 비교에 동일한 VP를 사용한다.
    FMatrix LightViewProjection = FMatrix::Identity;

    // World가 라이트 기준으로 Caster를 수집할 때 사용한다
    FFrustumPlanes CasterFrustum{};

    // GPU 타깃을 소유하지 않고 기존 패스 설정으로 참조한다.
    FRenderingInfo RenderingInfo{};

    // Caster 패킷과 큐 내부에서 사용하는 임시 행렬 저장소를 소유한다.
    FRenderQueue CasterQueue;
};