#include "EnginePCH.h"
#include "DirectionalLightSceneProxy.h"

#include "Components/DirectionalLightComponent.h"
#include "ProjectedShadowInfo.h"
#include "Render/ShadowSettings.h"

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
	Out.SourceAngle = FMath::DegreesToRadians(Dir->GetSourceAngle() * 0.5f);
}

// 카메라의 Receiver 범위를 광원 축의 AABB로 감싸 단일 직교 투영을 만든다.
bool FDirectionalLightSceneProxy::SetupShadowProjection(
    const TArray<FVector>& ReceiverCorners, FProjectedShadowInfo& OutInfo,
	uint32 ProjectionIndex, uint32 ViewId) const
{
	if (!ShouldCastShadow() || ReceiverCorners.Num() != 8)
		return false;

	// Directional 위치·Scale은 무시하고 회전으로 광원 축만 결정한다.
	const FMatrix Rotation = LightComponent->GetWorldRotation().Quaternion().ToFMatrix();
	const FMatrix WorldToLight = Rotation.GetTransposed();

	// 입력 순서는 Near 평면 4점, Far 평면 4점이다.
	FVector NearCenter = FVector::ZeroVector;
	FVector FarCenter = FVector::ZeroVector;

	for (int32 Index = 0; Index < 4; ++Index)
	{
		NearCenter += ReceiverCorners[Index];
		FarCenter += ReceiverCorners[Index + 4];
	}
	NearCenter *= 0.25f;
	FarCenter *= 0.25f;

	// 각 평면 중심에서 모서리까지의 거리 제곱을 구한다.
	float NearRadiusSquared = 0.0f;
	float FarRadiusSquared = 0.0f;

	for (int32 Index = 0; Index < 4; ++Index)
	{
		NearRadiusSquared = FMath::Max(NearRadiusSquared, (ReceiverCorners[Index] - NearCenter).SizeSquared());
		FarRadiusSquared = FMath::Max(FarRadiusSquared, (ReceiverCorners[Index + 4] - FarCenter).SizeSquared());
	}

	// Near→Far 구간에서 최소 포함 구 중심의 위치를 계산한다.
	const FVector FrustumAxis = FarCenter - NearCenter;
	const float FrustumLengthSquared = FrustumAxis.SizeSquared();
	float CenterAlpha = 0.5f;

	// 두 평면이 사실상 겹치는 경우에는 중간 중심을 사용한다.
	if (FrustumLengthSquared > SMALL_NUMBER)
	{
		CenterAlpha = FMath::Clamp(0.5f + (FarRadiusSquared - NearRadiusSquared) / (2.0f * FrustumLengthSquared), 0.0f, 1.0f);
	}

	// 같은 프러스텀 형상에서는 이동·회전에도 필요한 구 반경이 유지된다.
	const FVector SphereCenter = NearCenter + FrustumAxis * CenterAlpha;

	FVector Min = WorldToLight.TransformPosition(ReceiverCorners[0]);
	FVector Max = Min;
	float RadiusSquared = 0.0f;

	for (const FVector& Corner : ReceiverCorners)
	{
		RadiusSquared = FMath::Max(RadiusSquared, (Corner - SphereCenter).SizeSquared());

		// 깊이 범위는 기존처럼 실제 Receiver에 맞춘다.
		const FVector LightCorner = WorldToLight.TransformPosition(Corner);
		Min.X = FMath::Min(Min.X, LightCorner.X);
		Max.X = FMath::Max(Max.X, LightCorner.X);
	}
	Min.X -= 0.1f;
	Max.X += 0.1f;

	// 반경을 1/16 월드 단위로 올려 역투영 오차에 따른 미세한 크기 변화를 줄인다.
	const float Radius = FMath::Max(std::ceil(std::sqrt(RadiusSquared) * 16.0f) / 16.0f, 0.1f);

	// Atlas 전체 크기가 아닌, 실제로 배정하는 개별 타일 해상도다.
	const float Resolution = static_cast<float>(FShadowSettings::GetEffectiveResolution(LightComponent->GetShadowMapResolution()));

	// 한 텍셀의 여유로 중심 반올림 때문에 Receiver가 잘리는 것을 방지한다.
	// TODO: PCF 필터 범위가 정리되면 필터 샘플 여유도 함께 반영한다.
	const float HalfExtent = Radius * Resolution / (Resolution - 2.0f);
	const float WorldUnitsPerTexel = 2.0f * HalfExtent / Resolution;

	FVector Center = WorldToLight.TransformPosition(SphereCenter);
	Center.Y = std::round(Center.Y / WorldUnitsPerTexel) * WorldUnitsPerTexel;
	Center.Z = std::round(Center.Z / WorldUnitsPerTexel) * WorldUnitsPerTexel;

	Min.Y = Center.Y - HalfExtent;
	Max.Y = Center.Y + HalfExtent;
	Min.Z = Center.Z - HalfExtent;
	Max.Z = Center.Z + HalfExtent;

	const FVector Size = Max - Min;
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

	OutInfo.SetupProjection(*this, ViewOrigin, View * Projection, CasterFrustum, CasterPlaneMask, ProjectionIndex, ViewId);
	OutInfo.SetShadowViewSize(FVector(Size.X, Size.Y, Size.Z));
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

	// CSM을 끄면 같은 입력 범위를 분할하지 않는다.
	const int32 CascadeCount = Directional->IsCSMEnabled() ? Directional->GetCascadeCount() : 1;
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
