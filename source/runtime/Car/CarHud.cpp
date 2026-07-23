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
#include "Car.h"
#include "CarBench.h"
#include "CarState.h"
#include "CarSimulation.h"
#include "CarPresets.h"
#include "CarEngineSoundSynthesis.h"
#include "CarTireSquealSynthesis.h"
#include "../World/Components/Physics.h"
#include "../../editor/ImGui/Source/imgui.h"
#include "../../editor/Widgets/Viewport.h"
//==========================================

namespace spartan::car_hud
{
    namespace
    {
        // visual language shared by every panel in the hud
        constexpr float pi = 3.14159265f;

        const ImU32 panel_bg       = IM_COL32(14, 17, 22, 215);
        const ImU32 panel_shadow   = IM_COL32(0, 0, 0, 120);
        const ImU32 panel_border   = IM_COL32(80, 95, 110, 90);
        const ImU32 panel_inner    = IM_COL32(255, 255, 255, 14);
        const ImU32 accent_info    = IM_COL32(90, 180, 255, 255);
        const ImU32 accent_warn    = IM_COL32(255, 180, 50, 255);
        const ImU32 accent_ok      = IM_COL32(80, 200, 110, 255);
        const ImU32 accent_danger  = IM_COL32(220, 70, 70, 255);
        const ImU32 text_primary   = IM_COL32(232, 234, 238, 255);
        const ImU32 text_dim       = IM_COL32(150, 160, 172, 255);
        const ImU32 text_label     = IM_COL32(120, 130, 142, 255);
        const ImU32 track_dim      = IM_COL32(38, 44, 52, 255);

        // helper: draw the unified panel background with a subtle drop shadow and inner hairline
        void draw_panel_background(ImDrawList* dl, ImVec2 tl, ImVec2 br, float rounding = 8.0f)
        {
            // soft drop shadow under the panel
            dl->AddRectFilled(ImVec2(tl.x + 2, tl.y + 4), ImVec2(br.x + 2, br.y + 4), panel_shadow, rounding);
            // glass body
            dl->AddRectFilled(tl, br, panel_bg, rounding);
            // outer border
            dl->AddRect(tl, br, panel_border, rounding, 1.0f);
            // inner hairline highlight, 1px in
            dl->AddRect(ImVec2(tl.x + 1, tl.y + 1), ImVec2(br.x - 1, br.y - 1), panel_inner, rounding, 1.0f);
        }

        // helper: arrow with a triangular head, used everywhere we visualise a force/direction
        void draw_arrow(ImDrawList* dl, ImVec2 origin, float dx, float dy, ImU32 color, float thickness = 3.0f)
        {
            if (fabsf(dx) < 1.0f && fabsf(dy) < 1.0f)
            {
                return;
            }

            ImVec2 tip(origin.x + dx, origin.y + dy);
            dl->AddLine(origin, tip, color, thickness);

            float len = sqrtf(dx * dx + dy * dy);
            if (len <= 5.0f)
            {
                return;
            }

            float nx = dx / len;
            float ny = dy / len;
            float hs = std::min(len * 0.32f, 10.0f);
            dl->AddTriangleFilled(tip,
                ImVec2(tip.x - hs * (nx + ny * 0.5f), tip.y - hs * (ny - nx * 0.5f)),
                ImVec2(tip.x - hs * (nx - ny * 0.5f), tip.y - hs * (ny + nx * 0.5f)),
                color);
        }

        // build a colour from an rgb triple, used to compose ramps without ImU32 nesting
        ImU32 col_rgb(int r, int g, int b, int a = 255)
        {
            return IM_COL32(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255), std::clamp(a, 0, 255));
        }

        // gt style tire wear colour: blue -> green -> yellow -> red
        ImU32 wear_color(float wear)
        {
            wear = std::clamp(wear, 0.0f, 1.0f);
            int r, g, b;
            if (wear < 0.4f)
            {
                float t = wear / 0.4f;
                r = (int)(30  + t * (50  - 30));
                g = (int)(80  + t * (200 - 80));
                b = (int)(220 + t * (80  - 220));
            }
            else if (wear < 0.7f)
            {
                float t = (wear - 0.4f) / 0.3f;
                r = (int)(50  + t * (220 - 50));
                g = (int)(200 + t * (160 - 200));
                b = (int)(80  + t * (30  - 80));
            }
            else
            {
                float t = (wear - 0.7f) / 0.3f;
                r = (int)(220 + t * (200 - 220));
                g = (int)(160 - t * 130);
                b = (int)(30  - t * 10);
            }
            return col_rgb(r, g, b);
        }

        // colour ramp for tire surface and core temperatures
        ImU32 temp_color(float t)
        {
            if (t > 110.0f)
            {
                return accent_warn;
            }
            if (t < 70.0f)
            {
                return accent_info;
            }
            return accent_ok;
        }

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

