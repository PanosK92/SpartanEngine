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
#pragma once

#include "imgui/source/imgui.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cfloat>
#include <string>

// Presentation only: a snapshot keeps drawing independent of the vehicle and makes
// the exact dashboard usable in the offscreen layout check. Units are SI unless named.
namespace spartan::car_hud::telemetry
{
    struct corner
    {
        bool grounded = false, abs = false;
        float surface[3] = {}, core = 0, pressure = 0, wear = 0, damage = 0;
        float load = 0, saturation = 0, compression = 0, slip_ratio = 0, slip_angle = 0;
        float brake_temp = 0, brake_efficiency = 1;
        std::string road = "--";
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
            // Never join traces across closed windows, car switches or resets.
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

    inline float unit(float x) { return std::isfinite(x) ? std::clamp(x, 0.0f, 1.0f) : 0.0f; }

    // All coordinates use a 1200 x 700 design surface. Uniform scaling preserves
    // circular instruments and guarantees that every card stays in the window.
    struct painter
    {
        ImDrawList* dl;
        ImVec2 origin;
        float scale;

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
            rect(x, y, w, h, IM_COL32(20, 31, 43, 250), 9);
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
        p.card(x, y, 244, 189, names[i]);
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
        p.label(x + 113, y + 66, 12, muted, "CORE   %.2f bar", w.pressure);
        p.label(x + 15, y + 89, 12, muted, "WEAR %.0f%%", w.wear * 100);
        p.bar(x + 89, y + 93, 37, 4, w.wear, w.wear > 0.7f ? red : amber);
        p.label(x + 139, y + 89, 12, w.brake_efficiency < 0.8f ? red : ink, "BRK %.0f C", w.brake_temp);
        p.label(x + 15, y + 110, 12, ink, "%.2f kN", w.load * 0.001f);
        p.label(x + 112, y + 110, 12, status, "GRIP USED %.0f%%", w.saturation * 100);
        p.bar(x + 15, y + 130, 211, 5, w.saturation, status);
        p.label(x + 15, y + 144, 12, muted, "SLIP %+.0f%% / %+.1f deg", w.slip_ratio * 100, w.slip_angle);
        p.label(x + 15, y + 165, 11, muted, "TRAVEL %.0f%%", w.compression * 100);
        p.right(x + 229, y + 165, 11, muted, w.grounded ? w.road.c_str() : "NO CONTACT");
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

