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
    float4 color 	: COLOR;
};


struct Vertex_PosUvTbn
{
	float4 position 	: POSITION0;
    float2 uv 			: TEXCOORD0;
    float3 normal 		: NORMAL;
    float3 tangent		: TANGENT;
	float3 bitangent 	: BITANGENT;
};