#pragma once

#include "Math/EngineMath.h"
#include "Math/Color.h"

struct FLightRenderParameters
{
	FVector Position;
	float Radius;
	FVector Color;
	float Intensity;
	FVector Direction;
	float InnerConeCos;
	FVector Right;
	float FalloffExponent;
	float OuterConeCos;
	float HalfWidth;
	float HalfHeight;
	uint32 Type;
	// Rect 전용: 발광면 가장자리의 문(Barn Door)
	float BarnDoorCos;
	float BarnDoorSin;
	float BarnDoorLength;
	int32 ShadowIndex = INDEX_NONE;
	// PCSS의 원근 깊이 복원 계수와 광원 크기다.
	float DepthScale = 0.0f;
	float DepthOffset = 0.0f;
	float SourceRadius = 0.0f;
	float SourceAngle = 0.0f;
};

static_assert(sizeof(FLightRenderParameters) == 112,"sizeof(UberLit / TileBasedLightCulling) == sizeof(FLightRenderParameters)");
