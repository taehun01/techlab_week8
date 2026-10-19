#pragma pack_matrix(row_major)

#include "ColorSpace.hlsli"

#define LIGHT_TYPE_POINT 1   // C++ ELightType과 같아야 함
#define LIGHT_TYPE_SPOT  2
#define LIGHT_TYPE_RECT  3

// LightCullingCommon.hlsli의 MAX_LIGHTS_PER_CELL / CELL_STRIDE와 같아야 한다.
#define MAX_LIGHTS_PER_CELL 32
#define CELL_STRIDE (MAX_LIGHTS_PER_CELL + 1)

// LightGrid.LightDebugMode 값. 에디터 SettingsPanel의 "Light Debug" 콤보 순서와 같아야 한다.
#define LIGHT_DEBUG_MODE_NONE         0
#define LIGHT_DEBUG_MODE_CELL_HEATMAP 1
#define LIGHT_DEBUG_MODE_BRUTE_FORCE  2   // 컬링 결과를 무시하고 모든 로컬 라이트를 순회
#define LIGHT_DEBUG_MODE_SLICES       3

// 조명 모델은 C++(FRenderResourceManager::LoadUberLit)이 매크로로 하나만 넘긴다.
// 아무 매크로 없이 컴파일될 때(기존 머티리얼이 쓰는 기본 키)만 Lambert를 켠다.
#if !defined(LIGHTING_MODEL_UNLIT) && !defined(LIGHTING_MODEL_GOURAUD) && \
    !defined(LIGHTING_MODEL_LAMBERT) && !defined(LIGHTING_MODEL_BLINN_PHONG) && \
    !defined(WORLD_NORMAL)
#define LIGHTING_MODEL_LAMBERT 1
#endif

// 아래 구조체와 cbuffer Lighting은 Renderer.h의 FLightingConstants와 바이트 단위로 같아야 한다.
struct FAmbientLightInfo
{
    float3 Color;
    float Intensity;
};

struct FDirectionalLightInfo
{
    float3 Direction; // 빛이 향하는 방향
    float Intensity;
    float3 Color;
    uint DirectionalPadding;
};

// SceneManagement.h의 FLightRenderParameters와 바이트 단위로 같아야 한다.
struct FLightRenderParameters
{
    float3 Position;
    float Radius;
    float3 Color;
    float Intensity;
    float3 Direction;
    float InnerConeCos;
    float3 Right;
    float FalloffExponent;
    float OuterConeCos;
    float HalfWidth;
    float HalfHeight;
    uint Type;
    float BarnDoorCos;     // Rect 전용
    float BarnDoorSin;
    float BarnDoorLength;
    int ShadowIndex;
};



// Renderer.h의 FViewConstants와 바이트 단위로 같아야 한다. VS와 PS 둘 다 b0에 바인딩된다.
cbuffer ViewConstants : register(b0)
{
    matrix ViewProjection;
    float3 CameraWorldPos; // Specular의 V = normalize(CameraWorldPos - WorldPos)
    uint ClampShadowNear; // Directional Depth 패스에서만 Near Clamp를 적용한다.
    
    float3 CameraForward;
    uint DirectionalCascadeCount;

    // CPU의 연속된 4원소 배열과 일치하도록 벡터로 선언한다.
    int4 DirectionalShadowIndices;
    float4 DirectionalCascadeFarDistances;

    float DirectionalShadowNearDistance;
    float DirectionalCascadeTransitionFraction;
    uint VisualizeShadowCascades;
    float ViewPadding;
};

cbuffer MaterialParams : register(b1)
{
    float4 BaseColor;
    float2 UVOffset;
    float bOpaque;
    float Shininess; // Blinn-Phong 광택 지수 (Material.h FStaticMeshMaterialParams::Shininess)
};

// Renderer.h의 FPerObjectConstants와 바이트 단위로 같아야 한다.
cbuffer PerObjectConstants : register(b2)
{
    matrix World;
    matrix WorldInverseTranspose; // (World^-1)^T
    uint ReceiveShadow;
    uint3 ObjectPadding;
};

// b3는 FireBall이 쓰는 동안 비워 둔다.

