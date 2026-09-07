// Compact traversal result shared by reflection and path tracing libraries.
#ifndef SPARTAN_COMMON_RAY_HIT
#define SPARTAN_COMMON_RAY_HIT

// Four 32-bit words (16 bytes). Surface/material state stays in the caller.
// Hit/miss debug visualization needs only the 4-byte distance.
struct [raypayload] HitPayload
{
    float hit_distance       : read(caller) : write(closesthit, miss);
#if DEBUG_RAY_TRACING != 1
    uint instance_index      : read(caller) : write(closesthit, miss);
    uint primitive_index     : read(caller) : write(closesthit, miss);
    uint barycentrics_packed  : read(caller) : write(closesthit, miss);
#endif
};

float3 unpack_hit_barycentrics(uint packed)
{
    float2 uv = float2(f16tof32(packed & 0xffffu), f16tof32(packed >> 16));
    // Independent half rounding can move an edge sample just outside the triangle.
    float3 bary = float3(max(0.0f, 1.0f - uv.x - uv.y), uv);
    return bary / (bary.x + bary.y + bary.z);
}

[shader("closesthit")]
void closest_hit(inout HitPayload payload : SV_RayPayload, in BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes)
{
    payload.hit_distance      = RayTCurrent();
#if DEBUG_RAY_TRACING != 1
    payload.instance_index    = InstanceIndex();
    payload.primitive_index   = PrimitiveIndex();
    payload.barycentrics_packed = f32tof16(attribs.barycentrics.x) | (f32tof16(attribs.barycentrics.y) << 16);
#endif
}

[shader("miss")]
void miss(inout HitPayload payload : SV_RayPayload)
{
    payload.hit_distance      = -1.0f;
#if DEBUG_RAY_TRACING != 1
    payload.instance_index    = 0u;
    payload.primitive_index   = 0u;
    payload.barycentrics_packed = 0u;
#endif
}

#endif
