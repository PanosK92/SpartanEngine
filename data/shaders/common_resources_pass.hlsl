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

#ifndef SPARTAN_COMMON_RESOURCES_PASS
#define SPARTAN_COMMON_RESOURCES_PASS

// misc
Texture2D tex   : register(t7);
Texture2D tex2  : register(t8);
Texture2D tex3  : register(t9);
Texture2D tex4  : register(t10);
Texture2D tex5  : register(t11);
Texture2D tex6  : register(t12);
Texture3D tex3d : register(t13);

// storage textures/buffers (image_format unknown allows flexible format binding)
[[vk::image_format("unknown")]] RWTexture2D<float4> tex_uav                           : register(u0);
[[vk::image_format("unknown")]] RWTexture2D<float4> tex_uav2                          : register(u1);
[[vk::image_format("unknown")]] RWTexture2D<float4> tex_uav3                          : register(u2);
[[vk::image_format("unknown")]] RWTexture2D<float4> tex_uav4                          : register(u3);
[[vk::image_format("unknown")]] RWTexture3D<float4> tex3d_uav                         : register(u4);
[[vk::image_format("unknown")]] RWTexture2DArray<float4> tex_uav_sss                  : register(u5);
RWStructuredBuffer<uint> visibility                                                   : register(u6); // unused, kept for descriptor layout stability
globallycoherent RWStructuredBuffer<uint> g_atomic_counter                            : register(u7); // used by FidelityFX SPD
[[vk::image_format("unknown")]] globallycoherent RWTexture2D<float4> tex_uav_mips[12] : register(u8); // used by FidelityFX SPD
// integer format textures (vrs, etc)
RWTexture2D<uint> tex_uav_uint : register(u30);

#endif