cbuffer Lighting : register(b4)
{
    FAmbientLightInfo Ambient;
    FDirectionalLightInfo Directional;
    uint NumLocalLights;
    uint3 LightingPadding;
};


cbuffer LightGrid : register(b5)
{
    uint NumTilesX;
    uint NumTilesY;
    uint NumSlices;
    uint LightDebugMode; // LIGHT_DEBUG_MODE_*
    float NearZ;
    float FarZ;
    float SliceScale;
    float SliceBias;
    uint TileSize;
    uint bOrthographic;
    uint2 LightGridPadding;
};

StructuredBuffer<FLightRenderParameters> LocalLights : register(t10);
// 셀마다 [개수][라이트 번호 x MAX_LIGHTS_PER_CELL] 형태로 CELL_STRIDE칸씩 이어져 있다.
StructuredBuffer<uint> CellLightIndices : register(t12);

Texture2D DiffuseTexture : register(t0);
Texture2D NormalTexture : register(t1);
SamplerState MaterialSampler : register(s0);
SamplerComparisonState PCFSampler : register(s1);

#if defined(LIGHTING_MODEL_LAMBERT) || defined(LIGHTING_MODEL_BLINN_PHONG)

// Renderer의 MaxBoundShadowAtlases·ShadowAtlasTilesPerRow와 같아야 한다.
#define MAX_BOUND_SHADOW_ATLASES 8
#define SHADOW_ATLAS_TILES_PER_ROW 2

// C++ FShadowShaderParameters와 동일한 96바이트 레이아웃이다.
struct FShadowShaderParameters
{
    matrix LightViewProjection;
    float Bias;
    uint Resolution;
    int AtlasPageIndex;
    uint TileIndex;
    float ShadowPCFRadius;
    float3 pad;
};

// 전체 그림자 정보는 가변 개수로 전달한다.
StructuredBuffer<FShadowShaderParameters> ShadowParameters : register(t13);

// 같은 페이지에 배정된 타일들은 하나의 SRV를 공유한다.
Texture2D<float> ShadowAtlases[MAX_BOUND_SHADOW_ATLASES] : register(t14);

#endif

struct VS_INPUT
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float4 Tangent : TANGENT;   // w = 바이탄젠트 부호
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
};

// VS가 출력하고 PS가 입력으로 받는다.
struct VS_OUTPUT
{
    float4 SvPosition : SV_POSITION; // VS: 클립 공간 / PS: 픽셀 좌표(xy), 깊이(z), View 깊이(w)
    float3 WorldPos : TEXCOORD1;
    float3 Normal : NORMAL;
    float4 Tangent : TANGENT;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR;
#if defined(LIGHTING_MODEL_LAMBERT) || defined(LIGHTING_MODEL_BLINN_PHONG)
    // 물체별 정수 설정은 삼각형 내부에서 보간하지 않는다.
    nointerpolation uint ReceivesShadow : TEXCOORD2;
#endif
#if LIGHTING_MODEL_GOURAUD
    float3 VertexDiffuse : COLOR1;   // 정점에서 미리 계산한 Diffuse 조명
#endif
};

// Diffuse와 Specular를 담는 구조체
struct FLightContribution
{
    float3 Diffuse;
    float3 Specular;
};

#if defined(LIGHTING_MODEL_LAMBERT) || defined(LIGHTING_MODEL_BLINN_PHONG)

// 현재 SM5.0 경로에서 정적 텍스처 인덱스로 비교 샘플링한 가시성을 반환한다.
float LoadShadowDepth(int AtlasPageIndex, SamplerComparisonState Sampler, float2 UV, float CompareValue)
{
#define SHADOW_DEPTH_CASE(Index) case Index: return ShadowAtlases[Index].SampleCmpLevelZero(Sampler, UV, CompareValue)

    switch (AtlasPageIndex)
    {
        SHADOW_DEPTH_CASE(0);SHADOW_DEPTH_CASE(1);SHADOW_DEPTH_CASE(2);SHADOW_DEPTH_CASE(3);
        SHADOW_DEPTH_CASE(4);SHADOW_DEPTH_CASE(5);SHADOW_DEPTH_CASE(6);SHADOW_DEPTH_CASE(7);
        default:
            return 1.0f;
    }

#undef SHADOW_DEPTH_CASE
}

