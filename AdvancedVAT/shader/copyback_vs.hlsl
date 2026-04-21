cbuffer global : register(b0)
{
    matrix WorldMVP;
};
struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float2 Texcoord : TEXCOORD;
};
VS_OUTPUT main(float3 Pos : POSITION, float2 UV : TEXCOORD0)
{
    VS_OUTPUT output = (VS_OUTPUT) 0;
    float4 worldPos = mul(float4(Pos, 1.0), WorldMVP);
    output.Position = worldPos;
    output.Texcoord = UV;
    return output;
}