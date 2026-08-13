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

#ifndef SPARTAN_COMMON_RESOURCES_GBUFFER
#define SPARTAN_COMMON_RESOURCES_GBUFFER

// g-buffer
Texture2D tex_albedo   : register(t0);
Texture2D tex_normal   : register(t1);
Texture2D tex_material : register(t2);
Texture2D tex_velocity : register(t3);
Texture2D tex_depth    : register(t4);

// ray-tracing, the declaration alone makes the spir-v require the ray query capability, even when unused,
// because bindings are preserved, so it must not exist in modules built for devices without support
#ifdef RAY_TRACING_ENABLED
RaytracingAccelerationStructure tlas : register(t5);
#endif

// other
Texture2D tex_ssao : register(t6);

#endif
