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
#include "pch.h"
#include "CarDyno.h"
#include "Car.h"
#include "CarSimulation.h"
#include "imgui/source/imgui.h"

namespace spartan::car_hud
{
    namespace
    {
        using sample = car::dyno_sample;
        struct trace { const char* name; float sample::*field; ImU32 color; };
        const ImU32 orange = IM_COL32(255, 186, 92, 255);
        const ImU32 blue = IM_COL32(84, 191, 255, 255);
        const ImU32 green = IM_COL32(110, 228, 176, 255);

        void graph(const char* title, const char* units, const std::vector<sample>& rows,
            std::initializer_list<trace> traces, bool time_axis = false,
            const std::vector<sample>* previous = nullptr)
        {
            ImGui::TextUnformatted(title);
            const auto origin = ImGui::GetCursorScreenPos();
            const float width = std::max(ImGui::GetContentRegionAvail().x, 200.0f);
            const ImVec2 a(origin.x + 58, origin.y + 8), b(origin.x + width - 12, origin.y + 127);
            auto* draw = ImGui::GetWindowDrawList();
            float xmax = time_axis ? 1.0f : 1000.0f, ymin = 0, ymax = 1;
            auto range = [&](const std::vector<sample>& data, bool reference)
            {
                for (const auto& row : data)
                {
                    if (!time_axis && row.time < 2) continue;
                    xmax = std::max(xmax, time_axis ? row.time : row.rpm);
                    if (reference) { ymin = std::min(ymin, row.axle_kw); ymax = std::max(ymax, row.axle_kw); }
                    else for (const auto& t : traces) { ymin = std::min(ymin, row.*(t.field)); ymax = std::max(ymax, row.*(t.field)); }
                }
            };
            range(rows, false);
            if (previous) range(*previous, true);
            ymax *= 1.1f;
            ymin = ymin < 0 ? ymin * 1.1f : 0;
            auto point = [&](const sample& row, float value)
            {
                return ImVec2(a.x + (time_axis ? row.time : row.rpm) / xmax * (b.x - a.x),
                    b.y - (value - ymin) / (ymax - ymin) * (b.y - a.y));
            };
            draw->AddRectFilled(a, b, IM_COL32(17, 23, 30, 255));
            char text[64];
            for (int i = 0; i <= 4; ++i)
            {
                const float f = i / 4.0f, y = b.y - f * (b.y - a.y), x = a.x + f * (b.x - a.x);
                draw->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), IM_COL32(62, 76, 89, 140));
                snprintf(text, sizeof(text), "%.0f", ymin + f * (ymax - ymin));
                draw->AddText(ImVec2(origin.x + 2, y - 7), IM_COL32(165, 181, 196, 255), text);
                snprintf(text, sizeof(text), "%.0f", f * xmax);
                draw->AddText(ImVec2(x - 12, b.y + 4), IM_COL32(165, 181, 196, 255), text);
            }
            auto line = [&](const std::vector<sample>& data, float sample::*field, ImU32 color)
            {
                bool valid = false; ImVec2 last;
                for (const auto& row : data)
                {
                    if (!time_axis && row.time < 2) continue;
                    auto p = point(row, row.*field);
                    if (valid) draw->AddLine(last, p, color, 1.5f);
                    last = p; valid = true;
                }
            };
            for (const auto& t : traces) line(rows, t.field, t.color);
            if (previous) line(*previous, &sample::axle_kw, IM_COL32(203, 151, 239, 180));
            ImGui::InvisibleButton(title, ImVec2(width, 149));
            if (ImGui::IsItemHovered() && !rows.empty())
            {
                const float x = std::clamp((ImGui::GetIO().MousePos.x - a.x) / (b.x - a.x), 0.0f, 1.0f) * xmax;
                const sample* nearest = nullptr; float distance = FLT_MAX;
                for (const auto& row : rows)
                {
                    if (!time_axis && row.time < 2) continue;
                    const float d = fabsf((time_axis ? row.time : row.rpm) - x);
                    if (d < distance) { nearest = &row; distance = d; }
                }
                if (nearest)
                {
                    ImGui::BeginTooltip();
                    ImGui::Text("%.2f s / %.0f RPM", nearest->time, nearest->rpm);
                    for (const auto& t : traces) ImGui::Text("%s: %.2f %s", t.name, nearest->*(t.field), units);
                    ImGui::EndTooltip();
                }
            }
            for (const auto& t : traces)
            {
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(t.color), "%s  ", t.name);
                ImGui::SameLine();
            }
            ImGui::TextDisabled("%s / %s", units, time_axis ? "seconds" : "engine RPM");
        }
    }

    void draw_dyno_window(Car* vehicle, Physics* physics)
    {
        auto* sim = physics->GetVehicleSimulation();
        auto& d = sim->dyno;
        ImGui::SetNextWindowSize(ImVec2(670, 840), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Dyno | Drivetrain laboratory")) { ImGui::End(); return; }
        ImGui::TextWrapped("Speed-controlled hub dyno. Production drivetrain; tires, roller losses and road load excluded.");
        ImGui::BeginDisabled(d.running);
        ImGui::SetNextItemWidth(330);
        const car::car_definition* next = nullptr;
        if (ImGui::BeginCombo("Car", sim->get_spec().name))
        {
            for (const auto& entry : car::preset_registry)
                if (ImGui::Selectable(entry.name, entry.definition == vehicle->GetDefinition())) next = entry.definition;
            ImGui::EndCombo();
        }
        if (next && next != vehicle->GetDefinition())
        {
            if (!d.samples.empty()) { d.previous_samples = d.samples; d.previous_car = d.car_name; }
            d.samples.clear();
            const bool mounted = d.mounted;
            sim->mount_dyno(false);
            vehicle->LoadDefinition(next);
            sim->mount_dyno(mounted);
            d.status = "Ready";
            d.export_path.clear();
        }
        const auto& spec = sim->get_spec();
        int gear = d.gear - 1;
        ImGui::SliderInt("Test gear", &gear, 1, spec.gear_count - 2);
        d.gear = gear + 1;
        ImGui::Checkbox("RPM sweep (off = hold start RPM)", &d.sweep);
        ImGui::SliderFloat("Start / hold RPM", &d.start_rpm, spec.engine_idle_rpm, spec.engine_redline_rpm - 200, "%.0f");
        ImGui::SliderFloat("End RPM", &d.end_rpm, spec.engine_idle_rpm + 100, spec.engine_redline_rpm - 50, "%.0f");
        ImGui::SliderFloat("Duration (s)", &d.sweep_seconds, 1, 120, "%.1f");
        ImGui::SliderFloat("Throttle", &d.throttle, 0, 1, "%.2f");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!d.mounted || d.running);
        if (ImGui::Button("Run test"))
        {
            if (!d.samples.empty()) { d.previous_samples = d.samples; d.previous_car = d.car_name; }
            sim->start_dyno();
        }
        ImGui::EndDisabled(); ImGui::SameLine();
        ImGui::BeginDisabled(!d.running);
        if (ImGui::Button("Stop")) sim->stop_dyno();
        ImGui::EndDisabled();
        if (!d.mounted) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(orange), "Press Play to power the test stand.");
        ImGui::Text("%s | %.1f s | %zu samples", d.status.c_str(), d.time, d.samples.size());
        if (!d.export_path.empty()) ImGui::TextWrapped("Saved: %s", d.export_path.c_str());
        ImGui::TextDisabled("Each run resets battery and drivetrain; 2 s conditioning precedes capture curves.");
        if (!d.samples.empty())
        {
            const auto& s = d.samples.back();
            float peak_kw = 0, peak_nm = 0;
            for (const auto& row : d.samples) if (row.time >= 2) { peak_kw = std::max(peak_kw, row.axle_kw); peak_nm = std::max(peak_nm, row.axle_nm); }
            ImGui::Text("RPM %.0f | Hub %.0f RPM | Boost %.2f bar | SOC %.1f%%", s.rpm, s.wheel_rpm, s.boost_bar, s.battery_soc * 100);
            ImGui::Text("Peak axle: %.1f kW / %.1f hp / %.0f Nm | Clutch slip %.0f RPM", peak_kw, peak_kw * 1.34102209f, peak_nm, s.clutch_slip_rpm);
        }
        ImGui::Separator();
        graph("Torque", "Nm", d.samples, {{"Combustion (gross)", &sample::combustion_nm, orange}, {"Axle", &sample::axle_nm, blue}});
        graph("Power", "kW", d.samples, {{"Combustion (gross)", &sample::combustion_kw, orange}, {"Axle", &sample::axle_kw, blue}, {"Motor shaft", &sample::motor_kw, green}}, false, &d.previous_samples);
        if (!d.previous_samples.empty()) ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.94f, 1), "Purple: previous axle power / %s", d.previous_car.c_str());
        graph("RPM history", "RPM", d.samples, {{"Engine", &sample::rpm, orange}, {"Shaft target (engine equivalent)", &sample::target_rpm, blue}}, true);
        ImGui::TextWrapped("Combustion is gross simulated torque before friction and rotor acceleration. Axle power includes clutch/gearbox losses and hybrid assist; it is not tire-contact wheel horsepower. No atmospheric correction is applied.");
        if (ImGui::CollapsingHeader("Gear ratios and theoretical speeds"))
        {
            ImGui::Text("Final drive %.4f | Drivetrain efficiency %.3f", spec.final_drive, spec.drivetrain_efficiency);
            for (int g = 2; g < spec.gear_count; ++g)
            {
                const float speed = spec.engine_redline_rpm / (spec.gear_ratios[g] * spec.final_drive) * 2 * math::pi * sim->get_driven_wheel_radius() * 60 / 1000;
                ImGui::Text("Gear %d: %.4f | overall %.4f | %.1f km/h at redline (no slip)", g - 1, spec.gear_ratios[g], spec.gear_ratios[g] * spec.final_drive, speed);
            }
        }
        ImGui::End();
    }
}
