Texture2D textureAtlas 	: register(t0);
SamplerState texSampler : register(s0);

//= Constant Buffers ============
cbuffer MiscBuffer : register(b0)
{
	matrix mWorldViewProjection;
	float4 color;
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
	float4 finalColor = float4(0.0f, 0.0f, 0.0f, 1.0f);
	
	// Sample text from texture atlas
	finalColor.r = textureAtlas.Sample(texSampler, input.uv).r;
	finalColor.g = finalColor.r;
	finalColor.b = finalColor.r;
	finalColor.a = finalColor.r;
	
	// Color it
	finalColor *= color;
	
	return finalColor;
}