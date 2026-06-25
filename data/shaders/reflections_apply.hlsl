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
static const float refraction_strength_water  = 0.8f;   // water refraction strength
static const float ior_water                  = 1.333f; // water IOR
static const float ior_glass                  = 1.5f;   // glass IOR
static const float ior_air                    = 1.0f;   // air IOR
static const float chromatic_aberration       = 0.02f;  // chromatic aberration strength
static const float absorption_scale           = 0.45f;  // water absorption scale
static const float3 water_body_color          = float3(0.0f, 0.09f, 0.13f); // deep blue-green in-scattering tint

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

// Compute Fresnel for dielectrics using Schlick approximation
float3 compute_dielectric_fresnel(float cos_theta, float ior_outer, float ior_inner)
{
    // compute F0 for dielectric
    float f0_val = pow((ior_outer - ior_inner) / (ior_outer + ior_inner), 2.0f);
    float3 F0 = float3(f0_val, f0_val, f0_val);
    
    // Schlick Fresnel
    return F_Schlick(F0, get_f90(), cos_theta);
}

// Apply water absorption using Beer-Lambert law, with depth-driven in-scattering toward the body color
float3 apply_water_absorption(float3 color, float depth)
{
    // wavelength-dependent extinction, red dies first and blue persists, so the column tints toward blue-green with depth
    float3 extinction  = float3(0.45f, 0.15f, 0.08f) * absorption_scale;
    float3 transmitted = color * exp(-extinction * depth);
    
    // in-scattered body color fills in as the column deepens, so deep water reads opaque and colored instead of like thin glass
    float scatter = 1.0f - exp(-depth * 0.35f);
    return lerp(transmitted, water_body_color, scatter);
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

// Screen-space raymarching for refraction (handles curved glass correctly)
float3 compute_refraction_raymarch(float3 surface_pos_ws, float3 refracted_dir_ws, float depth_transparent, float2 uv)
{
    // convert to view space
    float3 ray_pos = world_to_view(surface_pos_ws);
    float3 ray_dir = world_to_view(refracted_dir_ws, false);
    
    // compute ray step
    float3 ray_step = ray_dir * g_refraction_step_length;
    
    // offset starting position slightly to avoid self-intersection with glass surface
    ray_pos += ray_dir * 0.02f;
    
    // ray march along refracted direction
    float3 refracted_color = float3(0.0f, 0.0f, 0.0f);
    bool found_intersection = false;
    
    for (uint i = 0; i < g_refraction_max_steps; i++)
    {
        // step the ray
        ray_pos += ray_step;
        float2 ray_uv = view_to_uv(ray_pos);
        
        // check if UV is valid
        if (!is_valid_uv(ray_uv))
            break;
        
        // get depth at ray position
        float depth_z = get_linear_depth(ray_uv);
        float depth_delta = ray_pos.z - depth_z;
        
        // check if ray passed through glass and hit geometry behind it
        if (depth_delta > 0.0f && depth_delta < g_refraction_thickness && depth_z > depth_transparent + 0.01f)
        {
            // we've found an intersection behind the glass
            refracted_color = tex2.SampleLevel(samplers[sampler_bilinear_clamp], ray_uv, 0.0f).rgb;
            found_intersection = true;
            break;
        }
        
        // early exit if ray is too far from any surface
        if (abs(depth_delta) > g_refraction_thickness * 10.0f && i > 4)
            break;
    }
    
    return found_intersection ? refracted_color : tex2.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0.0f).rgb;
}

