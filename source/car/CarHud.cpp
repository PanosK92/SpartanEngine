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

//= INCLUDES ===============================
#include "pch.h"
#include "CarHud.h"
#include "CarTelemetry.h"
#include "Car.h"
#include "CarState.h"
#include "CarSimulation.h"
#include "CarPresets.h"
#include "../world/components/Physics.h"
#include "imgui/source/imgui.h"
#include "widgets/Viewport.h"
//==========================================

namespace spartan::car_hud
{
    namespace
    {
        // visual language shared by every panel in the hud
        constexpr float pi = 3.14159265f;

        const ImU32 accent_warn    = IM_COL32(255, 180, 50, 255);
        const ImU32 accent_ok      = IM_COL32(80, 200, 110, 255);
        const ImU32 accent_danger  = IM_COL32(220, 70, 70, 255);
        const ImU32 text_label     = IM_COL32(120, 130, 142, 255);

        // status pill, only drawn when state >= 'idle'. cross fades alpha based on a static per-tag timer
        enum class pill_state { off, idle, active };

        struct pill_anim
        {
            float alpha = 0.0f;
        };

        // accent is the colour the pill lights up with the instant the system engages, each system passes
        // its own so a glance tells you which one fired, gran turismo style tell tales
        void draw_status_pill(ImDrawList* dl, ImVec2 tl, const char* text, pill_state state, pill_anim& anim, float dt, ImU32 accent, float min_w = 0.0f, bool instant = false)
        {
            // off tell tales stay faintly visible so the driver always sees the full set of systems
            float target = (state == pill_state::active) ? 1.0f : (state == pill_state::idle ? 0.55f : 0.3f);
            if (instant)
            {
                // snap with no smoothing so a fast modulating system, like abs, visibly flickers
                anim.alpha = target;
            }
            else
            {
                float rate  = 14.0f; // snappy so an engaged system reads instantly
                anim.alpha += (target - anim.alpha) * std::clamp(dt * rate, 0.0f, 1.0f);
            }
            if (anim.alpha < 0.02f)
            {
                return;
            }

            ImVec2 ts    = ImGui::CalcTextSize(text);
            float pad_x  = 7.0f;
            float pad_y  = 3.0f;
            float box_w  = std::max(ts.x + pad_x * 2.0f, min_w);
            ImVec2 br(tl.x + box_w, tl.y + ts.y + pad_y * 2.0f);
            float text_x = tl.x + (box_w - ts.x) * 0.5f; // centred so a fixed width column stays tidy

            int  a      = (int)(anim.alpha * 255.0f);
            bool active = state == pill_state::active;

            if (active)
            {
                // pull the accent channels apart so the fill, border and glow are all tints of the same hue
                int ar = (accent >> IM_COL32_R_SHIFT) & 0xFF;
                int ag = (accent >> IM_COL32_G_SHIFT) & 0xFF;
                int ab = (accent >> IM_COL32_B_SHIFT) & 0xFF;

                // soft pulsing glow around the pill so a freshly engaged system grabs the eye
                float pulse = 0.5f + 0.5f * sinf((float)ImGui::GetTime() * 8.0f);
                int   ga    = (int)(a * (0.2f + 0.35f * pulse));
                dl->AddRectFilled(ImVec2(tl.x - 2.0f, tl.y - 2.0f), ImVec2(br.x + 2.0f, br.y + 2.0f), IM_COL32(ar, ag, ab, ga), 6.0f);

                dl->AddRectFilled(tl, br, IM_COL32(ar, ag, ab, a * 3 / 5), 4.0f);
                dl->AddRect(tl, br, IM_COL32(ar, ag, ab, a), 4.0f, 1.0f);
                dl->AddText(ImVec2(text_x, tl.y + pad_y), IM_COL32(245, 250, 250, a), text);
            }
            else
            {
                // idle or disabled, dim grey so the tell tale is present but clearly not doing anything
                dl->AddRectFilled(tl, br, IM_COL32(60, 70, 82, a / 3), 4.0f);
                dl->AddRect(tl, br, IM_COL32(110, 120, 134, a), 4.0f, 1.0f);
                dl->AddText(ImVec2(text_x, tl.y + pad_y), IM_COL32(200, 210, 220, a), text);
            }
        }

