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
#include "Profiler.h"
#include "../imgui/ImGui_EditorUi.h"

#include "profiling/Profiler.h"
#include "resource/ResourceCache.h"
#include "rhi/RHI_Device.h"
#include "memory/Allocator.h"
#include <cmath>

using namespace std;

namespace
{
    constexpr size_t history_limit = 120;
    constexpr const char* lane_names[] = { "CPU / Main thread", "GPU / Graphics", "GPU / Compute", "GPU / Copy", "GPU / Present", "GPU / Other" };
    constexpr ImU32 canvas = IM_COL32(24, 26, 31, 255);
    constexpr ImU32 panel = IM_COL32(32, 35, 41, 255);
    constexpr ImU32 grid = IM_COL32(49, 53, 61, 255);
    constexpr ImU32 muted = IM_COL32(158, 168, 182, 255);
    constexpr ImU32 text = IM_COL32(230, 234, 241, 255);
    constexpr ImU32 accent = IM_COL32(99, 191, 232, 255);

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

    ImU32 scope_color(const string& name, int lane, bool dimmed = false)
    {
        // Stable colors across captures; related tracks retain a recognizable palette.
        uint32_t hash = 2166136261u;
        for (unsigned char c : name) hash = (hash ^ c) * 16777619u;
        const float base[] = { 0.31f, 0.55f, 0.08f, 0.75f, 0.91f, 0.65f };
        const float hue = base[lane] + static_cast<float>(hash % 45) / 360.0f;
        ImVec4 color = ImColor::HSV(hue, 0.48f, dimmed ? 0.26f : 0.72f);
        return ImGui::ColorConvertFloat4ToU32(color);
    }

    bool toggle(const char* label, bool active)
    {
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(49, 88, 109, 255));
        const bool pressed = ImGui::Button(label);
        if (active) ImGui::PopStyleColor();
        return pressed;
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
    const float dpi = spartan::Window::GetDpiScale();
    ImGui::TextDisabled("FRAME HISTORY");
    ImGui::SameLine();
    ImGui::TextDisabled("%zu / %zu samples", m_history.size(), history_limit);
    ImGui::SameLine();
    ImGui::TextDisabled("| click to inspect");
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size(max(1.0f, ImGui::GetContentRegionAvail().x), 72.0f * dpi);
    ImGui::InvisibleButton("##capture_history", size);
    const bool hovered = ImGui::IsItemHovered();
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), canvas);
    float maximum = 33.334f;
    for (const auto& capture : m_history) maximum = max(maximum, capture.wall * 1.1f);
    const float bar_width = size.x / static_cast<float>(history_limit);
    const float baseline = origin.y + size.y - 4.0f * dpi;
    const float graph_height = size.y - 12.0f * dpi;
    const float budget_y = baseline - (16.667f / maximum) * graph_height;
    draw->AddLine(ImVec2(origin.x, budget_y), ImVec2(origin.x + size.x, budget_y), grid);
    draw->AddText(ImVec2(origin.x + 5.0f * dpi, origin.y + 3.0f * dpi), muted, "16.67 ms budget");
    for (int i = 0; i < static_cast<int>(m_history.size()); ++i)
    {
        const float x = origin.x + static_cast<float>(i) * bar_width;
        const float top = baseline - ImClamp(m_history[i].wall / maximum, 0.0f, 1.0f) * graph_height;
        const ImU32 color = i == m_selected_capture ? accent :
            (m_history[i].wall > 16.667f ? IM_COL32(196, 143, 81, 255) : IM_COL32(81, 132, 157, 255));
        draw->AddRectFilled(ImVec2(x + 1.0f, min(top, baseline - 1.0f)), ImVec2(x + bar_width, baseline), color);
        if (i == m_selected_capture)
            draw->AddRect(ImVec2(x, origin.y), ImVec2(x + bar_width, origin.y + size.y), accent);
    }
    if (hovered && !m_history.empty())
    {
        const int index = static_cast<int>((ImGui::GetIO().MousePos.x - origin.x) / bar_width);
        if (index >= 0 && index < static_cast<int>(m_history.size()))
        {
            const auto& capture = m_history[index];
            ImGui::SetTooltip("Sample #%llu\nWall %.3f ms | CPU %.3f ms\nGPU span %.3f ms | Pacing %.3f ms",
                static_cast<unsigned long long>(capture.revision), capture.wall, capture.cpu, capture.gpu, capture.pacing);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) SelectCapture(index);
        }
    }
}

