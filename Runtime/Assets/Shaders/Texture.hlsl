Texture2D tex			: register(t0);
SamplerState texSampler : register(s0);

//= Constant Buffers ============
cbuffer MiscBuffer : register(b0)
{
	matrix mWorldViewProjection;
};

//= Structs =====================
struct VertexInputType
{
    float4 position : POSITION;
    float2 uv : TEXCOORD;
};

struct PixelInputType
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

//= Vertex Shader ================================================
PixelInputType DirectusVertexShader(VertexInputType input)
{
    PixelInputType output;
	
    input.position.w = 1.0f;
    output.position = mul(input.position, mWorldViewProjection);
    output.uv = input.uv;
	
    return output;
}

//= Pixel Shader =================================================
float4 DirectusPixelShader(PixelInputType input) : SV_TARGET
{
	return tex.Sample(texSampler, input.uv);
}