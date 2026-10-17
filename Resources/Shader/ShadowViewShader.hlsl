#pragma pack_matrix(row_major) 

cbuffer FShadowViewConstants : register(b0)
{
    float4x4 LightSpaceMatrix;
    float4x4 WorldMatrix;
};

struct VSInput
{
    float3 Position : POSITION;
};

struct PSInput
{
    float4 Position : POSITION;
};

float4 mainVS(VSInput Input) : SV_Position
{  
    float4 WorldPos = mul(float4(Input.Position, 1.0f), WorldMatrix);   // Local to World Space
    float4 Output = mul(WorldPos, LightSpaceMatrix);    // World Space에서 LightSpaceMatrix 곱을 통해 LightSpace에서의 좌표로 변환
    return Output;  // DSV 세팅으로 인해 가까운 Depth만 남음
}

void mainPS(PSInput Input)
{
    
}
