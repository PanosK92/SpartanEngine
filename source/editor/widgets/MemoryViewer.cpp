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

//= INCLUDES ========================
#include "pch.h"
#include "MemoryViewer.h"
#include "../imgui/ImGui_EditorUi.h"
#include "../imgui/ImGui_Extension.h"
#include "memory/GpuMemory.h"
#include "memory/Allocator.h"
#include "rhi/RHI_Device.h"
#include "resource/ResourceCache.h"
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan;
using namespace spartan::math;
//============================

namespace
{
    constexpr ImU32 color_hole   = IM_COL32(110, 42, 48, 255);
    constexpr ImU32 color_unused = IM_COL32(22, 26, 34, 255);
    constexpr ImU32 color_grid   = IM_COL32(8, 10, 14, 255);

    bool toggle_button(const char* label, const bool active)
    {
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        const bool clicked = ImGui::Button(label);
        if (active)
        {
            ImGui::PopStyleColor();
        }
        return clicked;
    }

    ImU32 kind_color(GpuMemoryKind kind)
    {
        switch (kind)
        {
            case GpuMemoryKind::Texture:               return IM_COL32(70, 168, 255, 255);
            case GpuMemoryKind::Vertex:                return IM_COL32(72, 196, 118, 255);
            case GpuMemoryKind::Index:                 return IM_COL32(156, 214, 72, 255);
            case GpuMemoryKind::Instance:              return IM_COL32(64, 210, 176, 255);
            case GpuMemoryKind::Storage:               return IM_COL32(255, 154, 58, 255);
            case GpuMemoryKind::Constant:              return IM_COL32(255, 214, 72, 255);
            case GpuMemoryKind::Upload:                return IM_COL32(154, 154, 210, 255);
            case GpuMemoryKind::Readback:              return IM_COL32(210, 150, 150, 255);
            case GpuMemoryKind::ShaderBindingTable:    return IM_COL32(255, 96, 176, 255);
            case GpuMemoryKind::AccelerationStructure: return IM_COL32(186, 82, 255, 255);
            default:                                   return IM_COL32(188, 188, 188, 255);
        }
    }

    ImU32 tag_color(MemoryTag tag)
    {
        switch (tag)
        {
            case MemoryTag::Rendering: return IM_COL32(70, 168, 255, 255);
            case MemoryTag::Physics:   return IM_COL32(255, 154, 58, 255);
            case MemoryTag::Audio:     return IM_COL32(72, 196, 118, 255);
            case MemoryTag::Scripting: return IM_COL32(255, 214, 72, 255);
            case MemoryTag::Resources: return IM_COL32(186, 82, 255, 255);
            case MemoryTag::World:     return IM_COL32(64, 210, 176, 255);
            case MemoryTag::Ui:        return IM_COL32(255, 96, 176, 255);
            default:                   return IM_COL32(154, 154, 170, 255);
        }
    }

