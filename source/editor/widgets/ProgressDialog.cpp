/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ====================
#include "pch.h"
#include "ProgressDialog.h"
#include "Viewport.h"
#include "../Editor.h"
#include "../imgui/source/imgui_internal.h"
#include "../imgui/ImGui_EditorUi.h"
#include "../imgui/ImGui_Style.h"
#include "rhi/RHI_Compiler.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
//===============================

using namespace spartan;
using namespace spartan::math;

namespace
{
    constexpr uint64_t k_compile_id_session = 0xC0DE0001ULL;

    void draw_clipped_text(ImDrawList* draw, const ImVec2& pos, float max_x, ImU32 color, const std::string& text)
    {
        if (text.empty() || max_x <= pos.x + 4.0f)
        {
            return;
        }

        const float height = ImGui::GetTextLineHeight();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color));
        ImGui::RenderTextEllipsis(
            draw,
            pos,
            ImVec2(max_x, pos.y + height),
            max_x,
            text.c_str(),
            text.c_str() + text.size(),
            nullptr
        );
        ImGui::PopStyleColor();
    }

    std::string format_elapsed(double seconds_raw)
    {
        char buf[32];
        const uint32_t seconds = static_cast<uint32_t>(std::max(0.0, seconds_raw));
        if (seconds >= 60)
        {
            snprintf(buf, sizeof(buf), "%um %02us", seconds / 60, seconds % 60);
        }
        else
        {
            snprintf(buf, sizeof(buf), "%us", seconds);
        }
        return buf;
    }

    std::string format_eta(double seconds_raw)
    {
        if (seconds_raw < 1.0)
        {
            return "< 1s left";
        }

        const uint32_t total = static_cast<uint32_t>(std::min(3599.0, std::ceil(seconds_raw)));
        char buf[32];
        if (total >= 60)
        {
            const uint32_t mins = total / 60;
            // round seconds to nearest 5s above 1 minute so the estimate feels calm
            const uint32_t secs = ((total % 60 + 2) / 5) * 5;
            if (secs >= 60)
            {
                snprintf(buf, sizeof(buf), "~%um left", mins + 1);
            }
            else if (secs == 0)
            {
                snprintf(buf, sizeof(buf), "~%um left", mins);
            }
            else
            {
                snprintf(buf, sizeof(buf), "~%um %02us left", mins, secs);
            }
        }
        else
        {
            snprintf(buf, sizeof(buf), "~%us left", total);
        }
        return buf;
    }
}

ProgressDialog::ProgressDialog(Editor* editor) : Widget(editor)
{
    m_title = "##viewport_progress_osd";
    m_visible = false;
    m_show_in_view_menu = false;
    m_flags |=
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoInputs;
}

