/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "Profiler.h"
#include "../imgui/ImGui_EditorUi.h"
#include "../imgui/ImGui_Properties.h"

#include "profiling/Profiler.h"
#include "resource/ResourceCache.h"
#include "rhi/RHI_Device.h"
#include "memory/Allocator.h"
#include <cmath>

using namespace std;

namespace
{
    constexpr size_t history_limit = 120;
    constexpr float budget_60      = 16.667f;
    constexpr float budget_30      = 33.334f;
    constexpr const char* lane_names[]  = { "CPU / Main thread", "GPU / Graphics", "GPU / Compute", "GPU / Copy", "GPU / Present", "GPU / Other" };
    constexpr const char* lane_shorts[] = { "CPU", "Graphics", "Compute", "Copy", "Present", "Other" };

    ImU32 u32(const ImVec4& color)
    {
        return ImGui::EditorUi::color(color);
    }

    int lane_for(const spartan::TimeBlock& block)
    {
        if (block.GetType() == spartan::TimeBlockType::Cpu) return 0;
        switch (block.GetQueueType())
        {
            case spartan::RHI_Queue_Type::Graphics: return 1;
            case spartan::RHI_Queue_Type::Compute: return 2;
            case spartan::RHI_Queue_Type::Copy: return 3;
            case spartan::RHI_Queue_Type::Present: return 4;
            default: return 5;
        }
    }

    // every lane owns a hue, a scope keeps its exact shade across captures so the eye can track it frame to frame
    ImVec4 lane_tint(const int lane)
    {
        const float hues[] = { 0.40f, 0.60f, 0.10f, 0.78f, 0.92f, 0.66f };
        ImVec4 color;
        ImGui::ColorConvertHSVtoRGB(hues[lane], 0.50f, 0.85f, color.x, color.y, color.z);
        color.w = 1.0f;
        return color;
    }

    ImU32 scope_color(const string& name, int lane, bool dimmed = false)
    {
        uint32_t hash = 2166136261u;
        for (unsigned char c : name) hash = (hash ^ c) * 16777619u;
        const float base[] = { 0.37f, 0.57f, 0.07f, 0.75f, 0.89f, 0.63f };
        const float hue    = base[lane] + static_cast<float>(hash % 45) / 360.0f;
        const float value  = 0.62f + static_cast<float>((hash >> 8) % 12) / 100.0f;
        ImVec4 color       = ImColor::HSV(hue, 0.46f, value);
        color.w            = dimmed ? 0.22f : 1.0f;
        return ImGui::ColorConvertFloat4ToU32(color);
    }

    // green inside a 60 fps frame, amber inside 30, red past it, the same read as the performance overlay
    ImVec4 budget_tint(const float ms)
    {
        if (ms <= budget_60) return ImGui::Style::color_ok;
        if (ms <= budget_30) return ImGui::Style::color_warning;
        return ImGui::Style::color_error;
    }

    string fps_text(const float ms)
    {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.0f fps", ms > 0.0f ? 1000.0f / ms : 0.0f);
        return buffer;
    }
}

Profiler::Profiler(Editor* editor) : Widget(editor)
{
    m_title = "Profiler";
    m_visible = false;
    m_toolbar_order = 1;
    m_toolbar_icon = static_cast<int>(spartan::IconType::Profiler);
    m_size_initial = spartan::math::Vector2(1180, 800);
    m_size_min = spartan::math::Vector2(600, 500);
}

void Profiler::OnTick()
{
    spartan::Profiler::SetVisualized(m_visible && !m_paused);
    if (m_visible && !m_paused) CaptureLatest();
}