// Atlas 타일 좌표 변환과 하드웨어 PCF 가시성 계산을 담당한다.
float SampleShadowMap(FShadowShaderParameters Shadow, float3 LightNDC)
{
    // D3D 텍스처의 좌상단 원점에 맞춰 Y를 뒤집는다.
    const float2 UV = LightNDC.xy * float2(0.5f, -0.5f) + 0.5f; // 카메라기준 LightSpace 변환한 NDC 좌표 UV 좌표 범위로 변환
    const uint2 Tile = uint2(Shadow.TileIndex % SHADOW_ATLAS_TILES_PER_ROW, Shadow.TileIndex / SHADOW_ATLAS_TILES_PER_ROW);
    // 투영 영역 밖은 먼저 반환해 PCF 반복문의 계산 경로를 명확히 한다.
    if (!(all(UV >= 0.0.xx) && all(UV <= 1.0.xx)))
        return 1.0f;

    float texel = 1.0f / max(Shadow.Resolution, 1.0f); // 타일 기준 한 텍셀의 UV 크기다.
    float halftexel = 0.5f * texel; // 비교 필터가 인접 타일을 읽지 않도록 텍셀 중심까지 제한한다.
    int r = max(int(Shadow.ShadowPCFRadius), 1); // (2r) x (2r)회 호출하며, 각 호출은 하드웨어의 2 x 2 비교 필터를 사용한다.
    float sum = 0.0f;
    int taps = 0;   // 평균 나눌때 분모, 픽셀 연산 횟수
    [loop]
    for (int dy = -r; dy < r; ++dy)
    {
        [loop]
        for (int dx = -r; dx < r; ++dx)
        {
            float2 offset = float2(dx, dy) + 0.5f; // 짝수 격자의 샘플 위치를 중심 양쪽에 대칭으로 배치한다.
            float2 uvOff = UV + offset * texel;
            float2 localUV = clamp(uvOff, float2(halftexel, halftexel), float2(1.0f - halftexel, 1.0f - halftexel));
            float2 atlasUV = (float2(Tile) + localUV) / float(SHADOW_ATLAS_TILES_PER_ROW);
            float d = LoadShadowDepth(Shadow.AtlasPageIndex, PCFSampler, atlasUV, LightNDC.z - Shadow.Bias);
            sum += d; // 깊이 비교와 선형 필터링을 마친 가시성을 누적한다.
            taps++;
        }
    }
    return sum / max(taps, 1); // 전체 깊이 비교 결과를 평균해 그림자 가시성을 구한다.
}

// 양수 인덱스의 범위는 CPU가 보장하고, 실제 타깃 존재 여부만 확인한다.
bool HasShadowMap(int ShadowIndex)
{
    if (ShadowIndex < 0)
        return false;

    const FShadowShaderParameters Shadow = ShadowParameters[ShadowIndex];
    return Shadow.AtlasPageIndex >= 0 && Shadow.Resolution > 0;
}

// 월드 위치를 해당 그림자의 투영 공간으로 옮겨 가시성을 구한다.
float GetShadowVisibility(int ShadowIndex, float3 WorldPos)
{
    // 타깃 실패·바인딩 상한 초과를 공통 경로에서 처리한다.
    if (!HasShadowMap(ShadowIndex))
        return 1.0f;

    const FShadowShaderParameters Shadow = ShadowParameters[ShadowIndex];

    const float4 LightClip = mul(float4(WorldPos, 1.0f), Shadow.LightViewProjection);
    if (LightClip.w <= 0.0f)
        return 1.0f;

    // 라이트 투영 범위 밖에서는 깊이를 읽지 않는다.
    const float3 LightNDC = LightClip.xyz / LightClip.w;
    if (any(abs(LightNDC.xy) > 1.0f) || LightNDC.z < 0.0f || LightNDC.z > 1.0f)
        return 1.0f;

    return SampleShadowMap(Shadow, LightNDC);
}

// 실제 그림자와 색상 표시가 동일한 선택 구간을 사용한다.
int SelectDirectionalCascade(float ViewDepth)
{
    if (ViewDepth < DirectionalShadowNearDistance)
        return -1;

    [loop]
    for (uint Index = 0; Index < DirectionalCascadeCount; ++Index)
    {
        if (ViewDepth <= DirectionalCascadeFarDistances[Index])
            return int(Index);
    }
    return -1;
}

