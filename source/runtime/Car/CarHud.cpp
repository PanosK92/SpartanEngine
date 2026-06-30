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

        // dashboard layout constants for telemetry overview tab
        constexpr float arc_start  = pi * 0.75f;
        constexpr float arc_end    = pi * 2.25f;
        constexpr float arc_range  = arc_end - arc_start;

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

        // helper: a sweep arc gauge (used for tach, speedo, boost). drawn entirely with the draw list
        // so it can be sized for both the compact driver hud and the full overview panel
        struct gauge_spec
        {
            float       radius           = 64.0f;
            float       value            = 0.0f;
            float       value_max        = 1.0f;
            const char* label            = "";
            const char* value_text       = nullptr;
            int         tick_count       = 8;
            int         major_every      = 1;
            bool        draw_ticks       = true;
            bool        draw_labels      = true;
            bool        labels_as_int    = true;
            float       label_divider    = 1.0f;  // tick label = (i * value_max / tick_count) / label_divider
            float       redline_fraction = 1.1f;  // > 1 disables the redline band
            ImU32       needle_color     = IM_COL32(255, 80, 80, 255);
            ImU32       value_color      = IM_COL32(255, 255, 255, 255);
        };

        // colour ramp for the sweep arc depending on the gauge type
        ImU32 arc_color_for_speed(float fraction)
        {
            if (fraction < 0.45f)
            {
                return IM_COL32(60, 140, 90, 255);
            }
            if (fraction < 0.75f)
            {
                return IM_COL32(160, 140, 60, 255);
            }
            return IM_COL32(190, 70, 70, 255);
        }
        ImU32 arc_color_for_rpm(float fraction, float redline_fraction)
        {
            if (fraction >= redline_fraction)
            {
                return IM_COL32(220, 70, 70, 255);
            }
            if (fraction < 0.65f)
            {
                return IM_COL32(70, 160, 100, 255);
            }
            return IM_COL32(190, 160, 60, 255);
        }
        ImU32 arc_color_for_boost(float fraction)
        {
            if (fraction < 0.30f)
            {
                return IM_COL32(80, 100, 130, 255);
            }
            if (fraction < 0.60f)
            {
                return IM_COL32(50, 130, 160, 255);
            }
            if (fraction < 0.85f)
            {
                return IM_COL32(60, 170, 100, 255);
            }
            return IM_COL32(210, 80, 60, 255);
        }

        enum class gauge_kind { speed, rpm, boost };

        void draw_gauge(ImDrawList* dl, ImVec2 center, gauge_kind kind, const gauge_spec& s)
        {
            float r = s.radius;
            float fraction = (s.value_max > 0.0f) ? (s.value / s.value_max) : 0.0f;
            fraction = std::clamp(fraction, 0.0f, 1.0f);

            // outer ring + face
            dl->AddCircleFilled(center, r, IM_COL32(20, 24, 30, 255), 64);
            dl->AddCircle(center, r, IM_COL32(60, 70, 82, 255), 64, 1.5f);

            // dim track behind the arc for context
            const int track_segments = 64;
            for (int i = 0; i < track_segments; ++i)
            {
                float a1 = arc_start + (arc_range * i / track_segments);
                float a2 = arc_start + (arc_range * (i + 1) / track_segments);
                ImVec2 p1(center.x + cosf(a1) * (r - 10), center.y + sinf(a1) * (r - 10));
                ImVec2 p2(center.x + cosf(a1) * (r - 4),  center.y + sinf(a1) * (r - 4));
                ImVec2 p3(center.x + cosf(a2) * (r - 4),  center.y + sinf(a2) * (r - 4));
                ImVec2 p4(center.x + cosf(a2) * (r - 10), center.y + sinf(a2) * (r - 10));
                dl->AddQuadFilled(p1, p2, p3, p4, IM_COL32(40, 46, 54, 220));
            }

            // active sweep, coloured by gauge kind
            const int sweep_segments = std::max(8, (int)(track_segments * fraction));
            for (int i = 0; i < sweep_segments; ++i)
            {
                float seg_frac_0 = (float)i       / track_segments;
                float seg_frac_1 = (float)(i + 1) / track_segments;
                if (seg_frac_0 >= fraction)
                {
                    break;
                }
                float a1 = arc_start + arc_range * seg_frac_0;
                float a2 = arc_start + arc_range * std::min(seg_frac_1, fraction);

                ImU32 c = IM_COL32_WHITE;
                if      (kind == gauge_kind::speed)
                {
                    c = arc_color_for_speed(seg_frac_0);
                }
                else if (kind == gauge_kind::rpm)
                {
                    c = arc_color_for_rpm(seg_frac_0, s.redline_fraction);
                }
                else
                {
                    c = arc_color_for_boost(seg_frac_0);
                }

                ImVec2 p1(center.x + cosf(a1) * (r - 10), center.y + sinf(a1) * (r - 10));
                ImVec2 p2(center.x + cosf(a1) * (r - 4),  center.y + sinf(a1) * (r - 4));
                ImVec2 p3(center.x + cosf(a2) * (r - 4),  center.y + sinf(a2) * (r - 4));
                ImVec2 p4(center.x + cosf(a2) * (r - 10), center.y + sinf(a2) * (r - 10));
                dl->AddQuadFilled(p1, p2, p3, p4, c);
            }

            // ticks
            if (s.draw_ticks)
            {
                for (int i = 0; i <= s.tick_count; ++i)
                {
                    float t = (float)i / (float)s.tick_count;
                    float a = arc_start + arc_range * t;
                    bool major = (i % s.major_every == 0);
                    float inner = major ? (r - 18) : (r - 14);
                    float outer = r - 4;
                    bool in_redline = (kind == gauge_kind::rpm) && (t >= s.redline_fraction);
                    ImU32 tc = in_redline ? IM_COL32(255, 100, 100, 255) : (major ? text_primary : text_label);
                    ImVec2 ip(center.x + cosf(a) * inner, center.y + sinf(a) * inner);
                    ImVec2 op(center.x + cosf(a) * outer, center.y + sinf(a) * outer);
                    dl->AddLine(ip, op, tc, major ? 1.6f : 1.0f);

                    if (major && s.draw_labels && r > 50.0f)
                    {
                        char buf[8];
                        if (s.labels_as_int)
                        {
                            snprintf(buf, sizeof(buf), "%d", (int)((s.value_max * t) / s.label_divider));
                        }
                        else
                        {
                            snprintf(buf, sizeof(buf), "%.1f", (s.value_max * t) / s.label_divider);
                        }
                        float lr = r - 28;
                        ImVec2 size = ImGui::CalcTextSize(buf);
                        ImVec2 lp(center.x + cosf(a) * lr - size.x * 0.5f, center.y + sinf(a) * lr - size.y * 0.5f);
                        dl->AddText(lp, in_redline ? IM_COL32(255, 120, 120, 255) : text_dim, buf);
                    }
                }
            }

            // needle
            float needle_angle  = arc_start + fraction * arc_range;
            float needle_length = r - 18;
            ImVec2 tip (center.x + cosf(needle_angle) * needle_length, center.y + sinf(needle_angle) * needle_length);
            ImVec2 bl  (center.x + cosf(needle_angle + 1.57f) * 3.0f,  center.y + sinf(needle_angle + 1.57f) * 3.0f);
            ImVec2 br_ (center.x + cosf(needle_angle - 1.57f) * 3.0f,  center.y + sinf(needle_angle - 1.57f) * 3.0f);
            ImVec2 back(center.x + cosf(needle_angle + pi) * 10.0f,    center.y + sinf(needle_angle + pi) * 10.0f);
            dl->AddTriangleFilled(tip, bl, br_, s.needle_color);
            dl->AddTriangleFilled(bl, br_, back, IM_COL32(140, 60, 60, 255));

            // hub
            dl->AddCircleFilled(center, 7.0f, IM_COL32(50, 56, 64, 255), 18);
            dl->AddCircle(center, 7.0f, IM_COL32(120, 130, 142, 255), 18, 1.5f);

            // big value text below center, then unit/label on its own line. spacing is derived from
            // the actual text height so they never visually touch even at small radii
            float text_h     = ImGui::GetTextLineHeight();
            float value_y    = center.y + 8.0f;
            float label_y    = value_y + text_h + 4.0f;
            if (s.value_text && s.value_text[0])
            {
                ImVec2 size = ImGui::CalcTextSize(s.value_text);
                dl->AddText(ImVec2(center.x - size.x * 0.5f, value_y), s.value_color, s.value_text);
            }

            if (s.label && s.label[0])
            {
                ImVec2 size = ImGui::CalcTextSize(s.label);
                dl->AddText(ImVec2(center.x - size.x * 0.5f, label_y), text_label, s.label);
            }
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

        // shared tire visual, renders the rubber gradient, wear%, force arrows. labels and slip
        // text are now rendered as regular ImGui text by the caller for clean table layout
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
                if (!grounded) { sr /= 2; sg /= 2; sb /= 2; }
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

        // shared spring coil visual. label and percentage are now rendered as ImGui text by the caller
        void draw_coil(ImDrawList* dl, float compression, ImVec2 center_top, float max_h, float min_h)
        {
            float ext     = 1.0f - std::clamp(compression, 0.0f, 1.0f);
            float spring_h = min_h + (max_h - min_h) * ext;

            ImU32 col = (compression > 0.8f) ? accent_danger
                       : (compression > 0.5f) ? accent_warn
                       : accent_ok;

            float cx     = center_top.x;
            float top_y  = center_top.y;
            dl->AddRectFilled(ImVec2(cx - 18, top_y), ImVec2(cx + 18, top_y + 6), IM_COL32(100, 110, 124, 255), 2.0f);

            const int segs = 7;
            float seg_h = spring_h / segs;
            float hw    = 22.0f;
            float top   = top_y + 8.0f;
            for (int i = 0; i < segs; ++i)
            {
                float y1 = top + i * seg_h;
                float y2 = top + (i + 0.5f) * seg_h;
                float y3 = top + (i + 1) * seg_h;
                if (i % 2 == 0)
                {
                    dl->AddLine(ImVec2(cx - hw, y1), ImVec2(cx + hw, y2), col, 3.5f);
                    dl->AddLine(ImVec2(cx + hw, y2), ImVec2(cx - hw, y3), col, 3.5f);
                }
                else
                {
                    dl->AddLine(ImVec2(cx + hw, y1), ImVec2(cx - hw, y2), col, 3.5f);
                    dl->AddLine(ImVec2(cx - hw, y2), ImVec2(cx + hw, y3), col, 3.5f);
                }
            }

            float bot = top + spring_h;
            dl->AddRectFilled(ImVec2(cx - 18, bot), ImVec2(cx + 18, bot + 6), IM_COL32(100, 110, 124, 255), 2.0f);
            dl->AddLine(ImVec2(cx, top_y + 6), ImVec2(cx, bot), IM_COL32(70, 80, 92, 255), 2.0f);
        }

        // helper: a thin horizontal level meter used in the engine tab and the legends
        void draw_level_bar(const char* label, float level, ImU32 color)
        {
            ImGui::Text("  %s", label);
            ImGui::SameLine(120);
            ImVec2 pos = ImGui::GetCursorScreenPos();
            float w = 200.0f;
            float h = 14.0f;
            float fill = std::clamp(level * 5.0f, 0.0f, 1.0f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), track_dim, 3.0f);
            dl->AddRectFilled(pos, ImVec2(pos.x + w * fill, pos.y + h), color, 3.0f);
            dl->AddRect(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(70, 80, 92, 255), 3.0f, 1.0f);
            ImGui::Dummy(ImVec2(w, h));
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.85f, 0.87f, 0.9f, 1.0f), "%.4f", level);
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
        if (!Engine::IsFlagSet(EngineMode::EditorVisible) || !physics)
        {
            return;
        }

        ImGuiIO& io = ImGui::GetIO();
        if (io.DisplaySize.x < 200.0f || io.DisplaySize.y < 200.0f)
        {
            return;
        }

        math::Vector3 velocity = physics->GetLinearVelocity();
        float speed_kmh        = velocity.Length() * 3.6f;
        float engine_rpm       = physics->GetEngineRPM();
        float redline_rpm      = physics->GetRedlineRPM();
        bool  turbo_enabled    = physics->GetTurboEnabled();
        float boost_bar        = physics->GetBoostPressure();
        bool  is_shifting      = physics->IsShifting();
        const char* gear_str   = physics->GetCurrentGearString();
        float throttle         = physics->GetVehicleThrottle();
        float brake            = physics->GetVehicleBrake();
        float steer            = physics->GetVehicleSteering();
        float handbrake        = physics->GetVehicleHandbrake();

        // anchor a full width strip to the bottom of the viewport so it spans left to right like a real
        // game hud, fall back to the whole display if the viewport rect is not published yet
        const float panel_h     = 178.0f;
        const float side_margin = 12.0f;

        const math::Vector2& vp_pos  = Viewport::GetScreenPosition();
        const math::Vector2& vp_size = Viewport::GetScreenSize();
        float region_left   = 0.0f;
        float region_width  = io.DisplaySize.x;
        float anchor_bottom = io.DisplaySize.y;
        if (vp_size.x > 100.0f && vp_size.y > 100.0f)
        {
            region_left   = vp_pos.x;
            region_width  = vp_size.x;
            anchor_bottom = vp_pos.y + vp_size.y;
        }
        const float panel_w = region_width - side_margin * 2.0f;
        ImVec2 panel_pos(region_left + side_margin, anchor_bottom - panel_h - 18.0f);

        ImGui::SetNextWindowPos(panel_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(panel_w, panel_h), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                               | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings
                               | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav
                               | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs
                               | ImGuiWindowFlags_NoDocking;

        if (ImGui::Begin("##car_driver_hud", nullptr, flags))
        {
            // draw on the foreground list so the strip always sits on top of the viewport image instead
            // of being hidden behind it, the transparent window is only used as a layout scaffold
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            ImVec2 tl     = ImGui::GetCursorScreenPos();

            // full width cockpit strip, elements are spread across the bar instead of clustered in the
            // middle, the input bars sit on the left, the dials spread across the centre and the assist
            // tell tales form a column on the right
            const float mid_y     = tl.y + panel_h * 0.5f;
            const float pad       = 30.0f;
            const float content_l = tl.x + pad;
            const float content_r = tl.x + panel_w - pad;

            // ---- left, throttle / brake / steering bars ----
            const float in_label_w = 48.0f;
            const float in_bar_w   = 168.0f;
            const float in_val_w   = 52.0f;
            const float in_block_w = in_label_w + in_bar_w + in_val_w;
            {
                const float bar_h_l = 12.0f;
                const float row_gap = 36.0f;
                float row_y         = mid_y - row_gap;

                auto input_bar = [&](const char* label, float value, ImU32 color, bool signed_pct, float y)
                {
                    dl->AddText(ImVec2(content_l, y - 7.0f), text_label, label);
                    float bx = content_l + in_label_w;
                    ImVec2 b_tl(bx, y - bar_h_l * 0.5f);
                    ImVec2 b_br(bx + in_bar_w, y + bar_h_l * 0.5f);
                    dl->AddRectFilled(b_tl, b_br, track_dim, 3.0f);
                    if (signed_pct)
                    {
                        float cx = b_tl.x + in_bar_w * 0.5f;
                        dl->AddLine(ImVec2(cx, b_tl.y), ImVec2(cx, b_br.y), IM_COL32(110, 120, 134, 200), 1.0f);
                        float ix = cx + std::clamp(value, -1.0f, 1.0f) * in_bar_w * 0.5f;
                        dl->AddRectFilled(ImVec2(ix - 3.0f, b_tl.y - 1.0f), ImVec2(ix + 3.0f, b_br.y + 1.0f), color, 2.0f);
                    }
                    else
                    {
                        dl->AddRectFilled(b_tl, ImVec2(b_tl.x + in_bar_w * std::clamp(value, 0.0f, 1.0f), b_br.y), color, 3.0f);
                    }
                    dl->AddRect(b_tl, b_br, IM_COL32(70, 80, 92, 255), 3.0f, 1.0f);
                    char buf[8];
                    if (signed_pct)
                    {
                        snprintf(buf, sizeof(buf), "%+.0f%%", value * 100.0f);
                    }
                    else
                    {
                        snprintf(buf, sizeof(buf), "%.0f%%", value * 100.0f);
                    }
                    dl->AddText(ImVec2(b_br.x + 8.0f, y - 7.0f), text_dim, buf);
                };

                input_bar("THR",   throttle, accent_ok,     false, row_y);
                input_bar("BRK",   brake,    accent_danger, false, row_y + row_gap);
                input_bar("STEER", steer,    accent_warn,   true,  row_y + row_gap * 2.0f);
            }

            // ---- right, assist tell tales as a fixed width column, always visible, dim when off ----
            float assists_l;
            {
                static pill_anim a_abs, a_tcs, a_drs, a_hbrk, a_turbo;
                bool abs_on    = physics->GetAbsEnabled();
                bool abs_act   = physics->IsAbsActiveAny();
                bool tcs_on    = physics->GetTcEnabled();
                bool tcs_act   = physics->IsTcActive();
                bool drs_on    = physics->GetDrsEnabled();
                bool drs_act   = physics->GetDrsActive();
                bool hbrk_act  = handbrake > 0.1f;
                bool turbo_act = turbo_enabled && boost_bar > 0.5f;

                // abs grabs and releases the disc at the modulation frequency, drive the tell tale straight
                // from that phase so it visibly flickers in sync instead of sitting solidly lit
                bool abs_grab = abs_act && physics->GetAbsPhase() >= 0.5f;

                pill_state s_abs   = abs_act ? (abs_grab ? pill_state::active : pill_state::idle) : (abs_on ? pill_state::idle : pill_state::off);
                pill_state s_tcs   = tcs_act ? pill_state::active : (tcs_on ? pill_state::idle : pill_state::off);
                pill_state s_drs   = drs_act ? pill_state::active : (drs_on ? pill_state::idle : pill_state::off);
                pill_state s_turbo = turbo_act ? pill_state::active : (turbo_enabled ? pill_state::idle : pill_state::off);
                pill_state s_hbrk  = hbrk_act ? pill_state::active : pill_state::idle;

                const float pill_w   = 66.0f;
                const float pill_h   = ImGui::CalcTextSize("A").y + 6.0f;
                const float pill_gap = 6.0f;
                float total_h        = pill_h * 5.0f + pill_gap * 4.0f;
                float py             = mid_y - total_h * 0.5f;
                float px             = content_r - pill_w;
                assists_l            = px;

                // each tell tale lights with a colour that matches the real car, abs amber, tcs yellow,
                // turbo cyan, drs green, handbrake red, so an engaged system is obvious at a glance
                auto stack_pill = [&](const char* text, pill_state st, pill_anim& anim, ImU32 accent, bool instant)
                {
                    draw_status_pill(dl, ImVec2(px, py), text, st, anim, io.DeltaTime, accent, pill_w, instant);
                    py += pill_h + pill_gap;
                };
                stack_pill("ABS",   s_abs,   a_abs,   accent_warn,              abs_act);
                stack_pill("TCS",   s_tcs,   a_tcs,   IM_COL32(255, 225, 60, 255), false);
                stack_pill("TURBO", s_turbo, a_turbo, accent_info,              false);
                stack_pill("DRS",   s_drs,   a_drs,   accent_ok,                false);
                stack_pill("HBRK",  s_hbrk,  a_hbrk,  accent_danger,            false);
            }

            // ---- centre, dials spread evenly across the free space between inputs and tell tales ----
            {
                int   slots      = turbo_enabled ? 4 : 3;
                float zone_l     = content_l + in_block_w + 26.0f;
                float zone_r     = assists_l - 26.0f;
                float zone_w     = zone_r - zone_l;

                float gauge_r    = 56.0f;
                float gear_box_w = 88.0f;
                float g_gap      = 24.0f;
                float cluster_w  = gauge_r * 2.0f * (turbo_enabled ? 3.0f : 2.0f) + gear_box_w + g_gap * (float)(slots - 1);

                // shrink to fit narrow viewports so the dials never overlap each other or the side blocks
                if (cluster_w > zone_w && cluster_w > 0.0f)
                {
                    float s     = zone_w / cluster_w;
                    gauge_r    *= s;
                    gear_box_w *= s;
                    g_gap      *= s;
                    cluster_w   = zone_w;
                }

                float gauge_d  = gauge_r * 2.0f;
                float gauge_cy = tl.y + 16.0f + gauge_r;
                float x        = zone_l + (zone_w - cluster_w) * 0.5f;

                float tach_cx  = x + gauge_r;                       x += gauge_d + g_gap;
                float gear_cx  = x + gear_box_w * 0.5f;             x += gear_box_w + g_gap;
                float speed_cx = x + gauge_r;                       x += gauge_d + g_gap;
                float boost_cx = x + gauge_r;

                // tach
                {
                    gauge_spec spec;
                    spec.radius           = gauge_r;
                    spec.value            = engine_rpm;
                    spec.value_max        = 10000.0f;
                    spec.tick_count       = 10;
                    spec.major_every      = 1;
                    spec.draw_labels      = false;
                    spec.label_divider    = 1000.0f;
                    spec.redline_fraction = redline_rpm / 10000.0f;
                    spec.needle_color     = (engine_rpm > redline_rpm) ? IM_COL32(255, 120, 120, 255) : IM_COL32(220, 80, 80, 255);
                    char rpm_text[16]; snprintf(rpm_text, sizeof(rpm_text), "%.1fk", engine_rpm / 1000.0f);
                    spec.value_text       = rpm_text;
                    spec.label            = "RPM";
                    spec.value_color      = spec.needle_color;
                    draw_gauge(dl, ImVec2(tach_cx, gauge_cy), gauge_kind::rpm, spec);
                }

                // gear box
                {
                    static float gear_pulse = 0.0f;
                    static bool  prev_shift = false;
                    if (is_shifting && !prev_shift)
                    {
                        gear_pulse = 1.0f;
                    }
                    prev_shift = is_shifting;
                    gear_pulse = std::max(0.0f, gear_pulse - io.DeltaTime * 5.0f);
                    float scale = 1.0f + gear_pulse * 0.18f;
                    float fsize = gauge_d * 0.58f * scale;
                    ImU32 gc    = is_shifting ? accent_warn : (engine_rpm > redline_rpm ? accent_danger : text_primary);

                    float box_h = gauge_d - 6.0f;
                    ImVec2 box_tl(gear_cx - gear_box_w * 0.5f, gauge_cy - box_h * 0.5f);
                    ImVec2 box_br(box_tl.x + gear_box_w, box_tl.y + box_h);
                    dl->AddRectFilled(box_tl, box_br, IM_COL32(8, 10, 14, 230), 8.0f);
                    dl->AddRect(box_tl, box_br, IM_COL32(80, 95, 110, 140), 8.0f, 1.0f);

                    ImFont* font = ImGui::GetFont();
                    ImVec2 gts   = font->CalcTextSizeA(fsize, FLT_MAX, 0.0f, gear_str);
                    dl->AddText(font, fsize, ImVec2(box_tl.x + (gear_box_w - gts.x) * 0.5f, box_tl.y + (box_h - gts.y) * 0.5f), gc, gear_str);
                    ImVec2 ls = ImGui::CalcTextSize("GEAR");
                    dl->AddText(ImVec2(box_tl.x + (gear_box_w - ls.x) * 0.5f, box_br.y + 4.0f), text_label, "GEAR");
                }

                // speed
                {
                    gauge_spec spec;
                    spec.radius           = gauge_r;
                    spec.value            = std::min(speed_kmh, 350.0f);
                    spec.value_max        = 350.0f;
                    spec.tick_count       = 7;
                    spec.major_every      = 1;
                    spec.draw_labels      = false;
                    spec.label_divider    = 1.0f;
                    spec.needle_color     = IM_COL32(220, 80, 80, 255);
                    char sp[16]; snprintf(sp, sizeof(sp), "%.0f", speed_kmh);
                    spec.value_text       = sp;
                    spec.label            = "km/h";
                    draw_gauge(dl, ImVec2(speed_cx, gauge_cy), gauge_kind::speed, spec);
                }

                // boost
                if (turbo_enabled)
                {
                    gauge_spec spec;
                    spec.radius           = gauge_r;
                    spec.value            = std::min(boost_bar, 2.5f);
                    spec.value_max        = 2.5f;
                    spec.tick_count       = 5;
                    spec.major_every      = 1;
                    spec.draw_labels      = false;
                    spec.label_divider    = 1.0f;
                    spec.needle_color     = (boost_bar > 2.0f) ? accent_danger : accent_info;
                    char bp[16]; snprintf(bp, sizeof(bp), "%.1f", boost_bar);
                    spec.value_text       = bp;
                    spec.label            = "bar";
                    spec.value_color      = spec.needle_color;
                    draw_gauge(dl, ImVec2(boost_cx, gauge_cy), gauge_kind::boost, spec);
                }
            }

            // f3 hint, tucked into the top left of the strip
            dl->AddText(ImVec2(tl.x + 14.0f, tl.y + 6.0f), text_label, "F3 telemetry");
        }
        ImGui::End();

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    // ====================================================================================
    // telemetry window tabs
    // ====================================================================================

    namespace
    {
        // car tab: setup controls that used to live under the overview, the live telemetry is now the
        // bottom dashboard so this tab only keeps the driver assists and the car preset switcher
        void tab_car(Physics* physics)
        {
            ImGui::SeparatorText("Driver assists");

            bool abs_enabled    = physics->GetAbsEnabled();
            bool tc_enabled     = physics->GetTcEnabled();
            bool manual_trans   = physics->GetManualTransmission();
            bool drs_enabled    = physics->GetDrsEnabled();
            bool turbo_on       = physics->GetTurboEnabled();
            int  diff_type      = physics->GetDiffType();
            const char* diff_items[] = { "Open", "Locked", "LSD" };

            if (ImGui::BeginTable("##assists", 3, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoBordersInBody))
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
                hud_tooltip("Manual transmission: disables auto shifts. Use L1/R1.");

                ImGui::TableNextRow();
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
                ImGui::SameLine();
                ImGui::TextColored(imvec4_from_u32(text_dim), "Diff");
                hud_tooltip("Differential type: Open splits torque freely, Locked equalises wheel speed, LSD biases under load.");
                ImGui::EndTable();
            }

            ImGui::SeparatorText("Car preset");
            ImGui::SetNextItemWidth(280);
            if (ImGui::BeginCombo("Car", car::tuning::spec.name))
            {
                for (int i = 0; i < car::preset_count; ++i)
                {
                    bool selected = (i == car::active_preset_index);
                    if (ImGui::Selectable(car::preset_registry[i].name, selected))
                    {
                        car::active_preset_index = i;
                        car::load_car(*car::preset_registry[i].instance);
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        void tab_upgrades(Physics* /*physics*/)
        {
            ImGui::SeparatorText("Available upgrades");
            ImGui::TextColored(imvec4_from_u32(text_dim), "%s", car::tuning::spec.name ? car::tuning::spec.name : "");
            if (ImGui::Button("Reset to stock"))
            {
                car::reset_upgrades();
            }
            hud_tooltip("removes all upgrades, restores base preset");
            auto draw_stages = [](const char* name, int& level, int maxs, const char* desc)
            {
                if (maxs <= 0)
                {
                    int before = level;
                    car::clamp_upgrade_stage(level, 0);
                    if (level != before)
                    {
                        car::reapply_upgrades();
                    }
                    return;
                }
                int before = level;
                car::clamp_upgrade_stage(level, maxs);
                if (level != before)
                {
                    car::reapply_upgrades();
                }
                ImGui::PushID(name);
                ImGui::SeparatorText(name);
                for (int s = 0; s <= maxs; s++)
                {
                    if (s > 0)
                    {
                        ImGui::SameLine();
                    }
                    ImGui::PushID(s);
                    char lbl[2] = {'0', 0};
                    lbl[0] = (char)('0' + s);
                    bool pushed = false;
                    if (level == s)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.45f, 0.72f, 1.0f));
                        pushed = true;
                    }
                    if (ImGui::SmallButton(lbl))
                    {
                        level = s;
                        car::reapply_upgrades();
                    }
                    if (pushed)
                    {
                        ImGui::PopStyleColor();
                    }
                    ImGui::PopID();
                }
                if (desc && desc[0])
                {
                    ImGui::TextColored(imvec4_from_u32(text_dim), "%s", desc);
                }
                ImGui::PopID();
            };
            draw_stages("Engine", car::upgrades.engine, car::base_spec.engine_stage_max, "Increases engine power and redline.");
            draw_stages("Suspension", car::upgrades.suspension, car::base_spec.suspension_stage_max, "Stiffer springs and dampers for sharper handling.");
            draw_stages("Tires", car::upgrades.tires, car::base_spec.tires_stage_max, "Higher grip compound for better traction and cornering.");
            draw_stages("Brakes", car::upgrades.brakes, car::base_spec.brakes_stage_max, "Stronger brakes with improved cooling to resist fade.");
            draw_stages("Aero", car::upgrades.aero, car::base_spec.aero_stage_max, "Adds downforce for high-speed stability and cornering grip.");
            draw_stages("Weight", car::upgrades.weight, car::base_spec.weight_stage_max, "Reduces overall mass for better acceleration and handling.");
            if (car::base_spec.engine_peak_torque > 0.0f)
            {
                float cur = car::tuning::spec.engine_peak_torque;
                float bse = car::base_spec.engine_peak_torque;
                ImGui::SeparatorText("Delta");
                ImGui::TextColored(imvec4_from_u32(text_label), "torque %.0f vs %.0f", cur, bse);
            }
        }

        // tires tab: 2x2 table of wheel cells, each cell holds the tire visual plus a temps sub-table
        void tab_tires(Physics* physics)
        {
            ImGui::SeparatorText("Wheel forces");
            ImGui::TextColored(imvec4_from_u32(text_dim), "Arrows show contact forces. Slip angle in orange, slip ratio in purple.");

            const ImVec2 tire_size = ImVec2(64.0f, 100.0f);
            const char*  labels[4] = { "FL", "FR", "RL", "RR" };
            const WheelIndex idx[4] = { WheelIndex::FrontLeft, WheelIndex::FrontRight, WheelIndex::RearLeft, WheelIndex::RearRight };

            // a single 2x2 imgui table holds the four wheel cells; columns stretch evenly
            if (ImGui::BeginTable("##tires_grid", 2,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersInnerH))
            {
                for (int i = 0; i < 4; ++i)
                {
                    if ((i % 2) == 0)
                    {
                        ImGui::TableNextRow();
                    }
                    ImGui::TableNextColumn();

                    // wheel header
                    ImGui::TextColored(imvec4_from_u32(text_primary), "%s", labels[i]);
                    ImGui::SameLine();
                    ImGui::TextColored(imvec4_from_u32(text_dim), "  wear %.0f%%", physics->GetWheelWear(idx[i]) * 100.0f);

                    // tire visual: reserve a tire_size slot and render with draw_list inside it
                    {
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        ImVec2 base    = ImGui::GetCursorScreenPos();
                        float  cell_w  = ImGui::GetContentRegionAvail().x;
                        ImVec2 tl(base.x + (cell_w - tire_size.x) * 0.5f, base.y + 4.0f);
                        draw_tire_block(dl, physics, idx[i], tl, tire_size, true);
                        ImGui::Dummy(ImVec2(cell_w, tire_size.y + 8.0f));
                    }

                    // slip angle / slip ratio row, rendered with proper imgui text widgets
                    float slip_angle = physics->GetWheelSlipAngle(idx[i]) * 57.2958f;
                    float slip_ratio = physics->GetWheelSlipRatio(idx[i]) * 100.0f;
                    ImGui::TextColored(imvec4_from_u32(accent_warn), "%.1f\xC2\xB0", slip_angle);
                    ImGui::SameLine();
                    ImGui::TextColored(imvec4_from_u32(text_dim), "slip");
                    ImGui::SameLine(0.0f, 16.0f);
                    ImGui::TextColored(imvec4_from_u32(IM_COL32(200, 130, 255, 255)), "%.1f%%", slip_ratio);
                    ImGui::SameLine();
                    ImGui::TextColored(imvec4_from_u32(text_dim), "ratio");

                    // surface temperature strip as a 3-column sub-table
                    float s_in  = physics->GetWheelSurfaceTemp(idx[i], 0);
                    float s_mid = physics->GetWheelSurfaceTemp(idx[i], 1);
                    float s_out = physics->GetWheelSurfaceTemp(idx[i], 2);
                    if (ImGui::BeginTable("##temps", 3, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV))
                    {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextColored(imvec4_from_u32(text_label), "in");
                        ImGui::TextColored(imvec4_from_u32(temp_color(s_in)),  "%.0f", s_in);
                        ImGui::TableNextColumn();
                        ImGui::TextColored(imvec4_from_u32(text_label), "mid");
                        ImGui::TextColored(imvec4_from_u32(temp_color(s_mid)), "%.0f", s_mid);
                        ImGui::TableNextColumn();
                        ImGui::TextColored(imvec4_from_u32(text_label), "out");
                        ImGui::TextColored(imvec4_from_u32(temp_color(s_out)), "%.0f", s_out);
                        ImGui::EndTable();
                    }

                    // core, grip and brake values as a 3-column sub-table
                    float core  = physics->GetWheelCoreTemp(idx[i]);
                    float grip  = physics->GetWheelTempGripFactor(idx[i]);
                    float brk_t = physics->GetWheelBrakeTemp(idx[i]);
                    ImU32 brake_color = (brk_t > 700.0f) ? accent_danger : (brk_t > 400.0f ? accent_warn : text_primary);

                    if (ImGui::BeginTable("##corebrake", 3, ImGuiTableFlags_SizingStretchSame))
                    {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextColored(imvec4_from_u32(text_label), "core");
                        ImGui::TextColored(imvec4_from_u32(temp_color(core)), "%.0f\xC2\xB0" "C", core);
                        ImGui::TableNextColumn();
                        ImGui::TextColored(imvec4_from_u32(text_label), "grip");
                        ImGui::TextColored(imvec4_from_u32(text_primary), "%.0f%%", grip * 100.0f);
                        ImGui::TableNextColumn();
                        ImGui::TextColored(imvec4_from_u32(text_label), "brake");
                        ImGui::TextColored(imvec4_from_u32(brake_color), "%.0f\xC2\xB0" "C", brk_t);
                        ImGui::EndTable();
                    }
                }
                ImGui::EndTable();
            }

            ImGui::SeparatorText("Tire pressure");
            float psi     = physics->GetTirePressure();
            float psi_opt = physics->GetTirePressureOptimal();
            float dpsi    = psi - psi_opt;
            ImU32 pc = (fabsf(dpsi) < 0.1f) ? accent_ok : (fabsf(dpsi) < 0.3f ? accent_warn : accent_danger);

            if (ImGui::BeginTable("##pressure", 3, ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextColored(imvec4_from_u32(text_label), "current");
                ImGui::TableNextColumn(); ImGui::TextColored(imvec4_from_u32(text_label), "optimal");
                ImGui::TableNextColumn(); ImGui::TextColored(imvec4_from_u32(text_label), "delta");
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextColored(imvec4_from_u32(pc), "%.2f bar", psi);
                ImGui::TableNextColumn(); ImGui::TextColored(imvec4_from_u32(text_primary), "%.2f bar", psi_opt);
                ImGui::TableNextColumn(); ImGui::TextColored(imvec4_from_u32(pc), "%+.2f bar", dpsi);
                ImGui::EndTable();
            }
            hud_tooltip("Cold tire pressure. Off-optimal pressure costs grip and degrades thermals.");

            ImGui::SeparatorText("Legend");
            if (ImGui::BeginTable("##tire_legend", 2, ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); draw_legend_item(accent_info,    "lateral force");
                ImGui::TableNextColumn(); draw_legend_item(accent_ok,      "longitudinal traction");
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); draw_legend_item(accent_danger,  "longitudinal braking");
                ImGui::TableNextColumn(); draw_legend_item(accent_warn,    "slip angle (deg)");
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); draw_legend_item(IM_COL32(200, 130, 255, 255), "slip ratio (%)");
                ImGui::EndTable();
            }
        }

        // suspension tab: 2x2 table of wheel cells, each cell holds a coil and a travel sparkline
        void tab_suspension(Physics* physics)
        {
            ImGui::SeparatorText("Spring compression");
            ImGui::TextColored(imvec4_from_u32(text_dim), "Green: nominal travel. Amber: large compression. Red: bottoming out.");

            const float max_h     = 110.0f;
            const float min_h     = 40.0f;
            const float coil_w    = 64.0f;
            const float spark_w   = 140.0f;
            const float spark_h   = 70.0f;

            const char*       labels[4] = { "FL", "FR", "RL", "RR" };
            const WheelIndex  idx[4]    = { WheelIndex::FrontLeft, WheelIndex::FrontRight, WheelIndex::RearLeft, WheelIndex::RearRight };

            static constexpr int hist_n = 120;
            static float history[4][hist_n] = {};
            static int hist_pos = 0;
            for (int i = 0; i < 4; ++i)
                history[i][hist_pos] = physics->GetWheelCompression(idx[i]);
            hist_pos = (hist_pos + 1) % hist_n;

            if (ImGui::BeginTable("##susp_grid", 2,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersInnerH))
            {
                for (int i = 0; i < 4; ++i)
                {
                    if ((i % 2) == 0)
                    {
                        ImGui::TableNextRow();
                    }
                    ImGui::TableNextColumn();

                    float comp = physics->GetWheelCompression(idx[i]);
                    ImU32 comp_color = (comp > 0.8f) ? accent_danger : (comp > 0.5f ? accent_warn : accent_ok);

                    // header row: label on left, percentage on right
                    ImGui::TextColored(imvec4_from_u32(text_primary), "%s", labels[i]);
                    ImGui::SameLine();
                    ImGui::TextColored(imvec4_from_u32(comp_color), "  %.0f%%", comp * 100.0f);

                    // body: coil on the left, travel sparkline on the right via an inner 2-col table
                    if (ImGui::BeginTable("##susp_cell", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoBordersInBody))
                    {
                        ImGui::TableSetupColumn("##coil",  ImGuiTableColumnFlags_WidthFixed, coil_w);
                        ImGui::TableSetupColumn("##spark", ImGuiTableColumnFlags_WidthStretch);

                        ImGui::TableNextRow();

                        // coil column
                        ImGui::TableNextColumn();
                        {
                            ImDrawList* dl = ImGui::GetWindowDrawList();
                            ImVec2 base = ImGui::GetCursorScreenPos();
                            ImVec2 center_top(base.x + coil_w * 0.5f, base.y + 4.0f);
                            draw_coil(dl, comp, center_top, max_h, min_h);
                            ImGui::Dummy(ImVec2(coil_w, max_h + 16.0f));
                        }

                        // sparkline column
                        ImGui::TableNextColumn();
                        {
                            ImDrawList* dl  = ImGui::GetWindowDrawList();
                            ImVec2 base     = ImGui::GetCursorScreenPos();
                            float  w_avail  = std::max(spark_w, ImGui::GetContentRegionAvail().x);
                            ImVec2 tl(base.x, base.y + 8.0f);
                            ImVec2 br(tl.x + w_avail - 4.0f, tl.y + spark_h);
                            dl->AddRectFilled(tl, br, IM_COL32(18, 22, 28, 230), 4.0f);
                            dl->AddRect(tl, br, IM_COL32(70, 80, 92, 160), 4.0f, 1.0f);
                            float ww = br.x - tl.x;
                            float hh = br.y - tl.y;
                            ImVec2 prev_pt(0, 0);
                            for (int k = 0; k < hist_n; ++k)
                            {
                                int idx_k = (hist_pos + k) % hist_n;
                                float v = std::clamp(history[i][idx_k], 0.0f, 1.0f);
                                ImVec2 pt(tl.x + (float)k / (hist_n - 1) * ww, br.y - v * (hh - 2.0f) - 1.0f);
                                if (k > 0)
                                {
                                    dl->AddLine(prev_pt, pt, comp_color, 1.4f);
                                }
                                prev_pt = pt;
                            }
                            dl->AddText(ImVec2(tl.x + 4.0f, tl.y + 2.0f), text_label, "travel history");
                            ImGui::Dummy(ImVec2(ww, spark_h + 8.0f));
                        }
                        ImGui::EndTable();
                    }
                }
                ImGui::EndTable();
            }

            ImGui::SeparatorText("Aero ride height");
            if (car::aero_debug.valid)
            {
                ImGui::TextColored(imvec4_from_u32(text_primary), "%.2f m", car::aero_debug.ride_height);
                hud_tooltip("Average distance between chassis underside and the ground. Lower = more ground effect, more bottom-out risk.");
            }
            else
            {
                ImGui::TextColored(imvec4_from_u32(text_dim), "ride height telemetry not active");
            }
        }

        // aero tab: silhouette + arrows + numerical breakdown
        void tab_aero(Physics* physics)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();

            ImGui::SeparatorText("Aerodynamics");

            math::Vector3 velocity = physics->GetLinearVelocity();
            float speed_kmh        = velocity.Length() * 3.6f;
            float aero_speed_ms    = speed_kmh / 3.6f;

            const car::aero_debug_data& aero = car::get_aero_debug();
            float frontal_area = car::get_frontal_area();
            float side_area    = car::get_side_area();
            float drag_coeff   = car::get_drag_coeff();
            const car::shape_2d& shape = car::get_shape_data();

            const float side_view_w  = 320.0f;
            const float front_view_w = 220.0f;
            const float view_h       = 150.0f;

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

            // pre-compute forces so we can render the arrows over the silhouettes
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
                const float air_density = 1.225f;
                float dyn_pressure      = 0.5f * air_density * aero_speed_ms * aero_speed_ms;
                drag_n     = dyn_pressure * drag_coeff * frontal_area;
                front_df_n = fabsf(car::get_lift_coeff_front() * dyn_pressure * frontal_area);
                rear_df_n  = fabsf(car::get_lift_coeff_rear()  * dyn_pressure * frontal_area);
            }
            float total_df = front_df_n + rear_df_n;

            const float fs      = 0.035f;
            const float max_len = 60.0f;
            auto arrow_with_label = [&](ImVec2 from, float dx, float dy, ImU32 color, float force_n)
            {
                draw_arrow(dl, from, dx, dy, color, 3.0f);
                if (sqrtf(dx*dx + dy*dy) < 5.0f)
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
                    snprintf(buf, sizeof(buf), "%.0f N",   force_n);
                }
                dl->AddText(ImVec2(end.x + (dy != 0 ? 4.0f : -18.0f), end.y + (dx != 0 ? -14.0f : -4.0f)), color, buf);
            };

            // two-column table holds the side and front views
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

            ImGui::SeparatorText("Numbers");
            if (ImGui::BeginTable("##aero_numbers", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("metric", ImGuiTableColumnFlags_WidthStretch, 0.6f);
                ImGui::TableSetupColumn("value",  ImGuiTableColumnFlags_WidthStretch, 0.4f);

                ImGui::TableNextRow(); ImGui::TableNextColumn();
                ImGui::TextColored(imvec4_from_u32(text_label), "Frontal area");
                ImGui::TableNextColumn();
                ImGui::TextColored(imvec4_from_u32(text_primary), "%.2f m\xC2\xB2", frontal_area);

                ImGui::TableNextRow(); ImGui::TableNextColumn();
                ImGui::TextColored(imvec4_from_u32(text_label), "Side area");
                ImGui::TableNextColumn();
                ImGui::TextColored(imvec4_from_u32(text_primary), "%.2f m\xC2\xB2", side_area);

                ImGui::TableNextRow(); ImGui::TableNextColumn();
                ImGui::TextColored(imvec4_from_u32(text_label), "Drag coefficient");
                ImGui::TableNextColumn();
                ImGui::TextColored(imvec4_from_u32(text_primary), "%.2f", drag_coeff);

                ImGui::TableNextRow(); ImGui::TableNextColumn();
                ImGui::TextColored(imvec4_from_u32(text_label), "Speed");
                ImGui::TableNextColumn();
                ImGui::TextColored(imvec4_from_u32(text_primary), "%.0f km/h", speed_kmh);

                ImGui::TableNextRow(); ImGui::TableNextColumn();
                ImGui::TextColored(imvec4_from_u32(text_label), "Drag force");
                ImGui::TableNextColumn();
                ImGui::TextColored(imvec4_from_u32(accent_warn), "%.0f N", drag_n);

                ImGui::TableNextRow(); ImGui::TableNextColumn();
                ImGui::TextColored(imvec4_from_u32(text_label), "Front downforce");
                ImGui::TableNextColumn();
                ImGui::TextColored(imvec4_from_u32(accent_info), "%.0f N", front_df_n);

                ImGui::TableNextRow(); ImGui::TableNextColumn();
                ImGui::TextColored(imvec4_from_u32(text_label), "Rear downforce");
                ImGui::TableNextColumn();
                ImGui::TextColored(imvec4_from_u32(accent_info), "%.0f N", rear_df_n);

                if (side_n > 1.0f)
                {
                    ImGui::TableNextRow(); ImGui::TableNextColumn();
                    ImGui::TextColored(imvec4_from_u32(text_label), "Side force");
                    ImGui::TableNextColumn();
                    ImGui::TextColored(imvec4_from_u32(accent_warn), "%.0f N", side_n);
                }
                ImGui::EndTable();
            }

            if (total_df > 1.0f)
            {
                ImGui::SeparatorText("Downforce balance");
                float balance = front_df_n / total_df * 100.0f;
                ImGui::TextColored(imvec4_from_u32(text_primary), "Front %.0f%%   Rear %.0f%%", balance, 100.0f - balance);

                ImVec2 bar_tl = ImGui::GetCursorScreenPos();
                float bw = std::min(ImGui::GetContentRegionAvail().x - 8.0f, 420.0f);
                float bh = 10.0f;
                dl->AddRectFilled(bar_tl, ImVec2(bar_tl.x + bw, bar_tl.y + bh), track_dim, 3.0f);
                dl->AddRectFilled(bar_tl, ImVec2(bar_tl.x + bw * balance * 0.01f, bar_tl.y + bh), accent_info, 3.0f);
                dl->AddRect(bar_tl, ImVec2(bar_tl.x + bw, bar_tl.y + bh), IM_COL32(70, 80, 92, 255), 3.0f, 1.0f);
                ImGui::Dummy(ImVec2(bw, bh + 4.0f));

                if (aero.valid && aero.ground_effect_factor > 1.01f)
                {
                    ImGui::TextColored(imvec4_from_u32(accent_ok), "Ground effect: +%.0f%%", (aero.ground_effect_factor - 1.0f) * 100.0f);
                }
            }

            ImGui::SeparatorText("Legend");
            if (ImGui::BeginTable("##aero_legend", 2, ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); draw_legend_item(accent_warn, "drag / side force");
                ImGui::TableNextColumn(); draw_legend_item(accent_info, "downforce");
                ImGui::EndTable();
            }
        }

        // engine tab: rpm/throttle/boost header, layer meters, leveler, waveform, fft, tire squeal
        void tab_engine(Physics* /*physics*/)
        {
            const engine_sound::debug_data& dbg = engine_sound::get_debug();

            float ice_tq = car::get_engine_torque_current();
            float mot_tq = car::get_motor_torque();
            float rpm    = car::get_current_engine_rpm();
            float boost  = car::get_boost_pressure();
            float throt  = dbg.throttle;

            ImGui::SeparatorText("Powertrain");
            ImGui::TextColored(imvec4_from_u32(accent_warn), "RPM %.0f  |  Throttle %.0f%%  |  Boost %.2f bar", rpm, throt * 100.0f, boost);
            float mot_kw = mot_tq * rpm * (2.0f * 3.14159265f / 60.0f) / 1000.0f;
            ImGui::Text("ICE %.0f Nm  |  Motor %.0f Nm (%.0f kW)  |  Total %.0f Nm", ice_tq, mot_tq, mot_kw, ice_tq + mot_tq);

            if (dbg.ir_taps > 0)
            {
                ImGui::TextColored(imvec4_from_u32(text_dim), "Exhaust IR: %d taps @ %.0f Hz", dbg.ir_taps, (float)engine_sound::tuning::sample_rate);
            }
            else
            {
                ImGui::TextColored(imvec4_from_u32(accent_danger), "Exhaust IR: not loaded (check binaries/project/music/exhaust_ir.wav)");
            }

            ImGui::SeparatorText("Layer levels");
            draw_level_bar("Combustion", dbg.combustion_level, IM_COL32(255, 100, 100, 255));
            draw_level_bar("Exhaust",    dbg.exhaust_level,    IM_COL32(255, 180, 100, 255));
            draw_level_bar("Induction",  dbg.induction_level,  IM_COL32(100, 200, 255, 255));
            draw_level_bar("Mechanical", dbg.mechanical_level, IM_COL32(200, 200, 100, 255));
            draw_level_bar("Turbo",      dbg.turbo_level,      IM_COL32(100, 255, 200, 255));

            ImGui::SeparatorText("Auto leveler");
            draw_level_bar("Gain",       dbg.leveler_gain * 0.2f, IM_COL32(220, 130, 255, 255));
            ImGui::SameLine();
            ImGui::TextColored(imvec4_from_u32(text_dim), "(raw %.3f)", dbg.leveler_gain);
            draw_level_bar("Envelope",   dbg.leveler_envelope,    IM_COL32(180, 100, 220, 255));

            ImGui::SeparatorText("Waveform");
            ImGui::TextColored(imvec4_from_u32(text_dim), "green=output, red=cyl bank L, orange=conv input, cyan=conv output");
            {
                ImVec2 pos = ImGui::GetCursorScreenPos();
                float w = 480.0f, h = 100.0f;
                float cy = pos.y + h * 0.5f;
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(20, 24, 30, 255), 4.0f);
                dl->AddLine(ImVec2(pos.x, cy), ImVec2(pos.x + w, cy), IM_COL32(70, 80, 92, 255));

                auto trace = [&](const float* buf, ImU32 c)
                {
                    int n = engine_sound::debug_data::waveform_size;
                    int start = dbg.waveform_write_pos;
                    float peak = 1e-6f;
                    for (int i = 0; i < n; ++i) peak = std::max(peak, fabsf(buf[i]));
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
                trace(dbg.waveform_cyl, IM_COL32(255, 100, 100, 200));
                trace(dbg.waveform_vin, IM_COL32(255, 180, 100, 200));
                trace(dbg.waveform_exh, IM_COL32(100, 220, 255, 200));
                trace(dbg.waveform,     IM_COL32(100, 255, 100, 255));
                dl->AddRect(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(70, 80, 92, 255), 4.0f, 1.0f);
                ImGui::Dummy(ImVec2(w, h));
            }

            ImGui::SeparatorText("Spectrum");
            ImGui::TextColored(imvec4_from_u32(text_dim), "0..%.0f Hz, log mag, -80..0 dB", (float)engine_sound::tuning::sample_rate * 0.5f);
            {
                ImVec2 pos = ImGui::GetCursorScreenPos();
                float w = 480.0f, h = 100.0f;
                int bins = engine_sound::debug_data::spectrum_bins;
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(20, 24, 30, 255), 4.0f);
                for (int g = 1; g < 4; ++g)
                {
                    float gy = pos.y + h * ((float)g / 4.0f);
                    dl->AddLine(ImVec2(pos.x, gy), ImVec2(pos.x + w, gy), IM_COL32(50, 56, 64, 255));
                }
                const float freqs[] = { 50.0f, 100.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f };
                float nyq = (float)engine_sound::tuning::sample_rate * 0.5f;
                for (float f : freqs)
                {
                    float fx = pos.x + w * (f / nyq);
                    dl->AddLine(ImVec2(fx, pos.y), ImVec2(fx, pos.y + h), IM_COL32(50, 56, 70, 200));
                }
                float xs = w / (float)bins;
                for (int i = 0; i < bins - 1; ++i)
                {
                    float d0 = std::clamp(dbg.spectrum[i],     -80.0f, 0.0f);
                    float d1 = std::clamp(dbg.spectrum[i + 1], -80.0f, 0.0f);
                    float y0 = pos.y + h * (1.0f - (d0 + 80.0f) / 80.0f);
                    float y1 = pos.y + h * (1.0f - (d1 + 80.0f) / 80.0f);
                    dl->AddLine(ImVec2(pos.x + i * xs, y0), ImVec2(pos.x + (i + 1) * xs, y1), IM_COL32(180, 220, 255, 220), 1.0f);
                }
                float fx = pos.x + w * (dbg.firing_freq / nyq);
                if (fx >= pos.x && fx <= pos.x + w)
                {
                    dl->AddLine(ImVec2(fx, pos.y), ImVec2(fx, pos.y + h), IM_COL32(255, 200, 80, 220), 1.0f);
                }
                dl->AddRect(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(70, 80, 92, 255), 4.0f, 1.0f);
                ImGui::Dummy(ImVec2(w, h));
            }

            ImGui::SeparatorText("Tire squeal");
            const tire_squeal_sound::debug_data& tire_dbg = tire_squeal_sound::get_debug();
            ImGui::TextColored(imvec4_from_u32(accent_info), "Intensity %.0f%%  |  Speed %.0f%%", tire_dbg.intensity * 100.0f, tire_dbg.speed_norm * 100.0f);
            draw_level_bar("Screech",    tire_dbg.screech_level, IM_COL32(255, 100, 180, 255));
            draw_level_bar("Sibilance",  tire_dbg.sibilance_lvl, IM_COL32(200, 130, 255, 255));
            draw_level_bar("Body",       tire_dbg.body_level,    IM_COL32(180, 180, 100, 255));

            ImGui::SeparatorText("Output");
            draw_level_bar("Engine",     dbg.output_level,        IM_COL32(100, 255, 100, 255));
            draw_level_bar("Tire",       tire_dbg.output_level,   IM_COL32(180, 100, 255, 255));
            draw_level_bar("Eng peak",   dbg.output_peak,         IM_COL32(255, 255, 100, 255));
            draw_level_bar("Tire peak",  tire_dbg.output_peak,    IM_COL32(255, 200, 255, 255));
        }

        // debug tab: 3d-vis toggles + sound dev sliders + wav dump
        void tab_debug(Physics* physics)
        {
            ImGui::SeparatorText("3D visualization");
            bool draw_rays = physics->GetDrawRaycasts();
            bool draw_susp = physics->GetDrawSuspension();
            if (ImGui::Checkbox("Draw raycasts", &draw_rays))
            {
                physics->SetDrawRaycasts(draw_rays);
            }
            hud_tooltip("Draws wheel raycasts in the 3D viewport. Green = ground hit, red = miss.");
            if (ImGui::Checkbox("Draw suspension", &draw_susp))
            {
                physics->SetDrawSuspension(draw_susp);
            }
            hud_tooltip("Draws the suspension line between top mount and wheel contact in the 3D viewport.");

            if (draw_rays || draw_susp)
            {
                ImGui::Spacing();
                ImGui::TextColored(imvec4_from_u32(text_dim), "Legend:");
                if (draw_rays)
                {
                    draw_legend_item(IM_COL32(0,   255, 0,   255), "raycast hit ground");
                    draw_legend_item(IM_COL32(255, 0,   0,   255), "raycast missed");
                }
                if (draw_susp)
                {
                    draw_legend_item(IM_COL32(255, 255, 0,   255), "suspension top mount");
                    draw_legend_item(IM_COL32(0,   128, 255, 255), "suspension wheel contact");
                }
            }

            ImGui::SeparatorText("Sound tuning");
            ImGui::TextColored(imvec4_from_u32(text_dim), "Tweak the synth in real time. Reset restores tuning::* defaults.");

            if (ImGui::CollapsingHeader("Live parameters", ImGuiTreeNodeFlags_DefaultOpen))
            {
                engine_sound::synthesizer& s = engine_sound::get_synthesizer();
                ImGui::SliderFloat("df_f_mix",         &s.params.df_f_mix,         0.0f, 1.0f, "%.4f");
                ImGui::SliderFloat("air_noise",        &s.params.air_noise,        0.0f, 1.0f, "%.3f");
                ImGui::SliderFloat("convolution_wet", &s.params.convolution_wet,  0.0f, 1.0f, "%.3f");
                ImGui::SliderFloat("combustion_level", &s.params.combustion_level, 0.0f, 1.0f, "%.3f");
                ImGui::SliderFloat("exhaust_level",    &s.params.exhaust_level,    0.0f, 4.0f, "%.3f");
                ImGui::SliderFloat("drive_extra",      &s.params.drive_extra,     -2.0f, 4.0f, "%.2f");
                ImGui::SliderFloat("leveler_target",   &s.params.leveler_target,   0.05f, 1.5f, "%.3f");
                ImGui::SliderFloat("master_offset",    &s.params.master_offset,   -0.7f, 1.0f, "%.2f");
                ImGui::SliderFloat("notch_depth",      &s.params.notch_depth,      0.0f, 1.0f, "%.2f");
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

    void draw_telemetry_window(Physics* physics, bool* p_open)
    {
        if (!Engine::IsFlagSet(EngineMode::EditorVisible) || !physics)
        {
            return;
        }

        physics->DrawDebugVisualization();

        ImGuiIO& io = ImGui::GetIO();
        if (io.DisplaySize.x < 200.0f || io.DisplaySize.y < 200.0f)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(720.0f, 700.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 740.0f, 40.0f), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Telemetry", p_open, ImGuiWindowFlags_NoCollapse))
        {
            if (ImGui::BeginTabBar("##telemetry_tabs", ImGuiTabBarFlags_FittingPolicyShrink))
            {
                if (ImGui::BeginTabItem("Car"))
                {
                    if (ImGui::BeginChild("##car_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar))
                    {
                        tab_car(physics);
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Upgrades"))
                {
                    if (ImGui::BeginChild("##upgrades_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar))
                    {
                        tab_upgrades(physics);
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Tires"))
                {
                    if (ImGui::BeginChild("##tires_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar))
                    {
                        tab_tires(physics);
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Suspension"))
                {
                    if (ImGui::BeginChild("##susp_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar))
                    {
                        tab_suspension(physics);
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Aerodynamics"))
                {
                    if (ImGui::BeginChild("##aero_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar))
                    {
                        tab_aero(physics);
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Engine"))
                {
                    if (ImGui::BeginChild("##engine_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar))
                    {
                        tab_engine(physics);
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Debug"))
                {
                    if (ImGui::BeginChild("##debug_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar))
                    {
                        tab_debug(physics);
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }
}
