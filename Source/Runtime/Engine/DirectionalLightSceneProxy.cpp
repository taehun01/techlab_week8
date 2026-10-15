#include "EnginePCH.h"
#include "DirectionalLightSceneProxy.h"

#include "Components/DirectionalLightComponent.h"

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