void ProgressDialog::OnTick()
{
    const double now = ImGui::GetTime();
    const double dt  = std::clamp(static_cast<double>(ImGui::GetIO().DeltaTime), 0.0, 0.1);

    // 1. Poll general progress tracker (worlds, downloads, imports, terrain, textures)
    const ProgressDisplay tracker = ProgressTracker::GetDisplay();
    bool show_tracker = false;
    if (tracker.count > 0)
    {
        if (m_tracker_visible_since == 0.0)
        {
            m_tracker_visible_since = now;
        }
        // world opens need immediate feedback; incidental background tasks are lightly debounced
        show_tracker = ProgressTracker::IsLoading(ProgressType::World) || (now - m_tracker_visible_since >= 0.15);
    }
    else
    {
        m_tracker_visible_since = 0.0;
    }

    // 2. Poll two-pass shader & pipeline compiler and aggregate into one monotonic session progress
    const bool shaders_p1   = !RHI_Compiler::Shaders().IsPriorityIdle();
    const bool shaders_p2   = !RHI_Compiler::Shaders().IsBackgroundIdle();
    const bool pipelines_p1 = !RHI_Compiler::Pipelines().IsPriorityIdle();
    const bool pipelines_p2 = !RHI_Compiler::Pipelines().IsBackgroundIdle();
    const bool compile_busy = shaders_p1 || shaders_p2 || pipelines_p1 || pipelines_p2;

    const auto sp1 = RHI_Compiler::Shaders().GetProgress();
    const auto sp2 = RHI_Compiler::Shaders().GetBackgroundProgress();
    const auto pp1 = RHI_Compiler::Pipelines().GetProgress();
    const auto pp2 = RHI_Compiler::Pipelines().GetBackgroundProgress();

    auto update_bucket = [](CompileBucket& b, bool active, const RHI_Compiler::Progress& cur)
    {
        if (active)
        {
            if (b.was_active && cur.submitted < b.last_submitted)
            {
                b.archived += b.last_submitted;
            }
            b.last_submitted = cur.submitted;
            b.last_completed = std::min(cur.completed, cur.submitted);
            b.was_active     = true;
        }
        else if (b.was_active)
        {
            b.archived      += b.last_submitted;
            b.last_submitted = 0;
            b.last_completed = 0;
            b.was_active     = false;
        }
    };

    if (compile_busy)
    {
        update_bucket(m_compile_buckets[0], shaders_p1,   sp1);
        update_bucket(m_compile_buckets[1], pipelines_p1, pp1);
        update_bucket(m_compile_buckets[2], shaders_p2,   sp2);
        update_bucket(m_compile_buckets[3], pipelines_p2, pp2);

        m_compile_busy_sec     += dt;
        m_compile_done_hold_sec = 0.0;
        if (shaders_p2 || pipelines_p2)
        {
            m_compile_last_was_pass2 = true;
        }
        else if (shaders_p1 || pipelines_p1)
        {
            m_compile_last_was_pass2 = false;
        }
    }
    else
    {
        if (m_compile_was_shown)
        {
            m_compile_done_hold_sec += dt;
            if (m_compile_done_hold_sec >= 0.85)
            {
                m_compile_was_shown     = false;
                m_compile_busy_sec      = 0.0;
                m_compile_done_hold_sec = 0.0;
                m_compile_buckets       = {};
            }
        }
        else
        {
            m_compile_busy_sec = 0.0;
            m_compile_buckets  = {};
        }
    }

    const uint32_t total_submitted = (shaders_p1 ? sp1.submitted : 0) + (shaders_p2 ? sp2.submitted : 0) + (pipelines_p1 ? pp1.submitted : 0) + (pipelines_p2 ? pp2.submitted : 0);
    const bool show_compile_active = compile_busy && (m_compile_busy_sec >= 0.15 || total_submitted >= 4);
    const bool show_compile_done   = !compile_busy && m_compile_was_shown && m_compile_done_hold_sec < 0.85 && !show_tracker;
    const bool show_compile        = show_compile_active || show_compile_done;
    if (show_compile_active)
    {
        m_compile_was_shown = true;
    }

    // weighted overall session progress so the bar climbs smoothly across Pass 1 -> Pass 2 without resetting
    constexpr float k_bucket_weights[4] = { 1.0f, 0.55f, 2.4f, 0.55f };
    float weighted_completed = 0.0f;
    float weighted_submitted = 0.0f;
    for (int b = 0; b < 4; ++b)
    {
        const uint32_t b_sub  = m_compile_buckets[b].archived + m_compile_buckets[b].last_submitted;
        const uint32_t b_done = m_compile_buckets[b].archived + m_compile_buckets[b].last_completed;
        weighted_submitted   += static_cast<float>(b_sub) * k_bucket_weights[b];
        weighted_completed   += static_cast<float>(b_done) * k_bucket_weights[b];
    }
    const float compile_overall_fraction = (weighted_submitted > 0.0f) ? std::clamp(weighted_completed / weighted_submitted, 0.0f, 1.0f) : -1.0f;

    // 3. Merge into unified OSD entries (preserve last snapshot during fade-out)
    const bool any_active = show_tracker || show_compile;
    if (any_active)
    {
        m_entry_count    = 0;
        m_overflow_count = 0;

        if (show_tracker)
        {
            const uint32_t tracker_limit = (show_compile_active && tracker.count >= 2) ? 1u : std::min(tracker.count, 2u);
            for (uint32_t i = 0; i < tracker_limit; ++i)
            {
                const ProgressSnapshot& task = tracker.tasks[i];
                OsdEntry& e = m_entries[m_entry_count++];
                e = OsdEntry();
                e.id              = task.id;
                e.step_id         = task.step_id;
                e.title           = task.title;
                e.step            = task.step.empty() ? "Working" : task.step;
                e.detail          = task.detail;
                e.elapsed_seconds = task.elapsed_seconds;
                e.fraction        = task.fraction;
                e.signal          = ImGui::Style::color_accent_1;
                e.finished        = false;
            }
            if (tracker.active_count > tracker_limit)
            {
                m_overflow_count += tracker.active_count - tracker_limit;
            }
        }

        if (show_compile && m_entry_count < 2)
        {
            OsdEntry& e = m_entries[m_entry_count++];
            e = OsdEntry();
            e.id              = k_compile_id_session;
            e.step_id         = 1;
            e.elapsed_seconds = m_compile_busy_sec;

            uint32_t stage_completed = 0;
            uint32_t stage_submitted = 0;

            if (shaders_p1)
            {
                e.title         = "Compiling shaders";
                e.step          = "Fast preview pass (-Od)";
                e.badge         = "PASS 1/2";
                e.signal        = ImGui::Style::color_warning;
                stage_completed = sp1.completed;
                stage_submitted = sp1.submitted;
            }
            else if (pipelines_p1)
            {
                e.title         = "Building pipelines";
                e.step          = "Fast preview PSOs";
                e.badge         = "PASS 1/2";
                e.signal        = ImGui::Style::color_warning;
                stage_completed = pp1.completed;
                stage_submitted = pp1.submitted;
            }
            else if (shaders_p2)
            {
                e.title         = "Optimizing shaders";
                e.step          = "Full optimization pass (-O3)";
                e.badge         = "PASS 2/2";
                e.signal        = ImGui::Style::color_accent_1;
                stage_completed = sp2.completed;
                stage_submitted = sp2.submitted;
            }
            else if (pipelines_p2)
            {
                e.title         = "Optimizing pipelines";
                e.step          = "Rebuilding optimized PSOs";
                e.badge         = "PASS 2/2";
                e.signal        = ImGui::Style::color_accent_1;
                stage_completed = pp2.completed;
                stage_submitted = pp2.submitted;
            }
            else
            {
                e.title    = m_compile_last_was_pass2 ? "Shaders optimized" : "Shaders ready";
                e.step     = "All shaders and pipelines are up to date";
                e.badge    = "READY";
                e.signal   = ImGui::Style::color_ok;
                e.fraction = 1.0f;
                e.finished = true;
            }

            if (!e.finished)
            {
                e.fraction = compile_overall_fraction;
                if (stage_submitted > 0)
                {
                    char detail_buf[32];
                    snprintf(detail_buf, sizeof(detail_buf), "%u / %u", stage_completed, stage_submitted);
                    e.detail = detail_buf;
                }
            }
        }
    }

    // 4. Smooth fade & slide visibility transition
    const float target_alpha = any_active ? 1.0f : 0.0f;
    const float speed        = any_active ? 14.0f : 9.0f;
    m_alpha_anim += (target_alpha - m_alpha_anim) * (1.0f - expf(-speed * static_cast<float>(dt)));
    if (std::fabs(target_alpha - m_alpha_anim) < 0.004f)
    {
        m_alpha_anim = target_alpha;
    }

    const bool visible = (m_alpha_anim > 0.005f) && (m_entry_count > 0);
    SetVisible(visible);
    if (!visible)
    {
        m_entry_count    = 0;
        m_overflow_count = 0;
        m_bars           = {};
    }
}

