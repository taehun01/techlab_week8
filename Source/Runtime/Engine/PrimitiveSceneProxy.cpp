#include "EnginePCH.h"
#include "PrimitiveSceneProxy.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Render/Mesh.h"
#include "Engine/Scene.h"

FPrimitiveSceneProxy::~FPrimitiveSceneProxy()
{
    if (Mesh) Mesh->UnregisterProxy(this);
}

// 준비한 데이터는 다음 Capture 구간까지 모든 View에서 재사용한다.
void FPrimitiveSceneProxy::PrepareDynamicData()
{
    if (bDynamicDataPrepared) return;
    UpdateDynamicData();
    bDynamicDataPrepared = true;
}

// 기본 Primitive는 동적 스냅샷이 필요하지 않다.
void FPrimitiveSceneProxy::UpdateDynamicData()
{
}

// 처리하지 않은 타입은 호출자가 기존 Component 제출 경로로 보낸다.
bool FPrimitiveSceneProxy::GatherDynamicRenderPackets(FRenderQueue&, FRenderQueue*,
    const FVector&, FBillboardMatrixBuilder, const void*, TArray<int32>&, TArray<float>&) const
{
    return false;
}

void FPrimitiveSceneProxy::OnMeshRenderDataChanged()
{
    if (Scene)
    {
        Scene->MarkRenderStateDirty(this);
        Scene->MarkDirty(this);
    }
}

void FPrimitiveSceneProxy::OnMeshDestroyed()
{
    // Shadow Cache 재생성
    ShadowVersion = ++GlobalShadowVersion;

    Mesh = nullptr;
    RenderState.reset();
}

void FPrimitiveSceneProxy::UpdateTransform()
{
    // Shadow Cache 재생성
    ShadowVersion = ++GlobalShadowVersion;

    LocalToWorld = Component->GetWorldMatrix();
    WorldToLocal = LocalToWorld.Inverse();
    const FBox LocalBounds = Mesh ? Mesh->GetRenderBounds() : Component->CalcLocalBounds();
    Bounds = MakeWorldBounds(LocalBounds.GetWorldAABB(LocalToWorld));
    LODSphere = Mesh ? Mesh->GetLocalLODSphere().Transform(LocalToWorld) : FLODSphere{};
}

void FPrimitiveSceneProxy::UpdateRenderState()
{
    // Shadow Cache 재생성
    ShadowVersion = ++GlobalShadowVersion;

    UStaticMesh* PreviousMesh = Mesh;
    Mesh = nullptr;
    RenderState.reset();
    bVisible = Component->IsVisible();
    bCastShadow = Component->IsShadowCaster();
    bReceiveShadow = Component->IsShadowReceiver();
    UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
    if (StaticMeshComponent) Mesh = StaticMeshComponent->GetStaticMesh();
    if (Mesh != PreviousMesh)
    {
        if (PreviousMesh) PreviousMesh->UnregisterProxy(this);
        if (Mesh) Mesh->RegisterProxy(this);
        if (Scene) Scene->MarkDirty(this);
    }
    if (!Mesh) return;
    std::vector<UMaterial*> Materials;
    Materials.reserve(Mesh->MeshData.MaterialSlots.Num());
    for (uint32 Slot = 0; Slot < static_cast<uint32>(Mesh->MeshData.MaterialSlots.Num()); ++Slot)
        Materials.push_back(StaticMeshComponent->GetMaterial(Slot));
    RenderState = Mesh->GetRenderState(Materials);
}
