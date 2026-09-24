/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

namespace spartan
{
    class Car;
    class Physics;

    namespace car_hud
    {
        // always-on cockpit overlay anchored to the bottom of the viewport
        void draw_driver_hud(Physics* physics);

        // single glanceable dashboard, p_open is bound to the F3 toggle state
        void draw_telemetry_window(Car* car, Physics* physics, bool* p_open);

    }
}
