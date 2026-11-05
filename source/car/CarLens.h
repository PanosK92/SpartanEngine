/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ========================
#include "WaterDrops.h"
#include "../rendering/SurfaceWater.h"
//===================================

namespace spartan
{
    // the water on the front element of the camera that follows the car
    // the lens is a flat pane of coated glass in the camera's frame, x right, y up, its normal z looks where the camera looks,
    // the drops on it live on the gpu (lens_drops.hlsl) and follow the same physics as the drops on the paint (WaterDrops.h):
    // the head on air spreads out over the glass from its centre and sweeps the drops to the edges the faster the car goes,
    // the camera's swing through a corner and its braking push them sideways and down, gravity runs the big ones down when
    // the car stands, drops that touch merge, and the glass a drop ran over stays wet for a while so later drops follow its path
    // this side only works out what the glass feels and how much water reaches it
    class CarLens
    {
    public:
        // conditions are in the camera's frame, spray is the kg/(m^2 s) of tyre mist the wake carries into the lens,
        // amount scales how much of the water reaches the lens
        static void Tick(const water_drops::Conditions& conditions, float spray, float aspect, float amount, float delta_time);
        static void Clear();

        // what the gpu steps the drops with, the impacts and the time since the last call, a wipe once after a clear
        static LensWater Consume();
    };
}
