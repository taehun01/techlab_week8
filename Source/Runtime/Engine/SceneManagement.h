#pragma once

#include "Math/EngineMath.h"
#include "Math/Color.h"

//static_assert(sizeof(FLightRenderParameters) == 96, "UberLit / 컬링 CS의 구조체와 같아야 한다");

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
	float Padding;
};

static_assert(sizeof(FLightRenderParameters) == 96,"sizeof(UberLit / TileBasedLightCulling) == sizeof(FLightRenderParameters)");
