/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef SPARTAN_LIGHT_CLUSTER
#define SPARTAN_LIGHT_CLUSTER

#include "common_resources.hlsl"

// converts a screen uv plus view space z into a 3d cluster id
// uv is top-left origin, view_z is positive forward in left handed view space
uint3 cluster_id_from_screen(float2 uv, float view_z)
{
    uint cx = (uint)floor(saturate(uv.x) * (float)buffer_frame.cluster_count_x);
    uint cy = (uint)floor(saturate(uv.y) * (float)buffer_frame.cluster_count_y);

    float vz_clamped = max(view_z, buffer_frame.camera_near);
    int cz_signed    = (int)floor(log(vz_clamped) * buffer_frame.cluster_z_scale + buffer_frame.cluster_z_bias);
    uint cz          = (uint)max(0, cz_signed);

    return uint3(min(cx, buffer_frame.cluster_count_x - 1u),
                 min(cy, buffer_frame.cluster_count_y - 1u),
                 min(cz, buffer_frame.cluster_count_z - 1u));
}

// flattens a 3d cluster id to a linear slot index
uint cluster_flat(uint3 cluster_id)
{
    return cluster_id.x
         + cluster_id.y * buffer_frame.cluster_count_x
         + cluster_id.z * buffer_frame.cluster_count_x * buffer_frame.cluster_count_y;
}

// returns the view space near and far z planes for a cluster slice
void cluster_z_range(uint slice_z, out float z_near, out float z_far)
{
    float inv_scale = 1.0f / max(buffer_frame.cluster_z_scale, 1e-6f);
    z_near = exp(((float)slice_z       - buffer_frame.cluster_z_bias) * inv_scale);
    z_far  = exp(((float)slice_z + 1.0 - buffer_frame.cluster_z_bias) * inv_scale);
    z_near = max(z_near, buffer_frame.camera_near);
}

// builds the view space aabb of a cluster by unprojecting the tile corners onto the cluster z planes
// approximates the frustum-tilted cell with a conservative axis aligned box, good enough for sphere overlap
void cluster_view_space_aabb(uint3 cluster_id, out float3 aabb_min, out float3 aabb_max)
{
    float z_near, z_far;
    cluster_z_range(cluster_id.z, z_near, z_far);

    float2 uv_min = float2(cluster_id.x, cluster_id.y) / float2((float)buffer_frame.cluster_count_x, (float)buffer_frame.cluster_count_y);
    float2 uv_max = float2(cluster_id.x + 1u, cluster_id.y + 1u) / float2((float)buffer_frame.cluster_count_x, (float)buffer_frame.cluster_count_y);

    matrix proj_inv = get_projection_inverted();

    aabb_min = float3( 1e30f,  1e30f,  1e30f);
    aabb_max = float3(-1e30f, -1e30f, -1e30f);

    [unroll]
    for (int i = 0; i < 4; i++)
    {
        float2 uv_corner = float2((i & 1) ? uv_max.x : uv_min.x, (i & 2) ? uv_max.y : uv_min.y);
        // dx convention, uv top-left, ndc y up
        float2 ndc = float2(uv_corner.x * 2.0f - 1.0f, 1.0f - uv_corner.y * 2.0f);

        float4 view_h = mul(float4(ndc, 1.0f, 1.0f), proj_inv);
        float3 view_p = view_h.xyz / view_h.w;

        // build a ray from the origin with view space z normalized to one, then scale it to the cluster z planes
        float inv_z = 1.0f / max(view_p.z, 1e-6f);
        float3 ray  = view_p * inv_z;

        float3 p_near = ray * z_near;
        float3 p_far  = ray * z_far;

        aabb_min = min(aabb_min, min(p_near, p_far));
        aabb_max = max(aabb_max, max(p_near, p_far));
    }
}

// returns the squared distance from a point to an aabb, zero when the point is inside
float aabb_sq_distance_to_point(float3 aabb_min, float3 aabb_max, float3 p)
{
    float3 clamped = clamp(p, aabb_min, aabb_max);
    float3 d       = p - clamped;
    return dot(d, d);
}

// tests a sphere against an aabb, true when they overlap
bool sphere_intersects_aabb(float3 sphere_center, float radius, float3 aabb_min, float3 aabb_max)
{
    float sq = aabb_sq_distance_to_point(aabb_min, aabb_max, sphere_center);
    return sq <= radius * radius;
}

// tests a spot cone against a sphere, conservative, real time rendering 4th ch 22 cone vs sphere
// apex is the tip, dir the unit axis, range the slant length, cos_half and sin_half the half angle
bool cone_intersects_sphere(
    float3 apex,
    float3 dir,
    float  range,
    float  cos_half,
    float  sin_half,
    float3 sphere_center,
    float  sphere_radius)
{
    float3 v        = sphere_center - apex;
    float  axial    = dot(v, dir);

    // sphere fully past the cone base, no overlap
    if (axial - sphere_radius > range)
        return false;

    // sphere fully behind the apex, no overlap
    if (axial + sphere_radius < 0.0f)
        return false;

    // perpendicular distance from the sphere center to the cone axis
    float vv   = dot(v, v);
    float perp = sqrt(max(0.0f, vv - axial * axial));

    // signed perpendicular distance to the lateral cone surface, negative means inside
    // the early rejects above guarantee this is meaningful even when axial is slightly negative
    float cone_distance = cos_half * perp - sin_half * axial;

    return cone_distance <= sphere_radius;
}

// tests a spot cone against an aabb in two passes, the spherical falloff then the angular region
bool cone_intersects_aabb(
    float3 apex,
    float3 dir,
    float  range,
    float  cos_half,
    float  sin_half,
    float3 aabb_min,
    float3 aabb_max)
{
    if (!sphere_intersects_aabb(apex, range, aabb_min, aabb_max))
        return false;

    float3 aabb_center = 0.5f * (aabb_min + aabb_max);
    float  aabb_radius = 0.5f * length(aabb_max - aabb_min);
    return cone_intersects_sphere(apex, dir, range, cos_half, sin_half, aabb_center, aabb_radius);
}

#endif // SPARTAN_LIGHT_CLUSTER
