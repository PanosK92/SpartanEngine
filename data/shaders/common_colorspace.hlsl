/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// for details, read my blog post: https://panoskarabelas.com/blog/posts/hdr_in_under_10_minutes/

float get_gamma()
{
    return 2.4f;
}

float3 srgb_to_linear(float3 color)
{
    float3 linear_low  = color / 12.92;
    float3 linear_high = pow(max((color + 0.055) / 1.055, 0.0f), get_gamma());
    float3 is_high     = step(0.0404482362771082, color);
    return lerp(linear_low, linear_high, is_high);
}

float3 linear_to_srgb(float3 color)
{
    float3 srgb_low  = color * 12.92;
    float3 srgb_high = 1.055 * pow(max(color, 0.0f), 1.0 / get_gamma()) - 0.055;
    float3 is_high   = step(0.00313066844250063, color);
    return lerp(srgb_low, srgb_high, is_high);
}
