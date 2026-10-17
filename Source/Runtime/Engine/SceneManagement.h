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
	// 이전 Padding 자리. -1 = 그림자 없음, 0 이상 = Spot Shadow Map 슬라이스이자 ShadowDatas 번호.
	// 프록시가 매 프레임 FLightRenderParameters{}로 초기화하므로 기본값 -1이 곧 "이번 프레임 그림자 없음"이다.
	int32 ShadowIndex = -1;
};

static_assert(sizeof(FLightRenderParameters) == 96,"sizeof(UberLit / TileBasedLightCulling) == sizeof(FLightRenderParameters)");
