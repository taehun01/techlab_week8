#pragma pack_matrix(row_major) 

cbuffer FShadowViewConstants : register(b0)
{
    float4x4 LightSpaceMatrix;
    float4x4 WorldMatrix;
};

struct VSInput
{
    float4 Position : POSITION;
};

struct PSInput
{
    float4 Position : POSITION;
};

float4 mainVS(VSInput Input) : SV_Position
{  
    float4 WorldPos = mul(Input.Position, WorldMatrix);
    float4 Output = mul(WorldPos, LightSpaceMatrix);
    return Output;
}

void mainPS(PSInput Input) : SV_Target
{
    
}
