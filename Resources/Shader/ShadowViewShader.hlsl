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
    float4 WorldPos = mul(float4(Input.Position, 1.0f), WorldMatrix);
    float4 Output = mul(WorldPos, LightSpaceMatrix);
    return Output;
}

void mainPS(PSInput Input)
{
    
}
