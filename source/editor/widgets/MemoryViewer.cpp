/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ========================
#include "pch.h"
#include "MemoryViewer.h"
#include "../imgui/ImGui_EditorUi.h"
#include "../imgui/ImGui_Extension.h"
#include "../imgui/ImGui_Properties.h"
#include "memory/GpuMemory.h"
#include "memory/Allocator.h"
#include "rhi/RHI_Device.h"
#include "rhi/RHI_TextureStreaming.h"
#include "resource/ResourceCache.h"
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan;
using namespace spartan::math;
//============================

namespace
{
    constexpr ImU32 color_hole = IM_COL32(150, 62, 70, 255);

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

    // what each kind is for, in words, the short engine names (as, sbt) mean nothing at a glance
    const char* kind_label(GpuMemoryKind kind)
    {
        switch (kind)
        {
            case GpuMemoryKind::Texture:               return "Textures";
            case GpuMemoryKind::Vertex:                return "Vertices";
            case GpuMemoryKind::Index:                 return "Indices";
            case GpuMemoryKind::Instance:              return "Instances";
            case GpuMemoryKind::Storage:               return "Storage";
            case GpuMemoryKind::Constant:              return "Constants";
            case GpuMemoryKind::Upload:                return "Upload";
            case GpuMemoryKind::Readback:              return "Readback";
            case GpuMemoryKind::ShaderBindingTable:    return "Shader tables";
            case GpuMemoryKind::AccelerationStructure: return "Ray tracing BVH";
            default:                                   return "Other";
        }
    }

    ImVec4 to_vec4(const ImU32 color)
    {
        return ImGui::ColorConvertU32ToFloat4(color);
    }

    ImVec4 hole_tint()
    {
        return to_vec4(color_hole);
    }

    ImVec4 unused_tint()
    {
        return ImGui::Style::lerp(ImGui::Style::color_canvas_deep, ImGui::Style::color_text, 0.10f);
    }

    // returns how many bytes one square stands for, 0 when there is nothing to draw
    uint64_t draw_block_map(
        const vector<map_range>& ranges,
        uint64_t total_bytes,
        void*& selected_resource
    )
    {
        if (total_bytes == 0)
        {
            editor_ui::empty_state("No GPU allocations", "Allocations appear here as soon as the renderer creates its first buffer or texture.");
            return 0;
        }

        const float dpi        = Window::GetDpiScale();
        const float cell       = 11.0f * dpi;
        const float gap        = 2.0f * dpi;
        const float avail_x    = ImGui::GetContentRegionAvail().x;
        const int columns      = max(8, static_cast<int>(avail_x / (cell + gap)));
        const int max_rows     = 16;
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

        // hovering one square lights every square of that allocation, so its real extent is visible, not just one cell
        const map_range* hovered_range = nullptr;
        if (hover_cell >= 0 && hover_cell < cell_count)
        {
            hovered_range = range_at(ranges, static_cast<uint64_t>(hover_cell) * cell_bytes);
        }
        const GpuMemoryBlock* hovered_block = hovered_range ? hovered_range->block : nullptr;

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

            ImVec4 tint   = unused_tint();
            bool selected = false;
            bool lit      = false;
            if (range)
            {
                if (range->block)
                {
                    tint     = to_vec4(kind_color(range->block->kind));
                    selected = selected_resource && range->block->resource == selected_resource;
                    lit      = hovered_block && range->block == hovered_block;
                }
                else if (range->is_hole)
                {
                    tint = hole_tint();
                }
            }

            const bool anything_lit = hovered_block || selected_resource;
            float opacity           = 0.85f;
            if (lit || selected)
            {
                tint    = ImGui::Style::lerp(tint, ImVec4(1, 1, 1, 1), 0.25f);
                opacity = 1.0f;
            }
            else if (anything_lit && range && range->block)
            {
                opacity = 0.45f;
            }

            draw_list->AddRectFilled(p0, p1, ImGui::EditorUi::color(ImGui::EditorUi::alpha(tint, opacity)), 2.0f * dpi);
            if (selected)
            {
                draw_list->AddRect(p0, p1, ImGui::EditorUi::color(ImGui::Style::color_text), 2.0f * dpi, 1.5f * dpi);
            }
        }

