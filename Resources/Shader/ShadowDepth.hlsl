#pragma pack_matrix(row_major)

// Spot Light 그림자용 Depth 전용 셰이더. FRenderer::RenderShadowDepths가 슬라이스 DSV 하나에 깊이만 그린다.

// Renderer.h의 FViewConstants와 같은 배치. ViewProjection 자리에 라이트 VP(FShadowViewInfo::ViewProjection)가 들어온다.
cbuffer ViewConstants : register(b0)
{
    matrix ViewProjection;
    float3 CameraWorldPos;
    float ViewPadding;
};

// Renderer.h의 FPerObjectConstants 앞부분(World)만 읽는다. 뒤의 노멀 행렬은 쓰지 않는다.
cbuffer PerObjectConstants : register(b2)
{
    matrix World;
};

// UberLit의 VS_INPUT과 같게 둔다. Input Layout이 VS 입력 리플렉션 + APPEND_ALIGNED로 만들어지므로 정점 배치를 맞춘다.
struct VS_INPUT
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float4 Tangent : TANGENT; // w = 바이탄젠트 부호
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
};

float4 mainVS(VS_INPUT Input) : SV_POSITION
{
    const float4 WorldPos = mul(float4(Input.Position, 1.0f), World);
    return mul(WorldPos, ViewProjection);
}

// RenderCommand::BindShaderProgram이 PS를 요구해서 둔 빈 PS. 실제로는 바인딩 직후 PSSetShader(nullptr)로 끈다.
// 알파 테스트 머티리얼이 생기면 UV를 받아 clip()하는 PS로 바꾼다.
void mainPS()
{
}
