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

#ifndef SPARTAN_COMMON_CULLING
#define SPARTAN_COMMON_CULLING

// shared gpu culling primitives, the hi-z helpers take the hi-z texture as a parameter so the meshlet cull
// can pass its occluder hi-z (tex) and grass populate can pass the same occluder hi-z on a different slot (tex2)

// Conservative envelope for tree rotations (under 4 degrees combined) and
// centimetre leaf detail. Includes distance from the root for canopy-only bounds.
float tree_wind_cull_padding(float3 center, float radius, float3 root)
{
    return (length(center - root) + radius) * 0.07f + 0.03f;
}

// extracts the four side planes of the camera frustum from view_projection in world space
// only the side planes are used, near is unreliable on jittered projections and far is at infinity for reverse-z
// row-vector convention places the camera world-space planes on the columns of view_projection
void get_frustum_side_planes(out float4 plane_l, out float4 plane_r, out float4 plane_b, out float4 plane_t)
{
    matrix vp = buffer_frame.view_projection;
    plane_l = float4(vp._m00 + vp._m03, vp._m10 + vp._m13, vp._m20 + vp._m23, vp._m30 + vp._m33);
    plane_r = float4(vp._m03 - vp._m00, vp._m13 - vp._m10, vp._m23 - vp._m20, vp._m33 - vp._m30);
    plane_b = float4(vp._m01 + vp._m03, vp._m11 + vp._m13, vp._m21 + vp._m23, vp._m31 + vp._m33);
    plane_t = float4(vp._m03 - vp._m01, vp._m13 - vp._m11, vp._m23 - vp._m21, vp._m33 - vp._m31);

    // normalize so the radius compare lives in world units, the planes get reused for every task in the wave so doing it once is fine
    plane_l /= max(length(plane_l.xyz), 1e-8f);
    plane_r /= max(length(plane_r.xyz), 1e-8f);
    plane_b /= max(length(plane_b.xyz), 1e-8f);
    plane_t /= max(length(plane_t.xyz), 1e-8f);
}

bool sphere_in_side_planes(float3 center, float radius, float4 plane_l, float4 plane_r, float4 plane_b, float4 plane_t)
{
    float dl = dot(plane_l.xyz, center) + plane_l.w;
    float dr = dot(plane_r.xyz, center) + plane_r.w;
    float db = dot(plane_b.xyz, center) + plane_b.w;
    float dt = dot(plane_t.xyz, center) + plane_t.w;
    float min_dist = min(min(dl, dr), min(db, dt));
    return min_dist >= -radius;
}

// Expand in base-level pixels BEFORE selecting a mip. The resulting rectangle
// spans at most two cells on either axis, so four integer loads cover every cell.
// Expanding by a mip texel after choosing the level can skip interior cells.
float hiz_min_depth_over_box(Texture2D hiz_tex, float2 min_uv, float2 max_uv, float max_mip_level)
{
    uint width, height;
    hiz_tex.GetDimensions(width, height);
    float2 size = float2(width, height);
    float2 uv_scale = get_render_uv_scale();
    min_uv = saturate(min_uv * uv_scale - 1.0f / size);
    max_uv = saturate(max_uv * uv_scale + 1.0f / size);
    float2 size_px = (max_uv - min_uv) * size;
    uint mip = (uint)clamp(ceil(log2(max(max(size_px.x, size_px.y), 1.0f))), 0.0f, max_mip_level);

    uint mip_width, mip_height, levels;
    hiz_tex.GetDimensions(mip, mip_width, mip_height, levels);
    uint2 mip_size = uint2(mip_width, mip_height);
    uint2 lo = min((uint2)(min_uv * mip_size), mip_size - 1u);
    uint2 hi = min((uint2)(max_uv * mip_size), mip_size - 1u);
    float d0 = hiz_tex.Load(int3(lo, mip)).r;
    float d1 = hiz_tex.Load(int3(hi.x, lo.y, mip)).r;
    float d2 = hiz_tex.Load(int3(lo.x, hi.y, mip)).r;
    float d3 = hiz_tex.Load(int3(hi, mip)).r;
    return min(min(d0, d1), min(d2, d3));
}

