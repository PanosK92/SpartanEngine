/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===============================
#include "pch.h"
#include "CarPhysics.h"
#include "CarHud.h"
#include "../core/Engine.h"
#include "CarTelemetry.h"
#include "Car.h"
#include "CarState.h"
#include "CarSimulation.h"
#include "CarPresets.h"
#include "CarPaints.h"
#include "../commands/console/ConsoleCommands.h"
#include "../world/components/Physics.h"
#include "../world/Entity.h"
#include "imgui/source/imgui.h"
#include "widgets/Viewport.h"
#include <string_view>
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

            // dark backing at a fixed opacity so a dim tell tale stays legible over bright road markings
            dl->AddRectFilled(tl, br, IM_COL32(10, 13, 18, 200), 4.0f);

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

        ImU32 lerp_color(ImU32 a, ImU32 b, float t)
        {
            t = std::clamp(t, 0.0f, 1.0f);
            auto channel = [&](int shift)
            {
                const float ca = static_cast<float>((a >> shift) & 0xFF);
                const float cb = static_cast<float>((b >> shift) & 0xFF);
                return static_cast<ImU32>(ca + (cb - ca) * t + 0.5f) << shift;
            };
            return channel(IM_COL32_R_SHIFT) | channel(IM_COL32_G_SHIFT) | channel(IM_COL32_B_SHIFT) | channel(IM_COL32_A_SHIFT);
        }

        ImU32 with_alpha(ImU32 color, int alpha)
        {
            return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(std::clamp(alpha, 0, 255)) << IM_COL32_A_SHIFT);
        }

        // the viewport while the editor shows, the whole window otherwise, in a 1080p design space
        bool hud_region(ImVec2& region_pos, ImVec2& region_size, float& scale)
        {
            const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
            region_pos  = main_viewport->Pos;
            region_size = main_viewport->Size;
            const math::Vector2& vp_pos  = Viewport::GetScreenPosition();
            const math::Vector2& vp_size = Viewport::GetScreenSize();
            if (Engine::IsFlagSet(EngineMode::EditorVisible) && vp_size.x > 100.0f && vp_size.y > 100.0f)
            {
                region_pos  = ImVec2(vp_pos.x, vp_pos.y);
                region_size = ImVec2(vp_size.x, vp_size.y);
            }
            scale = std::clamp(std::min(region_size.y * 0.9f / 1080.0f, region_size.x / 2000.0f), 0.4f, 1.6f);
            return region_size.x >= 400 && region_size.y >= 300;
        }

        void draw_text_centered(ImDrawList* dl, ImFont* font, float size, ImVec2 center, ImU32 color, const char* text)
        {
            const ImVec2 text_size = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
            dl->AddText(font, size, ImVec2(center.x - text_size.x * 0.5f, center.y - text_size.y * 0.5f), color, text);
        }

        struct tachometer_state
        {
            float rpm         = 0.0f;
            float redline_rpm = 7000.0f;
            float max_rpm     = 7500.0f;
            float speed_kmh   = 0.0f;
            const char* gear  = "N";
            bool shifting     = false;
            bool limiter      = false;
            bool manual       = false;
        };

        // analog tachometer: needle sweeps 270 degrees clockwise from bottom left, gear lives in the hub, speed below it
        void draw_tachometer(ImDrawList* dl, ImFont* font, ImVec2 center, float radius, float scale, const tachometer_state& state, float dt)
        {
            const ImU32 cyan        = IM_COL32(104, 218, 255, 255);
            const ImU32 amber       = IM_COL32(255, 186, 64, 255);
            const ImU32 red         = IM_COL32(255, 64, 70, 255);
            const ImU32 green       = IM_COL32(90, 225, 120, 255);
            const ImU32 white       = IM_COL32(240, 245, 248, 255);
            const ImU32 needle_col  = IM_COL32(255, 96, 44, 255);
            const ImU32 tick_major  = IM_COL32(232, 238, 244, 255);
            const ImU32 tick_minor  = IM_COL32(150, 162, 175, 200);
            const float time        = static_cast<float>(ImGui::GetTime());

            // dial range rounds up past the rev limiter, labels thin out for high revving electric motors
            const float top_rpm    = std::max(state.redline_rpm + 500.0f, state.max_rpm);
            const float label_step = top_rpm <= 12000.0f ? 1000.0f : (top_rpm <= 24000.0f ? 2000.0f : 5000.0f);
            const float dial_max   = std::max(label_step * ceilf(top_rpm / label_step), 6000.0f);
            const float angle_min  = pi * 0.75f;
            const float sweep      = pi * 1.5f;
            auto rpm_to_angle = [&](float rpm)
            {
                return angle_min + sweep * std::clamp(rpm / dial_max, 0.0f, 1.0f);
            };
            auto polar = [&](float angle, float r)
            {
                return ImVec2(center.x + cosf(angle) * r, center.y + sinf(angle) * r);
            };

            // a needle has mass, so it chases the engine instead of teleporting
            static float needle_rpm = 0.0f;
            needle_rpm += (state.rpm - needle_rpm) * std::clamp(dt * 20.0f, 0.0f, 1.0f);
            const float needle_angle = rpm_to_angle(needle_rpm);
            const float near_redline = std::clamp((needle_rpm - state.redline_rpm * 0.85f) / (state.redline_rpm * 0.15f), 0.0f, 1.0f);

            // backplate
            for (int i = 0; i < 6; ++i)
            {
                dl->AddCircleFilled(ImVec2(center.x, center.y + 4.0f * scale), radius + (6 - i) * 2.0f * scale, IM_COL32(0, 0, 0, 10 + i * 4), 96);
            }
            dl->AddCircleFilled(center, radius, IM_COL32(9, 12, 16, 236), 96);
            dl->AddCircleFilled(center, radius * 0.97f, IM_COL32(16, 21, 27, 120), 96);
            dl->AddCircle(center, radius, IM_COL32(150, 166, 182, 120), 96, 1.5f * scale);
            dl->PathArcTo(center, radius - 2.5f * scale, pi * 1.15f, pi * 1.85f, 48);
            dl->PathStroke(IM_COL32(255, 255, 255, 28), 1.5f * scale);
            if (state.limiter)
            {
                const int alpha = static_cast<int>((0.5f + 0.5f * sinf(time * 30.0f)) * 220.0f);
                dl->AddCircle(center, radius + 1.5f * scale, with_alpha(red, alpha), 96, 3.0f * scale);
            }

            // redline zone sits under the ticks as a soft wedge
            const float angle_red = rpm_to_angle(state.redline_rpm);
            const float angle_max = angle_min + sweep;
            dl->PathArcTo(center, radius * 0.835f, angle_red, angle_max, 32);
            dl->PathStroke(with_alpha(red, 60 + static_cast<int>(near_redline * 70.0f)), radius * 0.09f);

            // live rpm arc around the rim, cyan through the band, amber approaching the redline, red past it
            const float arc_r  = radius * 0.945f;
            const float arc_th = 3.5f * scale;
            dl->PathArcTo(center, arc_r, angle_min, angle_max, 96);
            dl->PathStroke(IM_COL32(44, 52, 61, 190), arc_th);
            auto rpm_color = [&](float rpm)
            {
                const float f = rpm / std::max(state.redline_rpm, 1.0f);
                if (f < 0.72f)
                {
                    return cyan;
                }
                if (f < 0.94f)
                {
                    return lerp_color(cyan, amber, (f - 0.72f) / 0.22f);
                }
                return lerp_color(amber, red, (f - 0.94f) / 0.06f);
            };
            const float arc_step = pi / 90.0f;
            for (float a = angle_min; a < needle_angle; a += arc_step)
            {
                const float a_end = std::min(a + arc_step + 0.002f, needle_angle);
                const ImU32 col   = rpm_color((a - angle_min) / sweep * dial_max);
                dl->PathArcTo(center, arc_r, a, a_end, 3);
                dl->PathStroke(with_alpha(col, 46), arc_th * 3.2f);
                dl->PathArcTo(center, arc_r, a, a_end, 3);
                dl->PathStroke(col, arc_th);
            }

            // ticks and numerals
            const float minor_step  = label_step / 4.0f;
            const int   minor_count = static_cast<int>(dial_max / minor_step + 0.5f);
            const float tick_outer  = radius * 0.88f;
            const float label_size  = radius * 0.15f;
            const float label_r     = radius * 0.655f;
            for (int i = 0; i <= minor_count; ++i)
            {
                const float tick_rpm = i * minor_step;
                const bool  major    = i % 4 == 0;
                const bool  half     = i % 2 == 0;
                const bool  in_red   = tick_rpm >= state.redline_rpm - 1.0f;
                const float angle    = rpm_to_angle(tick_rpm);
                const float length   = radius * (major ? 0.13f : (half ? 0.085f : 0.05f));
                const ImU32 col      = in_red ? red : (major ? tick_major : tick_minor);
                dl->AddLine(polar(angle, tick_outer - length), polar(angle, tick_outer), col, (major ? 2.6f : 1.3f) * scale);
                if (major)
                {
                    char label[8];
                    snprintf(label, sizeof(label), "%d", static_cast<int>(tick_rpm / 1000.0f + 0.5f));
                    draw_text_centered(dl, font, label_size, polar(angle, label_r), in_red ? red : tick_major, label);
                }
            }
            draw_text_centered(dl, font, radius * 0.085f, ImVec2(center.x, center.y - radius * 0.46f), text_label, "x1000 RPM");

            // needle, a tapered blade that starts at the hub rim
            {
                const ImVec2 dir(cosf(needle_angle), sinf(needle_angle));
                const ImVec2 perp(-dir.y, dir.x);
                const float r_base = radius * 0.30f;
                const float r_tip  = radius * 0.92f;
                const float w_base = 3.8f * scale;
                const float w_tip  = 1.0f * scale;
                const ImVec2 base(center.x + dir.x * r_base, center.y + dir.y * r_base);
                const ImVec2 tip(center.x + dir.x * r_tip, center.y + dir.y * r_tip);
                dl->AddLine(base, tip, with_alpha(needle_col, 40), 10.0f * scale);
                const ImVec2 points[4] =
                {
                    ImVec2(base.x + perp.x * w_base, base.y + perp.y * w_base),
                    ImVec2(tip.x + perp.x * w_tip, tip.y + perp.y * w_tip),
                    ImVec2(tip.x - perp.x * w_tip, tip.y - perp.y * w_tip),
                    ImVec2(base.x - perp.x * w_base, base.y - perp.y * w_base)
                };
                dl->AddConvexPolyFilled(points, 4, needle_col);
            }

            // hub with the gear, its rim lights up while a shift is in progress
            const float hub_r = radius * 0.30f;
            dl->AddCircleFilled(center, hub_r + 3.0f * scale, IM_COL32(0, 0, 0, 120), 64);
            dl->AddCircleFilled(center, hub_r, IM_COL32(13, 17, 22, 255), 64);
            ImU32 hub_ring = IM_COL32(96, 112, 128, 200);
            if (state.limiter)
            {
                hub_ring = red;
            }
            else if (state.shifting)
            {
                hub_ring = amber;
            }
            dl->AddCircle(center, hub_r, hub_ring, 64, (state.shifting || state.limiter ? 2.5f : 1.5f) * scale);

            ImU32 gear_col = white;
            if (state.shifting)
            {
                gear_col = amber;
            }
            else if (state.gear[0] == 'R')
            {
                gear_col = amber;
            }
            else if (state.gear[0] == 'N')
            {
                gear_col = green;
            }
            draw_text_centered(dl, font, radius * 0.44f, ImVec2(center.x, center.y + radius * 0.01f), gear_col, state.gear);

            // digital speed under the hub
            char speed_text[16];
            snprintf(speed_text, sizeof(speed_text), "%.0f", state.speed_kmh);
            draw_text_centered(dl, font, radius * 0.27f, ImVec2(center.x, center.y + radius * 0.50f), white, speed_text);
            draw_text_centered(dl, font, radius * 0.085f, ImVec2(center.x, center.y + radius * 0.68f), text_label, "KM/H");

            // transmission mode tag in the gap at the bottom of the sweep
            {
                const char* mode      = state.manual ? "MANUAL" : "AUTO";
                const float mode_size = radius * 0.08f;
                const ImVec2 mode_ts  = font->CalcTextSizeA(mode_size, FLT_MAX, 0.0f, mode);
                const ImVec2 mode_c(center.x, center.y + radius * 0.84f);
                const ImVec2 tl(mode_c.x - mode_ts.x * 0.5f - 6.0f * scale, mode_c.y - mode_ts.y * 0.5f - 2.0f * scale);
                const ImVec2 br(mode_c.x + mode_ts.x * 0.5f + 6.0f * scale, mode_c.y + mode_ts.y * 0.5f + 2.0f * scale);
                const ImU32 mode_col  = state.manual ? amber : cyan;
                dl->AddRectFilled(tl, br, with_alpha(mode_col, 36), 3.0f * scale);
                dl->AddRect(tl, br, with_alpha(mode_col, 150), 3.0f * scale, 1.0f * scale);
                draw_text_centered(dl, font, mode_size, mode_c, mode_col, mode);
            }

            // shift lights above the dial: green, amber, red, then everything flashes at the shift point
            {
                const int   led_count = 10;
                const float led_r     = 4.2f * scale;
                const float row_w     = radius * 1.3f;
                const float row_y     = center.y - radius - 14.0f * scale;
                const float start_rpm = state.redline_rpm * 0.70f;
                const float shift_rpm = state.redline_rpm * 0.97f;
                const float lit       = std::clamp((state.rpm - start_rpm) / (shift_rpm - start_rpm), 0.0f, 1.0f) * led_count;
                const bool  flash     = state.rpm >= shift_rpm || state.limiter;
                const bool  flash_on  = fmodf(time * 12.0f, 2.0f) < 1.0f;
                for (int i = 0; i < led_count; ++i)
                {
                    const ImVec2 p(center.x - row_w * 0.5f + row_w * (i / static_cast<float>(led_count - 1)), row_y);
                    ImU32 col = i < 4 ? green : (i < 7 ? amber : red);
                    bool on   = static_cast<float>(i) < lit;
                    if (flash)
                    {
                        on  = flash_on;
                        col = red;
                    }
                    dl->AddCircleFilled(p, led_r + 1.5f * scale, IM_COL32(0, 0, 0, 150), 20);
                    if (on)
                    {
                        dl->AddCircleFilled(p, led_r * 2.4f, with_alpha(col, 50), 20);
                        dl->AddCircleFilled(p, led_r, col, 20);
                    }
                    else
                    {
                        dl->AddCircleFilled(p, led_r, IM_COL32(42, 48, 56, 220), 20);
                    }
                }
            }
        }

    } // anonymous namespace

    // ====================================================================================
    // driver hud, the always-on cockpit overlay
    // ====================================================================================

    void draw_driver_hud(Physics* physics, bool show_speedometer)
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
        const float engine_rpm       = CarPhysics::Get(*physics).GetEngineRPM();
        const float redline_rpm      = std::max(
            CarPhysics::Get(*physics).GetRedlineRPM(),
            1.0f
        );
        const bool turbo_enabled     = CarPhysics::Get(*physics).GetTurboEnabled();
        const float boost_bar        = CarPhysics::Get(*physics).GetBoostPressure();
        const bool is_shifting       = CarPhysics::Get(*physics).IsShifting();
        const char* gear_str         = CarPhysics::Get(*physics).GetCurrentGearString();
        const float throttle         = CarPhysics::Get(*physics).GetVehicleThrottle();
        const float brake            = CarPhysics::Get(*physics).GetVehicleBrake();
        const float steer            = CarPhysics::Get(*physics).GetVehicleSteering();
        const float handbrake        = CarPhysics::Get(*physics).GetVehicleHandbrake();

        const math::Vector2& vp_pos  = Viewport::GetScreenPosition();
        const math::Vector2& vp_size = Viewport::GetScreenSize();
        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        float region_left            = main_viewport->Pos.x;
        float region_width           = main_viewport->Size.x;
        float region_height          = main_viewport->Size.y;
        float anchor_bottom          = main_viewport->Pos.y + main_viewport->Size.y;
        if (Engine::IsFlagSet(EngineMode::EditorVisible) && vp_size.x > 100.0f && vp_size.y > 100.0f)
        {
            region_left   = vp_pos.x;
            region_width  = vp_size.x;
            region_height = vp_size.y;
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

            const ImU32 cyan       = IM_COL32(104, 218, 255, 255);
            const ImU32 red        = IM_COL32(255, 72, 78, 255);
            ImFont* font           = ImGui::GetFont();

            // bottom right cluster, sized from the viewport height so it reads the same on any aspect
            const float dial_scale  = std::clamp(region_height / 1080.0f, 0.75f, 1.5f);
            const float dial_radius = 128.0f * dial_scale;
            const ImVec2 dial_center(
                origin.x + region_width - margin - dial_radius,
                anchor_bottom - margin - dial_radius
            );

            // keep every dial digit resident at its raster size, a new glyph mid drive must not grow and upload the atlas
            for (const float size : {dial_radius * 0.44f, dial_radius * 0.27f, dial_radius * 0.15f})
            {
                ImFontBaked* baked = font->GetFontBaked(size);
                for (const char c : std::string_view("0123456789NRP-"))
                {
                    baked->FindGlyph(static_cast<ImWchar>(c));
                }
            }

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

            const bool abs_on     = CarPhysics::Get(*physics).GetAbsEnabled();
            const bool abs_active = CarPhysics::Get(*physics).IsAbsActiveAny();
            const bool abs_grab   =
                abs_active &&
                CarPhysics::Get(*physics).GetAbsPhase() >= 0.5f;
            const bool tcs_on     = CarPhysics::Get(*physics).GetTcEnabled();
            const bool tcs_active = CarPhysics::Get(*physics).IsTcActive();
            const bool drs_on     = CarPhysics::Get(*physics).GetDrsEnabled();
            const bool drs_active = CarPhysics::Get(*physics).GetDrsActive();
            const bool hbrk_active = handbrake > 0.1f;
            const bool lock_active = CarPhysics::Get(*physics).IsBurnoutActive();
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

            // tell tales stack up beside the dial, or run along the bottom edge when the telemetry hud owns the speedometer
            const float pill_w   = 54.0f * scale;
            const float pill_gap = 6.0f * scale;
            const float pill_h   = ImGui::GetTextLineHeight() + 6.0f;
            float pill_y =
                panel_br.y -
                24.0f * scale;
            float pill_x =
                origin.x +
                region_width -
                margin -
                pill_w;
            if (show_speedometer)
            {
                pill_x = dial_center.x - dial_radius - 18.0f * scale - pill_w;
                pill_y = dial_center.y + dial_radius - pill_h;
            }

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
                if (show_speedometer)
                {
                    pill_y -= pill_h + pill_gap;
                }
                else
                {
                    pill_x -= pill_w + pill_gap;
                }
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

            if (show_speedometer)
            {
                car::Simulation* simulation = CarPhysics::Get(*physics).GetVehicleSimulation();

                tachometer_state tach;
                tach.rpm         = engine_rpm;
                tach.redline_rpm = redline_rpm;
                tach.max_rpm     = simulation ? simulation->get_max_rpm() : redline_rpm * 1.07f;
                tach.speed_kmh   = speed_kmh;
                tach.gear        = gear_str ? gear_str : "N";
                tach.shifting    = is_shifting;
                tach.limiter     = simulation && simulation->get_rev_limiter_active();
                tach.manual      = CarPhysics::Get(*physics).GetManualTransmission();
                draw_tachometer(dl, font, dial_center, dial_radius, dial_scale, tach, io.DeltaTime);

                // boost readout tops the tell tale column
                if (turbo_enabled)
                {
                    char boost_text[24];
                    snprintf(boost_text, sizeof(boost_text), "%.1f BAR", boost_bar);
                    const ImVec2 boost_size = font->CalcTextSizeA(11.0f * scale, FLT_MAX, 0.0f, boost_text);
                    draw_text(boost_text, 11.0f, ImVec2(pill_x + (pill_w - boost_size.x) * 0.5f, pill_y + pill_h - boost_size.y), boost_bar > 2.0f ? red : cyan);
                }
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

    void draw_telemetry_hud(Car* car_instance, Physics* physics)
    {
        if (!car_instance || !physics || !CarPhysics::Get(*physics).GetVehicleSimulation())
            return;

        car::Simulation* simulation = CarPhysics::Get(*physics).GetVehicleSimulation();
        const auto& spec = simulation->get_spec();
        telemetry::snapshot s;
        s.vehicle_id = physics->GetObjectId();
        s.name = spec.name;
        s.gear = CarPhysics::Get(*physics).GetCurrentGearString();
        s.differential = CarPhysics::Get(*physics).GetDiffTypeName();
        s.speed = physics->GetLinearVelocity().Length() * 3.6f;
        s.rpm = CarPhysics::Get(*physics).GetEngineRPM();
        s.redline = CarPhysics::Get(*physics).GetRedlineRPM();
        s.boost = CarPhysics::Get(*physics).GetBoostPressure();
        s.boost_max = CarPhysics::Get(*physics).GetBoostMaxPressure();
        s.throttle = CarPhysics::Get(*physics).GetVehicleThrottle();
        s.brake = CarPhysics::Get(*physics).GetVehicleBrake();
        s.steering = CarPhysics::Get(*physics).GetVehicleSteering();
        s.handbrake = CarPhysics::Get(*physics).GetVehicleHandbrake();
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
        // signed, a positive lift coefficient makes lift and reads negative here
        s.front_downforce = spec.lift_coeff_front > 0 ? -aero.front_downforce.magnitude() : aero.front_downforce.magnitude();
        s.rear_downforce = spec.lift_coeff_rear > 0 ? -aero.rear_downforce.magnitude() : aero.rear_downforce.magnitude();
        s.ride_height = aero.ride_height;
        s.optimal_temp = spec.tire_optimal_temp;
        s.temp_range = spec.tire_temp_range;
        s.tc_reduction = CarPhysics::Get(*physics).GetTcReduction();
        s.distance = simulation->get_distance_m();
        s.abs_enabled = CarPhysics::Get(*physics).GetAbsEnabled();
        s.stability_enabled = spec.yaw_control_enabled;
        s.stability_active = simulation->get_assist_state().stability_active;
        s.steering_enabled = spec.assists.steering_speed_reduction > 0;
        s.automatic = !CarPhysics::Get(*physics).GetManualTransmission();
        s.tc_enabled = CarPhysics::Get(*physics).GetTcEnabled();
        s.tc_active = CarPhysics::Get(*physics).IsTcActive();
        s.drs_enabled = CarPhysics::Get(*physics).GetDrsEnabled();
        s.drs_active = CarPhysics::Get(*physics).GetDrsActive();
        s.turbo = CarPhysics::Get(*physics).GetTurboEnabled();
        s.shifting = CarPhysics::Get(*physics).IsShifting();
        s.limiter = simulation->get_rev_limiter_active();
        s.engine_running = simulation->get_engine_running();
        s.full_simulation = car_instance->GetVehicleSimMode() == VehicleSimMode::Full;
        const WheelIndex indices[] = {WheelIndex::FrontLeft, WheelIndex::FrontRight, WheelIndex::RearLeft, WheelIndex::RearRight};
        for (int i = 0; i < 4; ++i)
        {
            auto& w = s.wheels[i];
            const auto& physical = simulation->get_wheel_state(i);
            const WheelIndex index = indices[i];
            w.grounded = CarPhysics::Get(*physics).IsWheelGrounded(index);
            w.abs = CarPhysics::Get(*physics).IsAbsActive(index);
            for (int zone = 0; zone < 3; ++zone)
                w.surface[zone] = CarPhysics::Get(*physics).GetWheelSurfaceTemp(index, zone);
            w.core = CarPhysics::Get(*physics).GetWheelCoreTemp(index);
            w.pressure = physical.pressure_bar;
            w.wear = CarPhysics::Get(*physics).GetWheelWear(index);
            w.damage = physical.damage;
            w.load = CarPhysics::Get(*physics).GetWheelTireLoad(index);
            w.saturation = physical.tire_saturation;
            w.compression = CarPhysics::Get(*physics).GetWheelCompression(index);
            w.slip_ratio = CarPhysics::Get(*physics).GetWheelSlipRatio(index);
            w.slip_angle = CarPhysics::Get(*physics).GetWheelSlipAngle(index) * 180.0f / pi;
            w.brake_temp = CarPhysics::Get(*physics).GetWheelBrakeTemp(index);
            w.brake_efficiency = CarPhysics::Get(*physics).GetWheelBrakeEfficiency(index);
            w.road = simulation->get_surface_name(physical.contact_surface);
            w.surface_grip = physical.surface_grip;
            w.surface_rolling = physical.surface_rolling;
            w.mixed_surface = physical.mixed_surface;
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
        options.visualization = static_cast<int>(car_instance->GetVisualizationPreset());
        options.collision = car_instance->GetSkeletonShowCollision();
        const double now = ImGui::GetTime();
        history.update(s, now);
        // same region as the driver hud: the editor viewport when the editor is visible, otherwise the whole window
        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        ImVec2 region_pos = main_viewport->Pos;
        ImVec2 region_size = main_viewport->Size;
        const math::Vector2& vp_pos = Viewport::GetScreenPosition();
        const math::Vector2& vp_size = Viewport::GetScreenSize();
        if (Engine::IsFlagSet(EngineMode::EditorVisible) && vp_size.x > 100.0f && vp_size.y > 100.0f)
        {
            region_pos = ImVec2(vp_pos.x, vp_pos.y);
            region_size = ImVec2(vp_size.x, vp_size.y);
        }
        if (region_size.x < 400 || region_size.y < 300)
            return;

        // design space is 1200 units tall, and at least wide enough for both side columns and the tire cards
        const float scale = std::clamp(std::min(region_size.y * 0.9f / 1080.0f, region_size.x / 2000.0f), 0.4f, 1.6f);
        const float width = region_size.x / scale;
        const float height = region_size.y / scale;
        const float center_x = width * 0.5f;
        // the chase camera frames the car in the lower half, about a third of the screen wide
        const float car_half_w = 400;
        const float margin = 24;
        const float gap = 12;
        const float front_y = height * 0.47f;
        const float rear_y = front_y + telemetry::corner_h + 16;
        const float strip_x = center_x - telemetry::strip_w * 0.5f;
        const float strip_y = 18;
        const float strip_pad = 10;
        static bool setup_open = false;
        const float below_strip = strip_y + telemetry::strip_h + strip_pad + gap;
        const float header_y = below_strip + (setup_open ? telemetry::setup_h + gap : 0.0f); // the setup menu opens between the strip and the header
        const float column_y = below_strip; // high enough that the left column clears the driver hud input bars
        const ImU32 panel_fill = IM_COL32(14, 22, 31, 200);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        const ImGuiWindowFlags base_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse;

        // read only panels ring the car and let every click through to the viewport
        // NoBringToFrontOnFocus would create this window behind the editor dockspace, NoInputs already keeps it from taking focus
        ImGui::SetNextWindowPos(region_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(region_size, ImGuiCond_Always);
        if (ImGui::Begin("##telemetry_hud_panels", nullptr, base_flags | ImGuiWindowFlags_NoInputs))
        {
            const telemetry::painter hud{ImGui::GetWindowDrawList(), region_pos, scale, panel_fill};
            if (s.full_simulation)
            {
                telemetry::draw_header(hud.at(center_x - telemetry::header_w * 0.5f, header_y), s);

                // tire cards sit at their physical corners around the car
                telemetry::draw_corner(hud, center_x - car_half_w - telemetry::corner_w, front_y, 0, s);
                telemetry::draw_corner(hud, center_x + car_half_w, front_y, 1, s);
                telemetry::draw_corner(hud, center_x - car_half_w - telemetry::corner_w, rear_y, 2, s);
                telemetry::draw_corner(hud, center_x + car_half_w, rear_y, 3, s);

                // left edge: dynamics
                float y = column_y;
                telemetry::draw_g_force(hud.at(margin, y), s, history, now);
                y += telemetry::g_force_h + gap;
                telemetry::draw_inputs(hud.at(margin, y), s);
                y += telemetry::inputs_h + gap;
                telemetry::draw_history(hud.at(margin, y), s, history, now, telemetry::g_force_w, telemetry::trace_kind::speed);
                y += telemetry::history_h + gap;
                telemetry::draw_history(hud.at(margin, y), s, history, now, telemetry::g_force_w, telemetry::trace_kind::pedals);

                // right edge: powertrain and platform
                const float right_x = width - margin - telemetry::powertrain_w;
                y = column_y;
                telemetry::draw_powertrain(hud.at(right_x, y), s);
                y += telemetry::powertrain_h + gap;
                telemetry::draw_aero(hud.at(right_x, y), s);
                y += telemetry::aero_h + gap;
                telemetry::draw_history(hud.at(right_x, y), s, history, now, telemetry::aero_w, telemetry::trace_kind::travel);
                y += telemetry::history_h + gap;
                telemetry::draw_chassis(hud.at(right_x, y), s);
            }
            else
            {
                telemetry::draw_limited(hud.at(center_x - 300, header_y), s);
            }
        }
        ImGui::End();

        // the top strip is the only part that takes the mouse
        const float controls_h = telemetry::strip_h + (setup_open ? strip_pad + gap + telemetry::setup_h : 0.0f);
        ImGui::SetNextWindowPos(ImVec2(region_pos.x + (strip_x - strip_pad) * scale, region_pos.y + (strip_y - strip_pad) * scale), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2((telemetry::strip_w + strip_pad * 2) * scale, (controls_h + strip_pad * 2) * scale), ImGuiCond_Always);
        if (ImGui::Begin("##telemetry_hud_controls", nullptr, base_flags))
        {
            const telemetry::painter painter{ImGui::GetWindowDrawList(), ImVec2(region_pos.x + strip_x * scale, region_pos.y + strip_y * scale), scale, panel_fill};
            painter.rect(-strip_pad, -strip_pad, telemetry::strip_w + strip_pad * 2, telemetry::strip_h + strip_pad * 2, IM_COL32(8, 13, 19, 150), 10);
            ImGui::PushID(physics);
            ImGui::PushID(s.name.c_str());
            // Remember the actual configured strength per car/preset, rather than
            // replacing it with a generic value when the switch is re-enabled.
            ImGuiStorage* storage = ImGui::GetStateStorage();
            const ImGuiID steering_key = ImGui::GetID("steering_assist_strength");
            if (s.steering_enabled)
                storage->SetFloat(steering_key, spec.assists.steering_speed_reduction);
            switch (telemetry::draw_strip(painter, s, setup_open))
            {
                case telemetry::control::abs: CarPhysics::Get(*physics).SetAbsEnabled(!s.abs_enabled); break;
                case telemetry::control::traction: CarPhysics::Get(*physics).SetTcEnabled(!s.tc_enabled); break;
                case telemetry::control::drs: CarPhysics::Get(*physics).SetDrsEnabled(!s.drs_enabled); break;
                case telemetry::control::turbo: CarPhysics::Get(*physics).SetTurboEnabled(!s.turbo); break;
                case telemetry::control::automatic: CarPhysics::Get(*physics).SetManualTransmission(s.automatic); break;
                case telemetry::control::stability: simulation->get_spec().yaw_control_enabled = !s.stability_enabled; break;
                case telemetry::control::steering:
                    simulation->get_spec().assists.steering_speed_reduction = s.steering_enabled ? 0.0f :
                        storage->GetFloat(steering_key, car::assist_settings().steering_speed_reduction);
                    break;
                case telemetry::control::none: break;
            }
            if (setup_open)
            {
                telemetry::painter setup = painter.at(0, telemetry::strip_h + strip_pad + gap);
                setup.fill = IM_COL32(20, 31, 43, 250);
                telemetry::draw_vehicle_options(setup, options);
                float tire_pressure = simulation->get_tire_pressure();
                if (telemetry::draw_tire_pressure(setup.at(0, 58), s.full_simulation, tire_pressure, simulation->get_base_spec().tire_pressure))
                    simulation->set_tire_pressure(tire_pressure);

                // csv export and camera options, 1200 x 34
                const telemetry::painter row = setup.at(0, 116);
                row.rect(0, 0, 1200, 34, setup.fill, 9);
                ImGui::PushFont(nullptr, 14 * scale);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4 * scale, 2 * scale));
                ImGui::SetCursorScreenPos(row.point(12, 6));
                bool recording = simulation->get_log_to_file();
                if (ImGui::Checkbox("Record CSV", &recording))
                    simulation->set_log_to_file(recording);
                hud_tooltip("Start or stop CSV telemetry export. Starting a new recording replaces the previous CSV.");
                row.text(150, 10, 12, recording ? telemetry::red : telemetry::muted, recording ? "RECORDING" : "STOPPED");
                ImGui::SetCursorScreenPos(row.point(150, 6));
                ImGui::InvisibleButton("##csv_path", ImVec2(90 * scale, 22 * scale));
                hud_tooltip(simulation->get_telemetry_path().c_str());
                ImGui::SetCursorScreenPos(row.point(430, 6));
                bool wind_shake = car_instance->GetCameraWindShake();
                if (ImGui::Checkbox("Experimental wind shake", &wind_shake))
                    car_instance->SetCameraWindShake(wind_shake);
                hud_tooltip("Subtle chase-camera wind buffeting above 54 km/h, increasing with speed. Disable for a steady view.");
                ImGui::SetCursorScreenPos(row.point(680, 6));
                bool speed_effects = car_instance->GetCameraSpeedEffects();
                if (ImGui::Checkbox("Camera speed effects", &speed_effects))
                    car_instance->SetCameraSpeedEffects(speed_effects);
                hud_tooltip("Chase camera widens its lens with speed, pulls back under throttle, comes in under braking and lags over bumps and landings.");
                ImGui::PopStyleVar();
                ImGui::PopFont();
            }
            ImGui::PopID();
            ImGui::PopID();
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
        const CarVisualizationPreset view = static_cast<CarVisualizationPreset>(options.visualization);
        if (view != car_instance->GetVisualizationPreset())
            car_instance->SetVisualizationPreset(view);
        if (options.collision != car_instance->GetSkeletonShowCollision())
            car_instance->SetSkeletonShowCollision(options.collision);
    }

    Car* draw_car_picker(Car* viewed)
    {
        std::vector<Car*> cars;
        for (Car* car : Car::GetAll())
        {
            Entity* root = car ? car->GetRootEntity() : nullptr;
            if (car && car->IsDrivable() && root && root->IsActive())
            {
                cars.push_back(car);
            }
        }
        if (cars.empty())
        {
            return nullptr;
        }

        // same region and design space as the telemetry hud, the list sits above its right column
        ImVec2 region_pos;
        ImVec2 region_size;
        float scale = 1.0f;
        if (!hud_region(region_pos, region_size, scale))
        {
            return nullptr;
        }

        const float width         = region_size.x / scale;
        const float margin        = 24;
        const float panel_w       = telemetry::powertrain_w;
        const float title_h       = 26;
        const float row_h         = 24;
        const float visible_rows  = static_cast<float>(std::min<size_t>(cars.size(), 4));
        const bool show_lights    = viewed && viewed->HasLights();
        const float lights_h      = 34;
        const float lights_y      = title_h + row_h * visible_rows + 6;
        const float panel_h       = title_h + row_h * visible_rows + 8 + (show_lights ? lights_h : 0);
        const float panel_x       = width - margin - panel_w;
        const float panel_y       = 8;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(83 / 255.0f, 216 / 255.0f, 229 / 255.0f, 0.22f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(83 / 255.0f, 216 / 255.0f, 229 / 255.0f, 0.32f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(83 / 255.0f, 216 / 255.0f, 229 / 255.0f, 0.45f));
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse;

        Car* chosen = nullptr;
        ImGui::SetNextWindowPos(ImVec2(region_pos.x + panel_x * scale, region_pos.y + panel_y * scale), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(panel_w * scale, panel_h * scale), ImGuiCond_Always);
        if (ImGui::Begin("##car_picker", nullptr, flags))
        {
            const telemetry::painter panel{ImGui::GetWindowDrawList(), ImGui::GetWindowPos(), scale, IM_COL32(14, 22, 31, 200)};
            panel.rect(0, 0, panel_w, panel_h, panel.fill, 10);
            panel.text(12, 7, 13, telemetry::cyan, "CARS");
            panel.right(panel_w - 12, 7, 12, telemetry::muted, "CLICK TO DRIVE OR WATCH");

            ImGui::PushFont(nullptr, 14 * scale);
            ImGui::SetCursorScreenPos(panel.point(6, title_h));
            if (ImGui::BeginChild("##car_picker_rows", ImVec2((panel_w - 12) * scale, row_h * visible_rows * scale), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground))
            {
                for (Car* car : cars)
                {
                    const bool is_viewed = car == viewed;
                    // someone else holds the pedals of an externally controlled car, the player can only watch it
                    const char* role = car->IsOccupied() ? "DRIVING" : (car->IsSpectated() ? "WATCHING" : (car->IsExternallyControlled() ? "WATCH" : "DRIVE"));
                    const std::string name = car->GetDisplayName();

                    ImGui::PushID(car);
                    const ImVec2 row_min = ImGui::GetCursorScreenPos();
                    if (ImGui::Selectable("##car", is_viewed, ImGuiSelectableFlags_None, ImVec2(0, row_h * scale)) && !is_viewed)
                    {
                        chosen = car;
                    }
                    const float row_w = ImGui::GetItemRectSize().x;
                    ImDrawList* dl    = ImGui::GetWindowDrawList();
                    const float font  = 14 * scale;
                    const float text_y = row_min.y + (row_h * scale - font) * 0.5f;
                    dl->AddText(nullptr, font, ImVec2(row_min.x + 6 * scale, text_y), is_viewed ? IM_COL32(245, 250, 250, 255) : IM_COL32(200, 210, 220, 255), name.c_str());
                    const ImU32 role_color = is_viewed ? telemetry::green : (car->IsExternallyControlled() ? telemetry::amber : telemetry::cyan);
                    const float role_w = ImGui::CalcTextSize(role).x * (12.0f / 14.0f);
                    dl->AddText(nullptr, 12 * scale, ImVec2(row_min.x + row_w - role_w - 8 * scale, text_y + 1 * scale), role_color, role);
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();

            // exterior lamps of the viewed car, driven or watched
            if (show_lights)
            {
                panel.line(12, lights_y, panel_w - 12, lights_y, IM_COL32(53, 73, 90, 180));
                panel.text(12, lights_y + 10, 12, telemetry::muted, "LIGHTS");

                struct LightButton
                {
                    const char* label;
                    bool active;
                };
                const CarHeadlights headlights = viewed->GetHeadlights();
                const LightButton buttons[] =
                {
                    { "OFF",  headlights == CarHeadlights::Off },
                    { "LOW",  headlights == CarHeadlights::Low },
                    { "HIGH", headlights == CarHeadlights::High },
                    { "REAR", viewed->GetRearLamps() }
                };
                const float button_x = 70;
                const float button_w = (panel_w - button_x - 12 - 3 * 6) / 4;
                const float button_h = 22;
                for (int i = 0; i < 4; ++i)
                {
                    const float x = button_x + i * (button_w + 6);
                    const float y = lights_y + 6;
                    ImGui::PushID(i);
                    ImGui::SetCursorScreenPos(panel.point(x, y));
                    if (ImGui::Selectable("##light", buttons[i].active, ImGuiSelectableFlags_None, ImVec2(button_w * scale, button_h * scale)))
                    {
                        if (i < 3)
                        {
                            viewed->SetHeadlights(static_cast<CarHeadlights>(i));
                        }
                        else
                        {
                            viewed->SetRearLamps(!viewed->GetRearLamps());
                        }
                    }
                    ImDrawList* dl         = ImGui::GetWindowDrawList();
                    const ImVec2 min       = panel.point(x, y);
                    const ImVec2 max       = panel.point(x + button_w, y + button_h);
                    const ImU32 edge       = buttons[i].active ? (i == 3 ? telemetry::amber : telemetry::green) : IM_COL32(53, 73, 90, 200);
                    dl->AddRect(min, max, edge, 4 * scale);
                    const float font       = 12 * scale;
                    const ImVec2 text_size = ImGui::GetFont()->CalcTextSizeA(font, FLT_MAX, 0, buttons[i].label);
                    const ImVec2 text_pos  = ImVec2((min.x + max.x - text_size.x) * 0.5f, (min.y + max.y - text_size.y) * 0.5f);
                    dl->AddText(nullptr, font, text_pos, buttons[i].active ? IM_COL32(245, 250, 250, 255) : IM_COL32(170, 185, 195, 255), buttons[i].label);
                    ImGui::PopID();
                }
            }
            ImGui::PopFont();
        }
        ImGui::End();
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(3);

        return chosen;
    }

    namespace
    {
        // radians per second, project/scripts/turntable.lua reads it every frame so any turntable in a showroom follows the panel
        TConsoleVar<float> cvar_turntable_speed("showroom.turntable_speed", 0.175f, "spin rate of showroom turntables in radians per second, negative spins the other way, 0 stops them");
        constexpr float turntable_default_speed = 0.175f;
        float turntable_resume_speed            = turntable_default_speed;

        const ImU32 panel_rule = IM_COL32(53, 73, 90, 180);

        // a flat button in the hud style, cyan outlined while it is the active choice
        bool hud_button(const telemetry::painter& panel, const char* id, float x, float y, float w, float h, const char* label, bool selected, bool enabled = true, float text_size = 12.0f)
        {
            ImGui::SetCursorScreenPos(panel.point(x, y));
            ImGui::BeginDisabled(!enabled);
            const bool pressed = ImGui::InvisibleButton(id, ImVec2(w * panel.scale, h * panel.scale));
            const bool hovered = ImGui::IsItemHovered();
            ImGui::EndDisabled();

            const ImU32 fill = selected ? IM_COL32(83, 216, 229, 60) : (hovered && enabled ? IM_COL32(53, 73, 90, 255) : IM_COL32(43, 58, 72, 230));
            panel.rect(x, y, w, h, fill, 4);
            if (selected)
            {
                panel.dl->AddRect(panel.point(x, y), panel.point(x + w, y + h), telemetry::cyan, 4.0f * panel.scale, 1.5f * panel.scale);
            }

            const ImVec2 size = ImGui::GetFont()->CalcTextSizeA(text_size * panel.scale, FLT_MAX, 0.0f, label);
            const ImU32 ink   = !enabled ? IM_COL32(151, 170, 186, 90) : (selected ? telemetry::ink : IM_COL32(200, 214, 226, 255));
            panel.text(x + (w - size.x / panel.scale) * 0.5f, y + (h - size.y / panel.scale) * 0.5f, text_size, ink, label);
            return pressed && enabled;
        }

        void draw_showroom_controls(Car* car, float panel_x, float panel_y, float scale, const ImVec2& region_pos)
        {
            const float panel_w   = 300;
            const float title_h   = 50;
            const float section_h = 22;
            const float row_h     = 34;
            const float table_y   = title_h;
            const float lamps_y   = table_y + section_h + row_h * 2 + 8;
            const float wheels_y  = lamps_y + section_h + row_h * 2 + 8;
            const float panel_h   = wheels_y + section_h + row_h * 2 + 10;
            const float column_x  = 108;
            const float column_w  = panel_w - column_x - 16;

            const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse;
            ImGui::SetNextWindowPos(ImVec2(region_pos.x + panel_x * scale, region_pos.y + panel_y * scale), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(panel_w * scale, panel_h * scale), ImGuiCond_Always);
            if (ImGui::Begin("##car_showroom_controls", nullptr, flags))
            {
                const telemetry::painter panel{ImGui::GetWindowDrawList(), ImGui::GetWindowPos(), scale, IM_COL32(14, 22, 31, 215)};
                panel.card(0, 0, panel_w, panel_h, "");
                panel.text(16, 10, 20, telemetry::ink, "SHOWROOM");
                panel.right(panel_w - 16, 16, 12, telemetry::cyan, "P TO HIDE");
                panel.line(16, title_h - 6, panel_w - 16, title_h - 6, panel_rule);

                ImGui::PushFont(nullptr, 13 * scale);

                // turntable
                {
                    panel.text(16, table_y + 4, 12, telemetry::muted, "TURNTABLE");
                    const float y = table_y + section_h;
                    panel.text(16, y + 7, 13, telemetry::ink, "Speed");
                    float degrees = cvar_turntable_speed.GetValue() * (180.0f / pi);
                    ImGui::SetCursorScreenPos(panel.point(column_x, y + 2));
                    ImGui::SetNextItemWidth(column_w * scale);
                    if (ImGui::SliderFloat("##turntable_speed", &degrees, -30.0f, 30.0f, "%.1f deg/s", ImGuiSliderFlags_AlwaysClamp))
                    {
                        cvar_turntable_speed.SetValue(degrees * (pi / 180.0f));
                    }
                    hud_tooltip("How fast the turntable spins, negative spins it the other way.");

                    const float speed  = cvar_turntable_speed.GetValue();
                    const bool stopped = speed == 0.0f;
                    const float b_w    = (panel_w - 32 - 12) / 3.0f;
                    const float b_y    = y + row_h;
                    if (hud_button(panel, "##turntable_stop", 16, b_y, b_w, 26, stopped ? "PLAY" : "STOP", stopped))
                    {
                        if (stopped)
                        {
                            cvar_turntable_speed.SetValue(turntable_resume_speed != 0.0f ? turntable_resume_speed : turntable_default_speed);
                        }
                        else
                        {
                            turntable_resume_speed = speed;
                            cvar_turntable_speed.SetValue(0.0f);
                        }
                    }
                    if (hud_button(panel, "##turntable_reverse", 16 + (b_w + 6), b_y, b_w, 26, "REVERSE", false, !stopped))
                    {
                        cvar_turntable_speed.SetValue(-speed);
                    }
                    if (hud_button(panel, "##turntable_default", 16 + (b_w + 6) * 2, b_y, b_w, 26, "DEFAULT", speed == turntable_default_speed))
                    {
                        cvar_turntable_speed.SetValue(turntable_default_speed);
                    }
                }

                // lamps
                {
                    panel.line(16, lamps_y - 4, panel_w - 16, lamps_y - 4, panel_rule);
                    const bool has_lamps = car->HasSwitchableLamps();
                    panel.text(16, lamps_y + 4, 12, telemetry::muted, "LAMPS");
                    if (!has_lamps)
                    {
                        panel.right(panel_w - 16, lamps_y + 5, 11, telemetry::muted, "NO LAMPS ON THIS CAR");
                    }

                    const float y   = lamps_y + section_h;
                    const float b_w = (column_w - 12) / 3.0f;
                    panel.text(16, y + 7, 13, has_lamps ? telemetry::ink : telemetry::muted, "Headlights");
                    const char* beams[] = { "OFF", "LOW", "HIGH" };
                    for (int i = 0; i < 3; i++)
                    {
                        ImGui::PushID(i);
                        if (hud_button(panel, "##beam", column_x + (b_w + 6) * i, y + 2, b_w, 26, beams[i], has_lamps && static_cast<int>(car->GetHeadlights()) == i, has_lamps))
                        {
                            car->SetHeadlights(static_cast<CarHeadlights>(i));
                        }
                        ImGui::PopID();
                    }

                    const float t_y = y + row_h;
                    const float t_w = (column_w - 6) / 2.0f;
                    panel.text(16, t_y + 7, 13, has_lamps ? telemetry::ink : telemetry::muted, "Tail lamps");
                    if (hud_button(panel, "##tail_off", column_x, t_y + 2, t_w, 26, "OFF", has_lamps && !car->GetRearLamps(), has_lamps))
                    {
                        car->SetRearLamps(false);
                    }
                    if (hud_button(panel, "##tail_on", column_x + t_w + 6, t_y + 2, t_w, 26, "ON", has_lamps && car->GetRearLamps(), has_lamps))
                    {
                        car->SetRearLamps(true);
                    }
                }

                // wheels
                {
                    panel.line(16, wheels_y - 4, panel_w - 16, wheels_y - 4, panel_rule);
                    const bool has_wheels = car->IsDrivable();
                    panel.text(16, wheels_y + 4, 12, telemetry::muted, "WHEELS");
                    if (!has_wheels)
                    {
                        panel.right(panel_w - 16, wheels_y + 5, 11, telemetry::muted, "NO STEERING ON THIS CAR");
                    }

                    const float y = wheels_y + section_h;
                    panel.text(16, y + 7, 13, has_wheels ? telemetry::ink : telemetry::muted, "Steering");
                    float steering = car->GetSteeringRequest();
                    ImGui::BeginDisabled(!has_wheels);
                    ImGui::SetCursorScreenPos(panel.point(column_x, y + 2));
                    ImGui::SetNextItemWidth(column_w * scale);
                    if (ImGui::SliderFloat("##steering", &steering, -1.0f, 1.0f, steering < -0.005f ? "%.2f left" : (steering > 0.005f ? "%.2f right" : "centred"), ImGuiSliderFlags_AlwaysClamp))
                    {
                        car->SetSteeringRequest(steering);
                    }
                    hud_tooltip("Turns the front wheels, full travel is the car's steering lock.");
                    ImGui::EndDisabled();

                    const float b_y = y + row_h;
                    const float b_w = (panel_w - 32 - 12) / 3.0f;
                    if (hud_button(panel, "##lock_left", 16, b_y, b_w, 26, "FULL LEFT", has_wheels && steering <= -1.0f, has_wheels))
                    {
                        car->SetSteeringRequest(-1.0f);
                    }
                    if (hud_button(panel, "##lock_centre", 16 + (b_w + 6), b_y, b_w, 26, "CENTRE", has_wheels && steering == 0.0f, has_wheels))
                    {
                        car->SetSteeringRequest(0.0f);
                    }
                    if (hud_button(panel, "##lock_right", 16 + (b_w + 6) * 2, b_y, b_w, 26, "FULL RIGHT", has_wheels && steering >= 1.0f, has_wheels))
                    {
                        car->SetSteeringRequest(1.0f);
                    }
                }

                ImGui::PopFont();
            }
            ImGui::End();
        }
    }

    void draw_showroom_panel(Car* car, bool collapsed)
    {
        if (!car || !ImGui::GetCurrentContext())
        {
            return;
        }

        ImVec2 region_pos;
        ImVec2 region_size;
        float scale = 1.0f;
        if (!hud_region(region_pos, region_size, scale))
        {
            return;
        }

        using namespace car_paints;
        const int family_count = static_cast<int>(family::count);
        const float margin     = 24;
        const float panel_w    = 372;
        const float title_h    = 50;
        const float row_h      = 40;
        const float chip_x     = 112;
        const float chip_d     = 30;
        const float chip_step  = 36;
        const float info_h     = 66;
        const int   finish_columns = 3;
        const float finish_title   = 30;
        const float finish_step    = 30;
        const int   finish_rows    = (finish_count + finish_columns - 1) / finish_columns;
        const float lab_title  = 34;
        const float slider_h   = 30;
        const int   lab_rows   = 5;
        const float rows_y     = title_h;
        const float info_y     = rows_y + row_h * family_count + 4;
        const float finish_y   = info_y + info_h;
        const float lab_y      = finish_y + finish_title + finish_step * finish_rows + 6;
        const float panel_h    = collapsed ? 40 : lab_y + lab_title + slider_h * lab_rows + 14;
        const float panel_x    = margin;
        // the editor keeps its viewport buttons in the top left corner
        const float panel_y    = Engine::IsFlagSet(EngineMode::EditorVisible) ? 52.0f : 8.0f;
        const int   selected   = car->GetPaintIndex();
        const int   selected_finish = car->GetPaintFinish();
        Material* material     = car->GetPaintMaterial();

        auto swatch = [](uint8_t r, uint8_t g, uint8_t b, int alpha = 255)
        {
            return IM_COL32(r, g, b, alpha);
        };

        // a chip hints at the finish: gloss gets a sharp highlight, metallic a soft sheen, matte none, iridescent a hue ring
        auto draw_chip = [&](ImDrawList* dl, ImVec2 center, float radius, const paint& p, bool is_selected, bool hovered)
        {
            const ImU32 base = swatch(p.r, p.g, p.b);
            if (is_selected || hovered)
            {
                dl->AddCircle(center, radius + 3.5f * scale, is_selected ? telemetry::ink : IM_COL32(151, 170, 186, 160), 32, (is_selected ? 2.0f : 1.0f) * scale);
            }
            dl->AddCircleFilled(center, radius, base, 32);

            const bool matte = p.preset == MaterialPaintPreset::Matte || p.preset == MaterialPaintPreset::Satin;
            if (!matte)
            {
                const bool metallic = p.preset == MaterialPaintPreset::Metallic || p.preset == MaterialPaintPreset::Pearl || p.preset == MaterialPaintPreset::Candy;
                const ImU32 sheen   = lerp_color(base, IM_COL32(255, 255, 255, 255), metallic ? 0.35f : 0.15f);
                dl->AddCircleFilled(ImVec2(center.x - radius * 0.22f, center.y - radius * 0.22f), radius * 0.62f, with_alpha(sheen, metallic ? 150 : 90), 24);
                dl->AddCircleFilled(ImVec2(center.x - radius * 0.38f, center.y - radius * 0.4f), radius * 0.2f, IM_COL32(255, 255, 255, metallic ? 120 : 190), 16);
            }

            if (p.pr || p.pg || p.pb)
            {
                dl->AddCircle(center, radius - 1.5f * scale, swatch(p.pr, p.pg, p.pb, 140), 32, 1.5f * scale);
            }

            if (p.iridescence > 0.0f)
            {
                const int segments  = 24;
                const float time    = static_cast<float>(ImGui::GetTime());
                const float thick   = 2.5f * scale;
                for (int s = 0; s < segments; s++)
                {
                    const float a0 = (s / static_cast<float>(segments)) * 2.0f * pi;
                    const float a1 = ((s + 1) / static_cast<float>(segments)) * 2.0f * pi;
                    float r, g, b;
                    ImGui::ColorConvertHSVtoRGB(std::fmod(s / static_cast<float>(segments) + time * 0.08f, 1.0f), 0.65f, 1.0f, r, g, b);
                    dl->PathArcTo(center, radius - thick * 0.5f, a0, a1 + 0.02f, 4);
                    dl->PathStroke(ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 0.85f)), thick);
                }
            }

            dl->AddCircle(center, radius, IM_COL32(0, 0, 0, 90), 32, 1.0f * scale);
        };

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 3.0f * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6 * scale, 4 * scale));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(43 / 255.0f, 58 / 255.0f, 72 / 255.0f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(53 / 255.0f, 73 / 255.0f, 90 / 255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(53 / 255.0f, 73 / 255.0f, 90 / 255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(83 / 255.0f, 216 / 255.0f, 229 / 255.0f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(126 / 255.0f, 236 / 255.0f, 245 / 255.0f, 1.0f));
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse;

        ImGui::SetNextWindowPos(ImVec2(region_pos.x + panel_x * scale, region_pos.y + panel_y * scale), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(panel_w * scale, panel_h * scale), ImGuiCond_Always);
        if (ImGui::Begin("##car_paint_picker", nullptr, flags))
        {
            const telemetry::painter panel{ImGui::GetWindowDrawList(), ImGui::GetWindowPos(), scale, IM_COL32(14, 22, 31, 215)};
            ImDrawList* dl = ImGui::GetWindowDrawList();

            if (collapsed)
            {
                panel.card(0, 0, panel_w, panel_h, "");
                panel.text(16, 12, 14, telemetry::ink, "SHOWROOM");
                const char* name = selected >= 0 ? palette[selected].name : "Factory paint";
                panel.text(112, 13, 13, telemetry::muted, name);
                panel.right(panel_w - 16, 13, 12, telemetry::cyan, "P TO OPEN");
            }
            else
            {
                panel.card(0, 0, panel_w, panel_h, "");
                panel.text(16, 10, 20, telemetry::ink, "PAINT");
                panel.right(panel_w - 16, 16, 12, telemetry::cyan, "P TO HIDE");
                panel.line(16, title_h - 6, panel_w - 16, title_h - 6, panel_rule);

                // one row per colour family, chips in palette order
                int hovered_index = -1;
                for (int f = 0; f < family_count; f++)
                {
                    const float y = rows_y + row_h * f;
                    panel.text(16, y + 13, 12, telemetry::muted, family_name(static_cast<family>(f)));

                    int column = 0;
                    for (int i = 0; i < palette_count; i++)
                    {
                        if (static_cast<int>(palette[i].group) != f)
                        {
                            continue;
                        }

                        const float x       = chip_x + chip_step * column;
                        const ImVec2 min    = panel.point(x, y + (row_h - chip_d) * 0.5f);
                        const ImVec2 center = ImVec2(min.x + chip_d * 0.5f * scale, min.y + chip_d * 0.5f * scale);
                        ImGui::PushID(i);
                        ImGui::SetCursorScreenPos(min);
                        if (ImGui::InvisibleButton("##paint", ImVec2(chip_d * scale, chip_d * scale)))
                        {
                            car->SetPaint(i);
                        }
                        const bool hovered = ImGui::IsItemHovered();
                        if (hovered)
                        {
                            hovered_index = i;
                        }
                        ImGui::PopID();

                        draw_chip(dl, center, chip_d * 0.5f * scale, palette[i], i == selected, hovered);
                        column++;
                    }
                }

                // the hovered paint previews its name, otherwise the applied one, a finish other than the original names itself
                const int shown = hovered_index >= 0 ? hovered_index : selected;
                panel.line(16, info_y, panel_w - 16, info_y, panel_rule);
                if (shown >= 0)
                {
                    const paint& p           = palette[shown];
                    const char* finish_label = selected_finish > 0 ? finishes[selected_finish].name : p.finish;
                    panel.rect(16, info_y + 12, 6, 42, swatch(p.r, p.g, p.b), 3);
                    panel.text(32, info_y + 10, 20, telemetry::ink, p.name);
                    if (p.code[0] != '\0')
                    {
                        panel.label(32, info_y + 36, 13, telemetry::amber, "%s  /  %s", p.code, finish_label);
                    }
                    else
                    {
                        panel.text(32, info_y + 36, 13, telemetry::amber, finish_label);
                    }
                    panel.right(panel_w - 16, info_y + 39, 11, telemetry::muted, hovered_index >= 0 && hovered_index != selected ? "CLICK TO APPLY" : "APPLIED");
                }
                else
                {
                    panel.text(32, info_y + 10, 20, telemetry::ink, "Factory paint");
                    panel.text(32, info_y + 36, 13, telemetry::muted, selected_finish > 0 ? finishes[selected_finish].name : "Pick a colour to repaint the car");
                }

                // any colour takes any finish
                panel.line(16, finish_y, panel_w - 16, finish_y, panel_rule);
                panel.text(16, finish_y + 9, 13, telemetry::muted, "FINISH");
                panel.right(panel_w - 16, finish_y + 10, 11, telemetry::muted, "ANY COLOUR, ANY FINISH");
                {
                    const float b_w = (panel_w - 32 - 6.0f * (finish_columns - 1)) / finish_columns;
                    for (int i = 0; i < finish_count; i++)
                    {
                        const int column = i % finish_columns;
                        const int row    = i / finish_columns;
                        ImGui::PushID(i);
                        if (hud_button(panel, "##finish", 16 + (b_w + 6) * column, finish_y + finish_title + finish_step * row, b_w, 26, finishes[i].name, i == selected_finish, true, 11.0f))
                        {
                            car->SetPaintFinish(i);
                        }
                        hud_tooltip(finishes[i].description);
                        ImGui::PopID();
                    }
                }

                // live finish controls on the painted panels, picking a colour or a finish resets them
                panel.line(16, lab_y, panel_w - 16, lab_y, panel_rule);
                panel.text(16, lab_y + 10, 13, telemetry::muted, "FINE TUNING");
                panel.right(panel_w - 16, lab_y + 11, 11, telemetry::muted, "SWATCHES ARE APPROXIMATE");
                if (material)
                {
                    struct lab_slider
                    {
                        const char* label;
                        const char* tooltip;
                        MaterialProperty property;
                        float min;
                        float max;
                        const char* format;
                    };
                    const lab_slider sliders[lab_rows] =
                    {
                        { "Flakes",         "Aluminium platelets under the coat that sparkle as the view moves.",                    MaterialProperty::FlakeStrength,        0.0f,   1.0f,    "%.2f"  },
                        { "Pearl",          "Mica pearl that shifts the colour toward the pearl tint at glancing angles.",          MaterialProperty::PearlStrength,        0.0f,   1.0f,    "%.2f"  },
                        { "Coat roughness", "Roughness of the clear coat, low is a wet mirror, high is satin.",                     MaterialProperty::Clearcoat_Roughness,  0.0f,   0.6f,    "%.3f"  },
                        { "Iridescence",    "Thin film interference: the colour shifts through hues with the viewing angle.",       MaterialProperty::Iridescence,          0.0f,   1.0f,    "%.2f"  },
                        { "Film thickness", "Thickness of the interference film, it picks which hues the paint travels through.", MaterialProperty::IridescenceThickness, 100.0f, 1200.0f, "%.0f nm" },
                    };

                    ImGui::PushFont(nullptr, 13 * scale);
                    for (int s = 0; s < lab_rows; s++)
                    {
                        const lab_slider& slider = sliders[s];
                        const float y            = lab_y + lab_title + slider_h * s;
                        const bool disabled      = slider.property == MaterialProperty::IridescenceThickness && material->GetProperty(MaterialProperty::Iridescence) <= 0.0f;
                        panel.text(16, y + 6, 13, disabled ? IM_COL32(151, 170, 186, 110) : telemetry::ink, slider.label);

                        ImGui::PushID(s);
                        ImGui::BeginDisabled(disabled);
                        ImGui::SetCursorScreenPos(panel.point(136, y + 1));
                        ImGui::SetNextItemWidth((panel_w - 136 - 16) * scale);
                        float value = material->GetProperty(slider.property);
                        if (ImGui::SliderFloat("##lab", &value, slider.min, slider.max, slider.format, ImGuiSliderFlags_AlwaysClamp))
                        {
                            car->SetPaintProperty(slider.property, value);
                        }
                        hud_tooltip(slider.tooltip);
                        ImGui::EndDisabled();
                        ImGui::PopID();
                    }
                    ImGui::PopFont();
                }
            }
        }
        ImGui::End();

        // the stage controls sit in the opposite corner so the car stays framed between the two
        if (!collapsed)
        {
            draw_showroom_controls(car, region_size.x / scale - margin - 300.0f, panel_y, scale, region_pos);
        }

        ImGui::PopStyleColor(6);
        ImGui::PopStyleVar(6);
    }

    int draw_service_menu(const service_menu& menu)
    {
        ImVec2 region_pos;
        ImVec2 region_size;
        float scale = 1.0f;
        if (!hud_region(region_pos, region_size, scale) || menu.row_count == 0)
        {
            return -1;
        }

        // centred above the cockpit overlay so the car stays in view behind it
        const float width    = region_size.x / scale;
        const float height   = region_size.y / scale;
        const float panel_w  = 440;
        const float title_h  = 44;
        const float row_h    = 34;
        const float footer_h = 46;
        const float panel_h  = title_h + row_h * menu.row_count + footer_h;
        const float panel_x  = (width - panel_w) * 0.5f;
        const float panel_y  = height * 0.52f;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(83 / 255.0f, 216 / 255.0f, 229 / 255.0f, 0.22f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(83 / 255.0f, 216 / 255.0f, 229 / 255.0f, 0.32f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(83 / 255.0f, 216 / 255.0f, 229 / 255.0f, 0.45f));
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse;

        int chosen = -1;
        ImGui::SetNextWindowPos(ImVec2(region_pos.x + panel_x * scale, region_pos.y + panel_y * scale), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(panel_w * scale, panel_h * scale), ImGuiCond_Always);
        if (ImGui::Begin("##car_service_menu", nullptr, flags))
        {
            const telemetry::painter panel{ImGui::GetWindowDrawList(), ImGui::GetWindowPos(), scale, IM_COL32(14, 22, 31, 215)};
            panel.card(0, 0, panel_w, panel_h, "");
            panel.text(16, 10, 20, telemetry::ink, menu.shop);
            panel.right(panel_w - 16, 15, 12, telemetry::cyan, "SERVICES");
            panel.line(16, title_h - 4, panel_w - 16, title_h - 4, IM_COL32(53, 73, 90, 180));

            ImGui::PushFont(nullptr, 16 * scale);
            for (uint32_t i = 0; i < menu.row_count; ++i)
            {
                const service_row& row = menu.rows[i];
                const float y          = title_h + row_h * i;
                ImGui::PushID(static_cast<int>(i));
                ImGui::SetCursorScreenPos(panel.point(8, y + 2));
                ImGui::BeginDisabled(!row.enabled);
                if (ImGui::Selectable("##service", false, ImGuiSelectableFlags_None, ImVec2((panel_w - 16) * scale, (row_h - 4) * scale)))
                {
                    chosen = static_cast<int>(i);
                }
                ImGui::EndDisabled();
                panel.text(18, y + 8, 16, row.enabled ? telemetry::ink : telemetry::muted, row.name);
                panel.right(panel_w - 18, y + 10, 13, telemetry::amber, row.detail);
                ImGui::PopID();
            }
            ImGui::PopFont();

            const float footer_y = title_h + row_h * menu.row_count + 6;
            if (menu.progress >= 0.0f)
            {
                panel.bar(16, footer_y + 4, panel_w - 32, 8, menu.progress, telemetry::cyan);
                panel.text(16, footer_y + 18, 12, telemetry::cyan, menu.status);
            }
            else
            {
                panel.text(16, footer_y + 10, 13, telemetry::green, menu.status);
            }
        }
        ImGui::End();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(3);

        return chosen;
    }
    void draw_simulated_systems(Physics* physics)
    {
        if (!physics) return;
        auto* simulation = CarPhysics::Get(*physics).GetVehicleSimulation();
        if (!simulation) return;
        auto& s = *simulation;
        const auto& p = s.get_spec();
        const auto& rotations = s.get_visual_rotations();
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 20, viewport->WorkPos.y + 80), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(380, 600), ImGuiCond_FirstUseEver);
        const std::string title = "Simulated systems###car_systems_" + std::to_string(physics->GetEntity()->GetObjectId());
        if (ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::TextWrapped("Physics bodies use solved poses. Powertrain mounts and housing sizes follow this car's packaging preset; motion follows simulation time.");
            ImGui::TextWrapped(!p.packaging.authored ? "Mounts: generic fallback (no packaging preset)." : p.packaging.estimated ? "Mounts: per-car engineering estimates, not factory measurements." : "Mounts: authored measured packaging.");
            ImGui::TextDisabled("Cylinder and valve internals are kinematic illustrations.");
            if (CarPhysics::Get(*physics).GetVehicleSimMode() == VehicleSimMode::Cheap)
                ImGui::TextWrapped("Cheap simulation: select Full simulation to inspect the complete mechanical assembly.");
            if (ImGui::CollapsingHeader("Engine and combustion", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Text("%d cylinders / %d banks / %.2f litres", p.engine_sound_cylinders, p.engine_sound_banks, p.engine_displacement_l);
                ImGui::Text("%s   %.0f RPM   %.1f Nm", s.get_engine_running() ? "RUNNING" : "STOPPED", s.get_current_engine_rpm(), s.get_engine_output_torque());
                ImGui::Text("Bore / stroke: %.1f / %.1f mm", p.engine_bore_mm, p.engine_stroke_mm);
                ImGui::Text("Crank: %.1f deg / 720", rotations.combustion_cycle * 180.0f / pi);
                ImGui::Text("Starter: %s   Intake: %s", s.get_starter_engaged() ? "engaged" : "off", s.get_intake_flooded() ? "FLOODED" : "clear");
                ImGui::Text("Limiter: %s", s.get_rev_limiter_active() ? "FUEL CUT" : "off");
            }
            if (ImGui::CollapsingHeader("Clutch, gearbox and driveshaft", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const int gear = std::clamp(s.get_current_gear(), 0, p.gear_count - 1);
                const float input_rpm = s.get_gearbox_input_angular_velocity() * 60.0f / (2*pi);
                ImGui::Text("Gear: %s   Ratio: %.3f   Final: %.3f", s.get_gear_string(), p.gear_ratios[gear], p.final_drive);
                ImGui::Text("Clutch: %.0f%%   Slip: %.0f RPM", s.get_clutch()*100, s.get_current_engine_rpm()-input_rpm);
                ImGui::Text("Gearbox input: %.0f RPM   %s", input_rpm, s.get_is_shifting() ? "SHIFTING" : "engaged");
                ImGui::Text("Shaft: %.1f Nm   Twist: %.4f rad", s.get_driveshaft_torque(), s.get_driveshaft_twist());
                ImGui::Text("Differential: %s   %s", s.get_diff_type_name(), p.drivetrain_type == 0 ? "RWD" : p.drivetrain_type == 1 ? "FWD" : "AWD");
                if (p.drivetrain_type == 2) ImGui::Text("Front torque split: %.0f%%", p.torque_split_front*100);
                ImGui::TextDisabled("Selected gear pair is highlighted in orange.");
            }
            if (ImGui::CollapsingHeader("Electric motor and energy storage", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (!p.electric_enabled) ImGui::TextDisabled("Not enabled on this car.");
                else
                {
                    const auto& battery = s.get_hybrid_state();
                    const float soc = battery.energy_j / std::max(p.battery_capacity_kwh * 3600000.0f, 1.0f);
                    ImGui::Text("Motor: %.1f Nm   %.1f kW", s.get_motor_torque(), s.get_motor_power_kw());
                    ImGui::Text("%s   %d pole pairs", battery.electrical_power_w < 0 ? "REGENERATING" : battery.electrical_power_w > 0 ? "MOTORING" : "IDLE", p.electric_motor_pole_pairs);
                    ImGui::ProgressBar(std::clamp(soc,0.0f,1.0f), ImVec2(-1,0), "Battery state of charge");
                    ImGui::Text("%.3f / %.3f kWh   %.1f C", battery.energy_j/3600000.0f, p.battery_capacity_kwh, battery.temperature);
                    ImGui::Text("Electrical: %.1f kW   Loss: %.0f W", battery.electrical_power_w/1000, battery.loss_power_w);
                    ImGui::Text("Thermal derating: %.0f%%", (1-std::clamp((p.battery_cutoff_temp-battery.temperature)/std::max(p.battery_cutoff_temp-p.battery_derate_temp,1.0f),0.0f,1.0f))*100);
                }
            }
            if (ImGui::CollapsingHeader("Boost and driver assists"))
            {
                ImGui::Text("Boost: %s   %.2f / %.2f bar", s.get_turbo_enabled() ? "enabled" : "not fitted", s.get_boost_pressure(), p.boost_max_pressure);
                ImGui::Text("ABS: %s   TC: %s", s.is_abs_active_any() ? "ACTIVE" : "idle", s.is_tc_active() ? "ACTIVE" : "idle");
                ImGui::Text("Yaw control: %s", p.yaw_control_enabled ? "enabled" : "disabled");
                ImGui::Text("Throttle %.0f%%   Brake %.0f%%   Steering %.2f", s.get_throttle()*100, s.get_brake()*100, s.get_steering());
            }
            if (ImGui::CollapsingHeader("Tires, suspension and brake thermals"))
            {
                for (int i = 0; i < 4; ++i)
                {
                    const auto& w = s.get_wheel_state(i);
                    ImGui::Text("%s: %.0f N   %.2f bar   radius %.3f m", car::wheel_names[i], w.tire_load, w.pressure_bar, w.effective_radius);
                    ImGui::Text("  Tire %.0f C / core %.0f C / wear %.1f%%", w.thermal.avg_surface(), w.thermal.core, w.wear*100);
                    ImGui::Text("  Brake %.0f C / %.0f%%   Slip %.3f", s.get_wheel_brake_temp(i), s.get_wheel_brake_efficiency(i)*100, w.slip_ratio);
                    ImGui::Text("  Spring %.0f N / drive %.0f Nm", s.get_wheel_suspension_force(i), w.drive_torque);
                }
            }
            if (ImGui::CollapsingHeader("Aerodynamics and ground effect"))
            {
                const auto& a = s.get_aero_debug();
                ImGui::Text("Drag %.0f N   Side %.0f N", a.drag_force.magnitude(), a.side_force.magnitude());
                ImGui::Text("Downforce front %.0f / rear %.0f N", a.front_downforce.magnitude(), a.rear_downforce.magnitude());
                ImGui::Text("Ride height %.3f m   Ground factor %.2f", a.ride_height, a.ground_effect_factor);
                ImGui::Text("DRS: %s", s.get_drs_active() ? "OPEN" : "closed");
            }
        }
        ImGui::End();
    }

}
