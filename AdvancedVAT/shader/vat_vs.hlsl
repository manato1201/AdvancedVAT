cbuffer global : register(b0)
{
    matrix Projection;
    matrix ModelView;
    matrix World;
    float4 Params; // x=width, y=height, z=frame, w=amp
};
Texture2D<float4> Vat : register(t0);
struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal : TEXCOORD1;
};
VS_OUTPUT main(float3 Pos : POSITION, uint vid : SV_VertexID)
{
    VS_OUTPUT output = (VS_OUTPUT) 0;
    const uint width = uint(Params.x);
    const uint height = uint(Params.y);
    const uint frame = min(uint(Params.z), height - 1);
    const float amp = Params.w;
    float4 vat = Vat.Load(int3(min(vid, width - 1), frame, 0));
    // vat.w ÇÕ ÅgäÓèÄêUïùÅh Çä‹ÇÒÇæçÇÇ≥ÅBamp(âπó )Ç≈ëùå∏Ç≥ÇπÇÈÅB
    float4 position = float4(Pos.x, Pos.y + vat.w * amp, Pos.z, 1.0);
    float4 worldPos = mul(position, World);
    output.WorldPos = worldPos.xyz;
    output.Normal = normalize(mul(float4(vat.xyz, 0), World).xyz);
    output.Position = mul(worldPos, mul(ModelView, Projection));
    return output;
}