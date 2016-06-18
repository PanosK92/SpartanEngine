/*------------------------------------------------------------------------------
								[BUFFERS]
------------------------------------------------------------------------------*/
cbuffer MiscBuffer : register(b0)
{
	matrix mWorldViewProjection;
};

/*------------------------------------------------------------------------------
								[STRUCTS]
------------------------------------------------------------------------------*/
struct VertexInputType
{
    float4 position : POSITION;
};

struct PixelInputType
{
    float4 position : SV_POSITION;
};

/*------------------------------------------------------------------------------
									[VS]
------------------------------------------------------------------------------*/
PixelInputType DirectusVertexShader(VertexInputType input)
{
	PixelInputType output;
     
    input.position.w = 1.0f;
    output.position = mul(input.position, mWorldViewProjection);
	
	return output;
}

/*------------------------------------------------------------------------------
									[PS]
------------------------------------------------------------------------------*/
float4 DirectusPixelShader(PixelInputType input) : SV_TARGET
{
	return input.position.z / input.position.w;
}