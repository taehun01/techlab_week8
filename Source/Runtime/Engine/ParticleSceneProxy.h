#pragma once

#include "Engine/BillboardSceneProxy.h"
#include "Components/ParticleSubUVComponent.h"

// 시뮬레이션과 분리한 파티클 스냅샷과 View 공통 SubUV 상수를 보관한다.
class FParticleSceneProxy : public FBillboardSceneProxy
{
public:
    explicit FParticleSceneProxy(UParticleSubUVComponent* Component) : FBillboardSceneProxy(Component) {}

    bool GatherDynamicRenderPackets(FRenderQueue& RenderQueue, FRenderQueue* IconQueue,
        const FVector& CameraLocation, FBillboardMatrixBuilder BuildBillboardMatrix, const void* ViewContext,
        TArray<int32>& SortedParticleIndices, TArray<float>& ParticleDistancesSquared) const override;

protected:
    // 기존 FParticle을 재사용하며 생존 인덱스와 상수는 원본 인덱스에 대응한다.
    void UpdateDynamicData() override;

private:
    TArray<FParticle> Particles;
    TArray<FSubUVConstants> Constants;
    TArray<int32> AliveParticleIndices;
};
