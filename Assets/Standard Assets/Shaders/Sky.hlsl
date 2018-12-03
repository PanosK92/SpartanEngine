#define INV_PI 1.0 / 3.14159;

float2 DirectionToSphereUV(float3 direction)
{
    float n = length(direction.xz);

    float2 uv = float2((n > 0.0000001) ? direction.x / n : 0.0, direction.y);
    uv = acos(uv) * INV_PI;
    uv.x = (direction.z > 0.0) ? uv.x * 0.5 : 1.0 - (uv.x * 0.5);
    uv.x = 1.0 - uv.x;
	
    return uv;
}