// 다음 맵이 존재하는 전환 구간에서만 보간한다.
float GetDirectionalCascadeBlend(uint CascadeIndex, float ViewDepth)
{
    if (CascadeIndex + 1 >= DirectionalCascadeCount || DirectionalCascadeTransitionFraction <= 0.0f)
        return 0.0f;

    const float NearDistance = CascadeIndex == 0 ? DirectionalShadowNearDistance : DirectionalCascadeFarDistances[CascadeIndex - 1];
    const float FarDistance = DirectionalCascadeFarDistances[CascadeIndex];
    const float Width = (FarDistance - NearDistance) * DirectionalCascadeTransitionFraction;
    const float Start = FarDistance - Width;

    if (ViewDepth <= Start || !HasShadowMap(DirectionalShadowIndices[CascadeIndex + 1]))
        return 0.0f;

    return saturate((ViewDepth - Start) / Width);
}

// 깊이 비교와 PCF를 마친 가시성을 전환 구간에서 보간한다.
float GetDirectionalShadowVisibility(float3 WorldPos)
{
    const float ViewDepth = dot(WorldPos - CameraWorldPos, CameraForward);
    const int Selected = SelectDirectionalCascade(ViewDepth);
    if (Selected < 0)
        return 1.0f;

    const uint CascadeIndex = uint(Selected);
    const int ShadowIndex = DirectionalShadowIndices[CascadeIndex];
    if (!HasShadowMap(ShadowIndex))
        return 1.0f;

    const float Visibility = GetShadowVisibility(ShadowIndex, WorldPos);
    const float Blend = GetDirectionalCascadeBlend(CascadeIndex, ViewDepth);
    if (Blend <= 0.0f)
        return Visibility;

    const float NextVisibility = GetShadowVisibility(DirectionalShadowIndices[CascadeIndex + 1], WorldPos);
    return lerp(Visibility, NextVisibility, Blend);
}

// 전환 구간은 두 색을 섞고, 범위 밖이나 사용 불가능한 맵은 회색으로 표시한다.
float3 GetDirectionalCascadeDebugColor(float3 WorldPos)
{
    const float ViewDepth = dot(WorldPos - CameraWorldPos, CameraForward);
    const int Selected = SelectDirectionalCascade(ViewDepth);
    if (Selected < 0)
        return float3(0.25f, 0.25f, 0.25f);

    const uint CascadeIndex = uint(Selected);
    if (!HasShadowMap(DirectionalShadowIndices[CascadeIndex]))
        return float3(0.25f, 0.25f, 0.25f); 

    static const float3 Colors[4] =
    {
        float3(1.0f, 0.2f, 0.2f),
        float3(0.2f, 1.0f, 0.2f),
        float3(0.2f, 0.4f, 1.0f),
        float3(1.0f, 1.0f, 0.2f)
    };

    float3 Color = Colors[CascadeIndex];
    const float Blend = GetDirectionalCascadeBlend(CascadeIndex, ViewDepth);
    if (Blend > 0.0f)
        Color = lerp(Color, Colors[CascadeIndex + 1], Blend);
    return Color;
}
#endif

// 원뿔 감쇠량. 0(Outer 밖) ~ 1(Inner 안)
float GetSpotConeAttenuation(FLightRenderParameters Light, float3 L)
{
    return smoothstep(Light.OuterConeCos, Light.InnerConeCos, dot(-L, Light.Direction));
}

// 거리 감쇠량을 돌려주고, 표면에서 빛을 향하는 단위 벡터 L을 OutL로 내보낸다.
float GetDistanceAttenuation(float3 LightPos, float AttenuationRadius, float FalloffExponent, float3 WorldPos, out float3 OutL)
{
    const float3 ToLight = LightPos - WorldPos; // 픽셀의 월드 좌표에서 빛을 향하는 벡터
    const float Distance = length(ToLight);
    OutL = ToLight / max(Distance, 0.0001f);
    // 거리가 Radius에 가까울수록 0으로
    return pow(saturate(1.0f - Distance / max(AttenuationRadius, 0.001f)), max(FalloffExponent, 0.001f));
}



