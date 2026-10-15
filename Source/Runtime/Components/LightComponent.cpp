#include "EnginePCH.h"
#include "LightComponent.h"

#include "Engine/Scene.h"
#include "Engine/World.h"

ULightComponent::ULightComponent() {}
ULightComponent::~ULightComponent() {}
void ULightComponent::DrawDebug(FLineBatcher* LineBatcher) const {}

void ULightComponent::CreateRenderState()
{
	Super::CreateRenderState();
	GetWorld()->GetScene().AddLight(this);
}

void ULightComponent::DestroyRenderState()
{
	GetWorld()->GetScene().RemoveLight(this);
	Super::DestroyRenderState();
}
