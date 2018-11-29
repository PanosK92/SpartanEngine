cbuffer GlobalBuffer : register(b0)
{	
	matrix mView;
	matrix mProjection;
	float3 cameraPosWS;
	float nearPlane;
    float farPlane;
    float2 resolution;
};