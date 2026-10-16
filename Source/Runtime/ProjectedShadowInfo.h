#pragma once

#include "Render/RenderPacket.h"
#include "Render/RenderingInfo.h"
#include "Math/Frustum.h"

class FLightSceneProxy;

// 쉐도우맵 하나를 만드는데 필요한 CPU 작업 데이터 묶음이다.
// 만들어진 쉐도우맵은 FRenderer가 소유한다.
class FProjectedShadowInfo
{
	// 그림자를 생성한 라이트 객체. 라이트의 수명을 소유하지 않는다.
	const FLightSceneProxy* LightProxy = nullptr;

	// 그림자 View의 원점. SpotLight, PointLight에서는 광원 위치를 사용한다.
	FVector ShadowViewOrigin = FVector::ZeroVector;

    // Caster 렌더링과 Receiver의 깊이 비교에 동일한 VP를 사용한다.
    FMatrix LightViewProjection = FMatrix::Identity;

    // World가 라이트 기준으로 Caster를 수집할 때 사용한다.
    FFrustumPlanes CasterFrustum{};

    // 기존 구조로 Depth 타깃 참조·Viewport·Clear/Load 설정을 표현한다.
    FRenderingInfo RenderingInfo{};

    // Caster 패킷과 패킷이 참조하는 임시 행렬 저장소를 함께 보관한다.
    // RenderShadowDepth에서 기존 Renderer의 정렬 경로로 전달되어 소비된다.
    FRenderQueue CasterQueue;

};

