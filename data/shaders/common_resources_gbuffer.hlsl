/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#ifndef SPARTAN_COMMON_RESOURCES_GBUFFER
#define SPARTAN_COMMON_RESOURCES_GBUFFER

// g-buffer
Texture2D tex_albedo   : register(t0);
Texture2D tex_normal   : register(t1);
Texture2D tex_material : register(t2);
Texture2D tex_velocity : register(t3);
Texture2D tex_depth    : register(t4);
Texture2D tex_emissive : register(t33);

// ray-tracing, the declaration alone makes the spir-v require the ray query capability, even when unused,
// because bindings are preserved, so it must not exist in modules built for devices without support
#ifdef RAY_TRACING_ENABLED
RaytracingAccelerationStructure tlas : register(t5);
#endif

// other
Texture2D tex_ssao : register(t6);

#endif
