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
        // always-on cockpit overlay anchored to the bottom of the viewport, the centre speedometer is skipped while the telemetry hud shows its own
        void draw_driver_hud(Physics* physics, bool show_speedometer);

        // full telemetry laid out around the car on screen, toggled with F3
        void draw_telemetry_hud(Car* car, Physics* physics);

    }
}