void Profiler::CaptureLatest()
{
    const uint64_t revision = spartan::Profiler::GetCaptureRevision();
    if (revision == m_last_revision) return;
    m_last_revision = revision;
    const auto& blocks = spartan::Profiler::GetTimeBlocks();
    if (blocks.empty()) return;

    Capture capture;
    capture.revision = spartan::Profiler::GetCapturedFrameNumber();
    capture.wall = spartan::Profiler::GetCapturedFrameDurationMs();
    capture.incomplete = spartan::Profiler::GetCapturedIncompleteCount();
    capture.dropped = spartan::Profiler::GetCapturedDroppedTimestamps();
    vector<pair<float, float>> cpu_intervals, wait_intervals, gpu_intervals;
    auto covered_ms = [](vector<pair<float, float>>& intervals)
    {
        sort(intervals.begin(), intervals.end());
        float end = -FLT_MAX, covered = 0.0f;
        for (const auto& interval : intervals)
        {
            covered += max(0.0f, interval.second - max(end, interval.first));
            end = max(end, interval.second);
        }
        return covered;
    };
    capture.pacing = spartan::Profiler::GetCapturedPacingTimeMs();
    float gpu_start = FLT_MAX;
    float gpu_end = 0.0f;
    for (const auto& block : blocks)
    {
        if (!block.IsComplete() || !block.GetName() || !isfinite(block.GetDuration()) || !isfinite(block.GetStartMs()) ||
            !isfinite(block.GetEndMs()) || block.GetEndMs() < block.GetStartMs()) continue;
        const int lane = lane_for(block);
        if (!block.IsTimingValid()) { if (lane != 0) ++capture.invalid_gpu; continue; }
        if (lane == 0)
        {
            if (!block.HasParent()) cpu_intervals.emplace_back(block.GetStartMs(), block.GetEndMs());
            if (spartan::Profiler::IsCpuWait(block.GetName())) wait_intervals.emplace_back(block.GetStartMs(), block.GetEndMs());
        }
        else
        {
            capture.has_gpu = true;
            capture.calibrated &= block.IsGpuCalibrated();
            capture.calibration_deviation_ms = max(capture.calibration_deviation_ms, block.GetCalibrationDeviationMs());
            gpu_intervals.emplace_back(block.GetStartMs(), block.GetEndMs());
        }
        capture.scopes.push_back({ block.GetName(), block.GetStartMs(), block.GetEndMs(),
            block.GetDuration(), block.GetTreeDepth(), lane, block.GetId(), block.GetParentId() });
        if (lane != 0)
        {
            gpu_start = min(gpu_start, block.GetStartMs());
            gpu_end = max(gpu_end, block.GetEndMs());
        }
    }
    // Subtract the union of direct children, not their sum (GPU scopes may overlap).
    unordered_map<uint32_t, vector<pair<float, float>>> children;
    unordered_map<uint32_t, const Scope*> by_id;
    for (const auto& scope : capture.scopes) by_id[scope.id] = &scope;
    for (const auto& scope : capture.scopes)
    {
        auto parent = by_id.find(scope.parent_id);
        if (parent == by_id.end() || parent->second->lane != scope.lane) continue;
        const float start = max(scope.start, parent->second->start);
        const float end = min(scope.end, parent->second->end);
        if (end > start) children[scope.parent_id].emplace_back(start, end);
    }
    for (auto& scope : capture.scopes) scope.self = max(0.0f, scope.duration - covered_ms(children[scope.id]));
    capture.cpu = covered_ms(cpu_intervals);
    capture.wait = covered_ms(wait_intervals);
    capture.gpu_covered = covered_ms(gpu_intervals);
    capture.gpu = gpu_start == FLT_MAX ? 0.0f : gpu_end - gpu_start;
    if (capture.scopes.empty()) return;
    m_history.push_back(move(capture));
    if (m_history.size() > history_limit) m_history.pop_front();
    m_selected_capture = static_cast<int>(m_history.size()) - 1;
    m_selected_scope = -1;
}

void Profiler::SelectCapture(int index)
{
    if (index < 0 || index >= static_cast<int>(m_history.size())) return;
    m_selected_capture = index;
    m_selected_scope = -1;
    m_paused = true;
    m_fit = true;
}

void Profiler::DrawHistory()
{
    // the strip reads against the two budgets people actually target, so a spike is judged without reading a number
    ImVec2 min, max;
    bool hovered = false;
    ImDrawList* draw = editor_ui::canvas("##capture_history", ImGui::EditorUi::scaled(64.0f), &min, &max, &hovered);
    const float pad_x = ImGui::EditorUi::scaled(8.0f);
    const float pad_y = ImGui::EditorUi::scaled(6.0f);

    float maximum = budget_30 * 1.25f;
    for (const auto& capture : m_history)
    {
        maximum = std::max(maximum, capture.wall * 1.1f);
    }

    const float left         = min.x + pad_x;
    const float right        = max.x - pad_x - ImGui::CalcTextSize("60 fps").x - ImGui::EditorUi::scaled(8.0f);
    const float baseline     = max.y - pad_y;
    const float graph_height = (max.y - min.y) - pad_y * 2.0f;
    const float bar_width    = (right - left) / static_cast<float>(history_limit);
    auto to_y = [&](const float ms)
    {
        return baseline - ImClamp(ms / maximum, 0.0f, 1.0f) * graph_height;
    };

    const float guides[2]      = { budget_60, budget_30 };
    const char* guide_names[2] = { "60 fps", "30 fps" };
    // when a slow frame stretches the scale the two guides crowd together, then only the 30 fps one is named
    const bool guides_crowded = fabsf(to_y(budget_60) - to_y(budget_30)) < ImGui::GetFontSize() + ImGui::EditorUi::scaled(2.0f);
    for (int i = 0; i < 2; i++)
    {
        const float y = IM_ROUND(to_y(guides[i]));
        draw->AddLine(ImVec2(left, y), ImVec2(right, y), u32(ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.10f)), 1.0f);
        if (guides_crowded && i == 0)
        {
            continue;
        }
        const ImVec2 size = ImGui::CalcTextSize(guide_names[i]);
        draw->AddText(ImVec2(right + ImGui::EditorUi::scaled(6.0f), y - size.y * 0.5f), u32(ImGui::Style::color_text_faint), guide_names[i]);
    }

    int hovered_index = -1;

    // the newest sample sits at the right edge, the way every scrolling graph in the engine reads
    const int offset = static_cast<int>(history_limit) - static_cast<int>(m_history.size());
    if (hovered)
    {
        const int index = static_cast<int>((ImGui::GetIO().MousePos.x - left) / bar_width) - offset;
        hovered_index   = (index >= 0 && index < static_cast<int>(m_history.size())) ? index : -1;
    }

    for (int i = 0; i < static_cast<int>(m_history.size()); ++i)
    {
        const float x0      = left + static_cast<float>(i + offset) * bar_width;
        const float x1      = x0 + std::max(1.0f, bar_width - 1.0f);
        const float top     = std::min(to_y(m_history[i].wall), baseline - 2.0f);
        const bool selected = i == m_selected_capture;
        const ImVec4 tint   = selected ? ImGui::Style::color_accent_1 : budget_tint(m_history[i].wall);
        const float opacity = selected ? 1.0f : (i == hovered_index ? 0.95f : 0.55f);
        if (selected)
        {
            ImGui::EditorUi::draw_glow(draw, ImVec2(x0, top), ImVec2(x1, baseline), tint, 1.0f, ImGui::EditorUi::scaled(5.0f), 0.8f);
        }
        draw->AddRectFilled(ImVec2(x0, top), ImVec2(x1, baseline), u32(ImGui::EditorUi::alpha(tint, opacity)), 1.0f, ImDrawFlags_RoundCornersTop);
    }

    if (hovered_index >= 0)
    {
        const auto& capture = m_history[hovered_index];
        ImGui::BeginTooltip();
        ImGui::Text("Frame %llu", static_cast<unsigned long long>(capture.revision));
        ImGui::TextColored(budget_tint(capture.wall), "%s  %s", editor_ui::format::milliseconds(capture.wall).c_str(), fps_text(capture.wall).c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_muted);
        ImGui::Text("CPU %s   GPU %s", editor_ui::format::milliseconds(capture.cpu).c_str(), editor_ui::format::milliseconds(capture.gpu).c_str());
        ImGui::TextUnformatted("Click to pause on this frame");
        ImGui::PopStyleColor();
        ImGui::EndTooltip();
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) SelectCapture(hovered_index);
    }
}