void Profiler::DrawTimeline(const Capture& capture, float height)
{
    const float dpi = spartan::Window::GetDpiScale();
    const float row = max(22.0f * dpi, ImGui::GetTextLineHeight() + 5.0f * dpi);
    const float header = row + 4.0f * dpi;
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

    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + total_height), canvas);
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
        draw->AddRectFilled(ImVec2(origin.x, y), ImVec2(x_end, y + header), panel);
        draw->AddRectFilled(ImVec2(origin.x, y + header), ImVec2(x_begin, y + lane_height), panel);
        char label[80];
        snprintf(label, sizeof(label), "%s %s", m_collapsed[lane] ? ">" : "v", lane_names[lane]);
        draw->PushClipRect(ImVec2(origin.x, y), ImVec2(x_begin - 2.0f * dpi, y + lane_height), true);
        draw->AddText(ImVec2(origin.x + 6.0f * dpi, y + 4.0f * dpi), text, label);
        draw->PopClipRect();
        char count[32];
        snprintf(count, sizeof(count), "%d scopes", counts[lane]);
        draw->AddText(ImVec2(x_begin + 8.0f * dpi, y + 4.0f * dpi), muted, count);
        if (hovered && ImGui::IsMouseHoveringRect(ImVec2(origin.x, y), ImVec2(x_begin, y + header)) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            m_collapsed[lane] = !m_collapsed[lane];
        draw->AddLine(ImVec2(x_begin, y), ImVec2(x_begin, y + lane_height), grid);
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
                draw->AddRectFilled(p0, p1, scope_color(scope.name, lane, !matches));
                if (i == m_selected_scope) draw->AddRect(p0, p1, text, 0.0f, 2.0f * dpi);
                if (x1 - x0 > 12.0f * dpi)
                {
                    draw->PushClipRect(p0, p1, true);
                    draw->AddText(ImVec2(x0 + 4.0f * dpi, y0 + 2.0f * dpi), matches ? text : muted, scope.name.c_str());
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
        draw->AddLine(ImVec2(x, origin.y + header), ImVec2(x, origin.y + total_height), IM_COL32(180, 190, 210, 22));
    }
    // Sticky ruler stays visible when scrolling tall CPU stacks.
    const float ruler_y = origin.y + ImGui::GetScrollY();
    draw->AddRectFilled(ImVec2(x_begin, ruler_y), ImVec2(x_end, ruler_y + header), panel);
    for (float tick = ceilf(m_offset_ms / step) * step; tick <= m_offset_ms + m_range_ms; tick += step)
    {
        char label[32];
        snprintf(label, sizeof(label), "%.2f ms", tick);
        draw->AddText(ImVec2(to_x(tick) + 3.0f * dpi, ruler_y + 4.0f * dpi), muted, label);
    }
    draw->PopClipRect();
    draw->AddRectFilled(ImVec2(origin.x, ruler_y), ImVec2(x_begin, ruler_y + header), panel);
    draw->AddText(ImVec2(origin.x + 6.0f * dpi, ruler_y + 4.0f * dpi), muted, "TRACKS");
    if (hovered_scope >= 0 && io.MousePos.y >= ruler_y + header)
    {
        const auto& scope = capture.scopes[hovered_scope];
        ImGui::SetTooltip("%s\n%s\nDuration %.4f ms\nStart %.4f ms | End %.4f ms\nClick to select; double-click to focus",
            scope.name.c_str(), lane_names[scope.lane], scope.duration, scope.start, scope.end);
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
        draw->AddText(ImVec2(x_begin + 8.0f * dpi, ruler_y + header + 8.0f * dpi), muted, "Enable CPU or GPU to show tracks.");
    ImGui::EndChild();
}

void Profiler::DrawDetails(const Capture& capture, float height)
{
    const float dpi = spartan::Window::GetDpiScale();
    ImGui::BeginChild("##scope_details", ImVec2(0, height), ImGuiChildFlags_Borders);
    const bool wide = ImGui::GetContentRegionAvail().x >= 760.0f * dpi;
    const float table_width = wide ? ImGui::GetContentRegionAvail().x * 0.63f : 0.0f;
    if (!wide)
    {
        if (toggle("Scopes", !m_inspect_narrow)) m_inspect_narrow = false;
        ImGui::SameLine();
        if (toggle("Selection", m_inspect_narrow)) m_inspect_narrow = true;
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
        ImGui::TextDisabled("SCOPES / %zu matching", order.size());
        if (ImGui::BeginTable("##scope_table", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_BordersInnerV, ImVec2(0, 0)))
        {
            ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Track", ImGuiTableColumnFlags_WidthFixed, 95.0f * dpi);
            ImGui::TableSetupColumn("Duration (ms)", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending, 110.0f * dpi);
            ImGui::TableSetupColumn("Start (ms)", ImGuiTableColumnFlags_WidthFixed, 90.0f * dpi);
            ImGui::TableSetupColumn("Self (ms)", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 90.0f * dpi);
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
                ImGui::TextUnformatted(scope.lane == 0 ? "CPU" : lane_names[scope.lane] + 6);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%.4f", scope.duration);
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%.4f", scope.start);
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%.4f", scope.self);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (order.empty()) ImGui::TextDisabled("No scopes match the current filter.");
        ImGui::EndChild();
    }
    if (wide) ImGui::SameLine();
    if (show_inspector)
    {
        ImGui::BeginChild("##scope_inspector", ImVec2(0, 0));
        ImGui::TextDisabled("SELECTION");
        if (m_selected_scope >= 0 && m_selected_scope < static_cast<int>(capture.scopes.size()))
        {
            const auto& scope = capture.scopes[m_selected_scope];
            ImGui::TextWrapped("%s", scope.name.c_str());
            ImGui::TextDisabled("%s", lane_names[scope.lane]);
            ImGui::Separator();
            ImGui::Text("Duration   %.4f ms", scope.duration);
            ImGui::Text("Self       %.4f ms", scope.self);
            ImGui::SetItemTooltip("Elapsed time excluding measured child scopes; includes uninstrumented work and waits.");
            ImGui::Text("Start      %.4f ms", scope.start);
            ImGui::Text("End        %.4f ms", scope.end);
            if (capture.wall > 0.0f) ImGui::Text("Wall share %.2f%%", scope.duration / capture.wall * 100.0f);
            if (ImGui::Button("Focus scope"))
            {
                m_range_ms = max(0.01f, (scope.end - scope.start) * 1.2f);
                m_offset_ms = max(0.0f, scope.start - m_range_ms * 0.08f);
                m_auto_fit = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Copy name")) ImGui::SetClipboardText(scope.name.c_str());
        }
        else
        {
            ImGui::TextWrapped("Select a timeline event or a scope row to inspect its timing.");
            ImGui::Spacing();
            ImGui::TextDisabled("Wheel: zoom at cursor");
            ImGui::TextDisabled("Right / middle drag: pan");
            ImGui::TextDisabled("Wheel on track names: scroll");
            ImGui::TextDisabled("Double-click event: focus");
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
}

void Profiler::OnTickVisible()
{
    const float dpi = spartan::Window::GetDpiScale();
    if (toggle(m_paused ? "Resume live" : "Pause", m_paused))
    {
        m_paused = !m_paused;
        if (!m_paused)
        {
            m_selected_capture = static_cast<int>(m_history.size()) - 1;
            m_selected_scope = -1;
        }
    }
    ImGui::SetItemTooltip("%s", "Pause sampled history for inspection; CSV recording runs independently");
    ImGui::SameLine();
    const bool recording = spartan::Profiler::IsRecording();
    ImGui::BeginDisabled(spartan::Profiler::IsRecordingStopping());
    if (toggle(recording ? "Stop CSV" : "Record CSV", recording))
    {
        if (recording) spartan::Profiler::StopRecording();
        else spartan::Profiler::StartRecording();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Fit")) { m_fit = true; m_auto_fit = true; }
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
    {
        m_history.clear();
        m_selected_capture = m_selected_scope = -1;
        m_fit = true;
    }
    ImGui::SameLine();
    if (toggle("CPU", m_show_cpu)) m_show_cpu = !m_show_cpu;
    ImGui::SameLine();
    if (toggle("GPU", m_show_gpu)) m_show_gpu = !m_show_gpu;
    ImGui::SameLine();
    bool continuous = spartan::Profiler::IsContinuous();
    if (ImGui::Checkbox("Every frame", &continuous)) spartan::Profiler::SetContinuous(continuous);
    ImGui::SetItemTooltip("Consecutive CPU/GPU samples while live. More profiling overhead; CSV always records every frame.");
    ImGui::SameLine();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(m_paused ? muted : accent), m_paused ? "PAUSED" : "LIVE");

    ImGui::SetNextItemWidth(max(100.0f * dpi, ImGui::GetContentRegionAvail().x - 205.0f * dpi));
    ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_F, ImGuiInputFlags_Tooltip);
    if (ImGui::InputTextWithHint("##scope_filter", "Find scopes...", m_filter.InputBuf, IM_ARRAYSIZE(m_filter.InputBuf), ImGuiInputTextFlags_EscapeClearsAll)) m_filter.Build();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    float interval = spartan::Profiler::GetUpdateInterval();
    ImGui::BeginDisabled(m_paused || continuous || recording || spartan::Profiler::IsRecordingStopping());
    if (ImGui::SliderFloat("##refresh", &interval, 0.05f, 2.0f, "Sample every %.2f s")) spartan::Profiler::SetUpdateInterval(interval);
    ImGui::EndDisabled();

    const auto& error = spartan::Profiler::GetRecordingError();
    const auto& path = spartan::Profiler::GetRecordingFilePath();
    if (!error.empty()) ImGui::TextWrapped("CSV error: %s", error.c_str());
    else if (!path.empty())
    {
        ImGui::TextDisabled(spartan::Profiler::IsRecordingStopping() ? "Saving CSV..." : recording ? "Recording CSV" : "CSV saved");
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy CSV path")) ImGui::SetClipboardText(path.c_str());
        ImGui::SetItemTooltip("%s", path.c_str());
        if (recording) { ImGui::SameLine(); ImGui::TextDisabled("%llu frames", static_cast<unsigned long long>(spartan::Profiler::GetRecordedFrameCount())); }
    }
    DrawHistory();
    if (m_selected_capture < 0 || m_history.empty())
    {
        ImGui::Spacing();
        ImGui::TextDisabled(m_paused ? "History is empty. Resume live sampling to capture timings." : "Waiting for a completed timing sample...");
        return;
    }
    const auto& capture = m_history[m_selected_capture];
    char gpu_summary[64] = "GPU span unavailable";
    if (capture.has_gpu && capture.calibrated) snprintf(gpu_summary, sizeof(gpu_summary), "GPU span %.2f ms", capture.gpu);
    ImGui::Text("Frame #%llu   Wall %.2f ms   CPU elapsed %.2f ms   Wait %.2f ms   %s",
        static_cast<unsigned long long>(capture.revision), capture.wall, capture.cpu, capture.wait, gpu_summary);
    ImGui::SetItemTooltip("%s", "CPU elapsed includes instrumented waits (shown separately), not CPU core utilization. GPU span includes gaps between measured passes; it is not hardware utilization.");
    const float available = ImGui::GetContentRegionAvail().y;
    const float footer = ImGui::GetTextLineHeightWithSpacing() * 2.0f + 8.0f * dpi;
    const float details = max(150.0f * dpi, min(245.0f * dpi, available * 0.37f));
    DrawTimeline(capture, max(100.0f * dpi, available - details - footer - ImGui::GetStyle().ItemSpacing.y * 2.0f));
    DrawDetails(capture, details);
    if (!capture.has_gpu)
        ImGui::TextDisabled("GPU timing unavailable | Incomplete scopes %u | Invalid GPU scopes %u | Dropped timestamps %u",
            capture.incomplete, capture.invalid_gpu, capture.dropped);
    else if (capture.calibrated)
        ImGui::TextDisabled("CPU/GPU clocks aligned | Calibration uncertainty ~%.1f us | GPU scope coverage %.2f ms | Pacing %.2f ms",
            capture.calibration_deviation_ms * 1000.0, capture.gpu_covered, capture.pacing);
    else
        ImGui::TextDisabled("UNCALIBRATED: tracks have independent origins; cross-track gaps and aggregate GPU times are unavailable.");
    if (capture.has_gpu && (capture.incomplete || capture.invalid_gpu || capture.dropped))
        ImGui::TextDisabled("Incomplete scopes %u | Invalid GPU scopes %u | Dropped timestamps %u",
            capture.incomplete, capture.invalid_gpu, capture.dropped);
    ImGui::TextDisabled("Live memory   RAM %.0f / %.0f MB    VRAM %.0f / %.0f MB",
        spartan::Allocator::GetMemoryAllocatedMb(), spartan::Allocator::GetMemoryTotalMb(),
        static_cast<double>(spartan::RHI_Device::MemoryGetAllocatedMb()), static_cast<double>(spartan::RHI_Device::MemoryGetTotalMb()));
}
