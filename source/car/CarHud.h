/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include <cstdint>

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
        void draw_simulated_systems(Physics* physics);

        // every car in play, top right, returns the one the player clicked: drive it if it is free, spectate it if someone else drives
        Car* draw_car_picker(Car* viewed);

        // showroom paint panel, top left: ferrari factory colours and a finish lab, applied live to the car
        void draw_showroom_panel(Car* car, bool collapsed);

        struct service_row
        {
            const char* name   = "";
            const char* detail = "";
            bool enabled       = true;
        };

        struct service_menu
        {
            const char* shop         = "";
            const service_row* rows  = nullptr;
            uint32_t row_count       = 0;
            const char* status       = "";
            float progress           = -1.0f; // a bar under the rows while a service runs
        };

        // the shop's menu, lower centre, returns the row the player clicked or -1
        int draw_service_menu(const service_menu& menu);
    }
}
