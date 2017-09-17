Texture2D textureAtlas 	: register(t0);
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
	float4 color = float4(0.0f, 0.0f, 0.0f, 1.0f);
	
	color.r = textureAtlas.Sample(texSampler, input.uv).r;
	color.g = color.r;
	color.b = color.r;
	color.a = color.r;
	
	return color;
}