void Profiler::DrawTimeline(const Capture& capture, float height)
{
    const float dpi = spartan::Window::GetDpiScale();
    const float row = max(22.0f * dpi, ImGui::GetTextLineHeight() + 6.0f * dpi);
    const float header = row + 4.0f * dpi;

    const ImU32 col_canvas = u32(ImGui::Style::color_canvas_deep);
    const ImU32 col_panel  = u32(ImGui::Style::lerp(ImGui::Style::color_canvas_deep, ImGui::Style::color_panel, 0.65f));
    const ImU32 col_grid   = u32(ImGui::Style::color_border);
    const ImU32 col_muted  = u32(ImGui::Style::color_text_muted);
    const ImU32 col_faint  = u32(ImGui::Style::color_text_faint);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::Style::color_canvas_deep);
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::Style::color_border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::EditorUi::scaled(6.0f));
    ImGui::BeginChild("##timeline", ImVec2(0, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollWithMouse);
    const float width = max(1.0f, ImGui::GetContentRegionAvail().x);
    const float labels = min(170.0f * dpi, width * 0.28f);
    const float track_width = max(1.0f, width - labels);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    auto* draw = ImGui::GetWindowDrawList();
    uint32_t depths[6] = {};
    uint32_t min_depths[6] = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };
    int counts[6] = {};
    float extent = max(capture.wall, 0.1f);
    float first = 0.0f;
    for (const auto& scope : capture.scopes)
    {
        depths[scope.lane] = max(depths[scope.lane], scope.depth);
        min_depths[scope.lane] = min(min_depths[scope.lane], scope.depth);
        ++counts[scope.lane];
        extent = max(extent, scope.end);
        first = min(first, scope.start);
    }
    float total_height = header;
    for (int lane = 0; lane < 6; ++lane)
    {
        if ((lane == 0 ? !m_show_cpu : !m_show_gpu) || counts[lane] == 0) continue;
        total_height += header + (m_collapsed[lane] ? 0.0f : row * static_cast<float>(depths[lane] - min_depths[lane] + 1)) + 6.0f * dpi;
    }
    total_height = max(total_height, ImGui::GetContentRegionAvail().y);
    if (m_fit || m_auto_fit)
    {
        m_offset_ms = first;
        m_range_ms = (extent - first) * 1.04f;
        m_fit = false;
    }
    ImGui::InvisibleButton("##timeline_input", ImVec2(width, total_height),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const auto& io = ImGui::GetIO();
    if (hovered) ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, ImGui::GetItemID());
    const bool over_tracks = io.MousePos.x >= origin.x + labels;
    if (hovered && over_tracks && io.MouseWheel != 0.0f)
    {
        const float fraction = ImClamp((io.MousePos.x - origin.x - labels) / track_width, 0.0f, 1.0f);
        const float anchor = m_offset_ms + fraction * m_range_ms;
        m_range_ms = ImClamp(m_range_ms * powf(0.8f, io.MouseWheel), 0.01f, max(1000.0f, extent * 2.0f));
        m_offset_ms = max(first, anchor - fraction * m_range_ms);
        m_auto_fit = false;
    }
    if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle)))
    {
        m_offset_ms = max(first, m_offset_ms - io.MouseDelta.x * m_range_ms / track_width);
        m_auto_fit = false;
    }
    // Wheel over the track names scrolls vertically; wheel over events zooms around the cursor.
    if (hovered && !over_tracks && io.MouseWheel != 0.0f)
        ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseWheel * row * 3.0f);

    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + total_height), col_canvas);
    const float x_begin = origin.x + labels;
    const float x_end = origin.x + width;
    auto to_x = [&](float ms) { return x_begin + (ms - m_offset_ms) / m_range_ms * track_width; };
    float y = origin.y + header;
    int hovered_scope = -1;
    float hovered_width = FLT_MAX;
    for (int lane = 0; lane < 6; ++lane)
    {
        if ((lane == 0 ? !m_show_cpu : !m_show_gpu) || counts[lane] == 0) continue;
        const float lane_height = header + (m_collapsed[lane] ? 0.0f : row * static_cast<float>(depths[lane] - min_depths[lane] + 1));
        const bool header_hovered = hovered && ImGui::IsMouseHoveringRect(ImVec2(origin.x, y), ImVec2(x_begin, y + header));
        draw->AddRectFilled(ImVec2(origin.x, y), ImVec2(x_end, y + header), col_panel);
        draw->AddRectFilled(ImVec2(origin.x, y + header), ImVec2(x_begin, y + lane_height), col_panel);

        // a lit edge in the lane's hue ties the label column to the blocks drawn in that hue
        draw->AddRectFilled(ImVec2(origin.x, y), ImVec2(origin.x + 2.0f * dpi, y + lane_height), u32(lane_tint(lane)));

        const float chevron = ImGui::GetFontSize() * 0.42f;
        const ImVec4 label_tint = header_hovered ? ImGui::Style::color_text : ImGui::Style::lerp(ImGui::Style::color_text_muted, ImGui::Style::color_text, 0.6f);
        ImGui::EditorUi::draw_chevron(draw, ImVec2(origin.x + 10.0f * dpi + chevron * 0.5f, y + header * 0.5f), chevron, m_collapsed[lane] ? 0.0f : 1.0f, label_tint);
        draw->PushClipRect(ImVec2(origin.x, y), ImVec2(x_begin - 2.0f * dpi, y + lane_height), true);
        draw->AddText(ImVec2(origin.x + 16.0f * dpi + chevron, y + (header - ImGui::GetFontSize()) * 0.5f), u32(label_tint), lane_names[lane]);
        draw->PopClipRect();
        char count[32];
        snprintf(count, sizeof(count), "%d scopes", counts[lane]);
        draw->AddText(ImVec2(x_begin + 8.0f * dpi, y + (header - ImGui::GetFontSize()) * 0.5f), col_faint, count);
        if (header_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            m_collapsed[lane] = !m_collapsed[lane];
        draw->AddLine(ImVec2(x_begin, y), ImVec2(x_begin, y + lane_height), col_grid);
        if (!m_collapsed[lane])
        {
            draw->PushClipRect(ImVec2(x_begin, y + header), ImVec2(x_end, y + lane_height), true);
            for (int i = 0; i < static_cast<int>(capture.scopes.size()); ++i)
            {
                const auto& scope = capture.scopes[i];
                if (scope.lane != lane || scope.end < m_offset_ms || scope.start > m_offset_ms + m_range_ms) continue;
                const float x0 = max(x_begin, to_x(scope.start));
                const float x1 = min(x_end, max(x0 + 1.0f, to_x(scope.end)));
                const float y0 = y + header + static_cast<float>(scope.depth - min_depths[lane]) * row + 1.0f;
                const ImVec2 p0(x0, y0), p1(x1, y0 + row - 2.0f);
                const bool matches = m_filter.PassFilter(scope.name.c_str());
                const bool wide    = x1 - x0 > 4.0f * dpi;
                draw->AddRectFilled(p0, ImVec2(wide ? x1 - 1.0f : x1, p1.y), scope_color(scope.name, lane, !matches), wide ? 3.0f * dpi : 0.0f);
                if (i == m_selected_scope)
                {
                    ImGui::EditorUi::draw_glow(draw, p0, p1, ImGui::Style::color_accent_1, 3.0f * dpi, 6.0f * dpi, 0.9f);
                    draw->AddRect(p0, p1, u32(ImGui::Style::color_accent_hi), 3.0f * dpi, 2.0f * dpi);
                }
                if (x1 - x0 > 16.0f * dpi)
                {
                    draw->PushClipRect(ImVec2(x0 + 4.0f * dpi, p0.y), ImVec2(x1 - 3.0f * dpi, p1.y), true);
                    draw->AddText(ImVec2(x0 + 5.0f * dpi, y0 + (row - 2.0f - ImGui::GetFontSize()) * 0.5f), matches ? IM_COL32(14, 16, 20, 235) : col_faint, scope.name.c_str());
                    draw->PopClipRect();
                }
                if (hovered && matches && ImGui::IsMouseHoveringRect(p0, p1) && x1 - x0 < hovered_width)
                {
                    hovered_scope = i;
                    hovered_width = x1 - x0;
                }
            }
            draw->PopClipRect();
        }
        y += lane_height + 6.0f * dpi;
    }

    // Grid is drawn after lane backgrounds, so it remains visible between nested events.
    const float target = m_range_ms / max(1.0f, track_width / (95.0f * dpi));
    const float magnitude = powf(10.0f, floorf(log10f(max(target, 0.0001f))));
    const float normalized = target / magnitude;
    const float step = (normalized <= 1.0f ? 1.0f : normalized <= 2.0f ? 2.0f : normalized <= 5.0f ? 5.0f : 10.0f) * magnitude;
    draw->PushClipRect(ImVec2(x_begin, origin.y), ImVec2(x_end, origin.y + total_height), true);
    for (float tick = ceilf(m_offset_ms / step) * step; tick <= m_offset_ms + m_range_ms; tick += step)
    {
        const float x = to_x(tick);
        draw->AddLine(ImVec2(x, origin.y + header), ImVec2(x, origin.y + total_height), u32(ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.05f)));
    }

    // the frame budget is drawn on the ruler, anything right of it is a frame too slow for 60 fps
    const float budget_x = to_x(budget_60);
    if (budget_x > x_begin && budget_x < x_end)
    {
        draw->AddLine(ImVec2(budget_x, origin.y + header), ImVec2(budget_x, origin.y + total_height), u32(ImGui::EditorUi::alpha(ImGui::Style::color_warning, 0.35f)), 1.0f);
    }

    // Sticky ruler stays visible when scrolling tall CPU stacks.
    const float ruler_y = origin.y + ImGui::GetScrollY();
    draw->AddRectFilled(ImVec2(x_begin, ruler_y), ImVec2(x_end, ruler_y + header), col_panel);
    const bool budget_visible = budget_x > x_begin && budget_x < x_end;
    const float budget_label_end = budget_x + 4.0f * dpi + ImGui::CalcTextSize("60 fps").x;
    for (float tick = ceilf(m_offset_ms / step) * step; tick <= m_offset_ms + m_range_ms; tick += step)
    {
        // every tick shares the precision of the step so the ruler reads 0, 5, 10 and not 0, 5.00, 10.0
        char label[32];
        snprintf(label, sizeof(label), step >= 1.0f ? "%.0f ms" : (step >= 0.1f ? "%.1f ms" : "%.2f ms"), tick);
        const float x       = to_x(tick);
        const float label_w = ImGui::CalcTextSize(label).x + 4.0f * dpi;
        draw->AddLine(ImVec2(x, ruler_y + header - 5.0f * dpi), ImVec2(x, ruler_y + header), col_grid);

        // the budget label wins over a tick label it would overlap
        if (budget_visible && x + label_w > budget_x && x < budget_label_end + 4.0f * dpi)
        {
            continue;
        }
        draw->AddText(ImVec2(x + 4.0f * dpi, ruler_y + (header - ImGui::GetFontSize()) * 0.5f), col_muted, label);
    }
    if (budget_visible)
    {
        draw->AddText(ImVec2(budget_x + 4.0f * dpi, ruler_y + (header - ImGui::GetFontSize()) * 0.5f), u32(ImGui::Style::color_warning), "60 fps");
    }
    draw->PopClipRect();
    draw->AddRectFilled(ImVec2(origin.x, ruler_y), ImVec2(x_begin, ruler_y + header), col_panel);
    ImGui::EditorUi::draw_micro_label(draw, ImVec2(origin.x + 10.0f * dpi, ruler_y), header, "Tracks", ImGui::Style::color_text_faint);
    draw->AddLine(ImVec2(origin.x, ruler_y + header), ImVec2(x_end, ruler_y + header), col_grid);
    if (hovered_scope >= 0 && io.MousePos.y >= ruler_y + header)
    {
        const auto& scope = capture.scopes[hovered_scope];
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(scope.name.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_muted);
        ImGui::Text("%s", lane_names[scope.lane]);
        ImGui::PopStyleColor();
        ImGui::Text("%s, self %s", editor_ui::format::milliseconds(scope.duration).c_str(), editor_ui::format::milliseconds(scope.self).c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_faint);
        ImGui::TextUnformatted("Click to select, double click to zoom to it");
        ImGui::PopStyleColor();
        ImGui::EndTooltip();
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            m_selected_scope = hovered_scope;
            m_inspect_narrow = true;
            m_paused = true;
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            m_range_ms = max(0.01f, (scope.end - scope.start) * 1.2f);
            m_offset_ms = max(0.0f, scope.start - m_range_ms * 0.08f);
            m_auto_fit = false;
        }
    }
    if (!m_show_cpu && !m_show_gpu)
        draw->AddText(ImVec2(x_begin + 8.0f * dpi, ruler_y + header + 8.0f * dpi), col_muted, "CPU and GPU are both hidden, turn one on in the toolbar.");
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

