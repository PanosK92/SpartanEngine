/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/
#pragma once

#include "imgui/source/imgui.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cfloat>
#include <string>
#include <vector>

// Presentation only: a snapshot keeps drawing independent of the vehicle. Units are SI unless named.
// Every panel draws in its own local design space (origin top left) so the hud can place panels freely.
namespace spartan::car_hud::telemetry
{
    inline constexpr float psi_per_bar = 14.503774f;
    enum class control { none, abs, traction, stability, steering, automatic, drs, turbo };
    struct vehicle_options
    {
        std::vector<std::string> names;
        int selected = -1;
        bool full_simulation = true, skeleton = false, collision = false;
    };
    struct corner
    {
        bool grounded = false, abs = false;
        float surface[3] = {}, core = 0, pressure = 0, wear = 0, damage = 0;
        float load = 0, saturation = 0, compression = 0, slip_ratio = 0, slip_angle = 0;
        float brake_temp = 0, brake_efficiency = 1;
        std::string road = "--";
        float surface_grip = 0, surface_rolling = 0;
        bool mixed_surface = false;
    };

    struct snapshot
    {
        uint64_t vehicle_id = 0;
        std::string name, gear, differential;
        float speed = 0, rpm = 0, redline = 1, boost = 0, boost_max = 1;
        float throttle = 0, brake = 0, steering = 0, handbrake = 0;
        float lateral_g = 0, longitudinal_g = 0, torque = 0, motor_kw = 0, clutch = 0;
        float battery_soc = 0, battery_temp = 0, battery_kw = 0, battery_hot = 55;
        float drag = 0, front_downforce = 0, rear_downforce = 0, ride_height = 0;
        float optimal_temp = 80, temp_range = 30, tc_reduction = 0;
        double distance = 0;
        bool abs_enabled = false, tc_enabled = false, tc_active = false;
        bool stability_enabled = false, stability_active = false, steering_enabled = false, automatic = false;
        bool drs_enabled = false, drs_active = false, turbo = false, hybrid = false;
        bool shifting = false, limiter = false, engine_running = false, aero_valid = false;
        bool full_simulation = true;
        std::array<corner, 4> wheels;
    };

    struct sample
    {
        double time = 0;
        float speed = 0, throttle = 0, brake = 0, lateral_g = 0, longitudinal_g = 0;
        std::array<float, 4> travel = {};
    };

    struct history
    {
        static constexpr int capacity = 241;
        static constexpr double duration = 8.0;
        std::array<sample, capacity> samples = {};
        int count = 0, next = 0;
        uint64_t vehicle_id = 0;
        std::string car_name;
        bool full_simulation = true;
        double last_seen = -1, last_distance = 0;

        const sample& at(int i) const { return samples[(next - count + capacity + i) % capacity]; }

        void update(const snapshot& s, double now)
        {
            // Never join traces across hidden huds, car switches or resets.
            if (vehicle_id != s.vehicle_id || car_name != s.name || full_simulation != s.full_simulation ||
                now - last_seen > 0.5 || now < last_seen || s.distance < last_distance)
            {
                count = next = 0;
                vehicle_id = s.vehicle_id;
                car_name = s.name;
                full_simulation = s.full_simulation;
            }
            last_seen = now;
            last_distance = s.distance;
            if (count && now - at(count - 1).time < 1.0 / 30.0)
                return;
            sample& p = samples[next];
            p = {now, s.speed, s.throttle, s.brake, s.lateral_g, s.longitudinal_g, {}};
            for (int i = 0; i < 4; ++i)
                p.travel[i] = s.wheels[i].compression;
            next = (next + 1) % capacity;
            count = std::min(count + 1, capacity);
        }
    };

    inline constexpr ImU32 ink = IM_COL32(234, 241, 246, 255);
    inline constexpr ImU32 muted = IM_COL32(151, 170, 186, 255);
    inline constexpr ImU32 cyan = IM_COL32(83, 216, 229, 255);
    inline constexpr ImU32 green = IM_COL32(126, 223, 163, 255);
    inline constexpr ImU32 amber = IM_COL32(255, 194, 106, 255);
    inline constexpr ImU32 red = IM_COL32(255, 111, 115, 255);
    inline constexpr ImU32 blue = IM_COL32(120, 169, 255, 255);
    inline constexpr ImU32 track = IM_COL32(43, 58, 72, 255);
    inline constexpr ImU32 wheel_colors[4] = {cyan, amber, blue, IM_COL32(206, 159, 244, 255)};