    inline void draw(const painter& p, const snapshot& s, const history& h, double now)
    {
        p.text(0, 2, 16, ink, "VEHICLE / TELEMETRY");
        // Clip arbitrary preset names without changing the rest of the header.
        p.dl->PushClipRect(p.point(242, 0), p.point(927, 29), true);
        p.text(242, 3, 14, muted, s.name.c_str());
        p.dl->PopClipRect();
        p.right(1200, 3, 12, cyan, s.full_simulation ? "LIVE   /   F3 TO CLOSE" : "CHEAP SIM   /   F3 TO CLOSE");

        if (!s.full_simulation)
        {
            // The cheap traffic model only updates chassis motion and input. Its
            // full-simulation wheel, powertrain and aero state can be stale.
            p.card(0, 34, 1200, 101, "");
            p.label(18, 41, 58, ink, "%.0f", s.speed);
            p.text(22, 107, 12, muted, "SPEED / km/h");
            p.label(288, 60, 25, cyan, "THROTTLE %.0f%%", s.throttle * 100);
            p.label(600, 60, 25, red, "BRAKE %.0f%%", s.brake * 100);
            p.label(900, 60, 25, ink, "STEER %+.0f%%", s.steering * 100);
            p.card(0, 147, 1200, 550, "LIMITED TELEMETRY");
            p.text(48, 292, 28, ink, "Cheap simulation is active");
            p.text(48, 342, 17, muted, "Tire, G-force, powertrain and aero measurements require the full vehicle model.");
            p.text(48, 383, 17, cyan, "Select Full under Sim mode in Workshop to enable the complete dashboard.");
            return;
        }

        p.card(0, 34, 1200, 101, "");
        p.label(18, 41, 58, ink, "%.0f", s.speed);
        p.text(22, 107, 12, muted, "SPEED / km/h");
        p.line(157, 53, 157, 117, track);
        p.text(181, 43, 51, s.shifting ? amber : cyan, s.gear.c_str());
        p.text(183, 107, 12, muted, s.shifting ? "SHIFTING" : "GEAR");
        p.label(288, 51, 27, ink, "%.0f", s.rpm);
        p.text(385, 62, 12, muted, "RPM");
        p.right(888, 60, 12, s.limiter ? red : muted, s.limiter ? "REV LIMITER" : "ENGINE SPEED");
        const float rpm_fraction = unit(s.rpm / std::max(s.redline, 1.0f));
        for (int i = 0; i < 40; ++i)
            p.rect(288 + i * 15.0f, 88, 11, 13, rpm_fraction * 40 > i ? (i >= 36 ? red : i >= 32 ? amber : cyan) : track, 2);
        p.label(288, 109, 11, muted, "0                           REDLINE %.0f rpm", s.redline);
        p.line(924, 53, 924, 117, track);
        p.label(947, 51, 27, s.turbo ? cyan : ink, "%.2f", s.boost);
        p.text(947, 88, 12, muted, s.turbo ? "BOOST / bar" : "NATURALLY ASPIRATED");
        p.bar(947, 113, 230, 4, s.turbo ? s.boost / std::max(s.boost_max, 0.01f) : 0, cyan);

        p.card(0, 147, 268, 225, "G-FORCE", "2 g RANGE");
        const float cx = 134, cy = 257, radius = 67;
        for (int r = 1; r <= 2; ++r)
            p.dl->AddCircle(p.point(cx, cy), radius * r * 0.5f * p.scale, track, 64);
        p.line(cx - radius, cy, cx + radius, cy, track);
        p.line(cx, cy - radius, cx, cy + radius, track);
        p.text(111, 180, 10, muted, "ACCEL");
        p.text(113, 329, 10, muted, "BRAKE");
        p.text(47, 252, 11, muted, "L");
        p.text(215, 252, 11, muted, "R");
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
        p.label(16, 349, 13, ink, "LAT %+.2f", s.lateral_g);
        p.label(147, 349, 13, ink, "LONG %+.2f", s.longitudinal_g);

        p.card(0, 384, 268, 153, "DRIVER INPUTS");
        const auto input = [&](float y, const char* name, float value, ImU32 color)
        {
            p.text(16, y, 12, muted, name);
            p.bar(100, y + 4, 99, 6, value, color);
            p.label(215, y - 1, 13, ink, "%.0f", value * 100);
        };
        input(424, "THROTTLE", s.throttle, cyan);
        input(449, "BRAKE", s.brake, red);
        input(474, "HANDBRAKE", s.handbrake, amber);
        p.text(16, 508, 12, muted, "STEERING");
        p.line(100, 514, 199, 514, track, 4);
        p.line(149, 507, 149, 521, muted);
        p.dl->AddCircleFilled(p.point(100 + unit(s.steering * 0.5f + 0.5f) * 99, 514), 4 * p.scale, cyan);
        p.label(210, 507, 12, ink, "%+.0f", s.steering * 100);

        draw_corner(p, 280, 147, 0, s);
        draw_corner(p, 640, 147, 1, s);
        draw_corner(p, 280, 348, 2, s);
        draw_corner(p, 640, 348, 3, s);

        // Schematic top view connects the cards to physical corners, with a load split.
        p.text(560, 153, 11, muted, "FRONT");
        p.rect(555, 214, 54, 197, IM_COL32(25, 42, 55, 255), 19);
        p.dl->AddRect(p.point(555, 214), p.point(609, 411), IM_COL32(71, 102, 124, 255), 19 * p.scale);
        p.rect(561, 247, 42, 36, IM_COL32(48, 74, 92, 255), 7);
        p.rect(561, 349, 42, 23, IM_COL32(48, 74, 92, 255), 5);
        for (int i = 0; i < 4; ++i)
        {
            const float x = i % 2 ? 610.0f : 543.0f, y = i < 2 ? 237.0f : 368.0f;
            p.rect(x, y, 11, 27, s.wheels[i].grounded ? wheel_colors[i] : track, 3);
            p.line(i % 2 ? 624.0f : 540.0f, y + 13, i % 2 ? 635.0f : 529.0f, y + 13, track);
        }
        float front_load = s.wheels[0].load + s.wheels[1].load;
        float rear_load = s.wheels[2].load + s.wheels[3].load;
        p.text(551, 439, 11, muted, "LOAD F/R");
        if (front_load + rear_load > 1)
        {
            float front = front_load / (front_load + rear_load);
            p.label(546, 462, 17, ink, "%.0f / %.0f", front * 100, (1 - front) * 100);
            p.bar(544, 491, 77, 5, front, cyan);
        }
        else
            p.text(552, 466, 12, amber, "UNLOADED");
        p.text(559, 517, 11, muted, "REAR");

        p.card(896, 147, 304, 225, "POWERTRAIN", s.engine_running ? "RUNNING" : "ENGINE OFF");
        p.label(912, 185, 30, ink, "%.0f", s.torque);
        p.text(912, 221, 12, muted, "ENGINE / Nm");
        p.label(1069, 185, 30, cyan, "%.0f", s.motor_kw);
        p.text(1069, 221, 12, muted, "MOTOR / kW");
        p.line(912, 248, 1184, 248, track);
        p.text(912, 261, 12, muted, "CLUTCH");
        p.bar(984, 266, 80, 5, s.clutch, cyan);
        p.label(1080, 261, 12, ink, "%.0f%% coupled", s.clutch * 100);
        if (s.hybrid)
        {
            p.label(912, 292, 13, s.battery_soc < 0.15f ? amber : green, "BATTERY %.0f%%", s.battery_soc * 100);
            p.label(1067, 292, 13, s.battery_temp >= s.battery_hot ? amber : ink, "%.0f C", s.battery_temp);
            p.bar(912, 316, 272, 6, s.battery_soc, green);
            p.label(912, 340, 12, s.battery_kw < 0 ? green : muted, "%s %.1f kW",
                s.battery_kw < 0 ? "REGEN" : "BATTERY DRAW", std::fabs(s.battery_kw));
        }
        else
        {
            p.text(912, 294, 13, muted, "COMBUSTION POWERTRAIN");
            p.text(912, 322, 12, muted, "No hybrid battery fitted");
        }
        p.right(1184, 340, 12, muted, s.differential.c_str());

        p.card(896, 384, 304, 153, "AERO / PLATFORM", "MEASURED");
        if (s.aero_valid)
        {
            p.label(912, 421, 23, cyan, "%.2f kN", (s.front_downforce + s.rear_downforce) * 0.001f);
            p.text(912, 450, 11, muted, "DOWNFORCE");
            p.label(1069, 421, 23, amber, "%.2f kN", s.drag * 0.001f);
            p.text(1069, 450, 11, muted, "DRAG");
            p.label(912, 478, 12, muted, "F %.2f / R %.2f kN", s.front_downforce * 0.001f, s.rear_downforce * 0.001f);
            p.label(912, 508, 12, ink, "RIDE HEIGHT  %.0f mm", s.ride_height * 1000);
        }
        else
        {
            p.text(912, 429, 17, muted, "Awaiting aero data");
            p.text(912, 465, 12, muted, "Forces appear when available");
        }

        p.card(0, 549, 390, 106, "SPEED HISTORY", "km/h");
        p.card(402, 549, 390, 106, "PEDAL HISTORY", "THR / BRK %");
        p.card(804, 549, 396, 106, "SUSPENSION TRAVEL %", "FL  FR  RL  RR");
        float speed_max = 50;
        for (int i = 0; i < h.count; ++i)
            if (now - h.at(i).time <= history::duration)
                speed_max = std::max(speed_max, std::ceil(h.at(i).speed / 50) * 50);
        for (int i = 0; i < 3; ++i)
        {
            float x = i * 402.0f + 16;
            for (int grid = 0; grid <= 4; ++grid)
                p.line(x + grid * 89.0f, 584, x + grid * 89.0f, 631, track);
            p.line(x, 631, x + 358, 631, track);
            p.text(x, 638, 10, muted, "-8 s");
            p.text(x + 334, 638, 10, muted, "NOW");
        }
        p.label(16, 579, 10, muted, "%.0f", speed_max);
        p.text(418, 579, 10, muted, "100");
        p.text(820, 579, 10, muted, "100");
        p.rect(1106 - 402, 576, 25, 2, cyan, 1);
        p.rect(1154 - 402, 576, 25, 2, red, 1);
        trace(p, h, now, 16, 592, 358, 39, speed_max, cyan, [](const sample& a) { return a.speed; });
        trace(p, h, now, 418, 589, 358, 42, 1, cyan, [](const sample& a) { return a.throttle; });
        trace(p, h, now, 418, 589, 358, 42, 1, red, [](const sample& a) { return a.brake; });
        for (int i = 0; i < 4; ++i)
        {
            trace(p, h, now, 820, 589, 364, 42, 1, wheel_colors[i], [i](const sample& a) { return a.travel[i]; });
            p.rect(1086 + i * 27.0f, 576, 17, 2, wheel_colors[i], 1);
        }
        bool abs_active = false, brake_fade = false, damage = false;
        for (const corner& w : s.wheels)
        {
            abs_active |= w.abs;
            brake_fade |= w.brake_efficiency < 0.8f;
            damage |= w.damage > 0.1f || w.wear > 0.7f;
        }
        p.pill(0, 668, 159, "ABS", abs_active ? "ACTIVE" : s.abs_enabled ? "READY" : "OFF", abs_active ? cyan : muted);
        p.pill(169, 668, 159, "TCS", s.tc_active ? "ACTIVE" : s.tc_enabled ? "READY" : "OFF", s.tc_active ? amber : muted);
        p.pill(338, 668, 159, "DRS", s.drs_active ? "OPEN" : s.drs_enabled ? "CLOSED" : "OFF", s.drs_active ? cyan : muted);
        p.pill(507, 668, 179, "BRAKES", brake_fade ? "FADING" : "OK", brake_fade ? red : green);
        p.pill(696, 668, 179, "TIRES", damage ? "CHECK" : "OK", damage ? amber : green);
        p.label(897, 676, 12, muted, "TC CUT %.0f%%", s.tc_reduction * 100);
        p.label(1075, 676, 12, muted, "%.2f km", s.distance * 0.001);
    }
}
