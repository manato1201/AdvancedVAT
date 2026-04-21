cbuffer global : register(b0)
{
	matrix Projection;
	matrix ModelView;
	matrix World;
};

struct VS_OUTPUT
{
	float4 Position : SV_POSITION;
	float3 WorldPos : TEXCOORD0;
	float3 Normal : TEXCOORD1;
	float2 UV : TEXCOORD2;
};

VS_OUTPUT main(float3 Pos : POSITION, float3 Normal : NORMAL, float2 UV : TEXCOORD)
{
	VS_OUTPUT output = (VS_OUTPUT)0;
	float4 worldPos = mul(float4(Pos, 1.0), World);
	output.WorldPos = worldPos.xyz;
	output.Normal = normalize(mul(float4(Normal, 0.0), World).xyz);
    output.UV = UV;
	output.Position = mul(worldPos, mul(ModelView, Projection));
	return output;
}