// 라이트 하나의 Diffuse와 Specular를 구한다. Radiance = Color * Intensity * Attenuation (* ConeAttenuation)
FLightContribution SurfaceShading(float3 N, float3 L, float3 V, float3 Radiance, float InShininess)
{
    const float NdotL = saturate(dot(normalize(N), normalize(L)));
    FLightContribution Contribution = (FLightContribution) 0;
    Contribution.Diffuse = NdotL;

#if LIGHTING_MODEL_BLINN_PHONG
    // Blinn Specular
    const float3 H = normalize(L + V);
    if (dot(N, L) > 0)
    {
        Contribution.Specular = pow(saturate(dot(normalize(N), H)), max(InShininess, 1.f));
    }
#endif
    Contribution.Diffuse *= Radiance;
    Contribution.Specular *= Radiance;
    return Contribution;
}

// 표면에 들어오는 빛의 양만 돌려준다. 표면색(albedo)은 호출한 쪽에서 곱한다.
float3 GetAmbientLighting()
{
    return Ambient.Color * Ambient.Intensity;
}

// Directional Light의 영향을 구한다
FLightContribution CalcDirectionalLight(float3 N, float3 V, float InShininess)
{
    const float3 Radiance = Directional.Color * Directional.Intensity;
    return SurfaceShading(N, -Directional.Direction, V, Radiance, InShininess);
}

// 해당 WorldPos에서 PointLight 하나의 영향(밝기)을 구한다
FLightContribution CalcPointLight(FLightRenderParameters Light, float3 N, float3 V, float3 WorldPos, float InShininess)
{
    float3 L;
    const float Attenuation = GetDistanceAttenuation(Light.Position, Light.Radius, Light.FalloffExponent, WorldPos, L);
    return SurfaceShading(N, L, V, Light.Color * Light.Intensity * Attenuation, InShininess);
}

// Spot은 Point에서 원뿔 감쇠만 곱하면 된다
FLightContribution CalcSpotLight(FLightRenderParameters Light, float3 N, float3 V, float3 WorldPos, float InShininess)
{
    float3 L;
    const float Attenuation = GetDistanceAttenuation(Light.Position, Light.Radius, Light.FalloffExponent, WorldPos, L);
    return SurfaceShading(N, L, V, Light.Color * Light.Intensity * Attenuation * GetSpotConeAttenuation(Light, L), InShininess);
}

FLightContribution CalcRectLight(FLightRenderParameters Light, float3 N, float3 V, float3 WorldPos, float InShininess)
{
    const float3 Up = cross(Light.Direction, Light.Right);  // Z축
    const float3 CenterToSurface = WorldPos - Light.Position;

    // 1) 사각형 위에서 표면과 가장 가까운 점 찾기
    const float RectX = clamp(dot(CenterToSurface, Light.Right), -Light.HalfWidth,  Light.HalfWidth);   // 사각형 가로 좌표
    const float RectY = clamp(dot(CenterToSurface, Up),          -Light.HalfHeight, Light.HalfHeight);  // 사각형 세로 좌표
    const float3 ClosestPoint = Light.Position + Light.Right * RectX + Up * RectY;

    // 2) 가장 가까운 점을 점광원처럼 취급
    float3 L;   // 표면 -> 빛
    const float Attenuation = GetDistanceAttenuation(ClosestPoint, Light.Radius, Light.FalloffExponent, WorldPos, L);

    // 3) 면의 앞쪽만 비춘다 (뒤쪽이면 0)
    const float FrontFace = saturate(dot(-L, Light.Direction));

    // 4) 문 안쪽(프러스텀)만 통과
    float BarnDoorMask = 1.0f;

    // Door Length가 0이면 그냥 rect (전방향)
    if (Light.BarnDoorLength > 0.0f)
    {
        const float3 EmitterToSurface = WorldPos - ClosestPoint;     // 발광면에서 표면으로
        const float Distance = max(length(EmitterToSurface), 0.0001f);
        const float ForwardDist = dot(EmitterToSurface, Light.Direction);
        const float LateralX = abs(dot(EmitterToSurface, Light.Right));
        const float LateralY = abs(dot(EmitterToSurface, Up));
        // 옆으로 벌어진 각도가 frustum 안인가? (양수면 안쪽)
        const float SlackX = (ForwardDist * Light.BarnDoorSin - LateralX * Light.BarnDoorCos) / Distance;
        const float SlackY = (ForwardDist * Light.BarnDoorSin - LateralY * Light.BarnDoorCos) / Distance;
        // Slack이 −0.05보다 작으면 0
        // x,y 모두 닿아야 빛이 닿으므로 곱으로 마스크 만들기
        BarnDoorMask = smoothstep(-0.05f, 0.0f, SlackX) * smoothstep(-0.05f, 0.0f, SlackY);
    }

    const float3 Radiance = Light.Color * Light.Intensity * Attenuation * FrontFace * BarnDoorMask;
    return SurfaceShading(N, L, V, Radiance, InShininess);
}

