#pragma once
//
//class FLightSceneInfoCompact
//{
//public:
//	// XYZ: origin, W:sphere radius
//	VectorRegister BoundingSphereVector;
//	FLinearColor Color;
//	// must not be 0
//	FLightSceneInfo* LightSceneInfo;
//	// e.g. LightType_Directional, LightType_Point or LightType_Spot
//	uint32 LightType : LightType_NumBits;
//	uint32 bCastDynamicShadow : 1;
//	uint32 bCastStaticShadow : 1;
//	uint32 bStaticLighting : 1;
//	uint32 bAffectReflection : 1;
//	uint32 bAffectGlobalIllumination : 1;
//	uint32 bIsMovable : 1;
//	uint32 bAllowMegaLights : 1;
//	TEnumAsByte<EMegaLightsShadowMethod::Type> MegaLightsShadowMethod;
//	TEnumAsByte<ECastRayTracedShadow::Type> CastRaytracedShadow;
//
//	/** Initializes the compact scene info from the light's full scene info. */
//	void Init(FLightSceneInfo* InLightSceneInfo);
//
//	/** Default constructor. */
//	FLightSceneInfoCompact() :
//		LightSceneInfo(NULL)
//	{
//	}
//
//	/** Initialization constructor. */
//	FLightSceneInfoCompact(FLightSceneInfo* InLightSceneInfo)
//	{
//		Init(InLightSceneInfo);
//	}
//
//	/**
//	 * Tests whether this light affects the given primitive.  This checks both the primitive and light settings for light relevance
//	 * and also calls AffectsBounds.
//	 *
//	 * @param CompactPrimitiveSceneInfo - The primitive to test.
//	 * @return True if the light affects the primitive.
//	 */
//	bool AffectsPrimitive(const FBoxSphereBounds& PrimitiveBounds, const FPrimitiveSceneProxy* PrimitiveSceneProxy) const;
//};
//
//
//using FSceneLightInfoArray = TLightSparseArray<FLightSceneInfoCompact, TAlignedSparseArrayAllocator<alignof(FLightSceneInfoCompact)>>;