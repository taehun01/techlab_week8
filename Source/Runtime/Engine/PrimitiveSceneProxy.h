#pragma once

#include "Containers/Array.h"
#include "Math/Frustum.h"
#include "Render/MeshRenderData.h"

class FScene;
class FRenderQueue;
class UPrimitiveComponent;
class UStaticMesh;
class UMaterial;

// View의 카메라 형식을 알지 않고 위치와 크기로 Billboard 행렬을 받는다.
using FBillboardMatrixBuilder = FMatrix(*)(const FVector&, float, float, const void*);

inline static uint64 GlobalShadowVersion = 0u;

class FPrimitiveSceneProxy
{
public:
    explicit FPrimitiveSceneProxy(UPrimitiveComponent* InComponent) : Component(InComponent) {}
    virtual ~FPrimitiveSceneProxy();
    void UpdateTransform();
    void UpdateRenderState();
    void OnMeshRenderDataChanged();
    void OnMeshDestroyed();

    // Capture 구간의 첫 가시 View에서만 동적 렌더 데이터를 복사한다.
    void InvalidateDynamicData() { bDynamicDataPrepared = false; }
    void PrepareDynamicData();

    // 기본 Proxy는 기존 Component 제출 경로를 사용한다.
    virtual bool GatherDynamicRenderPackets(FRenderQueue& RenderQueue, FRenderQueue* IconQueue,
        const FVector& CameraLocation, FBillboardMatrixBuilder BuildBillboardMatrix, const void* ViewContext,
        TArray<int32>& SortedParticleIndices, TArray<float>& ParticleDistancesSquared) const;

    UPrimitiveComponent* GetComponent() const { return Component; }
    const FAABB& GetBounds() const { return Bounds; }
    const FMatrix& GetLocalToWorld() const { return LocalToWorld; }
    const FMatrix& GetWorldToLocal() const { return WorldToLocal; }
    const FLODSphere& GetLODSphere() const { return LODSphere; }
    UStaticMesh* GetMesh() const { return Mesh; }
    bool IsVisible() const { return bVisible; }
    bool IsShadowCaster() const { return bCastShadow; }
    bool IsShadowReceiver() const { return bReceiveShadow; }
    uint8 GetLODCount() const { return RenderState ? RenderState->LODCount : 1; }
    const FMeshRenderState* GetRenderState() const { return RenderState.get(); }
    const float* GetLODThresholdsSq() const { return RenderState->LODThresholdSq; }
    const FCachedMeshLOD& GetLOD(uint32 Index) const { return RenderState->LODs[Index]; }
    const FCachedMeshSection& GetSection(uint32 Index) const { return RenderState->Sections[Index]; }
    FScene* GetScene() const { return Scene; }
    uint64 GetShadowVersion() const { return ShadowVersion; }

protected:
    // 파생 Proxy만 자신의 렌더 스냅샷을 갱신한다.
    virtual void UpdateDynamicData();

private:
    friend class FScene;
    FScene* Scene = nullptr;
    UPrimitiveComponent* Component = nullptr;
    UStaticMesh* Mesh = nullptr;
    TSharedPtr<const FMeshRenderState> RenderState;
    FMatrix LocalToWorld;
    FMatrix WorldToLocal;
    FAABB Bounds;
    FLODSphere LODSphere;
    int32 PackedIndex = INDEX_NONE;
    bool bQueuedForUpdate = false;
    bool bRenderStateQueued = false;
    bool bVisible = true;
    bool bCastShadow = true;
    bool bReceiveShadow = true;
    bool bDynamicDataPrepared = false;
    uint64 ShadowVersion = 0u;
};
