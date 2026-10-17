#include "EnginePCH.h"
#include "LightComponent.h"

#include "Engine/Scene.h"
#include "Engine/World.h"

ULightComponent::ULightComponent() {}
ULightComponent::~ULightComponent() {}
void ULightComponent::DrawDebug(FLineBatcher* LineBatcher) const {}

void ULightComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);

	// 기존 Getter의 128~4096 범위를 재사용하여 편집값과 실제 해상도를 맞춘다.
	if (Property.Name == "ShadowMapResolution")
	{
		ShadowMapResolution = GetShadowMapResolution();
	}
}

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
