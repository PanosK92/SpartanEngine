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

//= INCLUDES =========
#include "common.hlsl"
#include "brdf.hlsl"
//====================

// refraction and transparency constants
static const float ior_water                  = 1.333f; // water IOR
static const float ior_air                    = 1.0f;   // air IOR
static const float chromatic_aberration       = 0.02f;  // water chromatic aberration strength
static const float glass_dispersion           = 0.03f;  // per channel ior spread for glass
static const uint  glass_frost_taps           = 8;      // frosted transmission disc taps
static const float glass_parallax_scale       = 0.18f;  // thin-shell uv shift per meter of optical path
static const float glass_absorption_scale     = 50.0f;  // maps authored absorption*thickness into beer lambert exponent

// ray traced reflections now jitter the ray across the ggx lobe by surface roughness and a
// spatiotemporal denoiser reconstructs a roughness proportional blur, so rough surfaces show a
// correct soft reflection instead of a wrong sharp mirror, the fade band is pushed to the very
// top of the roughness range as a gentle safety tail, beyond the ray spread alpha cap the lobe
// stops widening so the last stretch fades out rather than reading as an under blurred mirror,
// smooth surfaces (glass, polished metal, car paint) keep the full sharp reflection
static const float reflection_roughness_fade_start = 0.85f; // full reflection at or below this
static const float reflection_roughness_fade_end   = 1.0f;  // no reflection at or above this

// screen-space raymarching constants
static const uint  g_refraction_max_steps     = 16;     // max ray steps for refraction
static const float g_refraction_max_distance  = 2.0f;   // max refraction distance
static const float g_refraction_thickness     = 0.1f;   // depth testing thickness
static const float g_refraction_step_length   = g_refraction_max_distance / (float)g_refraction_max_steps;

// contact foam only where opaque geometry sits within this 3d world distance of the water surface point,
// vertical clearance alone foams every submerged wall seen through the water, full distance cannot
static const float contact_foam_radius = 0.6f;

// Compute Fresnel for dielectrics using Schlick approximation
float3 compute_dielectric_fresnel(float cos_theta, float ior_outer, float ior_inner)
{
    // compute F0 for dielectric
    float f0_val = pow((ior_outer - ior_inner) / (ior_outer + ior_inner), 2.0f);
    float3 F0 = float3(f0_val, f0_val, f0_val);
    
    // Schlick Fresnel
    return F_Schlick(F0, get_f90(), cos_theta);
}

// subtle thin-film iridescence for clear windshield glass at grazing angles, heavy absorption kills it so taillight lenses stay clean
float3 glass_thin_film_fresnel(float3 fresnel, float n_dot_v, float absorption)
{
    float clear   = saturate(1.0f - absorption * 2.0f);
    float grazing = pow(saturate(1.0f - n_dot_v), 3.0f);
    float phase   = (1.0f - n_dot_v) * 14.0f;
    float3 irid   = 0.5f + 0.5f * float3(cos(phase), cos(phase + 2.094395f), cos(phase + 4.188790f));
    return lerp(fresnel, fresnel * lerp(1.0f, irid, 0.4f), clear * grazing);
}

// beer lambert water column, the closed form of single scattering in a homogeneous medium
// one extinction drives both the transmitted background and the in-scatter fill, per channel,
// so the fill takes over exactly as fast as the background fades, red dies first and blue persists,
// a separate scatter rate here made every channel converge to the body color at the same depth which read as flat milk
float3 apply_water_absorption(float3 color, float depth, float3 body_radiance)
{
    float3 transmittance = exp(-ocean_extinction * depth);
    return color * transmittance + body_radiance * (1.0f - transmittance);
}

// Compute refracted direction using Snell's law (returns zero if total internal reflection)
float3 compute_refracted_dir(float3 incident_dir, float3 normal, float ior_outer, float ior_inner)
{
    float ior_ratio   = ior_outer / ior_inner;
    float cos_theta_i = -dot(incident_dir, normal);
    float sin_theta_i = sqrt(max(0.0f, 1.0f - cos_theta_i * cos_theta_i));
    float sin_theta_t = ior_ratio * sin_theta_i;
    
    // check for total internal reflection
    if (sin_theta_t >= 1.0f)
        return float3(0.0f, 0.0f, 0.0f);
    
    // compute refracted direction
    float cos_theta_t = sqrt(max(0.0f, 1.0f - sin_theta_t * sin_theta_t));
    return normalize(ior_ratio * incident_dir + (ior_ratio * cos_theta_i - cos_theta_t) * normal);
}