        if (hovered_range)
        {
            if (map_clicked)
            {
                selected_resource = hovered_range->block ? hovered_range->block->resource : nullptr;
            }

            ImGui::BeginTooltip();
            if (hovered_range->block)
            {
                char size_text[32];
                format_bytes(size_text, sizeof(size_text), hovered_range->block->size);
                ImGui::TextUnformatted(hovered_range->block->name[0] ? hovered_range->block->name : "(unnamed)");
                ImGui::TextColored(to_vec4(kind_color(hovered_range->block->kind)), "%s, %s", kind_label(hovered_range->block->kind), size_text);
                ImGui::TextColored(ImGui::Style::color_text_faint, "Click to select it in the table");
            }
            else
            {
                char size_text[32];
                format_bytes(size_text, sizeof(size_text), hovered_range->size);
                ImGui::Text("Free, %s", size_text);
                ImGui::TextColored(ImGui::Style::color_text_muted, "A gap inside a heap, reusable by allocations that fit");
            }
            ImGui::EndTooltip();
        }

        return cell_bytes;
    }

    string csv_escape(const char* value)
    {
        if (!value || !value[0])
        {
            return "";
        }

        bool quote = false;
        for (const char* c = value; *c; c++)
        {
            if (*c == ',' || *c == '"' || *c == '\n' || *c == '\r')
            {
                quote = true;
                break;
            }
        }
        if (!quote)
        {
            return string(value);
        }

        string out = "\"";
        for (const char* c = value; *c; c++)
        {
            if (*c == '"')
            {
                out += "\"\"";
            }
            else
            {
                out += *c;
            }
        }
        out += '"';
        return out;
    }

    void csv_cell_str(string& csv, bool& first, const char* value)
    {
        if (!first)
        {
            csv += ',';
        }
        first = false;
        csv += csv_escape(value);
    }

    void csv_cell_u64(string& csv, bool& first, uint64_t value)
    {
        if (!first)
        {
            csv += ',';
        }
        first = false;
        char buf[32];
        snprintf(
            buf,
            sizeof(buf),
            "%llu",
            static_cast<unsigned long long>(value)
        );
        csv += buf;
    }

    void csv_cell_f(string& csv, bool& first, double value)
    {
        if (!first)
        {
            csv += ',';
        }
        first = false;
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", value);
        csv += buf;
    }

    void csv_end_row(string& csv)
    {
        csv += '\n';
    }

    struct csv_group
    {
        string name;
        string extra;
        uint32_t count = 0;
        uint64_t bytes = 0;
    };

    void csv_write_groups(
        string& csv,
        const char* section,
        const char* extra_header,
        vector<csv_group>& groups,
        uint64_t tracked
    )
    {
        sort(
            groups.begin(),
            groups.end(),
            [](const csv_group& a, const csv_group& b)
            {
                return a.bytes > b.bytes;
            }
        );

        csv += "## ";
        csv += section;
        csv += '\n';
        csv += "name,";
        csv += extra_header;
        csv += ",count,bytes,pct\n";
        for (const csv_group& group : groups)
        {
            bool first = true;
            csv_cell_str(csv, first, group.name.c_str());
            csv_cell_str(csv, first, group.extra.c_str());
            csv_cell_u64(csv, first, group.count);
            csv_cell_u64(csv, first, group.bytes);
            const double pct = tracked > 0
                ? (static_cast<double>(group.bytes) * 100.0 /
                    static_cast<double>(tracked))
                : 0.0;
            csv_cell_f(csv, first, pct);
            csv_end_row(csv);
        }
        csv_end_row(csv);
    }

    bool export_gpu_csv(
        const string& path,
        const vector<GpuMemoryBlock>& blocks,
        uint64_t vram_total,
        uint64_t driver_bytes
    )
    {
        vector<map_range> ranges;
        uint64_t used = 0;
        uint64_t holes = 0;
        uint64_t unused = 0;
        uint64_t largest_free = 0;
        build_ranges(
            blocks,
            vram_total,
            ranges,
            used,
            holes,
            unused,
            largest_free
        );

        const uint64_t total_free = holes + unused;
        const float fragmentation = (total_free > 0)
            ? (1.0f - static_cast<float>(largest_free) /
                static_cast<float>(total_free))
            : 0.0f;

        array<uint64_t, static_cast<size_t>(GpuMemoryKind::Count)> kind_bytes = {};
        array<uint32_t, static_cast<size_t>(GpuMemoryKind::Count)> kind_count = {};
        for (const GpuMemoryBlock& block : blocks)
        {
            const size_t index = static_cast<size_t>(block.kind);
            kind_bytes[index] += block.size;
            kind_count[index]++;
        }

        unordered_map<uint64_t, uint32_t> heap_index;
        uint32_t next_heap = 0;
        for (const GpuMemoryBlock& block : blocks)
        {
            if (heap_index.find(block.heap_id) == heap_index.end())
            {
                heap_index[block.heap_id] = next_heap++;
            }
        }

        unordered_map<string, csv_group> by_name;
        unordered_map<string, csv_group> by_format;
        unordered_map<string, csv_group> by_path;
        by_name.reserve(blocks.size());
        by_format.reserve(64);
        by_path.reserve(blocks.size());
        for (const GpuMemoryBlock& block : blocks)
        {
            const char* name = block.name[0] ? block.name : "(unnamed)";
            const char* kind = GpuMemory::GetKindName(block.kind);
            string name_key = string(name) + '\t' + kind;
            csv_group& name_group = by_name[name_key];
            if (name_group.count == 0)
            {
                name_group.name  = name;
                name_group.extra = kind;
            }
            name_group.count++;
            name_group.bytes += block.size;

            if (block.format[0])
            {
                csv_group& format_group = by_format[block.format];
                if (format_group.count == 0)
                {
                    format_group.name  = block.format;
                    format_group.extra = block.kind == GpuMemoryKind::Texture
                        ? "Texture"
                        : kind;
                }
                format_group.count++;
                format_group.bytes += block.size;
            }

            if (block.path[0])
            {
                csv_group& path_group = by_path[block.path];
                if (path_group.count == 0)
                {
                    path_group.name  = block.path;
                    path_group.extra = name;
                }
                path_group.count++;
                path_group.bytes += block.size;
            }
        }

        string csv;
        csv.reserve(blocks.size() * 192 + 8192);

        csv += "## summary\n";
        csv += "metric,value\n";
        {
            auto metric_u64 = [&](const char* name, uint64_t value)
            {
                bool first = true;
                csv_cell_str(csv, first, name);
                csv_cell_u64(csv, first, value);
                csv_end_row(csv);
            };
            auto metric_f = [&](const char* name, double value)
            {
                bool first = true;
                csv_cell_str(csv, first, name);
                csv_cell_f(csv, first, value);
                csv_end_row(csv);
            };
            metric_u64("tracked_bytes", used);
            metric_u64("vram_total_bytes", vram_total);
            metric_u64("alloc_count", blocks.size());
            metric_u64("holes_bytes", holes);
            metric_u64("unused_bytes", unused);
            metric_u64("largest_free_bytes", largest_free);
            metric_f("fragmentation_pct", fragmentation * 100.0);
            metric_u64("driver_bytes", driver_bytes);
            metric_u64("heap_count", next_heap);
        }
        csv_end_row(csv);

        csv += "## by_kind\n";
        csv += "kind,count,bytes,pct\n";
        for (uint8_t i = 0; i < static_cast<uint8_t>(GpuMemoryKind::Count); i++)
        {
            if (kind_bytes[i] == 0)
            {
                continue;
            }
            bool first = true;
            csv_cell_str(csv, first, GpuMemory::GetKindName(static_cast<GpuMemoryKind>(i)));
            csv_cell_u64(csv, first, kind_count[i]);
            csv_cell_u64(csv, first, kind_bytes[i]);
            const double pct = used > 0
                ? (static_cast<double>(kind_bytes[i]) * 100.0 /
                    static_cast<double>(used))
                : 0.0;
            csv_cell_f(csv, first, pct);
            csv_end_row(csv);
        }
        csv_end_row(csv);

        vector<csv_group> name_groups;
        name_groups.reserve(by_name.size());
        for (auto& pair : by_name)
        {
            name_groups.push_back(move(pair.second));
        }
        csv_write_groups(csv, "by_name", "kind", name_groups, used);

        vector<csv_group> format_groups;
        format_groups.reserve(by_format.size());
        for (auto& pair : by_format)
        {
            format_groups.push_back(move(pair.second));
        }
        csv_write_groups(csv, "by_format", "kind", format_groups, used);

        vector<csv_group> path_groups;
        path_groups.reserve(by_path.size());
        for (auto& pair : by_path)
        {
            path_groups.push_back(move(pair.second));
        }
        csv_write_groups(csv, "by_path", "name", path_groups, used);

        vector<const GpuMemoryBlock*> sorted;
        sorted.reserve(blocks.size());
        for (const GpuMemoryBlock& block : blocks)
        {
            sorted.push_back(&block);
        }
        sort(
            sorted.begin(),
            sorted.end(),
            [](const GpuMemoryBlock* a, const GpuMemoryBlock* b)
            {
                return a->size > b->size;
            }
        );

        csv += "## allocations\n";
        csv += "name,kind,type,width,height,depth,mips,format,size_bytes,offset,heap,dedicated,path\n";
        for (const GpuMemoryBlock* block : sorted)
        {
            bool first = true;
            csv_cell_str(
                csv,
                first,
                block->name[0] ? block->name : "(unnamed)"
            );
            csv_cell_str(csv, first, GpuMemory::GetKindName(block->kind));
            csv_cell_str(csv, first, block->type);
            csv_cell_u64(csv, first, block->width);
            csv_cell_u64(csv, first, block->height);
            csv_cell_u64(csv, first, block->depth);
            csv_cell_u64(csv, first, block->mip_count);
            csv_cell_str(csv, first, block->format);
            csv_cell_u64(csv, first, block->size);
            csv_cell_u64(csv, first, block->offset);
            csv_cell_u64(csv, first, heap_index[block->heap_id]);
            csv_cell_u64(
                csv,
                first,
                (block->heap_size == block->size) ? 1 : 0
            );
            csv_cell_str(csv, first, block->path);
            csv_end_row(csv);
        }

        return FileSystem::WriteFile(path, csv);
    }

    bool export_cpu_csv(const string& path)
    {
        const float allocated_mb = Allocator::GetMemoryAllocatedMb();
        const float process_mb   = Allocator::GetMemoryProcessUsedMb();
        const float available_mb = Allocator::GetMemoryAvailableMb();
        const float total_mb     = Allocator::GetMemoryTotalMb();

        string csv;
        csv.reserve(1024);

        char line[256];
        snprintf(
            line,
            sizeof(line),
            "# engine_mb=%.3f\n",
            allocated_mb
        );
        csv += line;
        snprintf(
            line,
            sizeof(line),
            "# process_mb=%.3f\n",
            process_mb
        );
        csv += line;
        snprintf(
            line,
            sizeof(line),
            "# system_used_mb=%.3f\n",
            total_mb - available_mb
        );
        csv += line;
        snprintf(
            line,
            sizeof(line),
            "# system_total_mb=%.3f\n",
            total_mb
        );
        csv += line;
        csv += "tag,mb\n";

        for (uint8_t i = 0; i < static_cast<uint8_t>(MemoryTag::Count); i++)
        {
            const MemoryTag tag = static_cast<MemoryTag>(i);
            const float mb = Allocator::GetMemoryAllocatedByTagMb(tag);
            csv += csv_escape(Allocator::GetTagName(tag));
            snprintf(line, sizeof(line), ",%.3f\n", mb);
            csv += line;
        }

        const float other_mb = max(0.0f, process_mb - allocated_mb);
        csv += "Process";
        snprintf(line, sizeof(line), ",%.3f\n", other_mb);
        csv += line;
        csv += "Free";
        snprintf(line, sizeof(line), ",%.3f\n", available_mb);
        csv += line;

        return FileSystem::WriteFile(path, csv);
    }
}

