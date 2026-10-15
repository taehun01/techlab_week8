#include "EnginePCH.h"
#include "FireBallComponent.h"
#include "Engine/World.h"

UFireBallComponent::UFireBallComponent()
{
    SetIntensity(1.8f);
    SetLightColor(FColor(255, 0, 204, 255));
    SetAttenuationRadius(6.5f);
    SetFalloff(3.0f);
}

UFireBallComponent::~UFireBallComponent() {}