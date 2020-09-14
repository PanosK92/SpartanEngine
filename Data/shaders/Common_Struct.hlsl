/*
Copyright(c) 2016-2020 Panos Karabelas

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

struct Material
{
    float4 albedo;
    float roughness;
    float metallic;
    float clearcoat;
    float clearcoat_roughness;
    float anisotropic;
    float anisotropic_rotation;
    float sheen;
    float sheen_tint;
    float occlusion;
    float3 padding;
    float emissive;
    float3 F0;
    bool is_sky;
};

struct Light
{
    float3  color;
    float3  position;
    float3  direction;
    float   distance_to_pixel;
    float   angle;
    float   bias;
    float   normal_bias;
    float   near;
    float   far;
    float   attenuation;
    float   intensity;
    float3  radiance;
};

struct Surface
{
    float2  uv;
    float   depth;
    float3  position;
    float3  normal;
    float3  camera_to_pixel;
    float   camera_to_pixel_length;
    float   n_dot_l;
};
