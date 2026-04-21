Texture2D<float4> g_texColor : register(t0);
SamplerState g_samplerLinear : register(s0);
struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float2 Texcoord : TEXCOORD;
};
float4 main(VS_OUTPUT input) : SV_Target
{
    return g_texColor.Sample(g_samplerLinear, input.Texcoord);
}