    // hud panel sizes in design units
    inline constexpr float corner_w = 244, corner_h = 189;
    inline constexpr float g_force_w = 268, g_force_h = 225;
    inline constexpr float powertrain_w = 304, powertrain_h = 225;
    inline constexpr float aero_w = 304, aero_h = 153;
    inline constexpr float history_h = 106;
    inline constexpr float header_w = 1200, header_h = 101;
    inline constexpr float inputs_w = 268, inputs_h = 153;
    inline constexpr float chassis_w = 304, chassis_h = 230;
    inline constexpr float strip_w = 1200, strip_h = 101;
    inline constexpr float setup_h = 150;

    inline float unit(float x) { return std::isfinite(x) ? std::clamp(x, 0.0f, 1.0f) : 0.0f; }

    // Uniform scaling preserves circular instruments. at() re-bases a painter on a panel's top left.
    struct painter
    {
        ImDrawList* dl;
        ImVec2 origin;
        float scale;
        ImU32 fill = IM_COL32(20, 31, 43, 250);

        painter at(float x, float y) const { return {dl, point(x, y), scale, fill}; }
        ImVec2 point(float x, float y) const { return {origin.x + x * scale, origin.y + y * scale}; }
        void rect(float x, float y, float w, float h, ImU32 color, float radius = 4) const
        {
            dl->AddRectFilled(point(x, y), point(x + w, y + h), color, radius * scale);
        }
        void line(float x, float y, float ex, float ey, ImU32 color, float width = 1) const
        {
            dl->AddLine(point(x, y), point(ex, ey), color, width * scale);
        }
        void text(float x, float y, float size, ImU32 color, const char* value) const
        {
            dl->AddText(ImGui::GetFont(), size * scale, point(x, y), color, value);
        }
        template<typename... Args>
        void label(float x, float y, float size, ImU32 color, const char* format, Args... args) const
        {
            char buffer[160];
            std::snprintf(buffer, sizeof(buffer), format, args...);
            text(x, y, size, color, buffer);
        }
        void right(float x, float y, float size, ImU32 color, const char* value) const
        {
            const float width = ImGui::GetFont()->CalcTextSizeA(size * scale, FLT_MAX, 0, value).x / scale;
            text(x - width, y, size, color, value);
        }
        void card(float x, float y, float w, float h, const char* title, const char* note = "") const
        {
            // soft drop shadow lifts the card off the 3d scene behind it
            dl->AddRectFilled(point(x + 2, y + 4), point(x + w + 2, y + h + 4), IM_COL32(0, 0, 0, 70), 9 * scale);
            rect(x, y, w, h, fill, 9);
            dl->AddRect(point(x, y), point(x + w, y + h), IM_COL32(53, 73, 90, 180), 9 * scale);
            text(x + 16, y + 13, 13, muted, title);
            right(x + w - 16, y + 13, 12, muted, note);
        }
        void bar(float x, float y, float w, float h, float value, ImU32 color) const
        {
            rect(x, y, w, h, track, h / 2);
            if (unit(value) > 0)
                rect(x, y, w * unit(value), h, color, h / 2);
        }
        void pill(float x, float y, float w, const char* label, const char* state, ImU32 color) const
        {
            rect(x, y, w, 29, IM_COL32(25, 39, 51, 255), 5);
            text(x + 9, y + 7, 12, muted, label);
            right(x + w - 9, y + 7, 12, color, state);
        }
    };

