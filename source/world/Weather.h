/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ============
#include "../math/Vector2.h"
#include "../math/Vector3.h"
//=======================

namespace spartan
{
    class Entity;

    // rain, driven by the world environment
    // it soaks exposed surfaces, fills the puddles, falls around the camera and is heard,
    // a height grid of the topmost static surface around the camera tells the renderer what is under cover
    class Weather
    {
    public:
        static constexpr uint32_t occlusion_resolution = 128;
        static constexpr float occlusion_cell_size     = 0.5f;

        static void Tick(float delta_time);
        static void Reset();

        // 0 dry to 1 downpour, as authored in the environment
        static float GetRain();
        // how soaked exposed surfaces are, lags the rain in both directions
        static float GetWetness();
        // the world's authored puddliness or the water the rain has left behind, whichever is more
        static float GetPuddliness();
        // only what the rain has filled, it stands in the open and not under a roof
        static float GetRainPuddliness();
        // 0 under open sky to 1 under a roof, at the camera
        static float GetShelter();

        // metres of water standing on the ground at a world point, the rain film plus the puddles the renderer draws there
        // porous ground (soil, grass, gravel) drinks the film and holds less standing water
        static float GetWaterDepth(const math::Vector3& position, bool porous);

        // Fraction of rain reaching a world position, sampled from the shelter grid.
        static float GetExposure(const math::Vector3& position);

        // occlusion_resolution squared heights of the topmost static surface, addressed toroidally by world cell
        static const float* GetOcclusionHeights();
        // world xz of the min corner of the window the grid currently covers
        static math::Vector2 GetOcclusionMin();
    };
}