// screen space raymarch along the refracted direction, returns the hit uv or the straight through uv, handles curved glass correctly
float2 compute_refraction_uv(float3 surface_pos_ws, float3 refracted_dir_ws, float depth_transparent, float2 uv)
{
    // convert to view space
    float3 ray_pos  = world_to_view(surface_pos_ws);
    float3 ray_dir  = world_to_view(refracted_dir_ws, false);
    float3 ray_step = ray_dir * g_refraction_step_length;

    // offset starting position slightly to avoid self-intersection with the glass surface
    ray_pos += ray_dir * 0.02f;

    for (uint i = 0; i < g_refraction_max_steps; i++)
    {
        ray_pos       += ray_step;
        float2 ray_uv  = view_to_uv(ray_pos);

        if (!is_valid_uv(ray_uv))
            break;

        // check if the ray passed through the glass and hit geometry behind it
        float depth_z     = get_linear_depth(ray_uv);
        float depth_delta = ray_pos.z - depth_z;
        if (depth_delta > 0.0f && depth_delta < g_refraction_thickness && depth_z > depth_transparent + 0.01f)
            return ray_uv;

        // early exit if the ray is too far from any surface
        if (abs(depth_delta) > g_refraction_thickness * 10.0f && i > 4)
            break;
    }

    return uv;
}

