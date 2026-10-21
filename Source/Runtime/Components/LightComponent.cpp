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
	else if (Property.Name == "ShadowPCFKernelSize")
	{
		// 음수를 편집하면 표시값도 실제 사용값인 0으로 맞춘다.
		ShadowPCFKernelSize = GetShadowPCFKernelSize();
	}
	else if (Property.Name == "ShadowBias")
	{
		// 편집 화면에도 Getter가 보장하는 음수 없는 값을 반영한다.
		ShadowBias = GetShadowBias();
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
