/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
#include "../world/OceanWake.h"
#include <vector>
#include <cstdint>

namespace car { class Simulation; }

namespace spartan
{
    // one vertical slice of the chassis hull, body space, metres
    struct CarWaterColumn
    {
        float x      = 0.0f;
        float z      = 0.0f;
        float bottom = 0.0f;
        float top    = 0.0f;
    };

    // the sea against a car
    //
    // the chassis convex hulls are cast into vertical columns once, every physics step each column under the
    // waves takes archimedes buoyancy and quadratic drag at its own point, so a car slows hard as it ploughs
    // in, floats nose down while the cabin holds air, swamps through the door seals and sinks, the tires are
    // pushed and buoyed too, a drowned intake stalls the engine, and the water every column and tire holds
    // out of its place goes to OceanWake, which turns its motion into the bow wave and the wake
    struct CarWaterState
    {
        std::vector<CarWaterColumn> columns;
        std::vector<OceanWakeColumn> wake;
        std::vector<float> wake_xz;
        std::vector<float> wake_heights;
        const void* hull_key      = nullptr;
        uint32_t hull_shape_count = 0;
        float column_area         = 0.0f;
        float hull_volume         = 0.0f;
        float center_x            = 0.0f; // body space footprint centre, half extents and height range
        float center_z            = 0.0f;
        float half_x              = 0.0f;
        float half_z              = 0.0f;
        float bottom              = 0.0f;
        float top                 = 0.0f;
        float flood               = 0.0f; // share of the cabin full of water, 0 dry, 1 swamped
        float intake_wet          = 0.0f; // seconds the intake has been under water, a passing wave does not drown it
        float submerged           = 0.0f; // share of the hull volume under water
        bool in_water             = false;
    };

    namespace car_water
    {
        // call every physics step after the vehicle simulation, id names the car in the wake
        void tick(car::Simulation& simulation, CarWaterState& state, uint64_t id, float dt);
    }
}