// projection status for a world space sphere against the current view projection
#define SPHERE_PROJECT_BEHIND   0u
#define SPHERE_PROJECT_STRADDLE 1u
#define SPHERE_PROJECT_VALID    2u

// conservative ndc bounds for a world-space sphere
// projects the center once and derives the rectangle from the row-axis sensitivities of view_projection
// this replaces the per-thread 8-corner cube projection in the meshlet path, the bound is also tighter than the cube
// projection that surrounds the sphere, so it culls more aggressively and chooses a smaller hi-z footprint
// shared by the hi-z and contribution tests so both agree on the projected footprint
uint sphere_project_ndc(float3 center_world, float radius_world, out float2 min_ndc, out float2 max_ndc, out float closest_z)
{
    min_ndc   = 0.0f;
    max_ndc   = 0.0f;
    closest_z = 0.0f;

    matrix vp = buffer_frame.view_projection;

    // partials of clip-space output w.r.t. world-space input, wave uniform so dxc keeps these in scalar registers
    float3 ax_x = float3(vp._m00, vp._m10, vp._m20);
    float3 ax_y = float3(vp._m01, vp._m11, vp._m21);
    float3 ax_z = float3(vp._m02, vp._m12, vp._m22);
    float3 ax_w = float3(vp._m03, vp._m13, vp._m23);

    float cx = dot(center_world, ax_x) + vp._m30;
    float cy = dot(center_world, ax_y) + vp._m31;
    float cz = dot(center_world, ax_z) + vp._m32;
    float cw = dot(center_world, ax_w) + vp._m33;

    float rx = radius_world * length(ax_x);
    float ry = radius_world * length(ax_y);
    float rz = radius_world * length(ax_z);
    float rw = radius_world * length(ax_w);

    // sphere entirely behind the camera
    if (cw + rw <= 0.0f)
        return SPHERE_PROJECT_BEHIND;

    // sphere straddles the near plane, the perspective divide is unstable there
    if (cw - rw <= 0.0f)
        return SPHERE_PROJECT_STRADDLE;

    // each ndc extreme is one of four (numerator extreme) * (1 / denominator extreme), enumerate and reduce
    float inv_w_close = 1.0f / (cw - rw);
    float inv_w_far   = 1.0f / (cw + rw);

    float xlc = (cx - rx) * inv_w_close;
    float xlf = (cx - rx) * inv_w_far;
    float xhc = (cx + rx) * inv_w_close;
    float xhf = (cx + rx) * inv_w_far;
    float ylc = (cy - ry) * inv_w_close;
    float ylf = (cy - ry) * inv_w_far;
    float yhc = (cy + ry) * inv_w_close;
    float yhf = (cy + ry) * inv_w_far;

    min_ndc = float2(min(min(xlc, xlf), min(xhc, xhf)), min(min(ylc, ylf), min(yhc, yhf)));
    max_ndc = float2(max(max(xlc, xlf), max(xhc, xhf)), max(max(ylc, ylf), max(yhc, yhf)));

    // closest sphere depth in reverse-z is max numerator over min positive denominator
    closest_z = (cz + rz) * inv_w_close;

    return SPHERE_PROJECT_VALID;
}

// fast analytical hi-z for a world-space sphere
bool sphere_hiz_visible(Texture2D hiz_tex, float3 center_world, float radius_world, float max_mip_level, float bias_floor = 2e-7f)
{
    float2 min_ndc, max_ndc;
    float  closest_box_z;
    uint   status = sphere_project_ndc(center_world, radius_world, min_ndc, max_ndc, closest_box_z);

    // the side-frustum has already rejected the behind case so that branch is paranoia
    if (status == SPHERE_PROJECT_BEHIND)
        return false;

    // straddling the near plane skips occlusion conservatively
    if (status == SPHERE_PROJECT_STRADDLE)
        return true;

    if (max_ndc.x < -1.0f || min_ndc.x > 1.0f || max_ndc.y < -1.0f || min_ndc.y > 1.0f)
        return false;

    float2 uv_a   = saturate(ndc_to_uv(min_ndc));
    float2 uv_b   = saturate(ndc_to_uv(max_ndc));
    float2 min_uv = min(uv_a, uv_b);
    float2 max_uv = max(uv_a, uv_b);

    float furthest_z = hiz_min_depth_over_box(hiz_tex, min_uv, max_uv, max_mip_level);
    // Reverse-Z shrinks with distance: a fixed 0.01 bias made occluders beyond
    // about 10 m ineffective with the default 0.1 m near plane. Retain a small
    // relative/absolute precision margin and keep equal-depth surfaces.
    float depth_bias = max(bias_floor, abs(closest_box_z) * 1e-4f);
    return closest_box_z >= furthest_z - depth_bias;
}

