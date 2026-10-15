#pragma once

#include "RenderPacket.h"
#include "Texture2D.h"
#include "Text/Font.h"

#include "RenderingInfo.h"
#include "ViewMode.h"
#include "Occlusion/GPUOcclusion.h"
#include "RenderEnums.h"
#include "Material.h"

constexpr uint32 ObjectSlotBytes = 256;

enum class ELightCullingMode : uint32 { Tiled, Clustered, Tiled25D };

struct FPerObjectConstants
{
	FMatrix World;
	// (World^-1)^T. 비균등 스케일에서도 노멀이 표면에 수직으로 남도록 노멀 변환에 쓴다.
	FMatrix NormalMatrix;
};

// 오브젝트 칸 하나를 채운다. 프록시는 WorldToLocal을 이미 들고 있어 역행렬 계산을 건너뛸 수 있다.
inline void WritePerObjectConstants(uint8* Dest, const FMatrix& World, const FMatrix& WorldToLocal)
{
	const FPerObjectConstants Constants{ World, WorldToLocal.GetTransposed() };
	std::memcpy(Dest, &Constants, sizeof(Constants));
}

// b0(ViewConstants)와 바이트 단위로 같아야 한다. View마다 RenderQueueSorting에서 한 번 올린다.
struct alignas(16) FViewConstants
{
	FMatrix ViewProjection;
	FVector CameraWorldPos;
	float Padding = 0.0f;
};
static_assert(sizeof(FViewConstants) == 80, "UberLit.hlsl cbuffer ViewConstants 크기와 맞아야 한다");

constexpr uint32 MaxLocalLights = 1024;

// 컬링 방식별 셀 크기. Tiled는 슬라이스 1개인 Clustered와 같다.
constexpr uint32 TiledTileSize = 16;
constexpr uint32 ClusteredTileSize = 32;
constexpr uint32 ClusteredSlices = 32;
// 셀 버퍼 한 칸 = [개수][라이트 번호 x MaxLightsPerCell]. 컬링 CS와 UberLit의 MAX_LIGHTS_PER_CELL과 같아야 한다.
constexpr uint32 MaxLightsPerCell = 32;
constexpr uint32 CellStride = MaxLightsPerCell + 1;

// UberLit.hlsl의 cbuffer Lighting과 바이트 단위로 같아야 한다. 모든 구조체를 16바이트 배수로 맞춘다.
struct FAmbientLightInfo
{
	FVector Color;
	float Intensity;
};

struct FDirectionalLightInfo
{
	FVector Direction;	// 빛이 향하는 방향
	float Intensity;
	FVector Color;
	float Padding;
};

struct FPointLightInfo
{
	FVector Position;
	float Radius;
	FVector Color;
	float Intensity;
	float FalloffExponent;
	float Padding[3];
};

struct FSpotLightInfo
{
	FVector Position;
	float Radius;
	FVector Color;
	float Intensity;
	FVector Direction;
	float InnerConeCos;
	float FalloffExponent;
	float OuterConeCos;
	float Padding[2];
};

struct FRectLightInfo
{
	FVector Position; float Radius;            	// 16
	FVector Color; float Intensity;         	// 32
	FVector Direction; float HalfWidth;         // 48  Direction = 로컬 +X (빛 진행 방향)
	FVector Right; float HalfHeight;        	// 64  Right = 로컬 +Y
	float BarnDoorCos; float BarnDoorSin;       
	float BarnDoorLength; float Padding;		// 80
};

struct alignas(16) FLightingConstants
{
	FAmbientLightInfo Ambient;
	FDirectionalLightInfo Directional;
	uint32 NumLocalLights;
	uint32 Padding[3];
};
static_assert(sizeof(FLightingConstants) == 64);

static_assert(sizeof(FAmbientLightInfo) == 16);
static_assert(sizeof(FDirectionalLightInfo) == 32);
static_assert(sizeof(FPointLightInfo) == 48);
static_assert(sizeof(FSpotLightInfo) == 64);
static_assert(sizeof(FRectLightInfo) == 80);

//static_assert(sizeof(FLightingConstants) == 512);	// UberLit.hlsl cbuffer Lighting 크기 (fxc 리플렉션 기준)

struct FSortEntry
{
	uint64 Key;
	uint32 PacketIndex;
};

class UCameraComponent;