// smooth value noise, used to carve the coarse jacobian foam into finer whitewater
float ocean_value_noise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f       = f * f * (3.0f - 2.0f * f);
    float a = hash(i + float2(0.0f, 0.0f));
    float b = hash(i + float2(1.0f, 0.0f));
    float c = hash(i + float2(0.0f, 1.0f));
    float d = hash(i + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // get output resolution and build surface data
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    Surface surface;
    surface.Build(thread_id.xy, resolution_out, true, false);
    
    // skip sky pixels
    if (surface.is_sky())
        return;

    // fft ocean foam, the water color below is built purely from reflection and refraction so the diffuse
    // foam written into the g-buffer never surfaces, it has to be injected here from the same cascade map
    float foam = 0.0f;
    if (surface.is_water() && buffer_frame.ocean_enabled > 0.5f)
    {
        uint cascades = buffer_frame.ocean_cascade_count;

        // the geometry was shifted horizontally by the choppiness displacement, undo it so foam lands on the waves it formed on
        // the depth here only gives the displaced position, fixed-point iterate g = p - displacement(g) to recover the undisplaced grid domain the fft indexes
        float2 world_xz = surface.position.xz;
        [unroll] for (uint it = 0; it < 3; ++it)
        {
            float2 displaced = 0.0f;
            [loop] for (uint c = 0; c < cascades; ++c)
            {
                float2 uv  = world_xz / buffer_frame.ocean_cascade_length[c];
                displaced += tex_ocean_displacement.SampleLevel(samplers[sampler_bilinear_wrap], float3(uv, (float)c), 0.0f).xz;
            }
            world_xz = surface.position.xz - displaced;
        }

        [loop] for (uint c = 0; c < cascades; ++c)
        {
            float2 uv = world_xz / buffer_frame.ocean_cascade_length[c];
            foam += tex_ocean_normal.SampleLevel(samplers[sampler_bilinear_wrap], float3(uv, (float)c), 0.0f).z;
        }
        // the jacobian mask is 512 texels per cascade so up close it magnifies into soft blobs, use it as
        // coverage that erodes a fine world locked fbm instead of multiplying it, the visible edges then
        // come from the noise frequency rather than the map resolution, dense coverage reads as near solid
        // whitewater with small holes and decaying coverage breaks apart into sparse lacy flecks
        float coverage = saturate(foam);
        float lace     = ocean_value_noise(world_xz * 4.0f) * 0.45f + ocean_value_noise(world_xz * 13.0f) * 0.35f + ocean_value_noise(world_xz * 41.0f) * 0.2f;
        float eroded   = saturate((lace + coverage - 1.0f) * 3.0f);

        // the fine octaves go subpixel with distance and would alias, ease back to the plain
        // coverage mask out there, the cascade resolution is adequate at that magnification anyway
        foam = lerp(eroded, coverage, saturate(surface.camera_to_pixel_length / 100.0f));

        // contact foam, the opaque point behind this water pixel must sit right at the surface point in
        // full 3d, a submerged wall seen through the water lies meters along the ray so it stays clean
        float2 uv_foam          = (thread_id.xy + 0.5f) / resolution_out;
        float  depth_water      = linearize_depth(surface.depth);
        float  depth_opaque_raw = tex4.SampleLevel(samplers[sampler_point_clamp], uv_foam, 0.0f).r;
        float  depth_opaque     = linearize_depth(depth_opaque_raw);
        if (depth_opaque > depth_water + 0.02f)
        {
            float3 opaque_pos = get_position(depth_opaque_raw, render_uv_to_screen_uv(uv_foam));
            float  contact    = saturate(1.0f - length(opaque_pos - surface.position) / contact_foam_radius);
            float  contact_foam = saturate((lace + contact - 1.0f) * 3.5f) * 0.7f;
            foam = saturate(max(foam, contact_foam));
        }
    }

    // get background color
    float3 background = tex2[thread_id.xy].rgb;
    float3 refraction = background;
    
    // determine material IOR
    float ior_material = surface.is_water() ? ior_water : max(surface.ior, 1.0001f);
    
    // compute view direction and angle
    float3 view_dir_normalized = normalize(surface.camera_to_pixel);
    float n_dot_v = saturate(dot(surface.normal, -view_dir_normalized));
    
    // compute Fresnel: F = reflection amount, (1-F) = refraction amount
    float3 F = compute_dielectric_fresnel(n_dot_v, ior_air, ior_material);
    
    // compute refraction for transparent surfaces
    if (surface.is_water() || surface.is_transparent())
    {
        float2 uv = (thread_id.xy + 0.5f) / resolution_out;
        float depth_transparent = linearize_depth(surface.depth);
        
        // compute refracted direction
        float3 refracted_dir = compute_refracted_dir(view_dir_normalized, surface.normal, ior_air, ior_material);
        
        // check for total internal reflection
        if (dot(refracted_dir, refracted_dir) < 0.001f)
        {
            // total internal reflection: all light reflects, no refraction
            refraction = background;
            F = float3(1.0f, 1.0f, 1.0f);
        }
        else
        {
            if (surface.is_water())
            {
                float depth_background = linearize_depth(tex4.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0.0f).r);
                float thickness        = clamp(depth_background - depth_transparent, 0.0f, 10.0f);

                // a physically bent ray shifts the whole underwater image systematically in one direction,
                // and wherever the shifted sample is unavailable in screen space the pixel has to fall back
                // toward the straight view, a constant shift next to its own fallback reads as two copies of
                // the object, so distort with the zero mean wave slope instead, the image wobbles around its
                // true position and always stays one object, the offset still grows with the water column so
                // it vanishes at the waterline and deeper content shimmers more
                float2 offset = surface.normal.xz * 0.05f * saturate(thickness * 0.5f);

                // the wobble can still land on geometry in front of the water, e.g. the part of a pillar
                // above the surface, halve it until the sample is submerged, the offset is small so the
                // collapse is invisible when it happens
                float2 refracted_uv = uv;
                float depth_shown   = depth_background;
                float scale         = 1.0f;
                [unroll]
                for (int i = 0; i < 4; i++)
                {
                    float2 uv_try   = uv + offset * scale;
                    float depth_try = linearize_depth(tex4.SampleLevel(samplers[sampler_bilinear_clamp], uv_try, 0.0f).r);
                    if (depth_try > depth_transparent && all(uv_try == saturate(uv_try)))
                    {
                        refracted_uv = uv_try;
                        depth_shown  = depth_try;
                        break;
                    }
                    scale *= 0.5f;
                }

                // chromatic aberration along the refraction delta
                float2 delta = refracted_uv - uv;
                refraction.r = tex2.SampleLevel(samplers[sampler_bilinear_clamp], uv + delta * (1.0f + chromatic_aberration), 0.0f).r;
                refraction.g = tex2.SampleLevel(samplers[sampler_bilinear_clamp], refracted_uv, 0.0f).g;
                refraction.b = tex2.SampleLevel(samplers[sampler_bilinear_clamp], uv + delta * (1.0f - chromatic_aberration), 0.0f).b;

                // the body color is an albedo, light it with the downwelling sun and the reflected sky, a constant radiance glows at night and reads black at noon
                float3 downwelling   = get_sun_radiance() * saturate(-light_parameters[0].direction.y) * (1.0f / PI) + tex[thread_id.xy].rgb;
                float3 body_radiance = ocean_scatter_albedo * downwelling;

                // absorption follows the water column of the sample actually shown so brightness stays consistent with the offset chosen above
                float water_depth = max(depth_shown - depth_transparent, 0.0f);
                refraction        = apply_water_absorption(refraction, water_depth, body_radiance);
            }
            else
            {
                float shell_thickness = max(surface.thickness, 0.001f);
                float optical_path    = max(shell_thickness / max(n_dot_v, 0.15f), shell_thickness);

                // thin-shell parallax, bend the background by the authored thickness even when the raymarch fails
                float3 view_tangent = view_dir_normalized - surface.normal * dot(view_dir_normalized, surface.normal);
                float2 parallax     = view_tangent.xz * (optical_path * glass_parallax_scale);
                float2 uv_parallax  = uv + parallax;
                if (!is_valid_uv(uv_parallax))
                {
                    uv_parallax = uv;
                }

                float2 refracted_uv = compute_refraction_uv(surface.position, refracted_dir, depth_transparent, uv_parallax);
                if (dot(refracted_uv - uv_parallax, refracted_uv - uv_parallax) < 1e-10f)
                {
                    refracted_uv = uv_parallax;
                }

                float2 delta = refracted_uv - uv;

                // dispersion, the ior rises toward blue so each channel bends by a slightly different amount
                refraction.r = tex2.SampleLevel(samplers[sampler_bilinear_clamp], uv + delta * (1.0f - glass_dispersion), 0.0f).r;
                refraction.g = tex2.SampleLevel(samplers[sampler_bilinear_clamp], refracted_uv, 0.0f).g;
                refraction.b = tex2.SampleLevel(samplers[sampler_bilinear_clamp], uv + delta * (1.0f + glass_dispersion), 0.0f).b;

                // frosted blur from roughness and authored thickness, screen depth is only an upper bound so thin windshields stay sharp
                float depth_background = linearize_depth(tex4.SampleLevel(samplers[sampler_bilinear_clamp], refracted_uv, 0.0f).r);
                float screen_thickness = clamp(depth_background - depth_transparent, 0.0f, 4.0f);
                float blur_metric      = min(max(shell_thickness * 40.0f, screen_thickness * 0.25f), screen_thickness + shell_thickness * 20.0f);
                float blur_radius      = min(surface.roughness_alpha * blur_metric / max(depth_transparent, 0.5f), 0.03f);
                if (blur_radius > 0.0002f)
                {
                    // golden angle disc with a per pixel rotation, taa resolves the residual noise
                    float  rotation = noise_interleaved_gradient(thread_id.xy) * PI2;
                    float3 sum      = 0.0f;
                    [unroll]
                    for (uint t = 0; t < glass_frost_taps; t++)
                    {
                        float  angle  = (t + 0.5f) * 2.399963f + rotation;
                        float  radius = blur_radius * sqrt((t + 0.5f) / glass_frost_taps);
                        float2 tap_uv = refracted_uv + float2(cos(angle), sin(angle)) * radius;
                        sum          += tex2.SampleLevel(samplers[sampler_bilinear_clamp], tap_uv, 0.0f).rgb;
                    }
                    refraction = sum / glass_frost_taps;
                }

                refraction = lerp(background, refraction, screen_fade(refracted_uv));
            }
        }
    }
    
    // compute specular reflection using fresnel and brdf split sum
    float3 reflection = tex[thread_id.xy].rgb;

    // glass reflections follow the clearcoat lobe when present, matching the rt tracer's source roughness blend
    float reflection_roughness = surface.roughness;
    if (surface.is_transparent() && !surface.is_water())
    {
        reflection_roughness = lerp(surface.roughness, surface.clearcoat_roughness, saturate(surface.clearcoat));
    }
    float2 brdf = tex3.SampleLevel(samplers[sampler_bilinear_clamp], float2(n_dot_v, reflection_roughness), 0.0f).rg;

    // pick the right F0, transparent surfaces use the ior derived dielectric F0, opaque surfaces use the actual surface F0 which is colored for metals
    float  f0_dielectric  = pow((ior_air - ior_material) / (ior_air + ior_material), 2.0f);
    float3 F0_dielectric  = float3(f0_dielectric, f0_dielectric, f0_dielectric);
    float3 F0_brdf        = (surface.is_water() || surface.is_transparent()) ? F0_dielectric : surface.F0;

    // fade out the mirror sharp reflection on rough surfaces, see band comment at top of file
    float roughness_fade = 1.0f - smoothstep(reflection_roughness_fade_start, reflection_roughness_fade_end, reflection_roughness);
    reflection          *= roughness_fade;

    // brdf.x is fresnel dependent and brdf.y is fresnel independent, this matches the split sum used in light_image_based.hlsl
    float3 specular_reflection = reflection * (F0_brdf * brdf.x + brdf.y);

    if (surface.is_transparent() && !surface.is_water())
    {
        // pbr glass, fresnel can pick up a thin-film rainbow on clear panes, beer lambert uses absorption*thickness so alpha stays a soft coverage term
        float3 F_glass           = glass_thin_film_fresnel(F, n_dot_v, surface.absorption);
        float  shell_thickness   = max(surface.thickness, 0.001f);
        float  optical_path      = max(shell_thickness / max(n_dot_v, 0.15f), shell_thickness);
        float3 transmission_tint = pow(max(surface.albedo, 0.02f), max(surface.absorption, 0.0f) * optical_path * glass_absorption_scale);
        float  coverage          = saturate(1.0f - surface.alpha * 0.25f);
        float3 reflection_total  = tex_uav[thread_id.xy].rgb + specular_reflection;
        float3 kT                = float3(1.0f, 1.0f, 1.0f) - F_glass;
        float3 transmission      = refraction * kT * transmission_tint * coverage;
        tex_uav[thread_id.xy]    = validate_output(float4(reflection_total + transmission, 1.0f));
    }
    else
    {
        float3 kT            = float3(1.0f, 1.0f, 1.0f) - F;
        float3 surface_color = specular_reflection + refraction * kT;
        tex_uav[thread_id.xy] += float4(surface_color, 0.0f);
    }

    // overlay lit foam on top of the composited water, whitewater is a bright near-white lambertian cap so it
    // must replace the reflection and refraction below rather than tint them, additive blending only desaturates the water
    if (foam > 0.0f)
    {
        // whitewater receives the sun as a wrap-lit diffuse term plus the reflected sky as an ambient fill, near-white albedo
        float  n_dot_l   = saturate(dot(surface.normal, -light_parameters[0].direction));
        float3 sun_diff  = get_sun_radiance() * (n_dot_l * 0.5f + 0.5f) * (1.0f / PI);
        float3 sky       = tex[thread_id.xy].rgb;
        float3 incoming  = sun_diff + sky;

        // drive the chroma to white and lift the brightness so foam reads as whitewater instead of dull tinted water
        float  luma       = luminance(incoming);
        float3 foam_color = lerp(incoming, luma.xxx, 0.85f) * float3(0.97f, 0.98f, 1.0f) * 2.0f;

        // foam sits on top of the water, dense foam fully hides the reflection and refraction below
        float3 base = tex_uav[thread_id.xy].rgb;
        tex_uav[thread_id.xy].rgb = lerp(base, foam_color, foam);
    }
}