// Compute refraction with chromatic aberration (2D offset method for water)
void compute_refraction_with_chromatic_aberration(
    float2 uv, float3 refracted_dir, float camera_to_pixel_length, float refraction_strength,
    out float3 refracted_color, out float2 base_uv_offset, out float2 refracted_uv)
{
    // compute base refraction offset
    float inv_dist        = saturate(1.0f / (camera_to_pixel_length + FLT_MIN));
    float3 refracted_view = world_to_view(refracted_dir, false);
    base_uv_offset        = refracted_view.xy * refraction_strength * inv_dist;
    
    // chromatic aberration: different offsets for RGB channels
    float2 offset_r = base_uv_offset * (1.0f + chromatic_aberration);
    float2 offset_g = base_uv_offset;
    float2 offset_b = base_uv_offset * (1.0f - chromatic_aberration);
    
    // sample refracted color with chromatic aberration
    float2 uv_r = clamp(uv + offset_r, 0.0f, 1.0f);
    float2 uv_g = clamp(uv + offset_g, 0.0f, 1.0f);
    float2 uv_b = clamp(uv + offset_b, 0.0f, 1.0f);
    
    float3 refracted_r = tex2.SampleLevel(samplers[sampler_bilinear_clamp], uv_r, 0.0f).rgb;
    float3 refracted_g = tex2.SampleLevel(samplers[sampler_bilinear_clamp], uv_g, 0.0f).rgb;
    float3 refracted_b = tex2.SampleLevel(samplers[sampler_bilinear_clamp], uv_b, 0.0f).rgb;
    
    // combine RGB channels
    refracted_color = float3(refracted_r.r, refracted_g.g, refracted_b.b);
    
    // base offset for depth checking
    refracted_uv = clamp(uv + base_uv_offset, 0.0f, 1.0f);
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
        foam = saturate(foam);

        // the jacobian foam is low frequency and reads as coarse blobs, carve it with two octaves of fine
        // world locked noise so it breaks into bubbly whitewater, sampled in the undisplaced domain so the detail rides the surface
        float detail = ocean_value_noise(world_xz * 6.0f) * 0.6f + ocean_value_noise(world_xz * 18.0f) * 0.4f;
        foam         = saturate(foam * (0.5f + detail));
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
                // water: use 2D offset method with chromatic aberration (works well for flat surfaces)
                float3 refracted;
                float2 base_uv_offset;
                float2 refracted_uv;
                compute_refraction_with_chromatic_aberration(
                    uv, refracted_dir, surface.camera_to_pixel_length, refraction_strength_water,
                    refracted, base_uv_offset, refracted_uv
                );
                
                // check depth to ensure valid geometry
                float depth_opaque = linearize_depth(tex4.SampleLevel(samplers[sampler_bilinear_clamp], refracted_uv, 0.0f).r);
                
                // depth validation: geometry must exist behind surface
                float depth_diff = depth_opaque - depth_transparent;
                float depth_valid = float(depth_diff > 0.0f);
                
                float use_refraction = depth_valid * screen_fade(refracted_uv);
                refraction = lerp(background, refracted, use_refraction);
                
                // apply water absorption
                float water_depth = max(depth_opaque - depth_transparent, 0.0f);
                refraction = apply_water_absorption(refraction, water_depth);
            }
            else
            {
                // glass: use screen-space raymarching (handles curved surfaces correctly)
                float3 refracted = compute_refraction_raymarch(surface.position, refracted_dir, depth_transparent, uv);
                refraction = lerp(background, refracted, screen_fade(uv));
            }
        }
    }
    
    // compute specular reflection using fresnel and brdf split sum
    float3 reflection = tex[thread_id.xy].rgb;
    float2 brdf       = tex3.SampleLevel(samplers[sampler_bilinear_clamp], float2(n_dot_v, surface.roughness), 0.0f).rg;

    // pick the right F0, transparent surfaces use the ior derived dielectric F0, opaque surfaces use the actual surface F0 which is colored for metals
    float  f0_dielectric  = pow((ior_air - ior_material) / (ior_air + ior_material), 2.0f);
    float3 F0_dielectric  = float3(f0_dielectric, f0_dielectric, f0_dielectric);
    float3 F0_brdf        = (surface.is_water() || surface.is_transparent()) ? F0_dielectric : surface.F0;

    // fade out the mirror sharp reflection on rough surfaces, see band comment at top of file
    float roughness_fade = 1.0f - smoothstep(reflection_roughness_fade_start, reflection_roughness_fade_end, surface.roughness);
    reflection          *= roughness_fade;

    // brdf.x is fresnel dependent and brdf.y is fresnel independent, this matches the split sum used in light_image_based.hlsl
    float3 specular_reflection = reflection * (F0_brdf * brdf.x + brdf.y);

    if (surface.is_transparent())
    {
        // pbr glass: sum reflection paths at full strength, add (1 - F) refracted background tinted by alpha
        float3 reflection_total  = tex_uav[thread_id.xy].rgb + specular_reflection;
        float3 transmission_tint = lerp(float3(1.0f, 1.0f, 1.0f), surface.albedo, surface.alpha);
        float3 kT                = float3(1.0f, 1.0f, 1.0f) - F;
        float3 transmission      = refraction * kT * transmission_tint;
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