// Type에 따라 Point / Spot / Rect로 나눠 계산한다
FLightContribution CalcLocalLight(FLightRenderParameters Light, float3 N, float3 V, float3 WorldPos, float InShininess)
{
    FLightContribution Result;
    if (Light.Type == LIGHT_TYPE_SPOT)
        Result = CalcSpotLight(Light, N, V, WorldPos, InShininess);
    else if (Light.Type == LIGHT_TYPE_RECT)
        Result = CalcRectLight(Light, N, V, WorldPos, InShininess);
    else
        Result = CalcPointLight(Light, N, V, WorldPos, InShininess);
    return Result;
}

// 픽셀이 속한 깊이 슬라이스 번호
uint GetSliceIndex(float4 SvPosition)
{
    float Slice;
    if (bOrthographic)
    {
        // 직교: SV_Position.z(0~1)가 Near~Far에 선형으로 대응
        Slice = SvPosition.z * NumSlices;
    }
    else
    {
        // 원근: SV_Position.w = 클립 공간 w = View 깊이(X)
        Slice = log(SvPosition.w) * SliceScale + SliceBias;
    }
    return (uint) clamp(Slice, 0.0f, NumSlices - 1.0f);
}

// 픽셀이 속한 셀(타일 x 슬라이스)의 목록이 CellLightIndices에서 시작하는 위치
uint GetCellListOffset(float4 SvPosition)
{
    const uint2 Tile = (uint2) SvPosition.xy / TileSize;
    const uint Slice = GetSliceIndex(SvPosition);
    return ((Slice * NumTilesY + Tile.y) * NumTilesX + Tile.x) * CELL_STRIDE;
}

// 라이트 개수를 검정 → 파랑 → 초록 → 빨강 → 흰색 히트맵으로
float3 GetHeatmapColor(uint LightCount)
{
    if (LightCount == 0)
        return float3(0, 0, 0);
    if (LightCount >= MAX_LIGHTS_PER_CELL)
        return float3(1, 1, 1);

    const float t = LightCount / (float) MAX_LIGHTS_PER_CELL;
    return saturate(float3(t * 2 - 0.5, 1 - abs(t * 2 - 1), 1.5 - t * 2));
}

