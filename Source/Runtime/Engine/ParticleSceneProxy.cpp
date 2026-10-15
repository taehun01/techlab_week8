#include "EnginePCH.h"
#include "Engine/ParticleSceneProxy.h"

#include <algorithm>

// 첫 가시 View에서만 복사하므로 이후 View가 같은 상수 주소를 재사용한다.
void FParticleSceneProxy::UpdateDynamicData()
{
    FBillboardSceneProxy::UpdateDynamicData();
    const UParticleSubUVComponent& ParticleComponent = *static_cast<const UParticleSubUVComponent*>(GetComponent());
    Particles = ParticleComponent.Particles;
    Constants.Reset();
    Constants.Reserve(Particles.Num());
    AliveParticleIndices.Reset();
    AliveParticleIndices.Reserve(Particles.Num());

    const int32 Count = Particles.Num();
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const FParticle& Particle = Particles[Index];
        FSubUVConstants Value{};
        Value.CurrentFrame = static_cast<float>(Particle.SubUVFrame);
        Value.AtlasColSize = static_cast<float>(ParticleComponent.ColSize);
        Value.AtlasRowSize = static_cast<float>(ParticleComponent.RowSize);
        Value.Alpha = Particle.Alpha;
        Constants.Add(Value);
        if (Particle.bAlive) AliveParticleIndices.Add(Index);
    }
}

// View별 거리와 정렬 배열은 World의 작업 버퍼를 사용한다.
bool FParticleSceneProxy::GatherDynamicRenderPackets(FRenderQueue& RenderQueue, FRenderQueue*,
    const FVector& CameraLocation, FBillboardMatrixBuilder BuildBillboardMatrix, const void* ViewContext,
    TArray<int32>& SortedParticleIndices, TArray<float>& ParticleDistancesSquared) const
{
    if (!QuadMesh || !Material) return true;

    // 동률은 항상 원본 인덱스 순서를 유지하도록 매 View의 정렬 입력을 복원한다.
    SortedParticleIndices = AliveParticleIndices;
    ParticleDistancesSquared.SetNum(Particles.Num(), false);
    for (const int32 Index : AliveParticleIndices)
    {
        const FVector Delta = Particles[Index].Location - CameraLocation;
        ParticleDistancesSquared[Index] = Delta.Dot(Delta);
    }

    // 반투명은 Renderer가 모든 emitter를 합쳐 정렬한다.
    if (Material->BlendState == EBlendState::Opaque)
    {
        std::stable_sort(SortedParticleIndices.begin(), SortedParticleIndices.end(),
            [&ParticleDistancesSquared](int32 A, int32 B)
            {
                return ParticleDistancesSquared[A] > ParticleDistancesSquared[B];
            });
    }

    for (const int32 Index : SortedParticleIndices)
    {
        const FParticle& Particle = Particles[Index];
        FRenderPacket Packet;
        Packet.Mesh = QuadMesh;
        Packet.Material = Material;
        Packet.Model = RenderQueue.StoreWorldMatrix(BuildBillboardMatrix(
            Particle.Location, Particle.Scale, Particle.Scale, ViewContext));
        Packet.CameraToParticleDistance = ParticleDistancesSquared[Index];
        Packet.MaterialParamData = &Constants[Index];
        Packet.MaterialParamDataSize = sizeof(FSubUVConstants);
        RenderQueue.Add(Packet);
    }
    return true;
}
