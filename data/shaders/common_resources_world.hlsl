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

#ifndef SPARTAN_COMMON_RESOURCES_WORLD
#define SPARTAN_COMMON_RESOURCES_WORLD

#include "shared_buffers.h"

// noise
Texture2D tex_perlin : register(t14);

// terrain heightfield analysis, baked once per terrain generate, sampled in normalized terrain xz
// map a: r = curvature (0.5 is flat), g = flow accumulation, b = sky occlusion, a = sediment deposition
// map b: r = wear (bedrock exposure), g = insolation, b = normalized height, a = talus (scree)
// space 0, next to perlin, bindless arrays live in other spaces so these registers are free here
Texture2D<float4> tex_terrain_map_a : register(t15);
Texture2D<float4> tex_terrain_map_b : register(t16);
Texture2D<float>  tex_terrain_height : register(t17);

// restir reservoir textures (shared across path tracing, temporal, and spatial passes)
// kept contiguous so a single loop can bind all five slots starting from tex_reservoir_prev0
// the 5th slot carries the source primary g-buffer for the chosen path so the temporal and
// spatial passes can evaluate the source brdf and reconnection jacobian without resampling
// the current frame's g-buffer at a reprojected pixel (which is wrong on motion)
// layout is documented at pack_reservoir in restir_reservoir.hlsl
Texture2D<float4> tex_reservoir_prev0 : register(t22);
Texture2D<float4> tex_reservoir_prev1 : register(t23);
Texture2D<float4> tex_reservoir_prev2 : register(t24);
Texture2D<float4> tex_reservoir_prev3 : register(t25);
Texture2D<float4> tex_reservoir_prev4 : register(t26);

// fft ocean displacement history
Texture2DArray<float4> tex_ocean_displacement_previous : register(t27);

// exposure resolved by the camera on the previous frame
Texture2D<float> tex_effective_exposure : register(t28);

// wind field, baked once per frame, sampled by all wind-driven geometry
// rg = flow vector (signed, [-1,1]), b = gust pressure (0..1), a = micro turbulence (0..1)
Texture2D<float4> tex_wind_field : register(t29);

// fft ocean cascades, one array slice per cascade
// displacement.xyz = world-space offset, normal.xy = surface slope, normal.z = foam
Texture2DArray<float4> tex_ocean_displacement : register(t30);
Texture2DArray<float4> tex_ocean_normal       : register(t31);

// geometry info buffer for ray tracing (per-blas-instance offsets)
RWStructuredBuffer<GeometryInfo> geometry_infos : register(u20);

// restir reservoir uav bindings
RWTexture2D<float4> tex_reservoir0 : register(u21);
RWTexture2D<float4> tex_reservoir1 : register(u22);
RWTexture2D<float4> tex_reservoir2 : register(u23);
RWTexture2D<float4> tex_reservoir3 : register(u24);
RWTexture2D<float4> tex_reservoir4 : register(u25);

#endif