void ProgressDialog::OnPreBegin()
{
    const float scale = ImGui::EditorUi::scaled(1.0f);
    const float row_h = IM_ROUND(34.0f * scale);
    const float bar_h = row_h * static_cast<float>(std::max(1u, m_entry_count));

    // anchor edge-to-edge along the exact bottom of the Viewport render area
    const Vector2& vp_pos  = Viewport::GetScreenPosition();
    const Vector2& vp_size = Viewport::GetScreenSize();

    float rect_x = ImGui::GetMainViewport()->WorkPos.x;
    float rect_y = ImGui::GetMainViewport()->WorkPos.y;
    float rect_w = ImGui::GetMainViewport()->WorkSize.x;
    float rect_h = ImGui::GetMainViewport()->WorkSize.y;

    if (vp_size.x > 16.0f && vp_size.y > 16.0f)
    {
        rect_x = vp_pos.x;
        rect_y = vp_pos.y;
        rect_w = vp_size.x;
        rect_h = vp_size.y;
    }
    else if (Viewport* viewport = m_editor->GetWidget<Viewport>())
    {
        if (ImGuiWindow* window = viewport->GetWindow())
        {
            rect_x = window->Pos.x;
            rect_y = window->Pos.y;
            rect_w = window->Size.x;
            rect_h = window->Size.y;
        }
    }

    if (Viewport* viewport = m_editor->GetWidget<Viewport>())
    {
        if (ImGuiWindow* window = viewport->GetWindow())
        {
            ImGui::SetNextWindowViewport(window->ViewportId);
        }
    }

    m_bar_x = floorf(rect_x);
    m_bar_w = std::max(120.0f, floorf(rect_w));
    m_bar_h = bar_h;
    m_bar_y = floorf(rect_y + rect_h - m_bar_h);

    ImGui::SetNextWindowPos(ImVec2(m_bar_x, m_bar_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(m_bar_w, m_bar_h), ImGuiCond_Always);

    PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.0f, 1.0f));
}

