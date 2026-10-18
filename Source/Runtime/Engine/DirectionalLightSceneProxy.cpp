#include "EnginePCH.h"
#include "DirectionalLightSceneProxy.h"

#include "Components/DirectionalLightComponent.h"
#include "ProjectedShadowInfo.h"

void FDirectionalLightSceneProxy::GetLightShaderParameters(FLightRenderParameters& Out) const
{
	const UDirectionalLightComponent* Dir = static_cast<const UDirectionalLightComponent*>(LightComponent);

	FTransform Transform;
	Transform.Rotation = Dir->GetWorldRotation();
	const FLinearColor Color(Dir->GetLightColor());

	Out.Type = static_cast<uint32>(ELightType::Directional);
	Out.Direction = Transform.GetForward();
	Out.Color = FVector(Color.R, Color.G, Color.B);
	Out.Intensity = Dir->GetIntensity();
}

// 카메라의 Receiver 범위를 광원 축의 AABB로 감싸 단일 직교 투영을 만든다.
bool FDirectionalLightSceneProxy::SetupShadowProjection(
    const TArray<FVector>& ReceiverCorners, FProjectedShadowInfo& OutInfo) const
{
	if (!ShouldCastShadow() || ReceiverCorners.IsEmpty())
		return false;

	// Directional 위치·Scale은 무시하고 회전으로 광원 축만 결정한다.
	const FMatrix Rotation = LightComponent->GetWorldRotation().Quaternion().ToFMatrix();
	const FMatrix WorldToLight = Rotation.GetTransposed();
	FVector Min = WorldToLight.TransformPosition(ReceiverCorners[0]);
	FVector Max = Min;
	for (const FVector& Corner : ReceiverCorners)
	{
		const FVector LightCorner = WorldToLight.TransformPosition(Corner);
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			Min[Axis] = FMath::Min(Min[Axis], LightCorner[Axis]);
			Max[Axis] = FMath::Max(Max[Axis], LightCorner[Axis]);
		}
	}

	// 경계 오차와 납작한 범위에서도 유효한 직교 투영을 확보한다.
	Min -= FVector(0.1f);
	Max += FVector(0.1f);
	const FVector Size = Max - Min;
	const FVector Center = (Min + Max) * 0.5f;
	const FVector ViewOrigin = Rotation.TransformPosition(FVector(Min.X, Center.Y, Center.Z));
	const FMatrix View = FMatrix::MakeTranslation(-ViewOrigin) * WorldToLight;

	// +X 전방·행 벡터·깊이 0~1 규약의 직교 투영이다.
	const FMatrix Projection(
		0.0f, 0.0f, 1.0f / Size.X, 0.0f,
		2.0f / Size.Y, 0.0f, 0.0f, 0.0f,
		0.0f, 2.0f / Size.Z, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	);

	// 단위 광원 축으로 월드 평면을 만들고 Near 검사만 제외한다.
	// 화면 밖 광원 쪽 Caster는 포함하되 Depth 범위는 Receiver에 맞춘다.
	const FVector Forward = Rotation.TransformVector(ForwardVector);
	const FVector Right = Rotation.TransformVector(RightVector);
	const FVector Up = Rotation.TransformVector(UpVector);
	FFrustumPlanes CasterFrustum{};
	CasterFrustum.Planes[0] = { Right, -Min.Y };
	CasterFrustum.Planes[1] = { -Right, Max.Y };
	CasterFrustum.Planes[2] = { Up, -Min.Z };
	CasterFrustum.Planes[3] = { -Up, Max.Z };
	CasterFrustum.Planes[4] = { Forward, -Min.X };
	CasterFrustum.Planes[5] = { -Forward, Max.X };
	const uint32 CasterPlaneMask = FrustumAllPlanesMask & ~(1u << 4);

	OutInfo.SetupProjection(*this, ViewOrigin, View * Projection, CasterFrustum, CasterPlaneMask);
	return true;
}

bool FDirectionalLightSceneProxy::CalculateCascadeSplitDistances(float ViewNear, float ViewFar, TArray<float>& OutSplitDistances) const
{
	// 호출 간에 이전 View의 분할 결과가 남지 않도록 비운다.
	OutSplitDistances.Reset();

	const auto* Directional = static_cast<const UDirectionalLightComponent*>(LightComponent);
	const float FarDistance = FMath::Min(ViewFar, Directional->GetShadowDistance());

	// 원근 카메라의 유효한 전방 깊이 구간만 계산한다.
	if (ViewNear <= 0.0f || FarDistance <= ViewNear)
	{
		return false;
	}

	// CSM을 끄면 같은 입력 범위를 분할하지 않고 한 구간으로 반환한다.
	const int32 CascadeCount = Directional->IsCSMEnabled()
		? Directional->GetCascadeCount() : 1;
	const float Exponent = Directional->GetCascadeDistributionExponent();

	// 구간 길이의 가중치 1, E, E²...의 전체 합을 구한다.
	float TotalWeight = 0.0f;
	float Weight = 1.0f;
	for (int32 CascadeIndex = 0; CascadeIndex < CascadeCount; ++CascadeIndex)
	{
		TotalWeight += Weight;
		Weight *= Exponent;
	}

	OutSplitDistances.Reserve(CascadeCount + 1);
	OutSplitDistances.Add(ViewNear);

	// 누적 가중치를 전체 깊이 범위에 적용해 내부 경계를 계산한다.
	float AccumulatedWeight = 0.0f;
	Weight = 1.0f;
	for (int32 CascadeIndex = 0; CascadeIndex < CascadeCount - 1; ++CascadeIndex)
	{
		AccumulatedWeight += Weight;
		const float Fraction = AccumulatedWeight / TotalWeight;
		OutSplitDistances.Add(ViewNear + (FarDistance - ViewNear) * Fraction);
		Weight *= Exponent;
	}

	// 마지막 경계는 입력 Far에 정확히 맞춰 누적 오차를 피한다.
	OutSplitDistances.Add(FarDistance);
	return true;

}