// contribution thresholds in pixels
// meshes and meshlets are tuned apart because meshlet screen size is far more uniform than mesh screen size
// the meshlet threshold stays the lower of the two, over-culling meshlets punches sub-pixel holes in solid surfaces
// while over-culling an instance only removes a speck
#define CULL_CONTRIBUTION_MESH_PX    2.0f
#define CULL_CONTRIBUTION_MESHLET_PX 1.0f

// contribution cull, rejects a sphere whose projected footprint is thinner than min_extent_pixels on both axes
// min_extent_pixels <= 0 disables the test, spheres crossing the near plane always contribute
bool sphere_contributes(float3 center_world, float radius_world, float min_extent_pixels)
{
    if (min_extent_pixels <= 0.0f)
        return true;

    float2 min_ndc, max_ndc;
    float  closest_z;
    uint   status = sphere_project_ndc(center_world, radius_world, min_ndc, max_ndc, closest_z);

    if (status == SPHERE_PROJECT_BEHIND)
        return false;

    if (status == SPHERE_PROJECT_STRADDLE)
        return true;

    // ndc spans two units across the viewport, so half the resolution converts an ndc extent into pixels
    float2 extent_pixels = (max_ndc - min_ndc) * get_render_resolution_active() * 0.5f;
    return max(extent_pixels.x, extent_pixels.y) >= min_extent_pixels;
}

// same screen-height fractions as Render::UpdateLodIndices, near-plane straddlers keep lod 0
uint sphere_lod_index(float3 center_world, float radius_world, uint lod_count)
{
    if (lod_count <= 1u)
    {
        return 0u;
    }

    float2 min_ndc, max_ndc;
    float  closest_z;
    uint   status = sphere_project_ndc(center_world, radius_world, min_ndc, max_ndc, closest_z);

    float screen_fraction = 1.0f;
    if (status == SPHERE_PROJECT_BEHIND)
    {
        screen_fraction = 0.0f;
    }
    else if (status == SPHERE_PROJECT_VALID)
    {
        float2 extent_ndc = (max_ndc - min_ndc) * 0.5f;
        screen_fraction   = max(extent_ndc.x, extent_ndc.y);
    }

    uint lod = lod_count - 1u;
    if (screen_fraction >= 0.05f)
    {
        lod = 0u;
    }
    else if (screen_fraction >= 0.025f)
    {
        lod = 1u;
    }
    else if (screen_fraction >= 0.012f)
    {
        lod = 2u;
    }
    else if (screen_fraction >= 0.006f)
    {
        lod = 3u;
    }
    else
    {
        lod = 4u;
    }

    return min(lod, lod_count - 1u);
}

// largest world-axis scale of the upper 3x3, used to lift a local-space radius into world units
// computes squared lengths first and only sqrt the winner, shaves two of the three sqrts in the hot per-task loop
float max_world_scale(float4x4 m)
{
    float3 r0 = float3(m._m00, m._m01, m._m02);
    float3 r1 = float3(m._m10, m._m11, m._m12);
    float3 r2 = float3(m._m20, m._m21, m._m22);
    float sx_sq = dot(r0, r0);
    float sy_sq = dot(r1, r1);
    float sz_sq = dot(r2, r2);
    return sqrt(max(sx_sq, max(sy_sq, sz_sq)));
}

#endif // SPARTAN_COMMON_CULLING