MemoryViewer::MemoryViewer(Editor* editor) : Widget(editor)
{
    m_title         = "Memory";
    m_visible       = false;
    m_toolbar_order = 8;
    m_toolbar_icon  = static_cast<int>(IconType::Memory);
    m_size_initial  = Vector2(920, 680);
    m_size_min      = Vector2(520, 420);
}

void MemoryViewer::OnTickVisible()
{
    using namespace editor_ui;
    const float gap = ImGui::EditorUi::scaled(4.0f);

    // two memories, two pills, the one on screen is lit
    if (toolbar::pill("Video memory", m_show_gpu, ImGui::Style::color_accent_1, "Every GPU allocation the engine made, mapped by heap"))
    {
        m_show_gpu = true;
    }
    ImGui::SameLine(0, gap);
    if (toolbar::pill("System memory", !m_show_gpu, ImGui::Style::color_accent_1, "What the engine and the process use of system RAM"))
    {
        m_show_gpu = false;
    }

    // freezing only means something for the video map, the system numbers are totals that are always live
    if (m_show_gpu)
    {
        toolbar::divider();
        if (toolbar::pill(m_frozen ? "Frozen" : "Live", !m_frozen, m_frozen ? ImGui::Style::color_warning : ImGui::Style::color_ok, m_frozen ? "The map is a snapshot, click to go live again" : "The map follows every allocation, click to freeze it and inspect", true, !m_frozen))
        {
            if (!m_frozen)
            {
                GpuMemory::GetBlocks(m_frozen_blocks);
            }
            m_frozen = !m_frozen;
        }
    }

    const float export_w = toolbar::ghost_button_width("Export CSV") + (m_export_path.empty() ? 0.0f : toolbar::ghost_button_width("Copy path"));
    toolbar::align_right(export_w);
    if (toolbar::ghost_button("Export CSV", m_show_gpu ? "Write every GPU allocation, grouped by kind, name, format and path, to memory_gpu.csv" : "Write the system memory totals per tag to memory_cpu.csv"))
    {
        const string path = FileSystem::GetExecutableDirectory() +
            (m_show_gpu ? "/memory_gpu.csv" : "/memory_cpu.csv");
        bool ok = false;
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
            const uint64_t vram_total =
                RHI_Device::MemoryGetTotalMb() * 1024ull * 1024ull;
            const uint64_t driver_bytes =
                RHI_Device::MemoryGetAllocatedMb() * 1024ull * 1024ull;
            ok = export_gpu_csv(
                path,
                blocks,
                vram_total,
                driver_bytes
            );
        }
        else
        {
            ok = export_cpu_csv(path);
        }

        if (ok)
        {
            m_export_path = path;
            SP_LOG_INFO("memory csv written to %s", path.c_str());
        }
        else
        {
            SP_LOG_ERROR("failed to write memory csv to %s", path.c_str());
        }
    }
    if (!m_export_path.empty())
    {
        ImGui::SameLine(0, 0);
        if (toolbar::ghost_button("Copy path", m_export_path.c_str()))
        {
            ImGui::SetClipboardText(m_export_path.c_str());
        }
    }

    ImGui::Dummy(ImVec2(0, ImGui::EditorUi::scaled(2.0f)));

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

        // the headline: how full the card is and whether what is free can still be used
        const float dpi = Window::GetDpiScale();
        char tracked_label[48];
        snprintf(tracked_label, sizeof(tracked_label), "Tracked of %s", format::bytes(static_cast<double>(vram_total)).c_str());
        stat_strip("##gpu_stats", {
            { format::bytes(static_cast<double>(used)), tracked_label, ImGui::Style::color_accent_1 },
            { vram_used_driver > 0 ? format::bytes(static_cast<double>(vram_used_driver)) : string("n/a"), "Driver reports" },
            { format::grouped(static_cast<double>(blocks.size())), "Allocations" },
            { format::bytes(static_cast<double>(largest_free)), "Largest free" },
            { to_string(static_cast<int>(fragmentation * 100.0f + 0.5f)) + "%", "Fragmented", fragmentation > 0.3f ? ImGui::Style::color_warning : ImGui::Style::color_text }
        });
        ImGui::SetItemTooltip("Driver reports is what the driver says the process holds, it includes allocations the engine does not track. Fragmented is the share of free memory that is not in the largest free range.");

        // the whole card in one bar: what each kind holds, the gaps inside heaps, and what no heap has claimed yet
        array<uint64_t, static_cast<size_t>(GpuMemoryKind::Count)> by_kind = {};
        for (const GpuMemoryBlock& block : blocks)
        {
            by_kind[static_cast<size_t>(block.kind)] += block.size;
        }
        vector<Segment> segments;
        for (uint8_t i = 0; i < static_cast<uint8_t>(GpuMemoryKind::Count); i++)
        {
            const GpuMemoryKind kind = static_cast<GpuMemoryKind>(i);
            segments.push_back({ kind_label(kind), static_cast<double>(by_kind[i]), to_vec4(kind_color(kind)), format::bytes(static_cast<double>(by_kind[i])) });
        }
        segments.push_back({ "Free in heaps", static_cast<double>(holes), hole_tint(), format::bytes(static_cast<double>(holes)) });
        segments.push_back({ "Unclaimed", static_cast<double>(unused), unused_tint(), format::bytes(static_cast<double>(unused)) });

        ImGui::Dummy(ImVec2(0, ImGui::EditorUi::scaled(4.0f)));
        const int hovered_segment = stacked_bar("##vram_composition", segments, static_cast<double>(max(vram_total, used + holes + unused)), ImGui::EditorUi::scaled(14.0f));
        ImGui::Dummy(ImVec2(0, ImGui::EditorUi::scaled(2.0f)));
        legend(segments, hovered_segment);

        // streaming gets its own meter, it is the one budget here the user can actually change
        const auto streaming = RHI_TextureStreaming::GetStatistics();
        if (streaming.budget_bytes > 0)
        {
            char streaming_text[96];
            snprintf(streaming_text, sizeof(streaming_text), "%s of %s, %u textures", format::bytes(static_cast<double>(streaming.resident_bytes)).c_str(), format::bytes(static_cast<double>(streaming.budget_bytes)).c_str(), streaming.texture_count);
            const string streaming_tooltip = "Texture payload that is resident against the streaming budget, allocation overhead and staging excluded. At full resolution these textures would need " + format::bytes(static_cast<double>(streaming.full_bytes)) + ", " + format::bytes(static_cast<double>(streaming.pending_bytes)) + " is waiting to be swapped in.";
            property_meter("Texture streaming", static_cast<float>(static_cast<double>(streaming.resident_bytes) / static_cast<double>(streaming.budget_bytes)), streaming_text, streaming_tooltip.c_str());
        }

        // the map spends its squares on the heaps only, unclaimed memory is already in the bar above
        // and drawing it here left the allocations squeezed into the top few rows
        ImGui::Dummy(ImVec2(0, ImGui::EditorUi::scaled(4.0f)));
        const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        ImGui::EditorUi::section_rule("Heaps");
        const uint64_t map_bytes = used + holes;
        const ImVec2 caption_pos = ImGui::GetItemRectMin();
        void* selected_before     = m_selected_resource;
        const uint64_t cell_bytes = draw_block_map(ranges, map_bytes, m_selected_resource);
        m_scroll_to_selected     |= m_selected_resource && m_selected_resource != selected_before;
        if (cell_bytes > 0)
        {
            // the scale sits on the section rule, right aligned, where it explains the map without costing a line
            char scale[64];
            snprintf(scale, sizeof(scale), "one square is %s", format::bytes(static_cast<double>(cell_bytes)).c_str());
            const ImVec2 size = ImGui::CalcTextSize(scale);
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            draw_list->AddRectFilled(ImVec2(right - size.x - ImGui::EditorUi::scaled(8.0f), caption_pos.y), ImVec2(right, caption_pos.y + size.y), ImGui::EditorUi::color(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg)));
            draw_list->AddText(ImVec2(right - size.x, caption_pos.y), ImGui::EditorUi::color(ImGui::Style::color_text_faint), scale);
        }

        ImGui::Dummy(ImVec2(0, ImGui::EditorUi::scaled(6.0f)));
        uint64_t largest_block = 0;
        for (const GpuMemoryBlock& block : blocks)
        {
            largest_block = max(largest_block, block.size);
        }

        ImGui::EditorUi::push_table_style();
        if (ImGui::BeginTable(
            "gpu_allocs",
            4,
            ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_BordersOuterH |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_Resizable,
            ImVec2(0.0f, ImGui::GetContentRegionAvail().y)
        ))
        {
            ImGui::TableSetupColumn("Allocation", ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 130.0f * dpi);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 120.0f * dpi);
            ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupScrollFreeze(0, 1);
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

            // a square clicked on the map brings its row into view
            int scroll_to = -1;
            if (m_scroll_to_selected && m_selected_resource)
            {
                for (int i = 0; i < static_cast<int>(sorted.size()); i++)
                {
                    if (sorted[i]->resource == m_selected_resource)
                    {
                        scroll_to = i;
                        break;
                    }
                }
            }
            m_scroll_to_selected = false;

            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(sorted.size()));
            if (scroll_to >= 0)
            {
                clipper.IncludeItemByIndex(scroll_to);
            }
            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
                {
                    const GpuMemoryBlock* block = sorted[i];
                    const ImVec4 tint           = to_vec4(kind_color(block->kind));
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::PushID(i);
                    const bool selected = m_selected_resource && block->resource == m_selected_resource;
                    if (ImGui::Selectable(block->name[0] ? block->name : "(unnamed)", selected, ImGuiSelectableFlags_SpanAllColumns))
                    {
                        m_selected_resource = selected ? nullptr : block->resource;
                    }
                    if (i == scroll_to)
                    {
                        ImGui::SetScrollHereY(0.5f);
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && block->path[0])
                    {
                        ImGui::SetTooltip("%s", block->path);
                    }
                    ImGui::PopID();

                    ImGui::TableNextColumn();
                    {
                        const ImVec2 pos   = ImGui::GetCursorScreenPos();
                        const float radius = ImGui::EditorUi::scaled(3.0f);
                        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos.x + radius + 1.0f, pos.y + ImGui::GetTextLineHeight() * 0.5f), radius, ImGui::EditorUi::color(tint));
                        ImGui::SetCursorScreenPos(ImVec2(pos.x + radius * 2.0f + ImGui::EditorUi::scaled(6.0f), pos.y));
                        ImGui::TextColored(ImGui::Style::color_text_muted, "%s", kind_label(block->kind));
                    }

                    ImGui::TableNextColumn();
                    cell_bar(format::bytes(static_cast<double>(block->size)).c_str(), largest_block > 0 ? static_cast<float>(static_cast<double>(block->size) / static_cast<double>(largest_block)) : 0.0f, tint);

                    ImGui::TableNextColumn();
                    if (block->width > 0)
                    {
                        char details[128];
                        if (block->depth > 1)
                        {
                            snprintf(details, sizeof(details), "%u \xC3\x97 %u \xC3\x97 %u  %s", block->width, block->height, block->depth, block->format);
                        }
                        else
                        {
                            snprintf(details, sizeof(details), "%u \xC3\x97 %u  %s", block->width, block->height, block->format);
                        }
                        ImGui::TextColored(ImGui::Style::color_text_muted, "%s", details);
                        if (block->mip_count > 1)
                        {
                            ImGui::SameLine();
                            ImGui::TextColored(ImGui::Style::color_text_faint, "%u mips", block->mip_count);
                        }
                    }
                    else if (block->path[0])
                    {
                        ImGui::TextColored(ImGui::Style::color_text_muted, "%s", block->path);
                    }
                }
            }
            ImGui::EndTable();
        }
        ImGui::EditorUi::pop_table_style();
    }
    else
    {
        const float allocated_mb = Allocator::GetMemoryAllocatedMb();
        const float process_mb   = Allocator::GetMemoryProcessUsedMb();
        const float available_mb = Allocator::GetMemoryAvailableMb();
        const float total_mb     = Allocator::GetMemoryTotalMb();
        const double mb          = 1024.0 * 1024.0;

        stat_strip("##cpu_stats", {
            { format::bytes(allocated_mb * mb), "Engine", ImGui::Style::color_accent_1 },
            { format::bytes(process_mb * mb), "Process" },
            { format::bytes((total_mb - available_mb) * mb), "System in use" },
            { format::bytes(total_mb * mb), "Installed" }
        });
        ImGui::SetItemTooltip("Engine is what the engine allocator tracks per tag, process is everything this process holds, including drivers and libraries.");

        // the heap layout belongs to the c runtime, so there are no holes to show, only what the memory is for,
        // a bar says that faster than a field of squares
        vector<Segment> segments;
        for (uint8_t i = 0; i < static_cast<uint8_t>(MemoryTag::Count); i++)
        {
            const MemoryTag tag = static_cast<MemoryTag>(i);
            const float tag_mb  = Allocator::GetMemoryAllocatedByTagMb(tag);
            if (tag_mb > 0.05f)
            {
                segments.push_back({ Allocator::GetTagName(tag), tag_mb * mb, to_vec4(tag_color(tag)), format::bytes(tag_mb * mb) });
            }
        }
        const size_t tag_segments = segments.size();
        const float other_mb      = max(0.0f, process_mb - allocated_mb);
        segments.push_back({ "Rest of the process", other_mb * mb, ImGui::Style::lerp(ImGui::Style::color_text_faint, ImGui::Style::color_canvas_deep, 0.35f), format::bytes(other_mb * mb) });
        segments.push_back({ "Free", available_mb * mb, unused_tint(), format::bytes(available_mb * mb) });

        double total = 0.0;
        for (const Segment& segment : segments)
        {
            total += segment.value;
        }

        ImGui::Dummy(ImVec2(0, ImGui::EditorUi::scaled(4.0f)));
        const int hovered_segment = stacked_bar("##ram_composition", segments, total, ImGui::EditorUi::scaled(14.0f));
        ImGui::Dummy(ImVec2(0, ImGui::EditorUi::scaled(2.0f)));
        legend(segments, hovered_segment);

        // the engine's own tags, largest first, each against the largest so the ranking is visible
        ImGui::Dummy(ImVec2(0, ImGui::EditorUi::scaled(6.0f)));
        ImGui::EditorUi::section_rule("Engine allocations by tag");
        vector<Segment> tags(segments.begin(), segments.begin() + tag_segments);
        sort(tags.begin(), tags.end(), [](const Segment& a, const Segment& b)
        {
            return a.value > b.value;
        });
        const double largest_tag = tags.empty() ? 0.0 : tags.front().value;
        const float dpi          = Window::GetDpiScale();

        if (tags.empty())
        {
            layout::caption("The engine allocator has not tagged anything yet.");
        }
        else
        {
            ImGui::EditorUi::push_table_style();
            if (ImGui::BeginTable("##cpu_tags", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 220.0f * dpi);
                ImGui::TableSetupColumn("Share of engine", ImGuiTableColumnFlags_WidthFixed, 130.0f * dpi);
                ImGui::TableHeadersRow();
                for (const Segment& tag : tags)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    const ImVec2 pos   = ImGui::GetCursorScreenPos();
                    const float radius = ImGui::EditorUi::scaled(3.0f);
                    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos.x + radius + 1.0f, pos.y + ImGui::GetTextLineHeight() * 0.5f), radius, ImGui::EditorUi::color(tag.tint));
                    ImGui::SetCursorScreenPos(ImVec2(pos.x + radius * 2.0f + ImGui::EditorUi::scaled(6.0f), pos.y));
                    ImGui::TextUnformatted(tag.label.c_str());
                    ImGui::TableNextColumn();
                    cell_bar(tag.detail.c_str(), static_cast<float>(tag.value / largest_tag), tag.tint);
                    ImGui::TableNextColumn();
                    char share[32];
                    snprintf(share, sizeof(share), "%.1f%%", allocated_mb > 0.0f ? tag.value / (allocated_mb * mb) * 100.0 : 0.0);
                    text_right(share, ImGui::Style::color_text_muted);
                }
                ImGui::EndTable();
            }
            ImGui::EditorUi::pop_table_style();
        }
    }
}