    // car model, simulation mode and visualization, 1200 x 50
    inline void draw_vehicle_options(const painter& p, vehicle_options& options)
    {
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImGui::PushID("telemetry_vehicle_options");
        ImGui::PushFont(nullptr, 14 * p.scale);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6 * p.scale, 3 * p.scale));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4 * p.scale);
        p.rect(0, 0, 1200, 50, p.fill, 9);
        p.text(12, 5, 11, muted, "CAR MODEL");
        ImGui::SetCursorScreenPos(p.point(12, 22));
        ImGui::SetNextItemWidth(396 * p.scale);
        const char* name = options.selected >= 0 && options.selected < static_cast<int>(options.names.size()) ?
            options.names[options.selected].c_str() : "Current car";
        if (ImGui::BeginCombo("##car", name))
        {
            for (int i = 0; i < static_cast<int>(options.names.size()); ++i)
            {
                ImGui::PushID(i);
                const bool selected = i == options.selected;
                if (ImGui::Selectable(options.names[i].c_str(), selected)) options.selected = i;
                if (selected) ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }

        p.text(430, 5, 11, muted, "SIMULATION MODE");
        ImGui::SetCursorScreenPos(p.point(430, 22));
        ImGui::SetNextItemWidth(210 * p.scale);
        int mode = options.full_simulation ? 0 : 1;
        if (ImGui::Combo("##simulation", &mode, "Full physics\0Cheap / traffic\0"))
            options.full_simulation = mode == 0;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Full simulates the physical suspension and drivetrain. Cheap uses the simplified traffic model.");

        p.text(664, 5, 11, muted, "VISUALIZATION");
        ImGui::SetCursorScreenPos(p.point(664, 22));
        ImGui::SetNextItemWidth(258 * p.scale);
        int view = options.skeleton ? 1 : 0;
        if (ImGui::Combo("##visualization", &view, "Full car\0Physics skeleton\0"))
            options.skeleton = view == 1;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Skeleton hides the body mesh and shows physical bodies, joints and forces. Frame and engine internals are schematic.");

        ImGui::SetCursorScreenPos(p.point(950, 22));
        bool collision = options.skeleton && options.collision;
        if (ImGui::Checkbox("Collision shape", &collision))
        {
            options.collision = collision;
            if (collision) options.skeleton = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Draw the chassis collision hull in purple. Enabling this also selects the physics skeleton view.");
        ImGui::PopStyleVar(2);
        ImGui::PopFont();
        ImGui::PopID();
        ImGui::SetCursorScreenPos(cursor);
    }

    // cold pressure for all four tires, 1200 x 50
    inline bool draw_tire_pressure(const painter& p, bool enabled, float& pressure_bar, float default_bar)
    {
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImGui::PushID("tire_pressure");
        ImGui::PushFont(nullptr, 14 * p.scale);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6 * p.scale, 3 * p.scale));
        p.rect(0, 0, 1200, 50, p.fill, 9);
        p.text(12, 8, 12, muted, "TIRE PRESSURE / ALL TIRES");
        p.text(12, 28, 11, muted, "Cold setting; live PSI shown on the tire cards");
        ImGui::BeginDisabled(!enabled);
        ImGui::SetCursorScreenPos(p.point(285, 13));
        ImGui::SetNextItemWidth(470 * p.scale);
        float psi = pressure_bar * psi_per_bar;
        bool changed = ImGui::SliderFloat("##psi", &psi, 0.05f * psi_per_bar, 4.0f * psi_per_bar,
            "%.1f PSI", ImGuiSliderFlags_AlwaysClamp);
        if (changed) pressure_bar = psi / psi_per_bar;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Lower pressure softens the tires and increases deformation. Higher pressure stiffens them.\nApplies live to all four tires. Ctrl+click to type a value.\nLive pressure also depends on temperature and damage.");
        ImGui::SetCursorScreenPos(p.point(777, 13));
        if (ImGui::Button("Reset pressure", ImVec2(150 * p.scale, 0)))
        {
            pressure_bar = default_bar;
            changed = true;
        }
        ImGui::EndDisabled();
        p.label(950, 19, 12, muted, "DEFAULT %.1f PSI", default_bar * psi_per_bar);
        ImGui::PopStyleVar();
        ImGui::PopFont();
        ImGui::PopID();
        ImGui::SetCursorScreenPos(cursor);
        return changed;
    }

    // clickable assist switches, one 1200 wide row of 36 high tiles
    inline control draw_controls(const painter& p, const snapshot& s)
    {
        control result = control::none;
        const ImVec2 saved_cursor = ImGui::GetCursorScreenPos();
        ImGui::PushID("telemetry_assists");
        bool abs_active = false;
        for (const corner& w : s.wheels) abs_active |= w.abs;
        const auto toggle = [&](int index, control action, const char* title, bool enabled, bool active, const char* hint)
        {
            constexpr float width = (1200.0f - 6 * 8) / 7;
            const float x = index * (width + 8);
            const float y = 0;
            ImGui::SetCursorScreenPos(p.point(x, y));
            ImGui::BeginDisabled(!s.full_simulation);
            const bool clicked = ImGui::InvisibleButton(title, ImVec2(width * p.scale, 36 * p.scale));
            const bool hovered = ImGui::IsItemHovered();
            ImGui::EndDisabled();
            if (clicked)
            {
                result = action;
                enabled = !enabled;
            }
            const ImU32 color = !s.full_simulation ? muted : enabled ? cyan : muted;
            p.rect(x, y, width, 36, hovered ? IM_COL32(40, 61, 77, 255) : IM_COL32(25, 39, 51, 235), 5);
            if (active && enabled && s.full_simulation)
                p.dl->AddRect(p.point(x, y), p.point(x + width, y + 36), cyan, 5 * p.scale, 1.5f * p.scale);
            p.text(x + 10, y + 4, 12, color, title);
            p.text(x + 10, y + 21, 10, color, !s.full_simulation ? "FULL SIM ONLY" :
                enabled ? active ? "ON / ACTIVE" : "ON" : "OFF");
            p.rect(x + width - 36, y + 12, 26, 13, enabled && s.full_simulation ? cyan : track, 7);
            p.dl->AddCircleFilled(p.point(x + width - (enabled ? 16 : 30), y + 18.5f), 4.5f * p.scale,
                enabled ? IM_COL32(18, 36, 46, 255) : muted);
            if (hovered)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::SetTooltip("%s\nClick to turn %s.", hint, enabled ? "off" : "on");
            }
        };
        toggle(0, control::abs, "ABS", s.abs_enabled, abs_active, "Anti-lock braking prevents wheel lock under braking.");
        toggle(1, control::traction, "TRACTION", s.tc_enabled, s.tc_active, "Traction control reduces power when driven wheels spin.");
        toggle(2, control::stability, "STABILITY", s.stability_enabled, s.stability_active, "Stability control adjusts wheel braking and power to limit oversteer.");
        toggle(3, control::steering, "STEER ASSIST", s.steering_enabled, false, "Reduces steering sensitivity at speed; restores your configured strength when enabled.");
        toggle(4, control::automatic, "AUTO SHIFT", s.automatic, false, "Automatic gear changes. When off, shift with PgUp/PgDn or L1/R1.");
        toggle(5, control::drs, "DRS", s.drs_enabled, s.drs_active, "Allows the drag reduction system to open the rear wing.");
        toggle(6, control::turbo, "TURBO", s.turbo, false, "Enables the car's configured turbocharger.");
        ImGui::PopID();
        ImGui::SetCursorScreenPos(saved_cursor);
        return result;
    }

    // title, health pills and a setup button over the assist switches, strip_w x strip_h
    inline control draw_strip(const painter& p, const snapshot& s, bool& setup_open)
    {
        p.text(4, 6, 15, ink, "VEHICLE / TELEMETRY");
        // Clip arbitrary preset names without changing the rest of the header.
        p.dl->PushClipRect(p.point(190, 0), p.point(560, 29), true);
        p.text(190, 7, 13, muted, s.name.c_str());
        p.dl->PopClipRect();

        bool brake_fade = false, damage = false;
        for (const corner& w : s.wheels)
        {
            brake_fade |= w.brake_efficiency < 0.8f;
            damage |= w.damage > 0.1f || w.wear > 0.7f;
        }
        if (s.full_simulation)
        {
            p.pill(570, 0, 150, "BRAKES", brake_fade ? "FADING" : "OK", brake_fade ? red : green);
            p.pill(728, 0, 150, "TIRES", damage ? "CHECK" : "OK", damage ? amber : green);
        }

        const ImVec2 saved_cursor = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(p.point(886, 0));
        const bool clicked = ImGui::InvisibleButton("##telemetry_setup", ImVec2(114 * p.scale, 29 * p.scale));
        const bool hovered = ImGui::IsItemHovered();
        ImGui::SetCursorScreenPos(saved_cursor);
        if (clicked)
            setup_open = !setup_open;
        p.rect(886, 0, 114, 29, hovered ? IM_COL32(40, 61, 77, 255) : IM_COL32(25, 39, 51, 235), 5);
        p.text(896, 7, 12, setup_open ? cyan : muted, setup_open ? "SETUP  -" : "SETUP  +");
        if (hovered)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("Car model, simulation mode, visualization, tire pressure and CSV recording.");
        }
        p.right(1200, 8, 12, cyan, s.full_simulation ? "LIVE   /   F3 TO HIDE" : "CHEAP SIM   /   F3 TO HIDE");

        const control result = draw_controls(p.at(0, 39), s);
        p.text(2, 84, 11, muted, "CLICK SWITCHES TO TOGGLE");
        p.right(1198, 84, 11, muted, "I/C/O: tread zones  |  Blue: cold  Green: target  Amber: hot  |  Inputs: %");
        return result;
    }

    // speed, gear, engine speed and boost, header_w x header_h
    inline void draw_header(const painter& p, const snapshot& s)
    {
        p.card(0, 0, header_w, header_h, "");
        p.label(18, 7, 58, ink, "%.0f", s.speed);
        p.text(22, 73, 12, muted, "SPEED / km/h");
        p.line(157, 19, 157, 83, track);
        p.text(181, 9, 51, s.shifting ? amber : cyan, s.gear.c_str());
        p.text(183, 73, 12, muted, s.shifting ? "SHIFTING" : "GEAR");
        p.label(288, 17, 27, ink, "%.0f", s.rpm);
        p.text(385, 28, 12, muted, "RPM");
        p.right(888, 26, 12, s.limiter ? red : muted, s.limiter ? "REV LIMITER" : "ENGINE SPEED");
        const float rpm_fraction = unit(s.rpm / std::max(s.redline, 1.0f));
        for (int i = 0; i < 40; ++i)
            p.rect(288 + i * 15.0f, 54, 11, 13, rpm_fraction * 40 > i ? (i >= 36 ? red : i >= 32 ? amber : cyan) : track, 2);
        p.label(288, 75, 11, muted, "0                           REDLINE %.0f rpm", s.redline);
        p.line(924, 19, 924, 83, track);
        p.label(947, 17, 27, s.turbo ? cyan : ink, "%.2f", s.boost);
        p.text(947, 54, 12, muted, s.turbo ? "BOOST / bar" : "NATURALLY ASPIRATED");
        p.bar(947, 79, 230, 4, s.turbo ? s.boost / std::max(s.boost_max, 0.01f) : 0, cyan);
    }

    // throttle, brake, handbrake and steering, inputs_w x inputs_h
    inline void draw_inputs(const painter& p, const snapshot& s)
    {
        p.card(0, 0, inputs_w, inputs_h, "DRIVER INPUTS");
        const auto input = [&](float y, const char* name, float value, ImU32 color)
        {
            p.text(16, y, 12, muted, name);
            p.bar(100, y + 4, 99, 6, value, color);
            p.label(215, y - 1, 13, ink, "%.0f", value * 100);
        };
        input(40, "THROTTLE", s.throttle, cyan);
        input(65, "BRAKE", s.brake, red);
        input(90, "HANDBRAKE", s.handbrake, amber);
        p.text(16, 124, 12, muted, "STEERING");
        p.line(100, 130, 199, 130, track, 4);
        p.line(149, 123, 149, 137, muted);
        p.dl->AddCircleFilled(p.point(100 + unit(s.steering * 0.5f + 0.5f) * 99, 130), 4 * p.scale, cyan);
        p.label(210, 123, 12, ink, "%+.0f", s.steering * 100);
    }

    // schematic top view with grounded wheels and the front/rear load split, chassis_w x chassis_h
    inline void draw_chassis(const painter& p, const snapshot& s)
    {
        p.card(0, 0, chassis_w, chassis_h, "CHASSIS", "TOP VIEW");
        p.text(49, 38, 11, muted, "FRONT");
        p.rect(40, 56, 54, 146, IM_COL32(25, 42, 55, 255), 19);
        p.dl->AddRect(p.point(40, 56), p.point(94, 202), IM_COL32(71, 102, 124, 255), 19 * p.scale);
        p.rect(46, 80, 42, 32, IM_COL32(48, 74, 92, 255), 7);
        p.rect(46, 158, 42, 20, IM_COL32(48, 74, 92, 255), 5);
        for (int i = 0; i < 4; ++i)
        {
            const float x = i % 2 ? 95.0f : 28.0f, y = i < 2 ? 70.0f : 160.0f;
            p.rect(x, y, 11, 27, s.wheels[i].grounded ? wheel_colors[i] : track, 3);
            p.line(i % 2 ? 109.0f : 25.0f, y + 13, i % 2 ? 120.0f : 14.0f, y + 13, track);
        }
        p.text(51, 208, 11, muted, "REAR");
        const float front_load = s.wheels[0].load + s.wheels[1].load;
        const float rear_load = s.wheels[2].load + s.wheels[3].load;
        p.text(150, 60, 11, muted, "LOAD F/R");
        if (front_load + rear_load > 1)
        {
            const float front = front_load / (front_load + rear_load);
            p.label(150, 80, 17, ink, "%.0f / %.0f", front * 100, (1 - front) * 100);
            p.bar(150, 108, 134, 5, front, cyan);
        }
        else
            p.text(150, 84, 12, amber, "UNLOADED");
        const char* names[] = {"FL", "FR", "RL", "RR"};
        for (int i = 0; i < 4; ++i)
        {
            const float y = 128 + i * 18.0f;
            p.rect(150, y + 5, 10, 3, wheel_colors[i], 1);
            p.label(166, y, 11, s.wheels[i].grounded ? ink : amber, "%s  %.2f kN%s", names[i], s.wheels[i].load * 0.001f, s.wheels[i].grounded ? "" : "  AIR");
        }
    }

    inline ImU32 heat(float temperature, const snapshot& s)
    {
        const float band = std::max(s.temp_range * 0.5f, 5.0f);
        return temperature < s.optimal_temp - band ? blue :
            temperature > s.optimal_temp + band ? amber : green;
    }

    inline void draw_corner(const painter& p, float x, float y, int i, const snapshot& s)
    {
        const corner& w = s.wheels[i];
        const char* names[] = {"FRONT LEFT", "FRONT RIGHT", "REAR LEFT", "REAR RIGHT"};
        const char* state = !w.grounded ? "AIR" : w.abs ? "ABS" : w.saturation >= 0.95f ? "SLIDE" : "CONTACT";
        const ImU32 status = !w.grounded || w.saturation >= 0.95f ? amber : w.abs ? cyan : green;
        p.card(x, y, corner_w, corner_h, names[i]);
        p.rect(x, y + 14, 3, 14, wheel_colors[i], 1);
        p.right(x + 230, y + 14, 12, status, state);
        // Inside / centre / outside are separate measured tread temperatures.
        const char* zones[] = {"I", "C", "O"};
        for (int z = 0; z < 3; ++z)
        {
            const float zx = x + 15 + z * 29.0f;
            p.rect(zx, y + 39, 25, 42, IM_COL32(33, 47, 61, 255), 4);
            p.rect(zx, y + 39, 25, 4, heat(w.surface[z], s), 2);
            p.text(zx + 9, y + 46, 10, muted, zones[z]);
            p.label(zx + 3, y + 61, 13, heat(w.surface[z], s), "%.0f", w.surface[z]);
        }
        p.label(x + 112, y + 37, 25, ink, "%.0f C", w.core);
        p.label(x + 113, y + 66, 12, muted, "%.1f PSI", w.pressure * psi_per_bar);
        p.label(x + 15, y + 89, 12, muted, "WEAR %.0f%%", w.wear * 100);
        p.bar(x + 89, y + 93, 37, 4, w.wear, w.wear > 0.7f ? red : amber);
        p.label(x + 139, y + 89, 12, w.brake_efficiency < 0.8f ? red : ink, "BRK %.0f C", w.brake_temp);
        p.label(x + 15, y + 110, 12, ink, "%.2f kN", w.load * 0.001f);
        p.label(x + 112, y + 110, 12, status, "GRIP USED %.0f%%", w.saturation * 100);
        p.bar(x + 15, y + 130, 211, 5, w.saturation, status);
        p.label(x + 15, y + 141, 11, muted, "SLIP %+.0f%% / %+.1f deg", w.slip_ratio * 100, w.slip_angle);
        p.right(x + 229, y + 141, 10, muted, ("TRAVEL " + std::to_string(static_cast<int>(w.compression * 100)) + "%").c_str());
        p.label(x + 15, y + 157, 11, w.surface_grip < 0.8f ? amber : ink, "%s%s",
            w.grounded ? w.road.c_str() : "NO CONTACT", w.grounded && w.mixed_surface ? " / MIXED" : "");
        if (w.grounded)
            p.label(x + 15, y + 173, 10, muted, "SURFACE GRIP %.0f%%   ROLL x%.1f", w.surface_grip * 100, w.surface_rolling);
    }

    template<typename Value>
    inline void trace(const painter& p, const history& h, double now, float x, float y, float w, float height,
        float maximum, ImU32 color, Value value)
    {
        bool previous_valid = false;
        ImVec2 previous;
        for (int i = 0; i < h.count; ++i)
        {
            const sample& s = h.at(i);
            const double age = now - s.time;
            if (age > history::duration || age < 0)
                continue;
            ImVec2 point = p.point(x + w * static_cast<float>(1.0 - age / history::duration),
                y + height * (1 - unit(value(s) / maximum)));
            if (previous_valid)
                p.dl->AddLine(previous, point, color, 1.4f * p.scale);
            previous = point;
            previous_valid = true;
        }
    }

    // friction circle with a two second trail, g_force_w x g_force_h
    inline void draw_g_force(const painter& p, const snapshot& s, const history& h, double now)
    {
        p.card(0, 0, g_force_w, g_force_h, "G-FORCE", "2 g RANGE");
        const float cx = 134, cy = 110, radius = 67;
        for (int r = 1; r <= 2; ++r)
            p.dl->AddCircle(p.point(cx, cy), radius * r * 0.5f * p.scale, track, 64);
        p.line(cx - radius, cy, cx + radius, cy, track);
        p.line(cx, cy - radius, cx, cy + radius, track);
        p.text(111, 33, 10, muted, "ACCEL");
        p.text(113, 182, 10, muted, "BRAKE");
        p.text(47, 105, 11, muted, "L");
        p.text(215, 105, 11, muted, "R");
        const auto g_point = [&](float lat, float lon)
        {
            const float length = std::sqrt(lat * lat + lon * lon);
            const float factor = radius / std::max(2.0f, length);
            return p.point(cx + lat * factor, cy - lon * factor);
        };
        for (int i = 0; i < h.count; ++i)
        {
            const sample& v = h.at(i);
            const double age = now - v.time;
            if (age > 2.0 || age < 0)
                continue;
            p.dl->AddCircleFilled(g_point(v.lateral_g, v.longitudinal_g), 2 * p.scale,
                IM_COL32(83, 216, 229, static_cast<int>(150 * (1 - age / 2.0))));
        }
        p.dl->AddCircleFilled(g_point(s.lateral_g, s.longitudinal_g), 5 * p.scale, cyan);
        p.label(16, 202, 13, ink, "LAT %+.2f", s.lateral_g);
        p.label(147, 202, 13, ink, "LONG %+.2f", s.longitudinal_g);
    }

    // engine, motor, clutch and battery, powertrain_w x powertrain_h
    inline void draw_powertrain(const painter& p, const snapshot& s)
    {
        p.card(0, 0, powertrain_w, powertrain_h, "POWERTRAIN", s.engine_running ? "RUNNING" : "ENGINE OFF");
        p.label(16, 38, 30, ink, "%.0f", s.torque);
        p.text(16, 74, 12, muted, "ENGINE / Nm");
        p.label(173, 38, 30, cyan, "%.0f", s.motor_kw);
        p.text(173, 74, 12, muted, "MOTOR / kW");
        p.line(16, 101, 288, 101, track);
        p.text(16, 114, 12, muted, "CLUTCH");
        p.bar(88, 119, 80, 5, s.clutch, cyan);
        p.label(184, 114, 12, ink, "%.0f%% coupled", s.clutch * 100);
        if (s.hybrid)
        {
            p.label(16, 145, 13, s.battery_soc < 0.15f ? amber : green, "BATTERY %.0f%%", s.battery_soc * 100);
            p.label(171, 145, 13, s.battery_temp >= s.battery_hot ? amber : ink, "%.0f C", s.battery_temp);
            p.bar(16, 169, 272, 6, s.battery_soc, green);
            p.label(16, 193, 12, s.battery_kw < 0 ? green : muted, "%s %.1f kW",
                s.battery_kw < 0 ? "REGEN" : "BATTERY DRAW", std::fabs(s.battery_kw));
        }
        else
        {
            p.text(16, 147, 13, muted, "COMBUSTION POWERTRAIN");
            p.text(16, 175, 12, muted, "No hybrid battery fitted");
        }
        p.right(288, 193, 12, muted, s.differential.c_str());
    }

    // downforce, drag and ride height, aero_w x aero_h
    inline void draw_aero(const painter& p, const snapshot& s)
    {
        p.card(0, 0, aero_w, aero_h, "AERO / PLATFORM", "MEASURED");
        if (s.aero_valid)
        {
            p.label(16, 37, 23, cyan, "%.2f kN", (s.front_downforce + s.rear_downforce) * 0.001f);
            p.text(16, 66, 11, muted, "DOWNFORCE");
            p.label(173, 37, 23, amber, "%.2f kN", s.drag * 0.001f);
            p.text(173, 66, 11, muted, "DRAG");
            p.label(16, 94, 12, muted, "F %.2f / R %.2f kN", s.front_downforce * 0.001f, s.rear_downforce * 0.001f);
            p.label(16, 124, 12, ink, "RIDE HEIGHT  %.0f mm", s.ride_height * 1000);
        }
        else
        {
            p.text(16, 45, 17, muted, "Awaiting aero data");
            p.text(16, 81, 12, muted, "Forces appear when available");
        }
    }

    enum class trace_kind { speed, pedals, travel };

    // eight second strip chart, w x history_h
    inline void draw_history(const painter& p, const snapshot& s, const history& h, double now, float w, trace_kind kind)
    {
        char note[48];
        if (kind == trace_kind::speed)
            std::snprintf(note, sizeof(note), "km/h   %.2f km", s.distance * 0.001);
        else if (kind == trace_kind::pedals)
            std::snprintf(note, sizeof(note), "TC CUT %.0f%%", s.tc_reduction * 100);
        else
            std::snprintf(note, sizeof(note), "%s", "");
        const char* titles[] = {"SPEED HISTORY", "PEDAL HISTORY", "SUSPENSION TRAVEL %"};
        p.card(0, 0, w, history_h, titles[static_cast<int>(kind)], note);

        const float x = 16, plot_w = w - 32;
        for (int grid = 0; grid <= 4; ++grid)
            p.line(x + grid * plot_w / 4, 35, x + grid * plot_w / 4, 82, track);
        p.line(x, 82, x + plot_w, 82, track);
        p.text(x, 89, 10, muted, "-8 s");
        p.right(x + plot_w, 89, 10, muted, "NOW");

        if (kind == trace_kind::speed)
        {
            float speed_max = 50;
            for (int i = 0; i < h.count; ++i)
                if (now - h.at(i).time <= history::duration)
                    speed_max = std::max(speed_max, std::ceil(h.at(i).speed / 50) * 50);
            p.label(x + 4, 30, 10, muted, "%.0f", speed_max);
            trace(p, h, now, x, 43, plot_w, 39, speed_max, cyan, [](const sample& a) { return a.speed; });
        }
        else if (kind == trace_kind::pedals)
        {
            p.text(x + 4, 30, 10, muted, "100");
            const float legend_x = x + plot_w * 0.5f - 50;
            p.rect(legend_x, 94, 18, 2, cyan, 1);
            p.text(legend_x + 22, 89, 10, muted, "THR");
            p.rect(legend_x + 56, 94, 18, 2, red, 1);
            p.text(legend_x + 78, 89, 10, muted, "BRK");
            trace(p, h, now, x, 40, plot_w, 42, 1, cyan, [](const sample& a) { return a.throttle; });
            trace(p, h, now, x, 40, plot_w, 42, 1, red, [](const sample& a) { return a.brake; });
        }
        else
        {
            p.text(x + 4, 30, 10, muted, "100");
            const char* names[] = {"FL", "FR", "RL", "RR"};
            for (int i = 0; i < 4; ++i)
            {
                trace(p, h, now, x, 40, plot_w, 42, 1, wheel_colors[i], [i](const sample& a) { return a.travel[i]; });
                p.rect(w - 136 + i * 30.0f, 19, 18, 2, wheel_colors[i], 1);
                p.text(w - 136 + i * 30.0f + 1, 22, 9, muted, names[i]);
            }
        }
    }

    // the cheap traffic model only updates chassis motion and input, so its wheel, powertrain and aero state can be stale
    inline void draw_limited(const painter& p, const snapshot& s)
    {
        p.card(0, 0, 600, 96, "LIMITED TELEMETRY");
        p.label(16, 34, 13, ink, "Cheap simulation is active   %.0f km/h   THR %.0f%%   BRK %.0f%%   STR %+.0f%%",
            s.speed, s.throttle * 100, s.brake * 100, s.steering * 100);
        p.text(16, 58, 12, muted, "Tire, G-force, powertrain and aero measurements require the full vehicle model.");
        p.text(16, 75, 12, cyan, "Open SETUP and select Full physics to enable all measurements and assists.");
    }
}
