//= Constant Buffers ===============
cbuffer MiscBuffer : register(b0)
{
	matrix worldMatrix;
	matrix viewMatrix;
	matrix projectionMatrix;
};

//= Structs ========================
struct VertexInputType
{
    float4 position : POSITION;
    float4 color : COLOR;
};

struct PixelInputType
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

//= Vertex Shader ======================================================================================
PixelInputType DirectusVertexShader(VertexInputType input)
{
    PixelInputType output;
    
	// Calculate the world space position for a full-screen quad
    input.position.w = 1.0f;
    output.position = mul(input.position, worldMatrix);
	output.position = mul(output.position, viewMatrix);
	output.position = mul(output.position, projectionMatrix);
	
	output.color = input.color;
	
	return output;
}

//= Pixel Shader =======================================================================================
float4 DirectusPixelShader(PixelInputType input) : SV_TARGET
{
    return input.color;
}