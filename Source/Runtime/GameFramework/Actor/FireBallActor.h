#pragma once
#include "GameFramework/Actor.h"
#include "Components/FireBallComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/RotatingMovementComponent.h"
#include "Components/ProjectileMovementComponent.h"

class AFireBallActor : public AActor
{
	DECLARE_CLASS(AFireBallActor, AActor)

public:
	AFireBallActor();
	UFireBallComponent* GetFireBallComponent() const { return FireballComponent; }

private:
	UFireBallComponent* FireballComponent;
	UStaticMeshComponent* StaticMeshComponent;

	URotatingMovementComponent* RotatingMovementComponent;
	UProjectileMovementComponent* ProjectileMovementComponent;
};