// Directional + 로컬 라이트의 Diffuse/Specular를 모두 더한다. Ambient는 포함하지 않는다.
// bUseLightGrid = true면 SvPosition이 속한 셀의 라이트만, false면 전체 로컬 라이트를 순회한다.
// VS(Gouraud)는 픽셀 위치가 없어 셀을 쓸 수 없으므로 false로 호출한다.
FLightContribution AccumulateDirectLighting(float3 N, float3 V, float3 WorldPos, float InShininess, float4 SvPosition,
bool bUseLightGrid, bool bReceiveShadow)
{
    // Directional Light는 처음에 한 번만 계산
    FLightContribution Total = CalcDirectionalLight(N, V, InShininess);

#if defined(LIGHTING_MODEL_LAMBERT) || defined(LIGHTING_MODEL_BLINN_PHONG)
    if (bReceiveShadow)
    {
        // 로컬 라이트를 누적하기 전에 Directional의 직접광에만 그림자를 적용한다.
        const float Visibility = GetDirectionalShadowVisibility(WorldPos);
        Total.Diffuse *= Visibility;
        Total.Specular *= Visibility;
    }
#endif

    uint LightCount = NumLocalLights;
    uint CellOffset = 0;
    bool bUseCellList = false;
    if (bUseLightGrid && NumTilesX > 0 && LightDebugMode != LIGHT_DEBUG_MODE_BRUTE_FORCE)
    {
        CellOffset = GetCellListOffset(SvPosition);
        const uint CellLightCount = CellLightIndices[CellOffset];
        // 셀이 넘쳤으면(상한 이상) 목록이 잘렸으므로 전체 순회로 돌아간다
        if (CellLightCount < MAX_LIGHTS_PER_CELL)
        {
            LightCount = CellLightCount;
            bUseCellList = true;
        }
    }

    for (uint Index = 0; Index < LightCount; ++Index)
    {
        const uint LightIndex = bUseCellList ? CellLightIndices[CellOffset + 1 + Index] : Index;
        const FLightRenderParameters Light = LocalLights[LightIndex];
        FLightContribution LightContribution = CalcLocalLight(Light, N, V, WorldPos, InShininess);

#if defined(LIGHTING_MODEL_LAMBERT) || defined(LIGHTING_MODEL_BLINN_PHONG)
        if (bReceiveShadow)
        {
        // 해당 라이트의 직접광만 가린다. Ambient와 다른 라이트는 유지한다.
            const float Visibility = GetShadowVisibility(Light.ShadowIndex, WorldPos);
            LightContribution.Diffuse *= Visibility;
            LightContribution.Specular *= Visibility;
        }
#endif
        Total.Diffuse += LightContribution.Diffuse;
        Total.Specular += LightContribution.Specular;
    }

    return Total;
}

VS_OUTPUT mainVS(VS_INPUT Input)
{
    VS_OUTPUT Output;

    const float4 WorldPos = mul(float4(Input.Position, 1.0f), World);
    Output.SvPosition = mul(WorldPos, ViewProjection);

#if defined(LIGHTING_MODEL_UNLIT)
    // 현재 엔진은 정방향 깊이 0~1을 사용하며 Directional 직교 투영의 W는 1이다.
    if (ClampShadowNear != 0)
        Output.SvPosition.z = max(Output.SvPosition.z, 0.0f);
#endif
    Output.WorldPos = WorldPos.xyz;

    // 노멀은 역전치로, 탄젠트는 표면을 따라 놓인 방향이라 World로 변환한다.
    // World가 뒤집혀 있으면(행렬식 < 0) 바이탄젠트 부호도 뒤집는다.
    Output.Normal = mul(float4(Input.Normal, 0.0f), WorldInverseTranspose).xyz;
    Output.Tangent = float4(mul(float4(Input.Tangent.xyz, 0.0f), World).xyz, Input.Tangent.w * (determinant((float3x3) World) < 0.0f ? -1.0f : 1.0f));

    Output.UV = Input.UV;
    Output.Color = Input.Color;
#if defined(LIGHTING_MODEL_LAMBERT) || defined(LIGHTING_MODEL_BLINN_PHONG)
    // 기존 b2의 VS 전용 바인딩을 유지하면서 PS에 전달한다.
    Output.ReceivesShadow = ReceiveShadow;
#endif
#if LIGHTING_MODEL_GOURAUD
    const float3 V = normalize(CameraWorldPos - Output.WorldPos);
    const FLightContribution Contribution = AccumulateDirectLighting(normalize(Output.Normal), V, Output.WorldPos, 1, Output.SvPosition, false, false);
    Output.VertexDiffuse = Contribution.Diffuse;
#endif
    return Output;
}

