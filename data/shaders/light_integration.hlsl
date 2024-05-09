/*
Copyright(c) 2016-2024 Panos Karabelas

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
//====================

// http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
// efficient VanDerCorpus calculation
float radical_inverse_vdc(uint bits)
{
     bits = (bits << 16u) | (bits >> 16u);
     bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
     bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
     bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
     bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
     return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

float2 hammersley(uint i, uint n)
{
    return float2(float(i)/float(n), radical_inverse_vdc(i));
}

float3 importance_sample_ggx(float2 Xi, float3 N, float roughness)
{
    float a = roughness * roughness;
    
    float phi      = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    
    // from spherical coordinates to cartesian coordinates - halfway vector
    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;
    
    // from tangent-space H vector to world-space sample vector
    float3 up        = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent   = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    
    float3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

float geometry_schlick_ggx(float NdotV, float roughness)
{
    // note that we use a different k for IBL
    float a = roughness;
    float k = (a * a) / 2.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

float geometry_smith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = saturate(dot(N, V));
    float NdotL = saturate(dot(N, L));
    float ggx2  = geometry_schlick_ggx(NdotV, roughness);
    float ggx1  = geometry_schlick_ggx(NdotL, roughness);

    return ggx1 * ggx2;
}

float2 integrate_brdf(float n_dot_v, float roughness)
{
    const uint sample_count = 1024;

    float3 v;
    v.x = sqrt(1.0 - n_dot_v * n_dot_v);
    v.y = 0.0;
    v.z = n_dot_v;

    float3 n = float3(0.0f, 0.0f, 1.0f);
    float A  = 0.0f;
    float B  = 0.0f;
    for(uint i = 0; i < sample_count; ++i)
    {
        // generates a sample vector that's biased towards the
        // preferred alignment direction (importance sampling)
        float2 Xi = hammersley(i, sample_count);
        float3 h  = importance_sample_ggx(Xi, n, roughness);
        float3 l  = normalize(2.0 * dot(v, h) * h - v);

        float n_dot_l = saturate(l.z);
        float n_dot_h = saturate(h.z);
        float v_dot_h = saturate(dot(v, h));

        if(n_dot_l > 0.0)
        {
            float G     = geometry_smith(n, v, l, roughness);
            float G_Vis = (G * v_dot_h) / (n_dot_h * n_dot_v);
            float Fc    = pow(1.0 - v_dot_h, 5.0);

            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    
    A /= float(sample_count);
    B /= float(sample_count);
    
    return float2(A, B);
}

float D_GGX(float n_dot_h, float roughness_alpha_squared)
{
    float f = (n_dot_h * roughness_alpha_squared - n_dot_h) * n_dot_h + 1.0f;
    return roughness_alpha_squared / (PI * f * f + FLT_MIN);
}

float3 prefilter_environment(float2 uv)
{
    uint mip_level          = pass_get_f3_value().x;
    uint mip_count          = pass_get_f3_value().y;
    const uint sample_count = 8196 / max(mip_level, 1);
    float roughness         = (float)mip_level / (float)(mip_count - 1);

    // convert spherical uv to direction
    float phi   = uv.x * 2.0 * PI;
    float theta = (1.0f - uv.y) * PI;
    float3 V    = normalize(float3(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi)));
    float3 N    = V;
    
    float3 color       = 0.0f;
    float total_weight = 0.0;
    for(uint i = 0; i < sample_count; i++)
    {
        float2 Xi = hammersley(i, sample_count);
        float3 H  = importance_sample_ggx(Xi, N, roughness);
        float3 L  = normalize(2.0 * dot(V, H) * H - V);

        float n_dot_l = saturate(dot(N, L));
        if (n_dot_l > 0.0)
        {
            // compute uv
            phi     = atan2(L.z, L.x) + PI;
            theta   = acos(L.y);
            float u = (phi / (2.0 * PI)) + 0.5; // shifting UV by half the texture width
            u       = fmod(u, 1.0);             // wrap manually if u goes out of bounds
            float v = 1.0 - (theta / PI);

            // sample
            float mip_level_previous = mip_level - 1;
            color += tex_environment.SampleLevel(samplers[sampler_bilinear_clamp], float2(u, v), mip_level_previous).rgb * n_dot_l;

            // accumulate weight
            total_weight += n_dot_l;
        }
    }

    return color / total_weight;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(int2(thread_id.xy) >= resolution_out))
        return;

    const float2 uv = (thread_id.xy + 0.5f) / resolution_out;
    float4 color    = 1.0f;

    #if BRDF_SPECULAR_LUT
    color.rg = integrate_brdf(uv.x, uv.y);
    #endif

    #if ENVIRONMENT_FILTER
    color.rgb = prefilter_environment(uv);
    #endif

    tex_uav[thread_id.xy] = color;
}

