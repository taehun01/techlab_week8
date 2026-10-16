#pragma once

#include "Engine/PrimitiveSceneProxy.h"
#include "Components/BillboardComponent.h"

// Billboard의 View 공통 렌더 상태를 보관하고 View별 패킷을 제출한다.
class FBillboardSceneProxy : public FPrimitiveSceneProxy
{
public:
    explicit FBillboardSceneProxy(UBillboardComponent* Component) : FPrimitiveSceneProxy(Component) {}

    bool GatherDynamicRenderPackets(FRenderQueue& RenderQueue, FRenderQueue* IconQueue,
        const FVector& CameraLocation, FBillboardMatrixBuilder BuildBillboardMatrix, const void* ViewContext,
        TArray<int32>& SortedParticleIndices, TArray<float>& ParticleDistancesSquared) const override;

protected:
    // Quad는 StaticMesh 빠른 경로를 선택하는 기본 Proxy의 Mesh와 구분한다.
    void UpdateDynamicData() override;
    UStaticMesh* QuadMesh = nullptr;
    UMaterial* Material = nullptr;

private:
    FSubUVConstants Parameters{};
    FVector WorldPosition;
    FVector2 Size;
    bool bEditorIcon = false;
};