float4 mainPS(VS_OUTPUT Input) : SV_TARGET
{
    const float2 UV = Input.UV + UVOffset;
    const float4 TexColor = DiffuseTexture.Sample(MaterialSampler, UV);
    float4 Albedo = TexColor * BaseColor;
#if !defined(EDITOR_ARROW_SHADER)
    // 씬은 HDR 타깃에 선형으로 쌓이므로 sRGB로 인코딩된 디퓨즈 텍스처와 BaseColor를 각각 선형으로 바꾼다.
    // 에디터 화살표는 톤 매핑 이후 LDR 타깃에 그려지므로 sRGB 그대로 둔다.
    Albedo.rgb = SRGBToLinear(TexColor.rgb) * SRGBToLinear(BaseColor.rgb);

    if (LightDebugMode == LIGHT_DEBUG_MODE_CELL_HEATMAP && NumTilesX > 0)
    {
        const uint CellLightCount = CellLightIndices[GetCellListOffset(Input.SvPosition)];
        return float4(lerp(Albedo.rgb * 0.2f, GetHeatmapColor(CellLightCount), 0.7f), 1.0f);
    }
    // 슬라이스 시각화: 이웃 슬라이스끼리 색이 크게 달라지도록 황금비로 색상환을 돈다.
    if (LightDebugMode == LIGHT_DEBUG_MODE_SLICES && NumTilesX > 0)
    {
        const float Hue = frac(GetSliceIndex(Input.SvPosition) * 0.618034f);
        const float3 SliceColor = saturate(abs(frac(Hue + float3(0.0f, 2.0f, 1.0f) / 3.0f) * 6.0f - 3.0f) - 1.0f);
        return float4(lerp(Albedo.rgb * 0.2f, SliceColor, 0.7f), 1.0f);
    }
#endif

    const float3 VertexNormal = { normalize(Input.Normal) };
#if defined(LIGHTING_MODEL_LAMBERT) || defined(LIGHTING_MODEL_BLINN_PHONG) || defined(WORLD_NORMAL)
    // TBN: 노멀맵의 탄젠트 공간 노멀을 월드 공간으로 옮긴다.
    const float3 T = { normalize(Input.Tangent.xyz - VertexNormal * dot(VertexNormal, Input.Tangent.xyz)) };
    const float3 B = { cross(VertexNormal, T) * (Input.Tangent.w < 0.0f ? -1.0f : 1.0f) };
    const float3 TangentNormal = { NormalTexture.Sample(MaterialSampler, UV).xyz * 2.0f - 1.0f };
    const float3 N = { normalize(T * TangentNormal.x + B * -TangentNormal.y + VertexNormal * TangentNormal.z) };
#endif

    // Opaque는 알파를 1로 고정한다. 뷰포트 RT를 ImGui가 알파 블렌딩으로 그리므로 알파가 남으면 비쳐 보인다
    const float Alpha = { bOpaque > 0.5f ? 1.0f : Albedo.a };

#if LIGHTING_MODEL_UNLIT
    return float4(Albedo.rgb, Alpha);
#elif WORLD_NORMAL
    return float4(N * 0.5f + 0.5f, 1.0f);
#else
    float3 DiffuseLighting = { GetAmbientLighting() };
    float3 SpecularLighting = { 0.0f, 0.0f, 0.0f };

#if defined(LIGHTING_MODEL_LAMBERT) || defined(LIGHTING_MODEL_BLINN_PHONG)
    const float3 V = { normalize(CameraWorldPos - Input.WorldPos) };
    const FLightContribution Contribution = { AccumulateDirectLighting(N, V, Input.WorldPos, Shininess, Input.SvPosition, true, Input.ReceivesShadow != 0) };
    DiffuseLighting += Contribution.Diffuse;
    SpecularLighting = Contribution.Specular;
#elif LIGHTING_MODEL_GOURAUD
    DiffuseLighting += Input.VertexDiffuse;
#endif

    // 먼저 기존 조명과 그림자를 모두 반영한 표면 색을 계산한다.
    float3 FinalColor = Albedo.rgb * DiffuseLighting + SpecularLighting;

#if defined(LIGHTING_MODEL_LAMBERT) || defined(LIGHTING_MODEL_BLINN_PHONG)
    if (VisualizeShadowCascades != 0 && Input.ReceivesShadow != 0)
    {
        // Directional의 깊이 비교 결과로만 마스크를 만든다.
        const float ShadowMask = saturate(1.0f - GetDirectionalShadowVisibility(Input.WorldPos));
        const float3 CascadeColor = GetDirectionalCascadeDebugColor(Input.WorldPos);

        // 그림자로 가려진 부분에만 색상을 섞고 기존 Alpha는 유지한다.
        FinalColor = lerp(FinalColor, CascadeColor, ShadowMask * 0.8f);
    }
#endif

    return float4(FinalColor, Alpha);
#endif
}
