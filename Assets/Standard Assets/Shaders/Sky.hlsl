static const float2 invAtan = float2(0.1591f, 0.3183f);

float2 DirectionToSphereUV(float3 direction)
{
    float2 uv = float2(atan2(direction.z, direction.x), asin(-direction.y));
    uv *= invAtan;
    uv += 0.5f;
    return uv;
}