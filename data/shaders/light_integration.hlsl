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

// compute radical inverse using van der corput sequence for low-discrepancy sampling
// reference: http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
float radical_inverse_vdc(uint bits)
{
     bits = (bits << 16u) | (bits >> 16u);
     bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
     bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
     bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
     bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
     return float(bits) * 2.3283064365386963e-10f; // / 0x100000000
}

// generate hammersley sequence point for low-discrepancy sampling
float2 hammersley(uint i, uint n)
{
    return float2(float(i) / float(n), radical_inverse_vdc(i));
}

// importance sample ggx distribution for specular brdf integration
// generates a halfway vector h biased toward the microfacet normal distribution
float3 importance_sample_ggx(float2 Xi, float3 N, float roughness)
{
    const float alpha    = D_GGX_Alpha(roughness);
                         
    const float phi      = 2.0f * PI * Xi.x;
    const float cosTheta = sqrt((1.0f - Xi.y) / (1.0f + (alpha * alpha - 1.0f) * Xi.y));
    const float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
   
    // convert from spherical coordinates to cartesian (halfway vector in tangent space)
    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;
   
    // build orthonormal basis from normal vector
    float3 up        = abs(N.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent   = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
   
    // transform halfway vector from tangent space to world space
    float3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

// integrate specular brdf for a given f0 value using monte carlo importance sampling
// returns scalar brdf value for environment map lookup table generation
float integrate_brdf_scalar(float n_dot_v, float roughness, float f0_val)
{
    const uint sample_count = 1024;
    
    // setup view vector in tangent space (viewing along z-axis)
    float3 v;
    v.x = sqrt(1.0f - n_dot_v * n_dot_v);
    v.y = 0.0f;
    v.z = n_dot_v;
    float3 n = float3(0.0f, 0.0f, 1.0f);
    
    float integral = 0.0f;
    for(uint i = 0; i < sample_count; ++i)
    {
        // generate low-discrepancy sample using hammersley sequence
        float2 Xi     = hammersley(i, sample_count);
        float3 h      = importance_sample_ggx(Xi, n, roughness);
        float3 l      = normalize(2.0f * dot(v, h) * h - v);
        float n_dot_l = saturate(l.z);
        float n_dot_h = saturate(h.z);
        float v_dot_h = saturate(dot(v, h));
        
        if(n_dot_l > 0.0f)
        {
            // setup minimal surface structure for brdf evaluation
            Surface surface;
            surface.roughness      = roughness;
            surface.F0             = float3(f0_val, f0_val, f0_val);
            surface.metallic       = 0.0f;
            surface.diffuse_energy = float3(1.0f, 1.0f, 1.0f);
            
            // setup angular information for brdf
            AngularInfo angular_info;
            angular_info.n_dot_l = n_dot_l;
            angular_info.n_dot_v = n_dot_v;
            angular_info.n_dot_h = n_dot_h;
            angular_info.v_dot_h = v_dot_h;
            
            // evaluate specular brdf
            float3 fs  = BRDF_Specular_Isotropic(surface, angular_info);
            float brdf = fs.r; // use red channel (monochromatic assumption)
            
            // compute probability density function for importance sampling
            float alpha  = D_GGX_Alpha(roughness);
            float alpha2 = alpha * alpha;
            float d      = D_GGX(n_dot_h, alpha2);
            float pdf    = (d * n_dot_h / (4.0f * v_dot_h)) + 1e-5f;
            
            // accumulate weighted contribution (importance sampling)
            float contrib = brdf * n_dot_l / pdf;
            integral     += contrib;
        }
    }
    return integral / float(sample_count);
}

// integrate brdf for environment map lookup table (split-sum approximation)
// returns (A, B) where: F = F0 * A + B, allowing f0 to be factored out
float2 integrate_brdf(float n_dot_v, float roughness)
{
    // integrate for f0 = 0 and f0 = 1 to factor out fresnel term
    float int0 = integrate_brdf_scalar(n_dot_v, roughness, 0.0f);
    float int1 = integrate_brdf_scalar(n_dot_v, roughness, 1.0f);
    float A    = int1 - int0;  // fresnel-dependent term
    float B    = int0;          // fresnel-independent term
    
    return float2(A, B);
}

// prefilter environment map using importance sampling for specular ibl
// generates mip levels with appropriate blur based on roughness
float3 prefilter_environment(float2 uv)
{
    float resolution        = 4096.0f;  // match skysphere width
    float base_resolution   = 2048.0f;  // match skysphere height
    float intensity_gain    = 2.5f;
    uint mip_level          = pass_get_f3_value().x;
    uint mip_count          = pass_get_f3_value().y;
    const uint sample_count = 512 / max(mip_level, 1);
    float roughness         = (float)mip_level / (float)(mip_count - 1);
    
    // convert spherical uv coordinates to view direction
    float phi              = uv.x * 2.0f * PI;
    float theta            = (1.0f - uv.y) * PI;
    float3 V               = normalize(float3(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi)));
    float3 N               = V;  // normal equals view direction for environment filtering

    // accumulate filtered color using importance sampling
    float3 color           = 0.0f;
    float total_weight     = 0.0f;
    for (uint i = 0; i < sample_count; i++)
    {
        // generate importance-sampled direction
        float2 Xi      = hammersley(i, sample_count);
        float3 H       = importance_sample_ggx(Xi, N, roughness);
        float3 L       = normalize(2.0f * dot(V, H) * H - V);
        float n_dot_l  = saturate(dot(N, L));
        float n_dot_h  = saturate(dot(N, H));
        float h_dot_v  = saturate(dot(H, V));
        
        if (n_dot_l > 0.0f)
        {
            // convert light direction to spherical uv coordinates
            phi            = atan2(L.z, L.x) + PI;
            theta          = acos(L.y);
            float u        = (phi / (2.0f * PI)) + 0.5f;
            u              = fmod(u, 1.0f);
            float v        = 1.0f - (theta / PI);
            
            // compute pdf-based mip level for proper filtering
            float alpha_ggx  = D_GGX_Alpha(roughness);
            float alpha2     = alpha_ggx * alpha_ggx;
            float D          = D_GGX(n_dot_h, alpha2);
            float pdf        = (D * n_dot_h / (4.0f * h_dot_v)) + 0.0001f;
            
            // compute solid angle and select appropriate mip level
            float sa_texel   = 4.0f * PI / (base_resolution * base_resolution);
            float sa_sample  = 1.0f / (float(sample_count) * pdf + 0.0001f);
            float mip_sample = roughness == 0.0f ? 0.0f : 0.5f * log2(sa_sample / sa_texel);
            
            // adjust for resolution difference between width and height
            float resolution_factor = log2(resolution / base_resolution);
            mip_sample              = max(0.0f, mip_sample - resolution_factor);
            
            // clamp mip level to valid range
            mip_sample = clamp(mip_sample, 0.0f, float(mip_count - 1));
            
            // sample environment map at computed mip level
            float3 sample_color = tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(u, v), mip_sample).rgb;
            
            // apply tone mapping to reduce impact of very bright spots (e.g. sun)
            sample_color = sample_color / (1.0f + luminance(sample_color));
            
            // clamp outliers to prevent fireflies
            sample_color = min(sample_color, 10.0f);
            
            // apply intensity gain to compensate for tone mapping attenuation
            sample_color *= intensity_gain;
            color        += sample_color * n_dot_l;
            total_weight += n_dot_l;
        }
    }
    // return weighted average, or magenta if no valid samples (error indicator)
    return total_weight > 0.0f ? color / total_weight : float3(1.0f, 0.0f, 1.0f);
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // get output resolution and check bounds
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(int2(thread_id.xy) >= resolution_out))
        return;

    // compute uv coordinates for lookup table or environment filtering
    const float2 uv = (thread_id.xy + 0.5f) / resolution_out;
    float4 color    = 1.0f;
    
    // generate brdf lookup table for split-sum approximation
    #if BRDF_SPECULAR_LUT
    color.rg = integrate_brdf(uv.x, uv.y);
    #endif
    
    // prefilter environment map for specular ibl
    #if ENVIRONMENT_FILTER
    color.rgb = prefilter_environment(uv);
    #endif
    
    // write result to output buffer
    tex_uav[thread_id.xy] = color;
}
