/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#ifndef SPARTAN_COMMON_PARTICLE_FLUID
#define SPARTAN_COMMON_PARTICLE_FLUID

// the smoke fluid
//
// a box of air around the car the camera follows, solved as an incompressible fluid by particles_fluid.hlsl.
// the box is fixed to the world and scrolls in whole cells, so smoke that is left behind stays where it was
// left. velocity is what the particles ride as tracers, and the scalar texture carries the smoke itself,
// which the volumetric march reads next to its own froxel grid. past the faces of the box the particles
// carry the trail on alone, and the weight below is the crossfade between the two.

// keep in sync with renderer_particle_fluid_width, height and depth in Renderer_Definitions.h
static const uint3 fluid_resolution = uint3(144u, 48u, 144u);
// metres over which the grid hands the smoke back to the particles on its way out of a side or the top,
// the top is the short one, a plume is tall long before it is wide and a slow fade there reads as a ceiling
static const float fluid_fade_near  = 0.5f;
static const float fluid_fade_far   = 3.5f;
static const float fluid_fade_top   = 1.5f;

Texture3D<float4> tex_fluid_velocity : register(t51); // xyz = metres per second, w = one inside a solid
Texture3D<float4> tex_fluid_scalar   : register(t52); // r = density, g = heat, b = density weighted age, a = density weighted shade

float3 fluid_origin()
{
    return pass_float3(pass_particles_fluid::origin);
}

float fluid_cell_size()
{
    return pass_float(pass_particles_fluid::cell_size);
}

bool fluid_enabled()
{
    return fluid_cell_size() > 0.0f;
}

float3 fluid_size()
{
    return float3(fluid_resolution) * fluid_cell_size();
}

float3 fluid_uvw(float3 position)
{
    return (position - fluid_origin()) / fluid_size();
}

// one well inside the box, zero at its sides and its top and anywhere outside it, the floor is the ground
// and smoke does not leave through it, so it takes no part in the fade
float fluid_weight(float3 position)
{
    if (!fluid_enabled())
    {
        return 0.0f;
    }

    float3 size  = fluid_size();
    float3 local = position - fluid_origin();
    if (local.y < 0.0f)
    {
        return 0.0f;
    }

    // one fade per pair of faces, multiplied, so the corners of the box come out rounded and what is
    // left of a thin haze at the edge does not draw the outline of a rectangle on the road
    float3 edge = float3(min(local.x, size.x - local.x), size.y - local.y, min(local.z, size.z - local.z));
    float3 fade = smoothstep(fluid_fade_near, float3(fluid_fade_far, fluid_fade_top, fluid_fade_far), edge);

    return fade.x * fade.y * fade.z;
}

// xyz = the velocity at a point, w = how much of what was sampled lies inside a solid, whose cells hold
// the speed of its surface rather than of any air
float4 fluid_sample_air(float3 position)
{
    return tex_fluid_velocity.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), fluid_uvw(position), 0.0f);
}

float4 fluid_sample_scalar(float3 position)
{
    return tex_fluid_scalar.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), fluid_uvw(position), 0.0f);
}

// the smoke standing at a point in the units the volumetric march works in
// r = shade, g = age from zero to one, a = density, the layout of a froxel of the particle volume
float4 fluid_sample_medium(float3 position)
{
    float weight = fluid_weight(position);
    if (weight <= 0.0f)
    {
        return 0.0f;
    }

    float4 scalar  = fluid_sample_scalar(position);
    float  density = max(scalar.r, 0.0f);
    float  inverse = 1.0f / max(density, 0.0001f);

    return float4(saturate(scalar.a * inverse), saturate(scalar.b * inverse), 0.0f, density * weight);
}

#endif
