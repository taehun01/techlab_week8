#include "EnginePCH.h"
#include "Engine/BillboardSceneProxy.h"

// Component는 원본 설정을 유지하고 Proxy는 Capture 구간의 렌더 값을 보관한다.
void FBillboardSceneProxy::UpdateDynamicData()
{
    const UBillboardComponent& Billboard = *static_cast<const UBillboardComponent*>(GetComponent());
    QuadMesh = Billboard.QuadMesh;
    Material = Billboard.Material;
    WorldPosition = Billboard.GetWorldLocation();
    const FVector Scale = Billboard.GetWorldScale3D();
    Size = { Scale.Y, Scale.Z };
    bEditorIcon = Billboard.bEditorIcon;
    Parameters = Billboard.IconParams;
    Parameters.Tint = Billboard.TintSource ? Billboard.TintSource->GetLightColor().ToVector4() : FVector4(1, 1, 1, 1);
    Parameters.DecodeSRGB = bEditorIcon ? 0.0f : 1.0f;
}

// 행렬은 Queue가 소유하고 상수는 모든 View 제출이 끝날 때까지 Proxy가 유지한다.
bool FBillboardSceneProxy::GatherDynamicRenderPackets(FRenderQueue& RenderQueue, FRenderQueue* IconQueue,
    const FVector&, FBillboardMatrixBuilder BuildBillboardMatrix, const void* ViewContext,
    TArray<int32>&, TArray<float>&) const
{
    if (!QuadMesh || !Material) return true;

    FRenderQueue& Target = bEditorIcon && IconQueue ? *IconQueue : RenderQueue;
    FRenderPacket Packet;
    Packet.Mesh = QuadMesh;
    Packet.Material = Material;
    Packet.Model = Target.StoreWorldMatrix(BuildBillboardMatrix(WorldPosition, Size.X, Size.Y, ViewContext));
    Packet.MaterialParamData = &Parameters;
    Packet.MaterialParamDataSize = sizeof(FSubUVConstants);
    Target.Add(Packet);
    return true;
}