        // tooltip with consistent formatting
        void hud_tooltip(const char* text)
        {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("%s", text);
            }
        }

    } // anonymous namespace

    // ====================================================================================
    // driver hud, the always-on cockpit overlay
    // ====================================================================================

    void draw_driver_hud(Physics* physics)
    {
        if (!physics)
        {
            return;
        }

        ImGuiIO& io = ImGui::GetIO();
        if (io.DisplaySize.x < 200.0f || io.DisplaySize.y < 200.0f)
        {
            return;
        }

        const math::Vector3 velocity = physics->GetLinearVelocity();
        const float speed_kmh        = velocity.Length() * 3.6f;
        const float engine_rpm       = physics->GetEngineRPM();
        const float redline_rpm      = std::max(
            physics->GetRedlineRPM(),
            1.0f
        );
        const bool turbo_enabled     = physics->GetTurboEnabled();
        const float boost_bar        = physics->GetBoostPressure();
        const bool is_shifting       = physics->IsShifting();
        const char* gear_str         = physics->GetCurrentGearString();
        const float throttle         = physics->GetVehicleThrottle();
        const float brake            = physics->GetVehicleBrake();
        const float steer            = physics->GetVehicleSteering();
        const float handbrake        = physics->GetVehicleHandbrake();

        const math::Vector2& vp_pos  = Viewport::GetScreenPosition();
        const math::Vector2& vp_size = Viewport::GetScreenSize();
        float region_left            = 0.0f;
        float region_width           = io.DisplaySize.x;
        float anchor_bottom          = io.DisplaySize.y;
        if (vp_size.x > 100.0f && vp_size.y > 100.0f)
        {
            region_left   = vp_pos.x;
            region_width  = vp_size.x;
            anchor_bottom = vp_pos.y + vp_size.y;
        }

        const float scale = std::clamp(
            region_width / 1600.0f,
            0.90f,
            1.15f
        );
        const float margin   = 24.0f * scale;
        const float panel_h  = 150.0f * scale;
        const float panel_w  = std::min(
            560.0f * scale,
            region_width * 0.58f
        );
        const float window_h = panel_h + 54.0f * scale;
        const ImVec2 window_pos(
            region_left,
            anchor_bottom - window_h - margin
        );

        ImGui::SetNextWindowPos(
            window_pos,
            ImGuiCond_Always
        );
        ImGui::SetNextWindowSize(
            ImVec2(region_width, window_h),
            ImGuiCond_Always
        );
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoDocking;

        if (ImGui::Begin("##car_driver_hud", nullptr, flags))
        {
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            ImVec2 origin = ImGui::GetCursorScreenPos();
            ImVec2 panel_tl(
                origin.x + (region_width - panel_w) * 0.5f,
                origin.y + 48.0f * scale
            );
            ImVec2 panel_br(
                panel_tl.x + panel_w,
                panel_tl.y + panel_h
            );

            dl->AddRectFilled(
                ImVec2(
                    panel_tl.x + 2.0f * scale,
                    panel_tl.y + 5.0f * scale
                ),
                ImVec2(
                    panel_br.x + 2.0f * scale,
                    panel_br.y + 5.0f * scale
                ),
                IM_COL32(0, 0, 0, 110),
                7.0f * scale
            );
            dl->AddRectFilled(
                panel_tl,
                panel_br,
                IM_COL32(10, 14, 19, 205),
                7.0f * scale
            );
            const float gradient_h = panel_h / 6.0f;
            for (int i = 0; i < 6; ++i)
            {
                const int alpha = 26 - i * 3;
                dl->AddRectFilled(
                    ImVec2(
                        panel_tl.x + 1.0f * scale,
                        panel_tl.y +
                        gradient_h * static_cast<float>(i)
                    ),
                    ImVec2(
                        panel_br.x - 1.0f * scale,
                        panel_tl.y +
                        gradient_h *
                        static_cast<float>(i + 1)
                    ),
                    IM_COL32(78, 105, 122, alpha),
                    i == 0 ? 7.0f * scale : 0.0f
                );
            }
            dl->AddRect(
                panel_tl,
                panel_br,
                IM_COL32(110, 135, 150, 100),
                7.0f * scale,
                1.0f * scale
            );
            dl->AddRect(
                ImVec2(
                    panel_tl.x + 2.0f * scale,
                    panel_tl.y + 2.0f * scale
                ),
                ImVec2(
                    panel_br.x - 2.0f * scale,
                    panel_br.y - 2.0f * scale
                ),
                IM_COL32(220, 240, 250, 24),
                6.0f * scale,
                1.0f
            );

            const ImU32 cyan       = IM_COL32(104, 218, 255, 255);
            const ImU32 red        = IM_COL32(255, 72, 78, 255);
            const ImU32 white      = IM_COL32(245, 248, 250, 255);
            const ImU32 line_color = IM_COL32(112, 126, 140, 70);
            ImFont* font           = ImGui::GetFont();

            auto draw_text = [&](
                const char* text,
                float size,
                ImVec2 position,
                ImU32 color
            )
            {
                dl->AddText(
                    font,
                    size * scale,
                    position,
                    color,
                    text
                );
            };

            static pill_anim abs_anim;
            static pill_anim tcs_anim;
            static pill_anim drs_anim;
            static pill_anim hbrk_anim;
            static pill_anim turbo_anim;
            static pill_anim lock_anim;

            const bool abs_on     = physics->GetAbsEnabled();
            const bool abs_active = physics->IsAbsActiveAny();
            const bool abs_grab   =
                abs_active &&
                physics->GetAbsPhase() >= 0.5f;
            const bool tcs_on     = physics->GetTcEnabled();
            const bool tcs_active = physics->IsTcActive();
            const bool drs_on     = physics->GetDrsEnabled();
            const bool drs_active = physics->GetDrsActive();
            const bool hbrk_active = handbrake > 0.1f;
            const bool lock_active = physics->IsBurnoutActive();
            const bool turbo_active =
                turbo_enabled &&
                boost_bar > 0.5f;

            const pill_state abs_state =
                abs_active
                ? (abs_grab ? pill_state::active : pill_state::idle)
                : (abs_on ? pill_state::idle : pill_state::off);
            const pill_state tcs_state =
                tcs_active
                ? pill_state::active
                : (tcs_on ? pill_state::idle : pill_state::off);
            const pill_state drs_state =
                drs_active
                ? pill_state::active
                : (drs_on ? pill_state::idle : pill_state::off);
            const pill_state turbo_state =
                turbo_active
                ? pill_state::active
                : (turbo_enabled ? pill_state::idle : pill_state::off);
            const pill_state hbrk_state =
                hbrk_active
                ? pill_state::active
                : pill_state::idle;
            const pill_state lock_state =
                lock_active
                ? pill_state::active
                : pill_state::idle;

            const float pill_w   = 54.0f * scale;
            const float pill_gap = 6.0f * scale;
            const float pill_y =
                panel_br.y -
                24.0f * scale;
            float pill_x =
                origin.x +
                region_width -
                margin -
                pill_w;

            auto add_pill = [&](
                const char* text,
                pill_state state,
                pill_anim& animation,
                ImU32 color,
                bool instant
            )
            {
                draw_status_pill(
                    dl,
                    ImVec2(pill_x, pill_y),
                    text,
                    state,
                    animation,
                    io.DeltaTime,
                    color,
                    pill_w,
                    instant
                );
                pill_x -= pill_w + pill_gap;
            };

            add_pill(
                "HBRK",
                hbrk_state,
                hbrk_anim,
                accent_danger,
                false
            );
            add_pill(
                "LOCK",
                lock_state,
                lock_anim,
                accent_danger,
                false
            );
            add_pill(
                "DRS",
                drs_state,
                drs_anim,
                accent_ok,
                false
            );
            add_pill(
                "TCS",
                tcs_state,
                tcs_anim,
                IM_COL32(255, 218, 72, 255),
                false
            );
            add_pill(
                "ABS",
                abs_state,
                abs_anim,
                accent_warn,
                abs_active
            );
            if (turbo_enabled)
            {
                add_pill(
                    "BOOST",
                    turbo_state,
                    turbo_anim,
                    cyan,
                    false
                );
            }

            const float tach_x = panel_tl.x + 18.0f * scale;
            const float tach_y = panel_tl.y + 14.0f * scale;
            const float tach_w = panel_w - 36.0f * scale;
            const float tach_h = 12.0f * scale;
            const int segment_count = 32;
            const float segment_gap = 2.0f * scale;
            const float segment_w =
                (tach_w - segment_gap * (segment_count - 1)) /
                segment_count;
            const float rpm_max = std::max(
                10000.0f,
                redline_rpm * 1.08f
            );
            const float rpm_fraction = std::clamp(
                engine_rpm / rpm_max,
                0.0f,
                1.0f
            );
            const float redline_fraction = std::clamp(
                redline_rpm / rpm_max,
                0.0f,
                1.0f
            );

            for (int i = 0; i < segment_count; ++i)
            {
                const float fraction =
                    static_cast<float>(i + 1) /
                    static_cast<float>(segment_count);
                const float x =
                    tach_x +
                    i * (segment_w + segment_gap);
                const bool active = fraction <= rpm_fraction;
                const bool redline = fraction >= redline_fraction;
                ImU32 color = IM_COL32(58, 67, 76, 150);

                if (active)
                {
                    color = redline ? red : cyan;
                }
                else if (redline)
                {
                    color = IM_COL32(100, 40, 44, 170);
                }

                dl->AddRectFilled(
                    ImVec2(x, tach_y),
                    ImVec2(x + segment_w, tach_y + tach_h),
                    color,
                    1.5f * scale
                );
            }

            if (engine_rpm >= redline_rpm || is_shifting)
            {
                const float pulse =
                    0.55f +
                    sinf(
                        static_cast<float>(ImGui::GetTime()) *
                        18.0f
                    ) *
                    0.35f;
                dl->AddRect(
                    ImVec2(tach_x - 2.0f, tach_y - 2.0f),
                    ImVec2(
                        tach_x + tach_w + 2.0f,
                        tach_y + tach_h + 2.0f
                    ),
                    IM_COL32(
                        255,
                        72,
                        78,
                        static_cast<int>(pulse * 255.0f)
                    ),
                    3.0f * scale,
                    2.0f * scale,
                    ImDrawFlags_None
                );
            }

            const float content_top = panel_tl.y + 40.0f * scale;
            const float content_bottom = panel_br.y - 12.0f * scale;
            const float speed_w = panel_w * 0.62f;
            const float gear_w = panel_w - speed_w;
            const float divider_1 = panel_tl.x + speed_w;
            const float divider_2 = divider_1 + gear_w;

            dl->AddRectFilled(
                ImVec2(
                    panel_tl.x + 10.0f * scale,
                    content_top - 3.0f * scale
                ),
                ImVec2(
                    divider_2 - 10.0f * scale,
                    content_bottom
                ),
                IM_COL32(2, 5, 8, 82),
                5.0f * scale
            );

            dl->AddLine(
                ImVec2(divider_1, content_top),
                ImVec2(divider_1, content_bottom),
                line_color,
                1.0f
            );
            static float gear_pulse = 0.0f;
            static bool was_shifting = false;
            if (is_shifting && !was_shifting)
            {
                gear_pulse = 1.0f;
            }
            was_shifting = is_shifting;
            gear_pulse = std::max(
                0.0f,
                gear_pulse - io.DeltaTime * 5.0f
            );

            if (gear_pulse > 0.0f)
            {
                dl->AddRectFilled(
                    ImVec2(
                        divider_1 + 5.0f * scale,
                        content_top - 3.0f * scale
                    ),
                    ImVec2(
                        divider_2 - 5.0f * scale,
                        content_bottom
                    ),
                    IM_COL32(
                        255,
                        184,
                        70,
                        static_cast<int>(gear_pulse * 48.0f)
                    ),
                    5.0f * scale
                );
            }

            const float gear_size =
                (70.0f + gear_pulse * 8.0f) * scale;
            const ImVec2 gear_text_size = font->CalcTextSizeA(
                gear_size,
                FLT_MAX,
                0.0f,
                gear_str
            );
            const float gear_center_x =
                divider_1 + gear_w * 0.5f;
            draw_text(
                "GEAR",
                12.0f,
                ImVec2(
                    gear_center_x - 18.0f * scale,
                    content_top + 3.0f * scale
                ),
                text_label
            );
            dl->AddText(
                font,
                gear_size,
                ImVec2(
                    gear_center_x - gear_text_size.x * 0.5f,
                    content_top + 18.0f * scale
                ),
                is_shifting ? accent_warn : white,
                gear_str
            );

            char speed_text[16];
            snprintf(
                speed_text,
                sizeof(speed_text),
                "%.0f",
                speed_kmh
            );
            const float speed_size = 62.0f * scale;
            const ImVec2 speed_text_size = font->CalcTextSizeA(
                speed_size,
                FLT_MAX,
                0.0f,
                speed_text
            );
            const float speed_center_x =
                panel_tl.x + speed_w * 0.5f;
            dl->AddText(
                font,
                speed_size,
                ImVec2(
                    speed_center_x - speed_text_size.x * 0.5f,
                    content_top + 10.0f * scale
                ),
                white,
                speed_text
            );
            draw_text(
                "KM/H",
                12.0f,
                ImVec2(
                    speed_center_x - 18.0f * scale,
                    content_bottom - 19.0f * scale
                ),
                text_label
            );

            if (turbo_enabled)
            {
                char boost_text[24];
                snprintf(
                    boost_text,
                    sizeof(boost_text),
                    "BOOST  %.1f BAR",
                    boost_bar
                );
                const ImVec2 boost_size = font->CalcTextSizeA(
                    11.0f * scale,
                    FLT_MAX,
                    0.0f,
                    boost_text
                );
                draw_text(
                    boost_text,
                    11.0f,
                    ImVec2(
                        gear_center_x - boost_size.x * 0.5f,
                        panel_tl.y + 27.0f * scale
                    ),
                    boost_bar > 2.0f ? red : cyan
                );
            }

            const float input_w = 180.0f * scale;
            const float input_x = origin.x + margin;
            const float input_y = panel_tl.y + 29.0f * scale;

            auto input_bar = [&](
                const char* label,
                float value,
                ImU32 color,
                float y
            )
            {
                const float label_w = 40.0f * scale;
                const float bar_w = input_w - label_w;
                const float bar_h = 5.0f * scale;
                const ImVec2 bar_tl(
                    input_x + label_w,
                    y + 5.0f * scale
                );
                const ImVec2 bar_br(
                    bar_tl.x + bar_w,
                    bar_tl.y + bar_h
                );

                draw_text(
                    label,
                    11.0f,
                    ImVec2(input_x, y),
                    text_label
                );
                dl->AddRectFilled(
                    bar_tl,
                    bar_br,
                    IM_COL32(48, 57, 66, 190),
                    2.0f * scale
                );
                dl->AddRectFilled(
                    bar_tl,
                    ImVec2(
                        bar_tl.x +
                        bar_w *
                        std::clamp(value, 0.0f, 1.0f),
                        bar_br.y
                    ),
                    color,
                    2.0f * scale
                );
            };

            input_bar(
                "THR",
                throttle,
                cyan,
                input_y
            );
            input_bar(
                "BRK",
                brake,
                red,
                input_y + 24.0f * scale
            );

            const float steer_y = input_y + 52.0f * scale;
            const float steer_label_w = 40.0f * scale;
            const float steer_bar_w = input_w - steer_label_w;
            const float steer_x = input_x + steer_label_w;
            const float steer_center =
                steer_x + steer_bar_w * 0.5f;
            draw_text(
                "STR",
                11.0f,
                ImVec2(input_x, steer_y),
                text_label
            );
            dl->AddLine(
                ImVec2(
                    steer_x,
                    steer_y + 7.0f * scale
                ),
                ImVec2(
                    steer_x + steer_bar_w,
                    steer_y + 7.0f * scale
                ),
                IM_COL32(70, 80, 90, 210),
                3.0f * scale
            );
            dl->AddLine(
                ImVec2(
                    steer_center,
                    steer_y + 2.0f * scale
                ),
                ImVec2(
                    steer_center,
                    steer_y + 12.0f * scale
                ),
                text_label,
                1.0f
            );
            const float steer_position =
                steer_center +
                std::clamp(steer, -1.0f, 1.0f) *
                steer_bar_w * 0.5f;
            dl->AddCircleFilled(
                ImVec2(
                    steer_position,
                    steer_y + 7.0f * scale
                ),
                4.0f * scale,
                cyan
            );
        }
        ImGui::End();

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    void draw_telemetry_window(Car* car_instance, Physics* physics, bool* p_open)
    {
        if (!car_instance || !physics || !physics->GetVehicleSimulation())
            return;

        car::Simulation* simulation = physics->GetVehicleSimulation();
        const auto& spec = simulation->get_spec();
        telemetry::snapshot s;
        s.vehicle_id = physics->GetObjectId();
        s.name = spec.name;
        s.gear = physics->GetCurrentGearString();
        s.differential = physics->GetDiffTypeName();
        s.speed = physics->GetLinearVelocity().Length() * 3.6f;
        s.rpm = physics->GetEngineRPM();
        s.redline = physics->GetRedlineRPM();
        s.boost = physics->GetBoostPressure();
        s.boost_max = physics->GetBoostMaxPressure();
        s.throttle = physics->GetVehicleThrottle();
        s.brake = physics->GetVehicleBrake();
        s.steering = physics->GetVehicleSteering();
        s.handbrake = physics->GetVehicleHandbrake();
        s.lateral_g = simulation->get_lateral_accel() / 9.81f;
        s.longitudinal_g = simulation->get_longitudinal_accel() / 9.81f;
        s.torque = simulation->get_engine_output_torque();
        s.motor_kw = simulation->get_motor_power_kw();
        s.clutch = simulation->get_clutch();
        s.hybrid = spec.electric_enabled && spec.battery_capacity_kwh > 0;
        const auto& battery = simulation->get_hybrid_state();
        s.battery_soc = s.hybrid ? battery.energy_j / (spec.battery_capacity_kwh * 3600000.0f) : 0;
        s.battery_temp = battery.temperature;
        s.battery_kw = battery.electrical_power_w * 0.001f;
        s.battery_hot = spec.battery_derate_temp;
        const auto& aero = simulation->get_aero_debug();
        s.aero_valid = aero.valid;
        s.drag = aero.drag_force.magnitude();
        s.front_downforce = aero.front_downforce.magnitude();
        s.rear_downforce = aero.rear_downforce.magnitude();
        s.ride_height = aero.ride_height;
        s.optimal_temp = spec.tire_optimal_temp;
        s.temp_range = spec.tire_temp_range;
        s.tc_reduction = physics->GetTcReduction();
        s.distance = simulation->get_distance_m();
        s.abs_enabled = physics->GetAbsEnabled();
        s.stability_enabled = spec.yaw_control_enabled;
        s.steering_enabled = spec.assists.steering_speed_reduction > 0;
        s.automatic = !physics->GetManualTransmission();
        s.tc_enabled = physics->GetTcEnabled();
        s.tc_active = physics->IsTcActive();
        s.drs_enabled = physics->GetDrsEnabled();
        s.drs_active = physics->GetDrsActive();
        s.turbo = physics->GetTurboEnabled();
        s.shifting = physics->IsShifting();
        s.limiter = simulation->get_rev_limiter_active();
        s.engine_running = simulation->get_engine_running();
        s.full_simulation = car_instance->GetVehicleSimMode() == VehicleSimMode::Full;
        const WheelIndex indices[] = {WheelIndex::FrontLeft, WheelIndex::FrontRight, WheelIndex::RearLeft, WheelIndex::RearRight};
        for (int i = 0; i < 4; ++i)
        {
            auto& w = s.wheels[i];
            const auto& physical = simulation->get_wheel_state(i);
            const WheelIndex index = indices[i];
            w.grounded = physics->IsWheelGrounded(index);
            w.abs = physics->IsAbsActive(index);
            for (int zone = 0; zone < 3; ++zone)
                w.surface[zone] = physics->GetWheelSurfaceTemp(index, zone);
            w.core = physics->GetWheelCoreTemp(index);
            w.pressure = physical.pressure_bar;
            w.wear = physics->GetWheelWear(index);
            w.damage = physical.damage;
            w.load = physics->GetWheelTireLoad(index);
            w.saturation = physical.tire_saturation;
            w.compression = physics->GetWheelCompression(index);
            w.slip_ratio = physics->GetWheelSlipRatio(index);
            w.slip_angle = physics->GetWheelSlipAngle(index) * 180.0f / pi;
            w.brake_temp = physics->GetWheelBrakeTemp(index);
            w.brake_efficiency = physics->GetWheelBrakeEfficiency(index);
            w.road = simulation->get_surface_name(physical.contact_surface);
        }

        static telemetry::history history;
        telemetry::vehicle_options options;
        std::vector<const car::car_definition*> definitions;
        for (const auto& entry : car::preset_registry)
        {
            if (entry.definition == car_instance->GetDefinition())
                options.selected = static_cast<int>(definitions.size());
            options.names.emplace_back(entry.name);
            definitions.push_back(entry.definition);
        }
        const int previous_selection = options.selected;
        options.full_simulation = s.full_simulation;
        options.skeleton = car_instance->GetVisualizationPreset() == CarVisualizationPreset::Skeleton;
        options.collision = car_instance->GetSkeletonShowCollision();
        const double now = ImGui::GetTime();
        history.update(s, now);
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 available(viewport->WorkSize.x - 24, viewport->WorkSize.y - 24);
        if (available.x < 200 || available.y < 200)
            return;
        const float default_scale = std::min({1.0f, (available.x - 24) / 1200, (available.y - 76) / telemetry::window_content_height});
        const ImVec2 size(1200 * default_scale + 24, telemetry::window_content_height * default_scale + 76);
        ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + available.x - size.x + 12,
            viewport->WorkPos.y + 12), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(960.0f, available.x), std::min(620.0f, available.y)), available);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.035f, 0.065f, 0.095f, 0.97f));
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNavInputs |
            ImGuiWindowFlags_NoFocusOnAppearing;
        if (ImGui::Begin("Telemetry", p_open, flags))
        {
            const ImVec2 content = ImGui::GetContentRegionAvail();
            const float footer_height = ImGui::GetFrameHeightWithSpacing() + 4;
            const float scale = std::max(0.01f, std::min(content.x / 1200, (content.y - footer_height) / telemetry::window_content_height));
            const ImVec2 start = ImGui::GetCursorScreenPos();
            const ImVec2 origin(start.x + (content.x - 1200 * scale) * 0.5f, start.y);
            telemetry::draw_vehicle_options({ImGui::GetWindowDrawList(), origin, scale}, options);
            const telemetry::painter painter{ImGui::GetWindowDrawList(),
                ImVec2(origin.x, origin.y + telemetry::vehicle_controls_height * scale), scale};
            ImGui::PushID(physics);
            ImGui::PushID(s.name.c_str());
            // Remember the actual configured strength per car/preset, rather than
            // replacing it with a generic value when the switch is re-enabled.
            ImGuiStorage* storage = ImGui::GetStateStorage();
            const ImGuiID steering_key = ImGui::GetID("steering_assist_strength");
            if (s.steering_enabled)
                storage->SetFloat(steering_key, spec.assists.steering_speed_reduction);
            switch (telemetry::draw(painter, s, history, now))
            {
                case telemetry::control::abs: physics->SetAbsEnabled(!s.abs_enabled); break;
                case telemetry::control::traction: physics->SetTcEnabled(!s.tc_enabled); break;
                case telemetry::control::drs: physics->SetDrsEnabled(!s.drs_enabled); break;
                case telemetry::control::turbo: physics->SetTurboEnabled(!s.turbo); break;
                case telemetry::control::automatic: physics->SetManualTransmission(s.automatic); break;
                case telemetry::control::stability: simulation->get_spec().yaw_control_enabled = !s.stability_enabled; break;
                case telemetry::control::steering:
                    simulation->get_spec().assists.steering_speed_reduction = s.steering_enabled ? 0.0f :
                        storage->GetFloat(steering_key, car::assist_settings().steering_speed_reduction);
                    break;
                case telemetry::control::none: break;
            }
            ImGui::PopID();
            ImGui::PopID();
            ImGui::Dummy(ImVec2(content.x, telemetry::window_content_height * scale));
            bool recording = simulation->get_log_to_file();
            if (ImGui::Checkbox("Record CSV", &recording))
                simulation->set_log_to_file(recording);
            hud_tooltip(simulation->get_telemetry_path().c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("I/C/O: tread zones | Blue: cold  Green: target  Amber: hot | Inputs: %%");
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);

        // A model change rebuilds vehicle resources. Apply it only after drawing and
        // do not use the old simulation/spec references again in this frame.
        if (options.selected != previous_selection && options.selected >= 0 &&
            options.selected < static_cast<int>(definitions.size()))
            car_instance->LoadDefinition(definitions[options.selected]);
        const VehicleSimMode mode = options.full_simulation ? VehicleSimMode::Full : VehicleSimMode::Cheap;
        if (mode != car_instance->GetVehicleSimMode())
            car_instance->SetVehicleSimMode(mode);
        const CarVisualizationPreset view = options.skeleton ? CarVisualizationPreset::Skeleton : CarVisualizationPreset::Full;
        if (view != car_instance->GetVisualizationPreset())
            car_instance->SetVisualizationPreset(view);
        if (options.collision != car_instance->GetSkeletonShowCollision())
            car_instance->SetSkeletonShowCollision(options.collision);
    }
}