void Profiler::DrawDetails(const Capture& capture, float height)
{
    const float dpi = spartan::Window::GetDpiScale();
    ImGui::BeginChild("##scope_details", ImVec2(0, height));
    const bool wide = ImGui::GetContentRegionAvail().x >= 760.0f * dpi;
    const float table_width = wide ? ImGui::GetContentRegionAvail().x * 0.66f : 0.0f;
    if (!wide)
    {
        if (editor_ui::toolbar::pill("Scopes", !m_inspect_narrow)) m_inspect_narrow = false;
        ImGui::SameLine(0, ImGui::EditorUi::scaled(4.0f));
        if (editor_ui::toolbar::pill("Selection", m_inspect_narrow)) m_inspect_narrow = true;
    }
    const bool show_list = wide || !m_inspect_narrow;
    const bool show_inspector = wide || m_inspect_narrow;
    if (show_list)
    {
        ImGui::BeginChild("##scope_list", ImVec2(table_width, 0.0f));
        vector<int> order;
        for (int i = 0; i < static_cast<int>(capture.scopes.size()); ++i)
        {
            const auto& scope = capture.scopes[i];
            if ((scope.lane == 0 ? m_show_cpu : m_show_gpu) && m_filter.PassFilter(scope.name.c_str())) order.push_back(i);
        }

        // the widest scope scales every bar, so the top of a duration sort is always a full bar
        float longest = 0.0f;
        for (const int index : order)
        {
            longest = max(longest, capture.scopes[index].duration);
        }

        ImGui::EditorUi::push_table_style();
        if (ImGui::BeginTable("##scope_table", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterH, ImVec2(0, 0)))
        {
            ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Track", ImGuiTableColumnFlags_WidthFixed, 95.0f * dpi);
            ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending, 130.0f * dpi);
            ImGui::TableSetupColumn("Start", ImGuiTableColumnFlags_WidthFixed, 80.0f * dpi);
            ImGui::TableSetupColumn("Self", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 80.0f * dpi);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            const auto* specs = ImGui::TableGetSortSpecs();
            const int column = specs && specs->SpecsCount ? specs->Specs[0].ColumnIndex : 2;
            const bool ascending = specs && specs->SpecsCount && specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
            stable_sort(order.begin(), order.end(), [&](int a, int b)
            {
                const auto& left = capture.scopes[ascending ? a : b];
                const auto& right = capture.scopes[ascending ? b : a];
                if (column == 0) return left.name < right.name;
                if (column == 1) return left.lane < right.lane;
                if (column == 4) return left.self < right.self;
                return column == 3 ? left.start < right.start : left.duration < right.duration;
            });
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(order.size()));
            while (clipper.Step())
            for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index)
            {
                const int index = order[row_index];
                const auto& scope = capture.scopes[index];
                ImGui::PushID(index);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Selectable(scope.name.c_str(), m_selected_scope == index, ImGuiSelectableFlags_SpanAllColumns))
                {
                    m_selected_scope = index;
                    m_inspect_narrow = true;
                    m_paused = true;
                }
                ImGui::TableSetColumnIndex(1);
                {
                    const ImVec2 pos = ImGui::GetCursorScreenPos();
                    const float radius = 3.0f * dpi;
                    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos.x + radius + 1.0f, pos.y + ImGui::GetTextLineHeight() * 0.5f), radius, u32(lane_tint(scope.lane)));
                    ImGui::SetCursorScreenPos(ImVec2(pos.x + radius * 2.0f + 6.0f * dpi, pos.y));
                    ImGui::TextColored(ImGui::Style::color_text_muted, "%s", lane_shorts[scope.lane]);
                }
                ImGui::TableSetColumnIndex(2);
                editor_ui::cell_bar(editor_ui::format::milliseconds(scope.duration).c_str(), longest > 0.0f ? scope.duration / longest : 0.0f, lane_tint(scope.lane));
                ImGui::TableSetColumnIndex(3);
                editor_ui::text_right(editor_ui::format::milliseconds(scope.start).c_str(), ImGui::Style::color_text_muted);
                ImGui::TableSetColumnIndex(4);
                editor_ui::text_right(editor_ui::format::milliseconds(scope.self).c_str(), ImGui::Style::color_text_muted);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EditorUi::pop_table_style();
        ImGui::EndChild();
    }
    if (wide) ImGui::SameLine(0, ImGui::EditorUi::scaled(12.0f));
    if (show_inspector)
    {
        ImGui::BeginChild("##scope_inspector", ImVec2(0, 0));
        ImGui::EditorUi::section_rule("Selection");
        if (m_selected_scope >= 0 && m_selected_scope < static_cast<int>(capture.scopes.size()))
        {
            const auto& scope = capture.scopes[m_selected_scope];
            ImGui::PushFont(Editor::font_bold, 0.0f);
            ImGui::TextWrapped("%s", scope.name.c_str());
            ImGui::PopFont();
            ImGui::TextColored(lane_tint(scope.lane), "%s", lane_names[scope.lane]);
            ImGui::Dummy(ImVec2(0, ImGui::EditorUi::scaled(4.0f)));

            // share of the frame is the number that decides whether a scope is worth optimizing, so it leads
            const float share = capture.wall > 0.0f ? scope.duration / capture.wall : 0.0f;
            char share_text[48];
            snprintf(share_text, sizeof(share_text), "%.1f%% of the frame", share * 100.0f);
            editor_ui::stat_strip("##scope_stats", {
                { editor_ui::format::milliseconds(scope.duration), "Duration", lane_tint(scope.lane) },
                { editor_ui::format::milliseconds(scope.self), "Self" },
                { editor_ui::format::milliseconds(scope.start), "Starts at" }
            });
            ImGui::SetItemTooltip("Self is the time not covered by measured child scopes, it includes uninstrumented work and waits.");
            editor_ui::property_meter("Frame share", share, share_text);

            ImGui::Dummy(ImVec2(0, ImGui::EditorUi::scaled(2.0f)));
            if (editor_ui::primary_button("Zoom to scope"))
            {
                m_range_ms = max(0.01f, (scope.end - scope.start) * 1.2f);
                m_offset_ms = max(0.0f, scope.start - m_range_ms * 0.08f);
                m_auto_fit = false;
            }
            ImGui::SameLine(0, ImGui::EditorUi::scaled(4.0f));
            if (editor_ui::toolbar::ghost_button("Copy name")) ImGui::SetClipboardText(scope.name.c_str());
        }
        else
        {
            editor_ui::layout::caption("Click a block in the timeline or a row in the table to see how long it took and how much of the frame it owns.");
            ImGui::Dummy(ImVec2(0, ImGui::EditorUi::scaled(6.0f)));
            const char* hints[][2] =
            {
                { "Wheel",                 "zoom at the cursor" },
                { "Right or middle drag",  "pan" },
                { "Wheel over track names", "scroll" },
                { "Double click a block",  "zoom to it" }
            };
            for (const auto& hint : hints)
            {
                ImGui::TextColored(ImGui::Style::color_text, "%s", hint[0]);
                ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.5f);
                ImGui::TextColored(ImGui::Style::color_text_muted, "%s", hint[1]);
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
}

void Profiler::DrawToolbar()
{
    using namespace editor_ui;
    const float gap = ImGui::EditorUi::scaled(4.0f);

    // live and paused is the state everything else depends on, so it is the first thing and it says which one it is
    const bool live = !m_paused;
    if (toolbar::pill(live ? "Live" : "Paused", live, live ? ImGui::Style::color_ok : ImGui::Style::color_warning, live ? "Sampling frames, click to pause and inspect" : "History is frozen, click to resume live sampling", true, live))
    {
        m_paused = !m_paused;
        if (!m_paused)
        {
            m_selected_capture = static_cast<int>(m_history.size()) - 1;
            m_selected_scope = -1;
        }
    }

    ImGui::SameLine(0, gap);
    const bool recording = spartan::Profiler::IsRecording();
    const bool stopping  = spartan::Profiler::IsRecordingStopping();
    char record_label[64];
    if (stopping)
    {
        snprintf(record_label, sizeof(record_label), "Saving CSV###record");
    }
    else if (recording)
    {
        snprintf(record_label, sizeof(record_label), "Recording %llu###record", static_cast<unsigned long long>(spartan::Profiler::GetRecordedFrameCount()));
    }
    else
    {
        snprintf(record_label, sizeof(record_label), "Record CSV###record");
    }
    ImGui::BeginDisabled(stopping);
    if (toolbar::pill(record_label, recording, ImGui::Style::color_error, "Writes every frame to a CSV, independent of pausing the view", true, recording))
    {
        if (recording) spartan::Profiler::StopRecording();
        else spartan::Profiler::StartRecording();
    }
    ImGui::EndDisabled();

    toolbar::divider();
    if (toolbar::pill("CPU", m_show_cpu, lane_tint(0), "Show the main thread track")) m_show_cpu = !m_show_cpu;
    ImGui::SameLine(0, gap);
    if (toolbar::pill("GPU", m_show_gpu, lane_tint(1), "Show the GPU queue tracks")) m_show_gpu = !m_show_gpu;
    ImGui::SameLine(0, gap);
    bool continuous = spartan::Profiler::IsContinuous();
    if (toolbar::pill("Every frame", continuous, ImGui::Style::color_accent_1, "Sample consecutive frames instead of one every interval, costs more while live. CSV recording always takes every frame."))
    {
        spartan::Profiler::SetContinuous(!continuous);
        continuous = !continuous;
    }

    // how often to sample only matters while sampling live at intervals, otherwise it is noise and goes away
    const bool show_interval = !(m_paused || continuous || recording || stopping);
    const float interval_w   = ImGui::EditorUi::scaled(200.0f);
    const float right_w      = toolbar::ghost_button_width("Fit") + toolbar::ghost_button_width("Clear") + gap + (show_interval ? interval_w + gap : 0.0f);

    toolbar::divider();
    char count[48] = "";
    if (m_selected_capture >= 0 && m_selected_capture < static_cast<int>(m_history.size()))
    {
        const auto& scopes = m_history[m_selected_capture].scopes;
        int matching = 0;
        for (const auto& scope : scopes) matching += m_filter.PassFilter(scope.name.c_str()) ? 1 : 0;
        snprintf(count, sizeof(count), "%d of %d", matching, static_cast<int>(scopes.size()));
    }
    const float search_w = max(ImGui::EditorUi::scaled(120.0f), ImGui::GetContentRegionAvail().x - right_w - ImGui::EditorUi::scaled(12.0f));
    toolbar::search("##scope_filter", "Find scopes", m_filter, search_w, count, count[0] == '0');

    toolbar::align_right(right_w);
    if (show_interval)
    {
        float interval = spartan::Profiler::GetUpdateInterval();
        char text[32];
        snprintf(text, sizeof(text), "Sample every %.2f s", interval);
        if (toolbar::slider("##refresh", &interval, 0.05f, 2.0f, text, interval_w, "How often a frame is sampled while live, drag to change"))
        {
            spartan::Profiler::SetUpdateInterval(interval);
        }
        ImGui::SameLine(0, gap);
    }
    if (toolbar::ghost_button("Fit", "Fit the whole frame into the timeline")) { m_fit = true; m_auto_fit = true; }
    ImGui::SameLine(0, 0);
    if (toolbar::ghost_button("Clear", "Forget the sampled history", ImGui::Style::color_error))
    {
        m_history.clear();
        m_selected_capture = m_selected_scope = -1;
        m_fit = true;
    }
}

void Profiler::OnTickVisible()
{
    const float dpi = spartan::Window::GetDpiScale();
    DrawToolbar();

    const auto& error = spartan::Profiler::GetRecordingError();
    const auto& path = spartan::Profiler::GetRecordingFilePath();
    if (!error.empty())
    {
        editor_ui::layout::note(("CSV could not be written: " + error).c_str(), ImGui::Style::color_error);
    }
    else if (!path.empty() && !spartan::Profiler::IsRecording() && !spartan::Profiler::IsRecordingStopping())
    {
        editor_ui::layout::note(("CSV saved to " + path).c_str(), ImGui::Style::color_ok);
        ImGui::SameLine();
        if (editor_ui::toolbar::ghost_button("Copy path")) ImGui::SetClipboardText(path.c_str());
    }
    ImGui::Dummy(ImVec2(0, ImGui::EditorUi::scaled(2.0f)));

    if (m_selected_capture < 0 || m_history.empty())
    {
        DrawHistory();
        editor_ui::empty_state(m_paused ? "History is empty" : "Waiting for the first sample", m_paused ? "Resume live sampling to capture frame timings." : "A frame is timed as soon as the renderer finishes one.");
        return;
    }

    // the frame at a glance: wall time decides the fps, cpu and gpu say which side to optimize
    const auto& capture = m_history[m_selected_capture];
    const bool gpu_known = capture.has_gpu && capture.calibrated;
    char frame_label[48];
    snprintf(frame_label, sizeof(frame_label), "Frame %llu", static_cast<unsigned long long>(capture.revision));
    editor_ui::stat_strip("##frame_stats", {
        { editor_ui::format::milliseconds(capture.wall) + "  " + fps_text(capture.wall), frame_label, budget_tint(capture.wall) },
        { editor_ui::format::milliseconds(capture.cpu), "CPU elapsed", lane_tint(0) },
        { gpu_known ? editor_ui::format::milliseconds(capture.gpu) : string("n/a"), "GPU span", lane_tint(1) },
        { editor_ui::format::milliseconds(capture.wait), "Waiting", ImGui::Style::color_text_muted },
        { editor_ui::format::milliseconds(capture.pacing), "Pacing", ImGui::Style::color_text_muted }
    });
    ImGui::SetItemTooltip("%s", "CPU elapsed includes the waits shown next to it, it is not core utilization. GPU span runs from the first to the last GPU pass including gaps, it is not hardware utilization.");

    ImGui::Dummy(ImVec2(0, ImGui::EditorUi::scaled(2.0f)));
    DrawHistory();
    ImGui::Dummy(ImVec2(0, ImGui::EditorUi::scaled(2.0f)));

    const float available = ImGui::GetContentRegionAvail().y;
    const float footer = ImGui::GetTextLineHeightWithSpacing() + 10.0f * dpi;
    const float details = max(170.0f * dpi, min(260.0f * dpi, available * 0.38f));
    DrawTimeline(capture, max(100.0f * dpi, available - details - footer - ImGui::GetStyle().ItemSpacing.y * 2.0f));
    DrawDetails(capture, details);
    DrawFooter(capture);
}

void Profiler::DrawFooter(const Capture& capture)
{
    // one line of trust: whether the tracks share a clock, then what was lost, then the memory the frame ran with
    const float radius = ImGui::EditorUi::scaled(3.0f);
    const ImVec2 pos   = ImGui::GetCursorScreenPos();
    ImVec4 tint        = ImGui::Style::color_ok;
    string text;
    char buffer[160];
    if (!capture.has_gpu)
    {
        tint = ImGui::Style::color_text_faint;
        text = "No GPU timings in this frame";
    }
    else if (capture.calibrated)
    {
        snprintf(buffer, sizeof(buffer), "CPU and GPU clocks aligned within %.1f \xC2\xB5s, GPU passes cover %s", capture.calibration_deviation_ms * 1000.0, editor_ui::format::milliseconds(capture.gpu_covered).c_str());
        text = buffer;
    }
    else
    {
        tint = ImGui::Style::color_warning;
        text = "Clocks not calibrated, gaps between CPU and GPU tracks are not meaningful";
    }
    if (capture.incomplete || capture.invalid_gpu || capture.dropped)
    {
        tint = ImGui::Style::color_warning;
        snprintf(buffer, sizeof(buffer), ", %u incomplete, %u invalid GPU scopes, %u timestamps dropped", capture.incomplete, capture.invalid_gpu, capture.dropped);
        text += buffer;
    }

    ImGui::EditorUi::status_dot(ImGui::GetWindowDrawList(), ImVec2(pos.x + radius * 2.0f, pos.y + ImGui::GetTextLineHeight() * 0.5f + ImGui::EditorUi::scaled(4.0f)), radius, tint);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + radius * 4.0f + ImGui::EditorUi::scaled(6.0f), pos.y + ImGui::EditorUi::scaled(4.0f)));
    ImGui::TextColored(ImGui::Style::color_text_muted, "%s", text.c_str());

    char memory[96];
    snprintf(memory, sizeof(memory), "RAM %s of %s    VRAM %s of %s",
        editor_ui::format::bytes(spartan::Allocator::GetMemoryAllocatedMb() * 1048576.0).c_str(), editor_ui::format::bytes(spartan::Allocator::GetMemoryTotalMb() * 1048576.0).c_str(),
        editor_ui::format::bytes(static_cast<double>(spartan::RHI_Device::MemoryGetAllocatedMb()) * 1048576.0).c_str(), editor_ui::format::bytes(static_cast<double>(spartan::RHI_Device::MemoryGetTotalMb()) * 1048576.0).c_str());
    const float memory_w = ImGui::CalcTextSize(memory).x;
    ImGui::SameLine();
    const float available = ImGui::GetContentRegionAvail().x;
    if (available > memory_w + ImGui::EditorUi::scaled(16.0f))
    {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available - memory_w);
        ImGui::TextColored(ImGui::Style::color_text_faint, "%s", memory);
    }
    else
    {
        ImGui::NewLine();
    }
}