// 오클루전 컬링의 효과 상한을 재기 위한 측정 결과 (디버그 전용)
struct FOcclusionMeasureResult
{
	bool bValid = false;
	uint32 TotalObjects = 0;      // 불투명 패스에 제출된 물체 수
	uint32 VisibleObjects = 0;    // 최종 깊이 버퍼에 픽셀을 1개 이상 남긴 물체 수
	uint32 TotalDraws = 0;
	uint32 VisibleDraws = 0;
	uint64 TotalTriangles = 0;
	uint64 VisibleTriangles = 0;
	double ElapsedMs = 0.0;       // 측정 자체에 걸린 시간 (GPU 대기 포함)
	// GPU 오클루전 검증. 판정만 하고 거르지 않은(Cull 끔) 상태에서 측정해야 의미가 있다.
	uint32 GPUOccludedDraws = 0;  // GPU가 가렸다고 판정한 드로우 수
	uint32 FalseCulls = 0;        // 그중 실제로는 픽셀이 보인 드로우 수. 0이어야 한다
};

struct alignas(16) FFireBallLight
{
	FVector4 PositionRadius; // xyz: 위치, w: 반경
	FVector4 ColorIntensity; // rgb: 색상, w: 강도
	FVector4 FalloffEnabled; // x: 반경 감쇠, y: 사용 여부, zw: 패딩
};

struct alignas(16) FLightCullingConstants
{
	FMatrix View;
	FMatrix Projection;
	FVector2 ViewportSize;
	uint32 NumTilesX;
	uint32 NumTilesY;
	uint32 NumLocalLights;
	uint32 NumSlices;
	uint32 TileSize;
	uint32 bOrthographic;
	float NearZ;
	float FarZ;
	float Padding[2];
};
static_assert(sizeof(FLightCullingConstants) == 176, "컬링 CS의 cbuffer FLightCullingConstants와 같아야 한다");

struct alignas(16) FLightGridConstants
{
	uint32 NumTilesX;
	uint32 NumTilesY;
	uint32 NumSlices;
	uint32 LightDebugMode;
	float  NearZ;
	float  FarZ;
	float  SliceScale;
	float  SliceBias;
	uint32 TileSize;
	uint32 bOrthographic;
	uint32 Padding[2];
};
static_assert(sizeof(FLightGridConstants) == 48, "UberLit의 cbuffer LightGrid와 같아야 한다");

struct FLightRenderParameters;

class FRenderer
{
public:
	bool Init();

	// 기존 단일 카메라의 ViewProjection으로 렌더 큐 전체를 그린다.
	void RenderAll(FRenderQueue& InQueue, UCameraComponent* CameraComponent);

	// Adapter가 계산한 ViewProjection을 직접 받아 View별 렌더 큐를 그린다.
	void RenderAll(FRenderQueue& InQueue, const FMatrix& ViewProjection, const FVector& CameraWorldPos);

	// 큐를 정렬해 불투명 패킷만 그린다. 반투명은 RenderTranslucent 호출 전까지 보관한다.
	void RenderOpaque(const FMatrix& ViewProjection);

	// Gather 전에 World가 불러 가려진 물체를 거른다.
	FGPUOcclusion& GetGPUOcclusion() { return GPUOcclusion; }

	// 캐시된 스태틱 메시 경로: World::Gather가 조각별로 채운 묶음을 이번 프레임 불투명 패스에 넘긴다.
	// 묶음 메모리는 World 소유이며 다음 Gather 전까지 유효하다. RenderQueueSorting이 묶음을 정렬하고 RenderOpaque가 그린다.
	void ResetStaticDrawGroups() { StaticGroups.clear(); }
	void AddStaticDrawGroup(const FStaticDrawGroup* Group) { StaticGroups.push_back(Group); }

	// RenderOpaque가 보관한 반투명 패킷을 먼 것부터 그린다.
	void RenderTranslucent(const FMatrix& ViewProjection);

	// 큐를 Material, Opaque, Mesh에 따라 정렬한다
	// CameraWorldPos는 ViewProjection과 함께 b0에 올라가 Specular 계산(V 벡터)에 쓰인다.
	void RenderQueueSorting(FRenderQueue& InQueue, const FMatrix& ViewProjection, const FVector& CameraWorldPos);

	// Depth Prepass: 불투명만 PS 없이 그려 깊이 버퍼를 채운다. RenderQueueSorting 뒤, RenderOpaque 전에 부른다.
	// 같은 VS·행렬이라 깊이가 비트 단위로 같으므로 본 패스는 LESS_EQUAL로 통과한다.
	void RenderDepth(const FMatrix& ViewProjection);

	// [측정 전용] RenderOpaque 직후에 호출한다. 불투명 패킷을 깊이 LESS_EQUAL·색 쓰기 없이 다시 그리며
	// 패킷마다 오클루전 쿼리를 걸어, 최종 화면에 실제로 픽셀을 남긴 물체 수를 센다.
	// GPU 결과를 기다리므로 매우 느리다. 버튼 등으로 한 프레임만 실행할 것.
	FOcclusionMeasureResult MeasureOpaqueOcclusion(const FMatrix& ViewProjection);