void ProgressDialog::OnTickVisible()
{
    // keep above the viewport without stealing focus or blocking mouse/keyboard input
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

    if (m_entry_count == 0 || m_alpha_anim <= 0.005f)
    {
        return;
    }

    const float scale   = ImGui::EditorUi::scaled(1.0f);
    const float alpha   = m_alpha_anim;
    const float slide_y = (1.0f - alpha) * 8.0f * scale;
    const float row_h   = m_bar_h / static_cast<float>(m_entry_count);
    const float track_h = std::max(2.5f, IM_ROUND(3.0f * scale));
    const float pad_x   = IM_ROUND(14.0f * scale);
    const float text_h  = ImGui::GetTextLineHeight();
    const double now    = ImGui::GetTime();
    const float dt      = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.1f);
    const float time    = static_cast<float>(now);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 win_min(m_bar_x, m_bar_y + slide_y);
    const ImVec2 win_max(m_bar_x + m_bar_w, m_bar_y + m_bar_h + slide_y);

    // 1. Frosted obsidian-slate bar backdrop spanning edge-to-edge across the viewport bottom
    const ImU32 bg_top = ImGui::EditorUi::color(ImVec4(0.030f, 0.036f, 0.048f, 0.94f * alpha));
    const ImU32 bg_bot = ImGui::EditorUi::color(ImVec4(0.018f, 0.022f, 0.030f, 0.97f * alpha));
    draw->AddRectFilledMultiColor(win_min, win_max, bg_top, bg_top, bg_bot, bg_bot);

    for (uint32_t i = 0; i < m_entry_count; ++i)
    {
        const OsdEntry& e = m_entries[i];
        const ImVec2 row_min(win_min.x, win_min.y + row_h * static_cast<float>(i));
        const ImVec2 row_max(win_max.x, row_min.y + row_h);

        // smooth bar fraction and calm ETA per active task/step
        BarState& bar = m_bars[i];
        if (bar.id != e.id || bar.step_id != e.step_id)
        {
            const float init_f = e.finished ? 1.0f : (e.fraction >= 0.0f ? e.fraction : 0.0f);
            bar.id           = e.id;
            bar.step_id      = e.step_id;
            bar.fraction     = init_f;
            bar.max_fraction = init_f;
            bar.start_time   = now;
            bar.eta_seconds  = -1.0;
        }

        const bool determinate = e.fraction >= 0.0f;
        if (determinate)
        {
            const float clamped_f = std::clamp(e.fraction, 0.0f, 1.0f);
            // keep session progress visually monotonic so late-arriving jobs don't drag the bar backwards
            const float display_target = (e.id == k_compile_id_session) ? std::max(bar.max_fraction, clamped_f) : clamped_f;
            bar.max_fraction = std::max(bar.max_fraction, display_target);
            bar.fraction += (display_target - bar.fraction) * (1.0f - expf(-12.0f * dt));

            if (!e.finished)
            {
                const double step_elapsed = std::max(0.0, now - bar.start_time);
                const float rate_f        = std::clamp(clamped_f, 0.0f, 0.999f);
                if (step_elapsed >= 0.35 && rate_f >= 0.02f)
                {
                    const double raw_eta = std::clamp(step_elapsed * (1.0 - static_cast<double>(rate_f)) / static_cast<double>(rate_f), 0.0, 3599.0);
                    if (bar.eta_seconds < 0.0)
                    {
                        bar.eta_seconds = raw_eta;
                    }
                    else
                    {
                        // count down smoothly with real time and pull gently toward the updated estimate
                        bar.eta_seconds = std::max(0.0, bar.eta_seconds - static_cast<double>(dt));
                        const double pull_speed = (raw_eta < bar.eta_seconds) ? 1.6 : 0.7;
                        bar.eta_seconds += (raw_eta - bar.eta_seconds) * (1.0 - exp(-pull_speed * static_cast<double>(dt)));
                    }
                }
            }
            else
            {
                bar.eta_seconds = 0.0;
            }
        }

        // 2. Full-width progress rail along the top edge of this row + subtle tinted wash
        const ImVec2 track_min(row_min.x, row_min.y);
        const ImVec2 track_max(row_max.x, row_min.y + track_h);
        draw->AddRectFilled(track_min, track_max, ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.08f * alpha)));

        const float glow_h = 8.0f * scale;
        if (determinate)
        {
            const float f = std::clamp(bar.fraction, 0.0f, 1.0f);
            if (f > 0.002f)
            {
                const float fill_x = row_min.x + m_bar_w * f;

                // subtle ambient wash behind the completed portion of the row
                const ImU32 wash_l = ImGui::EditorUi::color(ImGui::EditorUi::alpha(e.signal, 0.022f * alpha));
                const ImU32 wash_r = ImGui::EditorUi::color(ImGui::EditorUi::alpha(e.signal, (e.finished ? 0.065f : 0.095f) * alpha));
                draw->AddRectFilledMultiColor(ImVec2(row_min.x, track_max.y), ImVec2(fill_x, row_max.y), wash_l, wash_r, wash_r, wash_l);

                // soft vertical light spill directly underneath the glowing progress hairline
                const ImU32 spill_hi = ImGui::EditorUi::color(ImGui::EditorUi::alpha(e.signal, 0.22f * alpha));
                const ImU32 spill_lo = ImGui::EditorUi::color(ImGui::EditorUi::alpha(e.signal, 0.0f));
                draw->AddRectFilledMultiColor(ImVec2(row_min.x, track_max.y), ImVec2(fill_x, track_max.y + glow_h), spill_hi, spill_hi, spill_lo, spill_lo);

                // crisp progress hairline
                draw->AddRectFilled(track_min, ImVec2(fill_x, track_max.y), ImGui::EditorUi::color(ImGui::EditorUi::alpha(e.signal, 0.94f * alpha)));

                // bright specular tip highlight at the moving frontier
                if (!e.finished && f < 0.995f)
                {
                    const float tip_w = std::min(22.0f * scale, fill_x - row_min.x);
                    const ImU32 tip_l = ImGui::EditorUi::color(ImGui::EditorUi::alpha(e.signal, 0.0f));
                    const ImU32 tip_r = ImGui::EditorUi::color(ImVec4(0.90f, 0.98f, 1.00f, 0.95f * alpha));
                    draw->AddRectFilledMultiColor(ImVec2(fill_x - tip_w, track_min.y), ImVec2(fill_x, track_max.y), tip_l, tip_r, tip_r, tip_l);
                }
            }
        }
        else
        {
            // sleek bounded glowing sweep across the full viewport width for indeterminate work
            const float t     = fmodf(time * 0.42f + static_cast<float>(i) * 0.25f, 1.0f);
            const float left  = std::max(0.0f, t * 1.28f - 0.28f);
            const float right = std::min(1.0f, t * 1.28f);
            const float sx0   = row_min.x + m_bar_w * left;
            const float sx1   = row_min.x + m_bar_w * right;
            const float mid   = (sx0 + sx1) * 0.5f;

            const ImU32 edge_0 = ImGui::EditorUi::color(ImGui::EditorUi::alpha(e.signal, 0.0f));
            const ImU32 mid_c  = ImGui::EditorUi::color(ImGui::EditorUi::alpha(e.signal, 0.95f * alpha));
            draw->AddRectFilledMultiColor(ImVec2(sx0, track_min.y), ImVec2(mid, track_max.y), edge_0, mid_c, mid_c, edge_0);
            draw->AddRectFilledMultiColor(ImVec2(mid, track_min.y), ImVec2(sx1, track_max.y), mid_c, edge_0, edge_0, mid_c);

            const ImU32 spill_hi = ImGui::EditorUi::color(ImGui::EditorUi::alpha(e.signal, 0.16f * alpha));
            const ImU32 spill_lo = ImGui::EditorUi::color(ImGui::EditorUi::alpha(e.signal, 0.0f));
            draw->AddRectFilledMultiColor(ImVec2(sx0, track_max.y), ImVec2(sx1, track_max.y + glow_h), spill_hi, spill_hi, spill_lo, spill_lo);
        }

        // 3. Build right-aligned overall percentage + ETA readout
        std::string pct_str;
        std::string time_str;
        if (e.finished)
        {
            pct_str = "100%";
        }
        else if (determinate)
        {
            char pct_buf[16];
            snprintf(pct_buf, sizeof(pct_buf), "%.0f%%", std::clamp(bar.fraction, 0.0f, 1.0f) * 100.0f);
            pct_str = pct_buf;
            if (bar.eta_seconds >= 0.0)
            {
                time_str = format_eta(bar.eta_seconds);
            }
            else if (e.elapsed_seconds >= 1.0)
            {
                time_str = format_elapsed(e.elapsed_seconds);
            }
        }
        else if (e.elapsed_seconds >= 0.5)
        {
            time_str = format_elapsed(e.elapsed_seconds);
        }

        // draw right-aligned telemetry block first so left text never collides
        const float text_y = row_min.y + track_h + (row_h - track_h - text_h) * 0.5f;
        float right_cursor = row_max.x - pad_x;
        const float gap    = 10.0f * scale;

        // optional "+N active" pill on the first row when extra background tasks are queued
        if (i == 0 && m_overflow_count > 0)
        {
            char ov_buf[32];
            snprintf(ov_buf, sizeof(ov_buf), "+%u active", m_overflow_count);
            const float ov_tw   = ImGui::CalcTextSize(ov_buf).x;
            const float pill_px = 7.0f * scale;
            const float pill_py = 2.0f * scale;
            const float pill_w  = ov_tw + pill_px * 2.0f;
            const ImVec2 p_min(right_cursor - pill_w, text_y - pill_py);
            const ImVec2 p_max(right_cursor, text_y + text_h + pill_py);
            const float r = (p_max.y - p_min.y) * 0.5f;
            draw->AddRectFilled(p_min, p_max, ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.07f * alpha)), r);
            draw->AddText(ImVec2(p_min.x + pill_px, text_y), ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_text_muted, 0.85f * alpha)), ov_buf);
            right_cursor = p_min.x - gap;
        }

        // optional status badge ("PASS 1/2", "PASS 2/2", "READY")
        if (!e.badge.empty())
        {
            const float badge_tw = ImGui::CalcTextSize(e.badge.c_str()).x;
            const float pill_px  = 7.0f * scale;
            const float pill_py  = 2.0f * scale;
            const float pill_w   = badge_tw + pill_px * 2.0f;
            const ImVec2 p_min(right_cursor - pill_w, text_y - pill_py);
            const ImVec2 p_max(right_cursor, text_y + text_h + pill_py);
            const float r = 3.5f * scale;
            draw->AddRectFilled(p_min, p_max, ImGui::EditorUi::color(ImGui::EditorUi::alpha(e.signal, 0.14f * alpha)), r);
            draw->AddRect(p_min, p_max, ImGui::EditorUi::color(ImGui::EditorUi::alpha(e.signal, 0.36f * alpha)), r, std::max(1.0f, IM_ROUND(scale)));
            draw->AddText(ImVec2(p_min.x + pill_px, text_y), ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::lerp(ImGui::Style::color_text, e.signal, 0.45f), alpha)), e.badge.c_str());
            right_cursor = p_min.x - gap;
        }

        // ETA or elapsed readout ("~12s left", "< 1s left", or "4s")
        if (!time_str.empty())
        {
            const float tw = ImGui::CalcTextSize(time_str.c_str()).x;
            const float tx = right_cursor - tw;
            draw->AddText(ImVec2(tx, text_y), ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_text_muted, 0.88f * alpha)), time_str.c_str());
            right_cursor = tx - gap;
        }

        // overall percentage ("64%", "100%")
        if (!pct_str.empty())
        {
            const float pw   = ImGui::CalcTextSize(pct_str.c_str()).x;
            const float tx   = right_cursor - pw;
            const ImVec4 col = ImGui::EditorUi::alpha(ImGui::Style::lerp(ImGui::Style::color_text, e.signal, 0.30f), alpha);
            draw->AddText(ImVec2(tx, text_y), ImGui::EditorUi::color(col), pct_str.c_str());
            right_cursor = tx - gap;
        }

        const float left_limit = right_cursor - 8.0f * scale;

        // 4. Left-aligned status dot + Title + Step + Detail (cleanly truncated before telemetry)
        const float dot_r = 3.2f * scale;
        const ImVec2 dot_center(row_min.x + pad_x + dot_r, row_min.y + track_h + (row_h - track_h) * 0.5f);
        ImGui::EditorUi::status_dot(draw, dot_center, dot_r, ImGui::EditorUi::alpha(e.signal, alpha), !e.finished);

        float cursor_x = dot_center.x + dot_r + 10.0f * scale;

        // title
        if (!e.title.empty() && cursor_x < left_limit)
        {
            const float title_w = ImGui::CalcTextSize(e.title.c_str()).x;
            const ImU32 title_c = ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.96f * alpha));
            if (cursor_x + title_w <= left_limit)
            {
                draw->AddText(ImVec2(cursor_x, text_y), title_c, e.title.c_str());
                cursor_x += title_w;
            }
            else
            {
                draw_clipped_text(draw, ImVec2(cursor_x, text_y), left_limit, title_c, e.title);
                cursor_x = left_limit;
            }
        }

        // step
        if (!e.step.empty() && cursor_x + 28.0f * scale < left_limit)
        {
            const char* sep   = "  /  ";
            const float sep_w = ImGui::CalcTextSize(sep).x;
            draw->AddText(ImVec2(cursor_x, text_y), ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_text_muted, 0.42f * alpha)), sep);
            cursor_x += sep_w;

            const float step_w = ImGui::CalcTextSize(e.step.c_str()).x;
            const ImU32 step_c = ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_text_muted, 0.88f * alpha));
            if (cursor_x + step_w <= left_limit)
            {
                draw->AddText(ImVec2(cursor_x, text_y), step_c, e.step.c_str());
                cursor_x += step_w;
            }
            else
            {
                draw_clipped_text(draw, ImVec2(cursor_x, text_y), left_limit, step_c, e.step);
                cursor_x = left_limit;
            }
        }

        // optional detail (asset path, entity, or stage item count like "54 / 112")
        if (!e.detail.empty() && cursor_x + 36.0f * scale < left_limit)
        {
            const char* sep   = "  -  ";
            const float sep_w = ImGui::CalcTextSize(sep).x;
            draw->AddText(ImVec2(cursor_x, text_y), ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_text_muted, 0.35f * alpha)), sep);
            cursor_x += sep_w;

            const ImU32 detail_c = ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_text_muted, 0.65f * alpha));
            draw_clipped_text(draw, ImVec2(cursor_x, text_y), left_limit, detail_c, e.detail);
        }
    }
}
