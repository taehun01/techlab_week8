#pragma pack_matrix(row_major)

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
};

PSInput mainVS(uint VertexID : SV_VertexID)
{
    PSInput Output;
    float2 UV = float2((VertexID << 1) & 2, VertexID & 2); // (0,0) (2,0) (0,2)
    Output.Position = float4(UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 1.0f, 1.0f);
    Output.UV = UV;
    return Output;
}

void mainPS()
{
}