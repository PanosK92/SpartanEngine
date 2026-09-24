/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#ifndef SPARTAN_SPHERICAL_HARMONICS
#define SPARTAN_SPHERICAL_HARMONICS

// l2 real sh, sloan / ramamoorthi ordering
// 0: y00, 1-3: y1-1 y10 y11, 4-8: y2-2 .. y22

// cosine lobe band factors for irradiance from radiance sh, ramamoorthi 2001
static const float sh_a0 = 3.14159265f;
static const float sh_a1 = 2.09439510f;
static const float sh_a2 = 0.78539816f;

void sh_eval_basis_l2(float3 dir, out float b[9])
{
    float x = dir.x;
    float y = dir.y;
    float z = dir.z;

    b[0] = 0.28209479177387814f;
    b[1] = -0.4886025119029199f * y;
    b[2] =  0.4886025119029199f * z;
    b[3] = -0.4886025119029199f * x;
    b[4] =  1.0925484305920792f * x * y;
    b[5] = -1.0925484305920792f * y * z;
    b[6] =  0.31539156525252005f * (3.0f * z * z - 1.0f);
    b[7] = -1.0925484305920792f * x * z;
    b[8] =  0.5462742152960396f * (x * x - y * y);
}

// inverse of direction_sphere_uv in common.hlsl
float3 direction_from_sphere_uv(float2 uv)
{
    float phi   = (uv.x - 0.5f) * PI2;
    float sin_y = sin((0.5f - uv.y) * PI);
    float cos_y = sqrt(max(1.0f - sin_y * sin_y, 0.0f));
    return float3(cos(phi) * cos_y, sin_y, sin(phi) * cos_y);
}

void sh_load_l2(Texture2D tex_sh, out float3 L[9])
{
    [unroll]
    for (uint i = 0; i < 9; i++)
    {
        L[i] = tex_sh.Load(int3(i, 0, 0)).rgb;
    }
}

// cosine-convolved irradiance, optional ao band limit for dir gtao cone
// low ao narrows the visibility cone so higher sh bands fade out
float3 sh_irradiance_l2(float3 dir, float3 L[9], float ao)
{
    float b[9];
    sh_eval_basis_l2(normalize(dir), b);

    float visibility = saturate(ao);
    float k0         = sh_a0;
    float k1         = sh_a1 * visibility;
    float k2         = sh_a2 * visibility * visibility;

    float3 E = 0.0f.xxx;
    E += L[0] * (b[0] * k0);
    E += L[1] * (b[1] * k1);
    E += L[2] * (b[2] * k1);
    E += L[3] * (b[3] * k1);
    E += L[4] * (b[4] * k2);
    E += L[5] * (b[5] * k2);
    E += L[6] * (b[6] * k2);
    E += L[7] * (b[7] * k2);
    E += L[8] * (b[8] * k2);
    // Band attenuation only changes directionality. The DC band survives even
    // at zero visibility, so integrate the visible fraction as well. Otherwise
    // enclosed surfaces receive the full average sky and AO cannot darken them.
    return max(E, 0.0f.xxx) * visibility;
}

#endif
