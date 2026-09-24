/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
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
