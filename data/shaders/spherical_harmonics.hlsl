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
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
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
    return max(E, 0.0f.xxx);
}

#endif