        // draws tire condition and force direction while the caller owns labels
        void draw_tire_block(ImDrawList* dl, Physics* physics, WheelIndex wheel, ImVec2 tl, ImVec2 size, bool with_arrows)
        {
            bool  grounded = physics->IsWheelGrounded(wheel);
            float wear     = physics->GetWheelWear(wheel);

            ImU32 wc = wear_color(wear);
            int   wr = (wc >> 0)  & 0xFF;
            int   wg = (wc >> 8)  & 0xFF;
            int   wb = (wc >> 16) & 0xFF;

            const int strips = 8;
            float strip_h    = size.y / strips;
            ImVec2 br(tl.x + size.x, tl.y + size.y);

            for (int s = 0; s < strips; ++s)
            {
                float dist = fabsf(s - (strips - 1) * 0.5f) / ((strips - 1) * 0.5f);
                float brightness = 1.0f - dist * 0.4f;
                int sr = (int)(wr * brightness);
                int sg = (int)(wg * brightness);
                int sb = (int)(wb * brightness);
                if (!grounded)
                {
                    sr /= 2;
                    sg /= 2;
                    sb /= 2;
                }
                ImVec2 stl(tl.x, tl.y + s * strip_h);
                ImVec2 sbr(br.x, tl.y + (s + 1) * strip_h);
                float rounding = (s == 0 || s == strips - 1) ? 7.0f : 0.0f;
                int flags = (s == 0) ? ImDrawFlags_RoundCornersTop : ((s == strips - 1) ? ImDrawFlags_RoundCornersBottom : 0);
                dl->AddRectFilled(stl, sbr, col_rgb(sr, sg, sb), rounding, flags);
            }

            ImU32 border = grounded ? IM_COL32(160, 170, 180, 255) : IM_COL32(120, 70, 70, 255);
            dl->AddRect(tl, br, border, 7.0f, 1.7f);

            char pct[8];
            snprintf(pct, sizeof(pct), "%.0f%%", wear * 100.0f);
            ImVec2 ps = ImGui::CalcTextSize(pct);
            ImVec2 center(tl.x + size.x * 0.5f, tl.y + size.y * 0.5f);
            dl->AddText(ImVec2(center.x - ps.x * 0.5f, center.y - ps.y * 0.5f), IM_COL32(255, 255, 255, 230), pct);

            if (with_arrows)
            {
                float lat   = physics->GetWheelLateralForce(wheel);
                float lon   = physics->GetWheelLongitudinalForce(wheel);
                float scale = 0.003f;
                float max_arrow = std::min(size.x, size.y) * 0.95f;
                float lat_a = std::clamp(lat * scale, -max_arrow, max_arrow);
                float lon_a = std::clamp(-lon * scale, -max_arrow, max_arrow);

                if (fabsf(lat_a) > 2.0f)
                {
                    draw_arrow(dl, center, lat_a, 0.0f, accent_info, 3.0f);
                }
                if (fabsf(lon_a) > 2.0f)
                {
                    draw_arrow(dl, center, 0.0f, lon_a, (lon > 0.0f) ? accent_ok : accent_danger, 3.0f);
                }
            }
        }

