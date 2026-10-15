#include  "EnginePCH.h"
#include "ExponentialHeightFogComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

FFogInfo UExponentialHeightFogComponent::GetFogInfo() const
{
	FFogInfo FogInfo;
	FogInfo.FogDensity = FogDensity;
	FogInfo.FogHeightFalloff = FogHeightFalloff;
	FogInfo.StartDistance = StartDistance;
	FogInfo.FogCutoffDistance = FogCutoffDistance;
	FogInfo.FogMaxOpacity = FogMaxOpacity;
	FogInfo.FogColor = FVector4(FogColor);
	FogInfo.fogHeight = GetOwner()->GetActorLocation().Z;

	return FogInfo;
}

void UExponentialHeightFogComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor) return;

	UWorld* World = OwnerActor->GetWorld();
	if (!World) return;

	World->GetScene().UpdateFogInfo(GetUUID(),GetFogInfo());
}

void UExponentialHeightFogComponent::CreateRenderState()
{
	Super::CreateRenderState();
	GetWorld()->GetScene().AddFogInfo(GetUUID(), GetFogInfo());
}

void UExponentialHeightFogComponent::DestroyRenderState()
{
	GetWorld()->GetScene().RemoveFogInfo(GetUUID());
	Super::DestroyRenderState();
}
