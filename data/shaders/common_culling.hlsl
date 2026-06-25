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

// shared mip pick + 4-corner depth gather, the box uvs are in full-texture [0,1] space, the scale by buffer_frame.resolution_scale
// happens here so callers stay in canonical uv coordinates
// dropping the center sample is safe because the chosen mip ensures the box fits in roughly one texel and the four corner
// reads cover every texel the box can touch after the one-texel border expansion below
float hiz_min_depth_over_box(Texture2D hiz_tex, float2 min_uv, float2 max_uv, float max_mip_level)
{
    float2 render_size;
    hiz_tex.GetDimensions(render_size.x, render_size.y);

    float2 uv_extent = max_uv - min_uv;
    float2 size_px   = uv_extent * render_size;
    float  mip       = ceil(log2(max(max(size_px.x, size_px.y), 1.0f)));
    mip              = clamp(mip, 0, max_mip_level);

    float2 mip_texel = exp2(mip) / render_size;
    min_uv = saturate(min_uv - mip_texel);
    max_uv = saturate(max_uv + mip_texel);

    float2 uv_scale   = get_render_uv_scale();
    float4 scaled_uvs = float4(min_uv * uv_scale, max_uv * uv_scale);
    float d0 = hiz_tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), scaled_uvs.xy, mip).r;
    float d1 = hiz_tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), scaled_uvs.zy, mip).r;
    float d2 = hiz_tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), scaled_uvs.xw, mip).r;
    float d3 = hiz_tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), scaled_uvs.zw, mip).r;
    return min(min(d0, d1), min(d2, d3));
}

// fast analytical hi-z for a world-space sphere
// projects the center once and derives a conservative ndc rectangle from the row-axis sensitivities of view_projection
// this replaces the per-thread 8-corner cube projection in the meshlet path, the bound is also tighter than the cube
// projection that surrounds the sphere, so it culls more aggressively and chooses a smaller hi-z footprint
bool sphere_hiz_visible(Texture2D hiz_tex, float3 center_world, float radius_world, float max_mip_level)
{
    matrix vp = buffer_frame.view_projection;

    // partials of clip-space output w.r.t. world-space input, wave uniform so dxc keeps these in scalar registers
    float3 ax_x = float3(vp._m00, vp._m10, vp._m20);
    float3 ax_y = float3(vp._m01, vp._m11, vp._m21);
    float3 ax_z = float3(vp._m02, vp._m12, vp._m22);
    float3 ax_w = float3(vp._m03, vp._m13, vp._m23);

    float ax_x_len = length(ax_x);
    float ax_y_len = length(ax_y);
    float ax_z_len = length(ax_z);
    float ax_w_len = length(ax_w);

    float cx = dot(center_world, ax_x) + vp._m30;
    float cy = dot(center_world, ax_y) + vp._m31;
    float cz = dot(center_world, ax_z) + vp._m32;
    float cw = dot(center_world, ax_w) + vp._m33;

    float rx = radius_world * ax_x_len;
    float ry = radius_world * ax_y_len;
    float rz = radius_world * ax_z_len;
    float rw = radius_world * ax_w_len;

    // sphere entirely behind the camera, side-frustum has already rejected this so this is a paranoia branch
    if (cw + rw <= 0.0f)
        return false;

    // sphere straddles the near plane, the perspective divide is unstable so skip occlusion conservatively
    if (cw - rw <= 0.0f)
        return true;

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

    float2 min_ndc = float2(min(min(xlc, xlf), min(xhc, xhf)), min(min(ylc, ylf), min(yhc, yhf)));
    float2 max_ndc = float2(max(max(xlc, xlf), max(xhc, xhf)), max(max(ylc, ylf), max(yhc, yhf)));

    // closest sphere depth in reverse-z is max numerator over min positive denominator
    float closest_box_z = (cz + rz) * inv_w_close;

    if (max_ndc.x < -1.0f || min_ndc.x > 1.0f || max_ndc.y < -1.0f || min_ndc.y > 1.0f)
        return false;

    float2 uv_a   = saturate(ndc_to_uv(min_ndc));
    float2 uv_b   = saturate(ndc_to_uv(max_ndc));
    float2 min_uv = min(uv_a, uv_b);
    float2 max_uv = max(uv_a, uv_b);

    float furthest_z = hiz_min_depth_over_box(hiz_tex, min_uv, max_uv, max_mip_level);
    return closest_box_z > furthest_z - 0.01f;
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