        // helper: a thin horizontal level meter used in the engine section
        void draw_level_bar(const char* label, float level, ImU32 color)
        {
            ImGui::Text("  %s", label);
            ImGui::SameLine(100);
            ImVec2 pos = ImGui::GetCursorScreenPos();
            float w = std::max(120.0f, ImGui::GetContentRegionAvail().x - 70.0f);
            float h = 14.0f;
            float fill = std::clamp(level * 5.0f, 0.0f, 1.0f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), track_dim, 3.0f);
            dl->AddRectFilled(pos, ImVec2(pos.x + w * fill, pos.y + h), color, 3.0f);
            dl->AddRect(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(70, 80, 92, 255), 3.0f, 1.0f);
            ImGui::Dummy(ImVec2(w, h));
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.85f, 0.87f, 0.9f, 1.0f), "%.3f", level);
        }

        // legend item: filled rounded square + text
        void draw_legend_item(ImU32 color, const char* text)
        {
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(pos, ImVec2(pos.x + 12, pos.y + 12), color, 3.0f);
            ImGui::Dummy(ImVec2(16, 12));
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.78f, 0.82f, 0.88f, 1.0f), "%s", text);
        }

        // convert a 0..1 wear to a tinted ImVec4 for ImGui::TextColored
        ImVec4 imvec4_from_u32(ImU32 c)
        {
            float r = ((c >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
            float g = ((c >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
            float b = ((c >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;
            float a = ((c >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
            return ImVec4(r, g, b, a);
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

    // telemetry sections share one scrollable window

    namespace
    {
        void section_header(const char* title)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            float width = ImGui::GetContentRegionAvail().x;
            dl->AddRectFilled(pos, ImVec2(pos.x + 3.0f, pos.y + 20.0f), accent_info, 2.0f);
            dl->AddText(ImVec2(pos.x + 11.0f, pos.y + 2.0f), text_primary, title);
            dl->AddLine(ImVec2(pos.x + 11.0f, pos.y + 20.0f), ImVec2(pos.x + width, pos.y + 20.0f), panel_border, 1.0f);
            ImGui::Dummy(ImVec2(width, 27.0f));
        }

        void draw_telemetry_summary(Physics* physics)
        {
            car::Simulation* simulation = physics->GetVehicleSimulation();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 tl = ImGui::GetCursorScreenPos();
            float width = ImGui::GetContentRegionAvail().x;
            const float height = 58.0f;
            ImVec2 br(tl.x + width, tl.y + height);
            draw_panel_background(dl, tl, br, 7.0f);

            const float speed = physics->GetLinearVelocity().Length() * 3.6f;
            const float rpm = physics->GetEngineRPM();
            const float redline = std::max(physics->GetRedlineRPM(), 1.0f);
            const float boost = physics->GetBoostPressure();
            const char* gear = physics->GetCurrentGearString();
            const float left_width = std::clamp(width * 0.30f, 250.0f, 390.0f);

            dl->AddText(ImVec2(tl.x + 16.0f, tl.y + 10.0f), text_label, "ACTIVE CAR");
            dl->AddText(ImVec2(tl.x + 16.0f, tl.y + 29.0f), text_primary, simulation->get_spec().name);

            auto metric = [&](float x, const char* label, const char* value, ImU32 color)
            {
                dl->AddText(ImVec2(x, tl.y + 9.0f), text_label, label);
                dl->AddText(ImVec2(x, tl.y + 28.0f), color, value);
            };

            char speed_text[24];
            char rpm_text[24];
            char boost_text[24];
            snprintf(speed_text, sizeof(speed_text), "%.0f km/h", speed);
            snprintf(rpm_text, sizeof(rpm_text), "%.0f rpm", rpm);
            snprintf(boost_text, sizeof(boost_text), "%.2f bar", boost);
            const float metric_width = (width - left_width - 24.0f) * 0.25f;
            metric(tl.x + left_width, "SPEED", speed_text, text_primary);
            metric(tl.x + left_width + metric_width, "GEAR", gear, physics->IsShifting() ? accent_warn : text_primary);
            metric(tl.x + left_width + metric_width * 2.0f, "ENGINE", rpm_text, rpm >= redline ? accent_danger : accent_warn);
            metric(tl.x + left_width + metric_width * 3.0f, "BOOST", boost_text, accent_info);

            ImGui::Dummy(ImVec2(width, height + 6.0f));
        }

        void section_setup(Car* car_instance, Physics* physics)
        {
            section_header("Setup");
            car::Simulation* simulation = physics->GetVehicleSimulation();
            car::car_preset& spec = simulation->get_spec();
            car::active_upgrades& upgrades = simulation->get_upgrades();
            const car::car_preset& base_spec = simulation->get_base_spec();

            int visualization_preset = static_cast<int>(car_instance->GetVisualizationPreset());
            const char* visualization_presets[] = { "Full car", "Skeleton" };
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::Combo("View preset", &visualization_preset, visualization_presets, IM_ARRAYSIZE(visualization_presets)))
            {
                car_instance->SetVisualizationPreset(static_cast<CarVisualizationPreset>(visualization_preset));
            }
            if (visualization_preset == static_cast<int>(CarVisualizationPreset::Skeleton))
            {
                ImGui::TextColored(imvec4_from_u32(text_dim), "blue frame driveline and wheels  purple collision hull and sweep misses  silver links and joints  orange suspension longitudinal force and shifting power unit  green steering contact load and effective tire radius  heat colored tire zones  red brakes torque and bump stops  pink lateral force  cyan aero and rolling resistance");
            }

            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::BeginCombo("Car", spec.name))
            {
                for (size_t i = 0; i < car::preset_registry.size(); ++i)
                {
                    bool selected = car::preset_registry[i].definition == car_instance->GetDefinition();
                    if (ImGui::Selectable(car::preset_registry[i].name, selected))
                    {
                        car_instance->LoadDefinition(car::preset_registry[i].definition);
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset stock"))
            {
                simulation->reset_upgrades();
            }
            hud_tooltip("removes all upgrades, restores base preset");
            ImGui::SameLine();
            if (ImGui::SmallButton("Bench"))
            {
                car_bench::open_window();
            }
            hud_tooltip("opens car bench, fast scripted stress of the current car");

            bool abs_enabled  = physics->GetAbsEnabled();
            bool tc_enabled   = physics->GetTcEnabled();
            bool manual_trans = physics->GetManualTransmission();
            bool drs_enabled  = physics->GetDrsEnabled();
            bool turbo_on     = physics->GetTurboEnabled();
            int  diff_type    = physics->GetDiffType();
            const char* diff_items[] = { "Open", "Locked", "LSD" };

            if (ImGui::BeginTable("##setup_assists", 6, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoBordersInBody))
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Checkbox("ABS", &abs_enabled))
                {
                    physics->SetAbsEnabled(abs_enabled);
                }
                hud_tooltip("Anti-lock braking: prevents wheel lockup under hard braking.");
                ImGui::TableNextColumn();
                if (ImGui::Checkbox("TCS", &tc_enabled))
                {
                    physics->SetTcEnabled(tc_enabled);
                }
                hud_tooltip("Traction control: cuts throttle when driven wheels spin faster than the vehicle.");
                ImGui::TableNextColumn();
                if (ImGui::Checkbox("Manual", &manual_trans))
                {
                    physics->SetManualTransmission(manual_trans);
                }
                hud_tooltip("Manual transmission: disables auto shifts. Use PgUp/PgDn or L1/R1.");
                ImGui::TableNextColumn();
                if (ImGui::Checkbox("Turbo", &turbo_on))
                {
                    physics->SetTurboEnabled(turbo_on);
                }
                hud_tooltip("Forced induction. Boost spools with RPM and load.");
                ImGui::TableNextColumn();
                if (ImGui::Checkbox("DRS", &drs_enabled))
                {
                    physics->SetDrsEnabled(drs_enabled);
                }
                hud_tooltip("Drag reduction system: opens the rear wing on straights.");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::Combo("##diff", &diff_type, diff_items, 3))
                {
                    physics->SetDiffType(diff_type);
                }
                hud_tooltip("Differential type: Open splits torque freely, Locked equalises wheel speed, LSD biases under load.");
                ImGui::EndTable();
            }
            if (ImGui::BeginTable("##assist_levels", 3, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoBordersInBody))
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::SliderFloat("Steering assist", &spec.assists.steering_speed_reduction, 0.0f, 0.9f, "%.2f");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::SliderFloat("ABS level", &spec.assists.abs_level, 0.0f, 1.0f, "%.2f");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::SliderFloat("TCS level", &spec.assists.traction_control_level, 0.0f, 1.0f, "%.2f");
                ImGui::EndTable();
            }

            auto draw_stages = [&](const char* name, int& level, int maxs)
            {
                int before = level;
                simulation->clamp_upgrade_stage(level, std::max(maxs, 0));
                if (level != before)
                {
                    simulation->reapply_upgrades();
                }
                if (maxs <= 0)
                {
                    return;
                }
                ImGui::PushID(name);
                ImGui::TextColored(imvec4_from_u32(text_label), "%s", name);
                ImGui::SameLine();
                for (int s = 0; s <= maxs; s++)
                {
                    if (s > 0)
                    {
                        ImGui::SameLine(0.0f, 4.0f);
                    }
                    ImGui::PushID(s);
                    char lbl[2] = { (char)('0' + s), 0 };
                    bool pushed = false;
                    if (level == s)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.45f, 0.72f, 1.0f));
                        pushed = true;
                    }
                    if (ImGui::SmallButton(lbl))
                    {
                        level = s;
                        simulation->reapply_upgrades();
                    }
                    if (pushed)
                    {
                        ImGui::PopStyleColor();
                    }
                    ImGui::PopID();
                }
                ImGui::PopID();
                ImGui::SameLine(0.0f, 16.0f);
            };

            draw_stages("Eng", upgrades.engine, base_spec.engine_stage_max);
            draw_stages("Sus", upgrades.suspension, base_spec.suspension_stage_max);
            draw_stages("Tir", upgrades.tires, base_spec.tires_stage_max);
            draw_stages("Brk", upgrades.brakes, base_spec.brakes_stage_max);
            draw_stages("Aer", upgrades.aero, base_spec.aero_stage_max);
            draw_stages("Wgt", upgrades.weight, base_spec.weight_stage_max);
            if (base_spec.engine_peak_torque > 0.0f)
            {
                ImGui::TextColored(imvec4_from_u32(text_dim), "tq %.0f/%.0f",
                    spec.engine_peak_torque, base_spec.engine_peak_torque);
            }
            ImGui::NewLine();
        }

        void section_chassis(Physics* physics)
        {
            section_header("Chassis");
            car::Simulation* simulation = physics->GetVehicleSimulation();
            const char* labels[4] = { "FL", "FR", "RL", "RR" };
            const WheelIndex idx[4] = { WheelIndex::FrontLeft, WheelIndex::FrontRight, WheelIndex::RearLeft, WheelIndex::RearRight };
            static constexpr int hist_n = 120;
            static float history[4][hist_n] = {};
            static int hist_pos = 0;
            for (int i = 0; i < 4; ++i)
            {
                history[i][hist_pos] = physics->GetWheelCompression(idx[i]);
            }
            hist_pos = (hist_pos + 1) % hist_n;

            if (ImGui::BeginTable("##corner_cards", 4, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoPadOuterX))
            {
                ImGui::TableNextRow();
                for (int i = 0; i < 4; i++)
                {
                    ImGui::TableSetColumnIndex(i);
                    ImGui::PushID(i);
                    const float compression = physics->GetWheelCompression(idx[i]);
                    const ImU32 compression_color = compression > 0.8f ? accent_danger : (compression > 0.5f ? accent_warn : accent_ok);
                    ImGui::TextColored(imvec4_from_u32(text_primary), "%s", labels[i]);
                    ImGui::SameLine();
                    ImGui::TextColored(imvec4_from_u32(compression_color), "  suspension %.0f%%", compression * 100.0f);

                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImVec2 tire_pos = ImGui::GetCursorScreenPos();
                    float cell_width = ImGui::GetContentRegionAvail().x;
                    const ImVec2 tire_size(42.0f, 68.0f);
                    draw_tire_block(dl, physics, idx[i], ImVec2(tire_pos.x + 5.0f, tire_pos.y + 2.0f), tire_size, true);

                    const float slip_angle = physics->GetWheelSlipAngle(idx[i]) * 57.2958f;
                    const float slip_ratio = physics->GetWheelSlipRatio(idx[i]) * 100.0f;
                    const float wear = physics->GetWheelWear(idx[i]) * 100.0f;
                    const float core = physics->GetWheelCoreTemp(idx[i]);
                    const float brake = physics->GetWheelBrakeTemp(idx[i]);
                    const float grip = physics->GetWheelTempGripFactor(idx[i]) * 100.0f;
                    const float text_x = tire_pos.x + 58.0f;
                    dl->AddText(ImVec2(text_x, tire_pos.y + 2.0f), text_label, "SLIP");
                    char value[48];
                    snprintf(value, sizeof(value), "%+.1f deg  %+.0f%%", slip_angle, slip_ratio);
                    dl->AddText(ImVec2(text_x, tire_pos.y + 18.0f), accent_warn, value);
                    dl->AddText(ImVec2(text_x, tire_pos.y + 38.0f), text_label, "TIRE");
                    snprintf(value, sizeof(value), "%.0f C  %.0f%% grip", core, grip);
                    dl->AddText(ImVec2(text_x, tire_pos.y + 54.0f), temp_color(core), value);
                    ImGui::Dummy(ImVec2(cell_width, tire_size.y + 7.0f));

                    const float surface_in = physics->GetWheelSurfaceTemp(idx[i], 0);
                    const float surface_mid = physics->GetWheelSurfaceTemp(idx[i], 1);
                    const float surface_out = physics->GetWheelSurfaceTemp(idx[i], 2);
                    ImGui::TextColored(imvec4_from_u32(text_label), "surface");
                    ImGui::SameLine();
                    ImGui::TextColored(imvec4_from_u32(temp_color(surface_mid)), "%.0f / %.0f / %.0f C", surface_in, surface_mid, surface_out);
                    ImGui::TextColored(imvec4_from_u32(text_label), "wear");
                    ImGui::SameLine();
                    ImGui::TextColored(imvec4_from_u32(wear_color(wear * 0.01f)), "%.0f%%", wear);
                    ImGui::SameLine(0.0f, 12.0f);
                    ImGui::TextColored(imvec4_from_u32(text_label), "brake");
                    ImGui::SameLine();
                    ImGui::TextColored(imvec4_from_u32(brake > 700.0f ? accent_danger : (brake > 400.0f ? accent_warn : text_primary)), "%.0f C", brake);
                    ImGui::TextColored(imvec4_from_u32(text_dim), "camber %+.2f  toe %+.2f  bump %+.2f", simulation->get_wheel_dynamic_camber(i) * 180.0f / pi, simulation->get_wheel_dynamic_toe(i) * 180.0f / pi, simulation->get_wheel_bump_steer(i) * 180.0f / pi);

                    ImVec2 graph_tl = ImGui::GetCursorScreenPos();
                    const float graph_width = std::max(80.0f, ImGui::GetContentRegionAvail().x - 4.0f);
                    const float graph_height = 34.0f;
                    ImVec2 graph_br(graph_tl.x + graph_width, graph_tl.y + graph_height);
                    dl->AddRectFilled(graph_tl, graph_br, IM_COL32(18, 22, 28, 230), 4.0f);
                    ImVec2 previous;
                    for (int sample = 0; sample < hist_n; sample++)
                    {
                        const int history_index = (hist_pos + sample) % hist_n;
                        const float value_y = std::clamp(history[i][history_index], 0.0f, 1.0f);
                        const ImVec2 point(graph_tl.x + static_cast<float>(sample) / static_cast<float>(hist_n - 1) * graph_width, graph_br.y - value_y * (graph_height - 4.0f) - 2.0f);
                        if (sample > 0)
                        {
                            dl->AddLine(previous, point, compression_color, 1.4f);
                        }
                        previous = point;
                    }
                    dl->AddRect(graph_tl, graph_br, panel_border, 4.0f, 1.0f);
                    ImGui::Dummy(ImVec2(graph_width, graph_height + 3.0f));
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            float psi     = physics->GetTirePressure();
            float psi_opt = physics->GetTirePressureOptimal();
            float dpsi    = psi - psi_opt;
            ImU32 pc = (fabsf(dpsi) < 0.1f) ? accent_ok : (fabsf(dpsi) < 0.3f ? accent_warn : accent_danger);
            if (ImGui::BeginTable("##chassis_summary", 3, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoBordersInBody))
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(imvec4_from_u32(text_label), "ROLL STIFFNESS");
                ImGui::TextColored(imvec4_from_u32(text_primary), "F %.0f  R %.0f Nm/rad", simulation->get_axle_roll_stiffness(true), simulation->get_axle_roll_stiffness(false));
                ImGui::TableNextColumn();
                ImGui::TextColored(imvec4_from_u32(text_label), "TIRE PRESSURE");
                ImGui::TextColored(imvec4_from_u32(pc), "%.2f bar  %+.2f", psi, dpsi);
                ImGui::TableNextColumn();
                ImGui::TextColored(imvec4_from_u32(text_label), "RIDE HEIGHT");
                if (simulation->get_aero_debug().valid)
                {
                    ImGui::TextColored(imvec4_from_u32(text_primary), "%.2f m", simulation->get_aero_debug().ride_height);
                }
                else
                {
                    ImGui::TextColored(imvec4_from_u32(text_dim), "n/a");
                }
                ImGui::EndTable();
            }
        }

        void section_aero(Physics* physics)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            section_header("Aerodynamics");
            car::Simulation* simulation = physics->GetVehicleSimulation();

            math::Vector3 velocity = physics->GetLinearVelocity();
            float speed_kmh        = velocity.Length() * 3.6f;
            float aero_speed_ms    = speed_kmh / 3.6f;

            const car::aero_debug_data& aero = simulation->get_aero_debug();
            float frontal_area = simulation->get_frontal_area();
            float side_area    = simulation->get_side_area();
            float drag_coeff   = simulation->get_drag_coeff();
            const car::shape_2d& shape = simulation->get_shape_data();

            const float avail_w = ImGui::GetContentRegionAvail().x;
            const float side_view_w  = std::clamp(avail_w * 0.58f, 200.0f, 320.0f);
            const float front_view_w = std::clamp(avail_w * 0.38f, 140.0f, 220.0f);
            const float view_h       = 120.0f;

            float shape_length = shape.max_z - shape.min_z;
            float shape_width  = shape.max_x - shape.min_x;
            float max_h        = std::max(shape_length, shape_width);
            float ppm          = max_h > 0.01f ? (side_view_w * 0.90f) / max_h : 50.0f;

            auto draw_profile = [&](const std::vector<std::pair<float, float>>& profile,
                                    float min_axis, float max_axis, float min_y, float max_y,
                                    float draw_x, float draw_y, float draw_w, float draw_h)
            {
                if (profile.size() < 3)
                {
                    return;
                }
                float axis_range = max_axis - min_axis;
                float y_range    = max_y - min_y;
                if (axis_range < 0.01f || y_range < 0.01f)
                {
                    return;
                }
                float scale_x = axis_range * ppm;
                float scale_y = y_range * ppm;
                float off_x   = draw_x + (draw_w - scale_x) * 0.5f;
                float off_y   = draw_y + draw_h * 0.80f;
                std::vector<ImVec2> pts;
                pts.reserve(profile.size());
                for (const auto& pt : profile)
                {
                    float nx = (pt.first - min_axis) / axis_range;
                    float ny = (pt.second - min_y) / y_range;
                    pts.push_back(ImVec2(off_x + nx * scale_x, off_y - ny * scale_y));
                }
                dl->AddConvexPolyFilled(pts.data(), (int)pts.size(), IM_COL32(40, 48, 60, 230));
                dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(80, 130, 180, 230), 2.0f, ImDrawFlags_Closed);
            };

            float drag_n = 0.0f, front_df_n = 0.0f, rear_df_n = 0.0f, side_n = 0.0f;
            if (aero.valid && aero.drag_force.magnitude() > 0.1f)
            {
                drag_n     = aero.drag_force.magnitude();
                front_df_n = aero.front_downforce.magnitude();
                rear_df_n  = aero.rear_downforce.magnitude();
                side_n     = aero.side_force.magnitude();
            }
            else if (aero_speed_ms > 0.5f)
            {
                float dyn_pressure      = 0.5f * car::tuning::air_density * aero_speed_ms * aero_speed_ms;
                drag_n     = dyn_pressure * drag_coeff * frontal_area;
                front_df_n = fabsf(simulation->get_lift_coeff_front() * dyn_pressure * frontal_area);
                rear_df_n  = fabsf(simulation->get_lift_coeff_rear()  * dyn_pressure * frontal_area);
            }
            float total_df = front_df_n + rear_df_n;

            const float fs      = 0.035f;
            const float max_len = 60.0f;
            auto arrow_with_label = [&](ImVec2 from, float dx, float dy, ImU32 color, float force_n)
            {
                draw_arrow(dl, from, dx, dy, color, 3.0f);
                if (sqrtf(dx * dx + dy * dy) < 5.0f)
                {
                    return;
                }
                ImVec2 end(from.x + dx, from.y + dy);
                char buf[16];
                if (force_n >= 1000.0f)
                {
                    snprintf(buf, sizeof(buf), "%.1f kN", force_n / 1000.0f);
                }
                else
                {
                    snprintf(buf, sizeof(buf), "%.0f N", force_n);
                }
                dl->AddText(ImVec2(end.x + (dy != 0 ? 4.0f : -18.0f), end.y + (dx != 0 ? -14.0f : -4.0f)), color, buf);
            };

            if (ImGui::BeginTable("##aero_views", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV))
            {
                ImGui::TableSetupColumn("##side",  ImGuiTableColumnFlags_WidthFixed, side_view_w + 16.0f);
                ImGui::TableSetupColumn("##front", ImGuiTableColumnFlags_WidthFixed, front_view_w + 16.0f);
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::TextColored(imvec4_from_u32(text_label), "Side");
                {
                    ImVec2 base = ImGui::GetCursorScreenPos();
                    ImVec2 tl(base.x, base.y);
                    ImVec2 br(tl.x + side_view_w, tl.y + view_h);
                    draw_panel_background(dl, tl, br, 6.0f);
                    if (shape.valid)
                    {
                        draw_profile(shape.side_profile, shape.min_z, shape.max_z, shape.min_y, shape.max_y, tl.x, tl.y, side_view_w, view_h);
                    }
                    if (drag_n > 10.0f)
                    {
                        arrow_with_label(ImVec2(tl.x + side_view_w * 0.06f, tl.y + view_h * 0.50f), -std::clamp(drag_n * fs, 10.0f, max_len), 0, accent_warn, drag_n);
                    }
                    if (front_df_n > 10.0f)
                    {
                        arrow_with_label(ImVec2(tl.x + side_view_w * 0.22f, tl.y + view_h * 0.10f), 0, std::clamp(front_df_n * fs, 10.0f, max_len), accent_info, front_df_n);
                    }
                    if (rear_df_n > 10.0f)
                    {
                        arrow_with_label(ImVec2(tl.x + side_view_w * 0.80f, tl.y + view_h * 0.10f), 0, std::clamp(rear_df_n * fs, 10.0f, max_len), accent_info, rear_df_n);
                    }
                    ImGui::Dummy(ImVec2(side_view_w, view_h + 4.0f));
                }

                ImGui::TableNextColumn();
                ImGui::TextColored(imvec4_from_u32(text_label), "Front");
                {
                    ImVec2 base = ImGui::GetCursorScreenPos();
                    ImVec2 tl(base.x, base.y);
                    ImVec2 br(tl.x + front_view_w, tl.y + view_h);
                    draw_panel_background(dl, tl, br, 6.0f);
                    if (shape.valid)
                    {
                        draw_profile(shape.front_profile, shape.min_x, shape.max_x, shape.min_y, shape.max_y, tl.x, tl.y, front_view_w, view_h);
                    }
                    if (total_df > 10.0f)
                    {
                        arrow_with_label(ImVec2(tl.x + front_view_w * 0.5f, tl.y + view_h * 0.04f), 0, std::clamp(total_df * fs * 0.5f, 10.0f, max_len), accent_info, total_df);
                    }
                    if (side_n > 50.0f)
                    {
                        float dir = (aero.valid && aero.side_force.x < 0) ? -1.0f : 1.0f;
                        arrow_with_label(ImVec2(tl.x + front_view_w * 0.5f, tl.y + view_h * 0.45f), dir * std::clamp(side_n * fs, 10.0f, max_len), 0, accent_warn, side_n);
                    }
                    ImGui::Dummy(ImVec2(front_view_w, view_h + 4.0f));
                }
                ImGui::EndTable();
            }

            if (ImGui::BeginTable("##aero_numbers", 4, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV))
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(imvec4_from_u32(text_label), "Cd / Af / As");
                ImGui::TextColored(imvec4_from_u32(text_primary), "%.2f / %.2f / %.2f", drag_coeff, frontal_area, side_area);
                ImGui::TableNextColumn();
                ImGui::TextColored(imvec4_from_u32(text_label), "drag");
                ImGui::TextColored(imvec4_from_u32(accent_warn), "%.0f N", drag_n);
                ImGui::TableNextColumn();
                ImGui::TextColored(imvec4_from_u32(text_label), "front df");
                ImGui::TextColored(imvec4_from_u32(accent_info), "%.0f N", front_df_n);
                ImGui::TableNextColumn();
                ImGui::TextColored(imvec4_from_u32(text_label), "rear df");
                ImGui::TextColored(imvec4_from_u32(accent_info), "%.0f N", rear_df_n);
                ImGui::EndTable();
            }

            if (total_df > 1.0f)
            {
                float balance = front_df_n / total_df * 100.0f;
                ImGui::TextColored(imvec4_from_u32(text_primary), "df balance  F %.0f%%  R %.0f%%", balance, 100.0f - balance);
                ImVec2 bar_tl = ImGui::GetCursorScreenPos();
                float bw = std::min(ImGui::GetContentRegionAvail().x - 8.0f, 420.0f);
                float bh = 8.0f;
                dl->AddRectFilled(bar_tl, ImVec2(bar_tl.x + bw, bar_tl.y + bh), track_dim, 3.0f);
                dl->AddRectFilled(bar_tl, ImVec2(bar_tl.x + bw * balance * 0.01f, bar_tl.y + bh), accent_info, 3.0f);
                dl->AddRect(bar_tl, ImVec2(bar_tl.x + bw, bar_tl.y + bh), IM_COL32(70, 80, 92, 255), 3.0f, 1.0f);
                ImGui::Dummy(ImVec2(bw, bh + 2.0f));
                if (aero.valid && aero.ground_effect_factor > 1.01f)
                {
                    ImGui::TextColored(imvec4_from_u32(accent_ok), "ground effect +%.0f%%", (aero.ground_effect_factor - 1.0f) * 100.0f);
                }
            }
        }

        void section_engine(Physics* physics)
        {
            const engine_sound::debug_data& dbg = engine_sound::get_debug();
            car::Simulation* simulation = physics->GetVehicleSimulation();

            float ice_tq = simulation->get_engine_torque_current();
            float mot_tq = simulation->get_motor_torque();
            float rpm    = simulation->get_current_engine_rpm();
            float boost  = simulation->get_boost_pressure();
            float throt  = dbg.throttle;

            section_header("Engine / sound");
            ImGui::TextColored(imvec4_from_u32(accent_warn), "RPM %.0f  |  Throttle %.0f%%  |  Boost %.2f bar", rpm, throt * 100.0f, boost);
            float mot_kw = simulation->get_motor_power_kw();
            ImGui::Text("ICE %.0f Nm  |  Motor %.0f Nm (%.0f kW)  |  Total %.0f Nm", ice_tq, mot_tq, mot_kw, ice_tq + mot_tq);

            ImGui::TextColored(
                imvec4_from_u32(text_dim),
                "Ferrari 412 T2 upstream simulation and exhaust synthesis"
            );

            ImGui::TextColored(imvec4_from_u32(text_label), "Engine-sim exhaust");
            if (ImGui::BeginTable("##eng_layers", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoBordersInBody))
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                draw_level_bar("Exhaust", dbg.exhaust_level, IM_COL32(255, 180, 100, 255));
                ImGui::TableNextColumn();
                draw_level_bar("Gain", dbg.leveler_gain * 0.2f, IM_COL32(220, 130, 255, 255));
                draw_level_bar("Peak", dbg.output_peak, IM_COL32(180, 100, 220, 255));
                ImGui::EndTable();
            }

            {
                ImGui::TextColored(imvec4_from_u32(text_label), "Waveform");
                ImGui::TextColored(
                    imvec4_from_u32(text_dim),
                    "upstream mono output"
                );
                {
                    ImVec2 pos = ImGui::GetCursorScreenPos();
                    float w = std::max(180.0f, ImGui::GetContentRegionAvail().x - 4.0f);
                    float h = 80.0f;
                    float cy = pos.y + h * 0.5f;
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(20, 24, 30, 255), 4.0f);
                    dl->AddLine(ImVec2(pos.x, cy), ImVec2(pos.x + w, cy), IM_COL32(70, 80, 92, 255));

                    auto trace = [&](const float* buf, ImU32 c)
                    {
                        int n = engine_sound::debug_data::waveform_size;
                        int start = dbg.waveform_write_pos;
                        float peak = 1e-6f;
                        for (int i = 0; i < n; ++i)
                        {
                            peak = std::max(peak, fabsf(buf[i]));
                        }
                        float scale = 0.45f / peak;
                        float xs = w / (float)n;
                        for (int i = 0; i < n - 1; ++i)
                        {
                            int i0 = (start + i) % n;
                            int i1 = (start + i + 1) % n;
                            ImVec2 p0(pos.x + i * xs,       cy - buf[i0] * h * scale);
                            ImVec2 p1(pos.x + (i + 1) * xs, cy - buf[i1] * h * scale);
                            dl->AddLine(p0, p1, c, 1.4f);
                        }
                    };
                    trace(dbg.waveform,     IM_COL32(100, 255, 100, 255));
                    dl->AddRect(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(70, 80, 92, 255), 4.0f, 1.0f);
                    ImGui::Dummy(ImVec2(w, h));
                }
            }

            const tire_squeal_sound::debug_data& tire_dbg = tire_squeal_sound::get_debug();
            ImGui::TextColored(imvec4_from_u32(accent_info), "Squeal %.0f%%  speed %.0f%%", tire_dbg.intensity * 100.0f, tire_dbg.speed_norm * 100.0f);
            if (ImGui::BeginTable("##eng_out", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoBordersInBody))
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                draw_level_bar("Tone",    tire_dbg.tone_level,    IM_COL32(255, 160, 80, 255));
                draw_level_bar("Screech", tire_dbg.screech_level, IM_COL32(255, 100, 180, 255));
                draw_level_bar("Body",    tire_dbg.body_level,    IM_COL32(180, 180, 100, 255));
                ImGui::TableNextColumn();
                draw_level_bar("Engine",    dbg.output_level,      IM_COL32(100, 255, 100, 255));
                draw_level_bar("Tire",      tire_dbg.output_level, IM_COL32(180, 100, 255, 255));
                draw_level_bar("Eng peak",  dbg.output_peak,       IM_COL32(255, 255, 100, 255));
                draw_level_bar("Tire peak", tire_dbg.output_peak,  IM_COL32(255, 200, 255, 255));
                ImGui::EndTable();
            }
        }

        void section_audio_tools()
        {
            section_header("Audio tools");

            if (ImGui::CollapsingHeader("Sound tuning"))
            {
                ImGui::TextColored(
                    imvec4_from_u32(text_dim),
                    "Controls map directly to the upstream synthesizer."
                );
                engine_sound::synthesizer& s = engine_sound::get_synthesizer();
                ImGui::SliderFloat("convolution_wet", &s.params.convolution_wet,  0.0f, 1.0f, "%.3f");
                ImGui::SliderFloat("df_f_mix",         &s.params.df_f_mix,         0.0f, 0.1f, "%.3f");
                ImGui::SliderFloat("air_noise",        &s.params.air_noise,        0.0f, 1.0f, "%.3f");
                ImGui::SliderFloat("leveler_target",   &s.params.leveler_target,   0.05f, 1.5f, "%.3f");
                if (ImGui::Button("Reset to defaults"))
                {
                    s.params = engine_sound::runtime_params();
                }
            }

            if (ImGui::CollapsingHeader("WAV dump"))
            {
                ImGui::TextColored(imvec4_from_u32(text_dim), "Captures live synth output to binaries/last_synth.wav.");
                static double last_save_time = -1.0;
                static bool   last_save_ok   = false;
                double now = ImGui::GetTime();
                engine_sound::synthesizer& s         = engine_sound::get_synthesizer();
                const engine_sound::debug_data& dbg  = engine_sound::get_debug();

                if (dbg.dump_ready)
                {
                    last_save_ok   = s.save_dump("last_synth.wav");
                    last_save_time = now;
                }

                if (dbg.dump_total == 0)
                {
                    if (ImGui::Button("Dump 2s WAV"))
                    {
                        s.begin_dump(2.0f);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Dump 5s WAV"))
                    {
                        s.begin_dump(5.0f);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Dump 10s WAV"))
                    {
                        s.begin_dump(10.0f);
                    }
                }
                else
                {
                    float pct = (float)dbg.dump_progress / (float)dbg.dump_total;
                    ImGui::ProgressBar(pct, ImVec2(300, 0), "capturing...");
                }

                if (last_save_time > 0 && now - last_save_time < 6.0)
                {
                    if (last_save_ok)
                    {
                        ImGui::TextColored(imvec4_from_u32(accent_ok),     "saved binaries/last_synth.wav (%.1fs ago)", now - last_save_time);
                    }
                    else
                    {
                        ImGui::TextColored(imvec4_from_u32(accent_danger), "save FAILED (check working dir / permissions)");
                    }
                }
            }
        }
    } // anonymous namespace

    void draw_car_bench_window(Car* car_instance, Physics* physics)
    {
        car_bench::draw_window(car_instance, physics);
    }

    void draw_telemetry_window(Car* car_instance, Physics* physics, bool* p_open)
    {
        if (!car_instance || !physics)
        {
            return;
        }

        ImGuiIO& io = ImGui::GetIO();
        if (io.DisplaySize.x < 200.0f || io.DisplaySize.y < 200.0f)
        {
            return;
        }

        const float win_w = std::clamp(io.DisplaySize.x * 0.68f, 1180.0f, 1560.0f);
        const float win_h = std::clamp(io.DisplaySize.y * 0.88f, 760.0f, 1000.0f);
        ImGui::SetNextWindowSizeConstraints(ImVec2(1180.0f, 700.0f), ImVec2(io.DisplaySize.x, io.DisplaySize.y));
        ImGui::SetNextWindowSize(ImVec2(win_w, win_h), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - win_w - 16.0f, 36.0f), ImGuiCond_FirstUseEver);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 5.0f));

        if (ImGui::Begin("Telemetry", p_open, ImGuiWindowFlags_NoCollapse))
        {
            ImVec2 cur = ImGui::GetWindowSize();
            if (cur.x < 1180.0f)
            {
                ImGui::SetWindowSize(ImVec2(win_w, std::max(cur.y, win_h * 0.85f)));
            }

            draw_telemetry_summary(physics);
            if (ImGui::BeginChild("##telemetry_funnel", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar))
            {
                section_setup(car_instance, physics);
                section_chassis(physics);

                if (ImGui::BeginTable("##aero_engine", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoPadOuterX))
                {
                    ImGui::TableSetupColumn("##aero", ImGuiTableColumnFlags_WidthStretch, 0.42f);
                    ImGui::TableSetupColumn("##eng",  ImGuiTableColumnFlags_WidthStretch, 0.58f);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    section_aero(physics);
                    ImGui::TableNextColumn();
                    section_engine(physics);
                    ImGui::EndTable();
                }

                section_audio_tools();
            }
            ImGui::EndChild();
        }
        ImGui::End();
        ImGui::PopStyleVar(4);
    }
}
