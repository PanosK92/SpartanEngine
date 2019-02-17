struct Vertex_Pos
{
    float4 position : POSITION0;
};

struct Vertex_PosUv
{
    float4 position : POSITION0;
    float2 uv 		: TEXCOORD0;
};

struct Vertex_PosColor
{
    float4 position : POSITION0;
    float4 color 	: COLOR0;
};

struct Vertex_PosUvNorTan
{
	float4 position 	: POSITION0;
    float2 uv 			: TEXCOORD0;
    float3 normal 		: NORMAL0;
    float3 tangent		: TANGENT0;
};