	uint8* BeginObjectConstants(uint32 MaxSlots);
	void EndObjectConstants();

	//void SetFireBallLight(const FFireBallLightConstants& LightConstants);
	void SetLighting(FLightingConstants& LightingConstants, const TArray<FLightRenderParameters>& InLocalLights);

	// 이후 그리는 UberLit 머티리얼이 쓸 조명 모델. View마다 RenderOpaque 전에 설정한다.
	void SetViewMode(EViewMode InViewMode);
	EViewMode GetViewMode() const { return CurrentViewMode; }

	// DepthTexture는 Tiled25D에서만 쓴다. 이때는 DSV에서 풀린 상태(RenderDepth 뒤 EndRenderPass)여야 한다.
	void CullLights(const FMatrix& View, const FMatrix& Projection, uint32 Width, uint32 Height, FTexture2D* DepthTexture = nullptr);

	// public
	void SetLightDebugMode(uint32 Mode);
	void SetLightCullingMode(ELightCullingMode Mode) { LightCullingMode = Mode; }
	ELightCullingMode GetLightCullingMode() const { return LightCullingMode; }
private:
	void DrawPackets(uint32 Begin, uint32 End, const FMatrix& ViewProjection);
	void DrawStaticGroups();
	void UpdatePerObjectConstants(const FMatrix& World);
	void BindMaterial(UMaterial* Material);
	void UpdateMaterialParams(const FRenderPacket& RenderPacket);
	void UpdatePerObjectConstants(const FRenderPacket& RenderPacket, const FMatrix& ViewProjection);
	void EnsurePerObjectSlotCapacity(uint32 SlotCount);
	void UploadPerObjectConstants();

private:
	// private
	TUniquePtr<FConstantBuffer> GridLightingCB{};
	// NumTilesX = 0인 b5. 2.5D 목록은 불투명 깊이 기준이라 반투명은 이것으로 전체 순회한다.
	TUniquePtr<FConstantBuffer> GridLightingFallbackCB{};
	uint32 LightDebugMode{};
	ELightCullingMode LightCullingMode = ELightCullingMode::Clustered;
	// RenderDepth 동안 true. BindMaterial이 PS를 끈다.
	bool bDepthOnly = false;

	// FIFO 소비용 배열의 용량만 재사용하며 매 View의 패킷 값은 새로 채운다.
	FRenderQueue RenderPackets;
	// 정렬된 RenderPackets에서 반투명 패킷이 시작되는 위치
	uint32 FirstTranslucentIndex = 0;
	uint32 FirstMaterialIndex = 0;
	TUniquePtr<FConstantBuffer> PerObjectCB;
	TUniquePtr<FConstantBuffer> ViewCB;

	// 모든 패킷의 World 행렬을 256바이트 칸에 한 번에 올린 버퍼. D3D11.1 오프셋 바인딩을 못 쓰면 PerObjectCB로 돌아간다.
	TUniquePtr<FConstantBuffer> PerObjectSlotCB;
	uint32 PerObjectSlotCapacity = 0;
	bool bUsePerObjectSlots = false;
	bool bObjectConstantsPrepared = false;
	UMaterial* LastMaterial;
	UStaticMesh* LastMesh;

	// MeasureOpaqueOcclusion 전용. 처음 측정할 때 만들고 이후 재사용한다.
	TArray<ComPtr<ID3D11Query>> OcclusionQueries;
	ComPtr<ID3D11DepthStencilState> DepthLessEqualReadOnly;

	FGPUOcclusion GPUOcclusion;

	// 이번 프레임 스태틱 메시 묶음 (정렬 키 순). 비어 있지 않은 것만 담는다.
	std::vector<const FStaticDrawGroup*> StaticGroups;

	TArray<FSortEntry> SortEntries;

	// TUniquePtr<FConstantBuffer> FireBallLightCB;
	// FFireBallLightConstants FireBallLightData{};

	// UberLit의 b4. FireBall이 b3를 쓰는 동안은 슬롯을 나눠 둔다.
	TUniquePtr<FConstantBuffer> LightingCB;
	TUniquePtr<FConstantBuffer> LightCullingCB{};

	TUniquePtr<FStructuredBuffer> TileIndexBuffer{};
	TUniquePtr<FStructuredBuffer> LocalLightBuffer{};
	uint32 NumLocalLightsThisFrame = 0;

	EViewMode CurrentViewMode = EViewMode::Lit_Lambert;
};