    void format_bytes(char* buf, size_t buf_size, uint64_t bytes)
    {
        if (bytes >= 1024ull * 1024ull * 1024ull)
        {
            snprintf(buf, buf_size, "%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
        }
        else if (bytes >= 1024ull * 1024ull)
        {
            snprintf(buf, buf_size, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        }
        else if (bytes >= 1024ull)
        {
            snprintf(buf, buf_size, "%.1f KB", static_cast<double>(bytes) / 1024.0);
        }
        else
        {
            snprintf(buf, buf_size, "%llu B", static_cast<unsigned long long>(bytes));
        }
    }

    struct map_range
    {
        uint64_t start           = 0;
        uint64_t size            = 0;
        const GpuMemoryBlock* block = nullptr;
        bool is_hole             = false;
    };

    void build_ranges(
        const vector<GpuMemoryBlock>& blocks,
        uint64_t vram_total_bytes,
        vector<map_range>& ranges,
        uint64_t& out_used,
        uint64_t& out_holes,
        uint64_t& out_unused,
        uint64_t& out_largest_free
    )
    {
        ranges.clear();
        out_used         = 0;
        out_holes        = 0;
        out_unused       = 0;
        out_largest_free = 0;

        struct heap_info
        {
            uint64_t id   = 0;
            uint64_t size = 0;
            vector<const GpuMemoryBlock*> blocks;
        };

        unordered_map<uint64_t, heap_info> heaps;
        heaps.reserve(blocks.size());
        for (const GpuMemoryBlock& block : blocks)
        {
            heap_info& heap = heaps[block.heap_id];
            heap.id   = block.heap_id;
            heap.size = max(heap.size, block.heap_size);
            heap.size = max(heap.size, block.offset + block.size);
            heap.blocks.push_back(&block);
            out_used += block.size;
        }

        vector<heap_info*> ordered;
        ordered.reserve(heaps.size());
        for (auto& pair : heaps)
        {
            ordered.push_back(&pair.second);
        }
        sort(ordered.begin(), ordered.end(), [](const heap_info* a, const heap_info* b)
        {
            return a->id < b->id;
        });

        uint64_t linear = 0;
        uint64_t heap_bytes = 0;
        for (heap_info* heap : ordered)
        {
            sort(heap->blocks.begin(), heap->blocks.end(), [](const GpuMemoryBlock* a, const GpuMemoryBlock* b)
            {
                if (a->offset != b->offset)
                {
                    return a->offset < b->offset;
                }
                return a->size > b->size;
            });

            uint64_t cursor = 0;
            for (const GpuMemoryBlock* block : heap->blocks)
            {
                if (block->offset > cursor)
                {
                    const uint64_t hole = block->offset - cursor;
                    ranges.push_back({ linear + cursor, hole, nullptr, true });
                    out_holes += hole;
                    out_largest_free = max(out_largest_free, hole);
                }

                ranges.push_back({ linear + block->offset, block->size, block, false });
                cursor = max(cursor, block->offset + block->size);
            }

            if (heap->size > cursor)
            {
                const uint64_t hole = heap->size - cursor;
                ranges.push_back({ linear + cursor, hole, nullptr, true });
                out_holes += hole;
                out_largest_free = max(out_largest_free, hole);
            }

            linear     += heap->size;
            heap_bytes += heap->size;
        }

        if (vram_total_bytes > heap_bytes)
        {
            out_unused = vram_total_bytes - heap_bytes;
            ranges.push_back({ linear, out_unused, nullptr, false });
            out_largest_free = max(out_largest_free, out_unused);
        }
    }

    const map_range* range_at(const vector<map_range>& ranges, uint64_t offset)
    {
        if (ranges.empty())
        {
            return nullptr;
        }

        auto it = upper_bound(
            ranges.begin(),
            ranges.end(),
            offset,
            [](uint64_t value, const map_range& range)
            {
                return value < range.start;
            }
        );

        if (it == ranges.begin())
        {
            return nullptr;
        }

        --it;
        if (offset >= it->start && offset < it->start + it->size)
        {
            return &(*it);
        }

        return nullptr;
    }

    void draw_legend_swatch(ImU32 color, const char* label, uint64_t bytes)
    {
        const float dpi = Window::GetDpiScale();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        const float size = 12.0f * dpi;
        ImGui::GetWindowDrawList()->AddRectFilled(
            pos,
            ImVec2(pos.x + size, pos.y + size),
            color,
            2.0f * dpi
        );
        ImGui::Dummy(ImVec2(size, size));
        ImGui::SameLine();
        char bytes_text[32];
        format_bytes(bytes_text, sizeof(bytes_text), bytes);
        ImGui::Text("%s  %s", label, bytes_text);
    }

    void draw_block_map(
        const vector<map_range>& ranges,
        uint64_t total_bytes,
        void*& selected_resource
    )
    {
        if (total_bytes == 0)
        {
            ImGui::TextDisabled("no gpu allocations");
            return;
        }

        const float dpi        = Window::GetDpiScale();
        const float cell       = 10.0f * dpi;
        const float gap        = 1.0f * dpi;
        const float avail_x    = ImGui::GetContentRegionAvail().x;
        const int columns      = max(8, static_cast<int>(avail_x / (cell + gap)));
        const int max_rows     = 28;
        const int max_cells    = columns * max_rows;
        const uint64_t cell_bytes = (std::max)(
            static_cast<uint64_t>(1),
            (total_bytes + static_cast<uint64_t>(max_cells) - 1) / static_cast<uint64_t>(max_cells)
        );
        const int cell_count = static_cast<int>((total_bytes + cell_bytes - 1) / cell_bytes);
        const int rows       = max(1, (cell_count + columns - 1) / columns);
        const float map_w    = columns * (cell + gap) - gap;
        const float map_h    = rows * (cell + gap) - gap;

        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##memory_map", ImVec2(map_w, map_h));
        const bool map_hovered = ImGui::IsItemHovered();
        const bool map_clicked = ImGui::IsItemClicked();
        ImDrawList* draw_list  = ImGui::GetWindowDrawList();

        draw_list->AddRectFilled(
            origin,
            ImVec2(origin.x + map_w, origin.y + map_h),
            color_grid,
            4.0f * dpi
        );

        int hover_cell = -1;
        if (map_hovered)
        {
            ImVec2 mouse = ImGui::GetMousePos();
            int col = static_cast<int>((mouse.x - origin.x) / (cell + gap));
            int row = static_cast<int>((mouse.y - origin.y) / (cell + gap));
            if (col >= 0 && col < columns && row >= 0 && row < rows)
            {
                hover_cell = row * columns + col;
            }
        }

        for (int i = 0; i < cell_count; i++)
        {
            const int col = i % columns;
            const int row = i / columns;
            ImVec2 p0(
                origin.x + col * (cell + gap),
                origin.y + row * (cell + gap)
            );
            ImVec2 p1(p0.x + cell, p0.y + cell);

            const uint64_t offset = static_cast<uint64_t>(i) * cell_bytes;
            const map_range* range = range_at(ranges, offset);

            ImU32 color = color_unused;
            bool selected = false;
            if (range)
            {
                if (range->block)
                {
                    color = kind_color(range->block->kind);
                    selected = range->block->resource == selected_resource;
                }
                else if (range->is_hole)
                {
                    color = color_hole;
                }
            }

            if (i == hover_cell)
            {
                color = IM_COL32(
                    min(255, (int)((color >> 0) & 255) + 40),
                    min(255, (int)((color >> 8) & 255) + 40),
                    min(255, (int)((color >> 16) & 255) + 40),
                    255
                );
            }

            draw_list->AddRectFilled(p0, p1, color, 1.5f * dpi);
            if (selected)
            {
                draw_list->AddRect(p0, p1, IM_COL32(255, 255, 255, 255), 1.5f * dpi, 1.5f * dpi);
            }
        }

        if (hover_cell >= 0 && hover_cell < cell_count)
        {
            const uint64_t offset = static_cast<uint64_t>(hover_cell) * cell_bytes;
            const map_range* range = range_at(ranges, offset);
            if (range)
            {
                if (map_clicked)
                {
                    selected_resource = range->block ? range->block->resource : nullptr;
                }

                ImGui::BeginTooltip();
                if (range->block)
                {
                    char size_text[32];
                    format_bytes(size_text, sizeof(size_text), range->block->size);
                    ImGui::TextUnformatted(range->block->name[0] ? range->block->name : "(unnamed)");
                    ImGui::Text("%s  %s", GpuMemory::GetKindName(range->block->kind), size_text);
                }
                else if (range->is_hole)
                {
                    char size_text[32];
                    format_bytes(size_text, sizeof(size_text), range->size);
                    ImGui::Text("hole  %s", size_text);
                    ImGui::TextDisabled("free range inside a heap");
                }
                else
                {
                    char size_text[32];
                    format_bytes(size_text, sizeof(size_text), range->size);
                    ImGui::Text("unused  %s", size_text);
                    ImGui::TextDisabled("vram not yet given to a heap");
                }
                ImGui::EndTooltip();
            }
        }

        char cell_text[64];
        format_bytes(cell_text, sizeof(cell_text), cell_bytes);
        ImGui::TextDisabled("each square is %s", cell_text);
    }
}

MemoryViewer::MemoryViewer(Editor* editor) : Widget(editor)
{
    m_title         = "Memory";
    m_visible       = false;
    m_toolbar_order = 8;
    m_toolbar_icon  = static_cast<int>(IconType::Hybrid);
    m_size_initial  = Vector2(920, 680);
    m_size_min      = Vector2(520, 420);
}

void MemoryViewer::OnTickVisible()
{
    if (toggle_button("GPU", m_show_gpu))
    {
        m_show_gpu = true;
    }
    ImGui::SameLine();
    if (toggle_button("CPU", !m_show_gpu))
    {
        m_show_gpu = false;
    }
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    if (toggle_button("Freeze", m_frozen))
    {
        if (!m_frozen)
        {
            GpuMemory::GetBlocks(m_frozen_blocks);
        }
        m_frozen = !m_frozen;
    }
    ImGuiSp::tooltip(m_frozen ? "resume live capture" : "freeze the current map");

    ImGui::Separator();

    if (m_show_gpu)
    {
        vector<GpuMemoryBlock> blocks;
        if (m_frozen)
        {
            blocks = m_frozen_blocks;
        }
        else
        {
            GpuMemory::GetBlocks(blocks);
        }

        const uint64_t vram_total = RHI_Device::MemoryGetTotalMb() * 1024ull * 1024ull;
        const uint64_t vram_used_driver = RHI_Device::MemoryGetAllocatedMb() * 1024ull * 1024ull;

        vector<map_range> ranges;
        uint64_t used = 0;
        uint64_t holes = 0;
        uint64_t unused = 0;
        uint64_t largest_free = 0;
        build_ranges(blocks, vram_total, ranges, used, holes, unused, largest_free);

        const uint64_t total_free = holes + unused;
        const float fragmentation = (total_free > 0)
            ? (1.0f - static_cast<float>(largest_free) / static_cast<float>(total_free))
            : 0.0f;

        char used_text[32];
        char total_text[32];
        char holes_text[32];
        char unused_text[32];
        char largest_text[32];
        format_bytes(used_text, sizeof(used_text), used);
        format_bytes(total_text, sizeof(total_text), vram_total);
        format_bytes(holes_text, sizeof(holes_text), holes);
        format_bytes(unused_text, sizeof(unused_text), unused);
        format_bytes(largest_text, sizeof(largest_text), largest_free);

        ImGui::Text("tracked %s / %s", used_text, total_text);
        ImGui::SameLine();
        ImGui::TextDisabled("(%u allocs)", static_cast<unsigned>(blocks.size()));
        ImGui::Text("holes %s", holes_text);
        ImGui::SameLine();
        ImGui::Text("unused %s", unused_text);
        ImGui::SameLine();
        ImGui::Text("largest free %s", largest_text);
        ImGui::SameLine();
        ImGui::Text("fragmentation %.0f%%", fragmentation * 100.0f);
        if (vram_used_driver > 0)
        {
            char driver_text[32];
            format_bytes(driver_text, sizeof(driver_text), vram_used_driver);
            ImGui::SameLine();
            ImGui::TextDisabled("driver %s", driver_text);
        }

        ImGui::Dummy(ImVec2(0.0f, 4.0f * Window::GetDpiScale()));

        const uint64_t map_bytes = max(vram_total, used + holes + unused);
        draw_block_map(ranges, map_bytes, m_selected_resource);

        ImGui::Separator();
        ImGui::TextDisabled("legend");

        array<uint64_t, static_cast<size_t>(GpuMemoryKind::Count)> by_kind = {};
        for (const GpuMemoryBlock& block : blocks)
        {
            by_kind[static_cast<size_t>(block.kind)] += block.size;
        }

        int legend_on_line = 0;
        for (uint8_t i = 0; i < static_cast<uint8_t>(GpuMemoryKind::Count); i++)
        {
            if (by_kind[i] == 0)
            {
                continue;
            }
            if (legend_on_line > 0)
            {
                ImGui::SameLine();
            }
            draw_legend_swatch(
                kind_color(static_cast<GpuMemoryKind>(i)),
                GpuMemory::GetKindName(static_cast<GpuMemoryKind>(i)),
                by_kind[i]
            );
            legend_on_line++;
            if (legend_on_line >= 4)
            {
                legend_on_line = 0;
            }
        }
        if (holes > 0)
        {
            if (legend_on_line > 0)
            {
                ImGui::SameLine();
            }
            draw_legend_swatch(color_hole, "Hole", holes);
        }
        if (unused > 0)
        {
            ImGui::SameLine();
            draw_legend_swatch(color_unused, "Unused", unused);
        }

        ImGui::Separator();
        if (ImGui::BeginTable(
            "gpu_allocs",
            3,
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_SizingStretchProp,
            ImVec2(0.0f, ImGui::GetContentRegionAvail().y)
        ))
        {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90.0f * Window::GetDpiScale());
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80.0f * Window::GetDpiScale());
            ImGui::TableHeadersRow();

            vector<const GpuMemoryBlock*> sorted;
            sorted.reserve(blocks.size());
            for (const GpuMemoryBlock& block : blocks)
            {
                sorted.push_back(&block);
            }
            sort(sorted.begin(), sorted.end(), [](const GpuMemoryBlock* a, const GpuMemoryBlock* b)
            {
                return a->size > b->size;
            });

            const int show_count = min(static_cast<int>(sorted.size()), 256);
            for (int i = 0; i < show_count; i++)
            {
                const GpuMemoryBlock* block = sorted[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const bool selected = block->resource == m_selected_resource;
                if (ImGui::Selectable(
                    block->name[0] ? block->name : "(unnamed)",
                    selected,
                    ImGuiSelectableFlags_SpanAllColumns
                ))
                {
                    m_selected_resource = block->resource;
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(GpuMemory::GetKindName(block->kind));
                ImGui::TableNextColumn();
                char size_text[32];
                format_bytes(size_text, sizeof(size_text), block->size);
                ImGui::TextUnformatted(size_text);
            }
            ImGui::EndTable();
        }
    }
    else
    {
        const float allocated_mb = Allocator::GetMemoryAllocatedMb();
        const float process_mb   = Allocator::GetMemoryProcessUsedMb();
        const float available_mb = Allocator::GetMemoryAvailableMb();
        const float total_mb     = Allocator::GetMemoryTotalMb();

        ImGui::Text("engine %.0f MB", allocated_mb);
        ImGui::SameLine();
        ImGui::Text("process %.0f MB", process_mb);
        ImGui::SameLine();
        ImGui::Text("system %.0f / %.0f MB", total_mb - available_mb, total_mb);
        ImGui::TextDisabled("cpu heap layout is owned by the crt, squares show composition not holes");

        ImGui::Dummy(ImVec2(0.0f, 4.0f * Window::GetDpiScale()));

        struct ram_slice
        {
            const char* name;
            ImU32 color;
            float mb;
        };

        vector<ram_slice> slices;
        for (uint8_t i = 0; i < static_cast<uint8_t>(MemoryTag::Count); i++)
        {
            const MemoryTag tag = static_cast<MemoryTag>(i);
            const float mb = Allocator::GetMemoryAllocatedByTagMb(tag);
            if (mb <= 0.05f)
            {
                continue;
            }
            slices.push_back({ Allocator::GetTagName(tag), tag_color(tag), mb });
        }

        const float other_mb = max(0.0f, process_mb - allocated_mb);
        if (other_mb > 0.05f)
        {
            slices.push_back({ "Process", IM_COL32(90, 96, 110, 255), other_mb });
        }
        if (available_mb > 0.05f)
        {
            slices.push_back({ "Free", color_unused, available_mb });
        }

        float slice_total = 0.0f;
        for (const ram_slice& slice : slices)
        {
            slice_total += slice.mb;
        }

        const float dpi     = Window::GetDpiScale();
        const float cell    = 10.0f * dpi;
        const float gap     = 1.0f * dpi;
        const float avail_x = ImGui::GetContentRegionAvail().x;
        const int columns   = max(8, static_cast<int>(avail_x / (cell + gap)));
        const int max_rows  = 20;
        const int max_cells = columns * max_rows;
        const float mb_per_cell = max(1.0f, slice_total / static_cast<float>(max_cells));
        const int cell_count = max(1, static_cast<int>((slice_total + mb_per_cell - 0.001f) / mb_per_cell));
        const int rows = max(1, (cell_count + columns - 1) / columns);
        const float map_w = columns * (cell + gap) - gap;
        const float map_h = rows * (cell + gap) - gap;

        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##ram_map", ImVec2(map_w, map_h));
        const bool ram_hovered = ImGui::IsItemHovered();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(origin, ImVec2(origin.x + map_w, origin.y + map_h), color_grid, 4.0f * dpi);

        vector<int> cell_owner(cell_count, -1);
        int cursor = 0;
        for (int slice_index = 0; slice_index < static_cast<int>(slices.size()); slice_index++)
        {
            int count = max(1, static_cast<int>(round(slices[slice_index].mb / mb_per_cell)));
            count = min(count, cell_count - cursor);
            for (int i = 0; i < count && cursor < cell_count; i++)
            {
                cell_owner[cursor++] = slice_index;
            }
        }

        int hover_cell = -1;
        if (ram_hovered)
        {
            ImVec2 mouse = ImGui::GetMousePos();
            int col = static_cast<int>((mouse.x - origin.x) / (cell + gap));
            int row = static_cast<int>((mouse.y - origin.y) / (cell + gap));
            if (col >= 0 && col < columns && row >= 0 && row < rows)
            {
                hover_cell = row * columns + col;
            }
        }

        for (int i = 0; i < cell_count; i++)
        {
            const int col = i % columns;
            const int row = i / columns;
            ImVec2 p0(origin.x + col * (cell + gap), origin.y + row * (cell + gap));
            ImVec2 p1(p0.x + cell, p0.y + cell);
            ImU32 color = color_unused;
            if (cell_owner[i] >= 0)
            {
                color = slices[cell_owner[i]].color;
            }
            if (i == hover_cell)
            {
                color = IM_COL32(
                    min(255, (int)((color >> 0) & 255) + 40),
                    min(255, (int)((color >> 8) & 255) + 40),
                    min(255, (int)((color >> 16) & 255) + 40),
                    255
                );
            }
            draw_list->AddRectFilled(p0, p1, color, 1.5f * dpi);
        }

        if (hover_cell >= 0 && hover_cell < cell_count && cell_owner[hover_cell] >= 0)
        {
            const ram_slice& slice = slices[cell_owner[hover_cell]];
            ImGui::BeginTooltip();
            ImGui::Text("%s  %.1f MB", slice.name, slice.mb);
            ImGui::EndTooltip();
        }

        ImGui::Separator();
        int legend_on_line = 0;
        for (const ram_slice& slice : slices)
        {
            if (legend_on_line > 0)
            {
                ImGui::SameLine();
            }
            draw_legend_swatch(slice.color, slice.name, static_cast<uint64_t>(slice.mb * 1024.0f * 1024.0f));
            legend_on_line++;
            if (legend_on_line >= 4)
            {
                legend_on_line = 0;
            }
        }
    }
}
