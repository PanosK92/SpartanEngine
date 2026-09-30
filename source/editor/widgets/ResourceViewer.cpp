/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ======================
#include "pch.h"
#include "ResourceViewer.h"
#include "resource/ResourceCache.h"
#include "../imgui/ImGui_EditorUi.h"
#include "../imgui/ImGui_Properties.h"
//=================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan;
using namespace spartan::math;
//============================

namespace
{
    constexpr uint32_t type_count = static_cast<uint32_t>(ResourceType::Max);

    ImGuiTextFilter search_filter;
    uint32_t type_mask = 0xffffffffu;

    // the same tints the assets panel gives each kind, so a texture is blue and a material coral in both places
    ImVec4 type_tint(const ResourceType type)
    {
        switch (type)
        {
            case ResourceType::Texture:   return ImVec4(0.25f, 0.70f, 1.00f, 1.0f);
            case ResourceType::Material:  return ImVec4(1.00f, 0.52f, 0.42f, 1.0f);
            case ResourceType::Mesh:      return ImVec4(0.66f, 0.52f, 1.00f, 1.0f);
            case ResourceType::Audio:     return ImVec4(1.00f, 0.42f, 0.66f, 1.0f);
            case ResourceType::Font:      return ImVec4(0.82f, 0.55f, 1.00f, 1.0f);
            case ResourceType::Cubemap:   return ImVec4(0.30f, 0.88f, 0.90f, 1.0f);
            case ResourceType::Animation: return ImVec4(0.40f, 0.88f, 0.60f, 1.0f);
            case ResourceType::Shader:    return ImVec4(0.80f, 0.90f, 0.40f, 1.0f);
            default:                      return ImVec4(0.60f, 0.62f, 0.66f, 1.0f);
        }
    }

    const char* type_name(const ResourceType type)
    {
        switch (type)
        {
            case ResourceType::Texture:   return "Textures";
            case ResourceType::Material:  return "Materials";
            case ResourceType::Mesh:      return "Meshes";
            case ResourceType::Audio:     return "Audio";
            case ResourceType::Font:      return "Fonts";
            case ResourceType::Cubemap:   return "Cubemaps";
            case ResourceType::Animation: return "Animations";
            case ResourceType::Shader:    return "Shaders";
            default:                      return "Other";
        }
    }

    // "./project/car_playground_resources/albedo.texture" reads as "car_playground_resources/albedo.texture",
    // every path starts in the project folder so the prefix carries no information
    string readable_path(const string& path)
    {
        string result = path;
        replace(result.begin(), result.end(), '\\', '/');
        for (const char* prefix : { "./project/", "project/", "./" })
        {
            if (result.rfind(prefix, 0) == 0)
            {
                result = result.substr(strlen(prefix));
                break;
            }
        }
        return result;
    }

    bool contains_ignore_case(const string& haystack, const char* needle)
    {
        const auto it = ranges::search(haystack, string_view(needle), [](unsigned char a, unsigned char b)
        {
            return tolower(a) == tolower(b);
        }).begin();
        return it != haystack.end();
    }

    bool matches_search(IResource* resource, const char* needle)
    {
        return contains_ignore_case(resource->GetResourceTypeCstr(), needle)
            || contains_ignore_case(to_string(resource->GetObjectId()), needle)
            || contains_ignore_case(resource->GetObjectName(), needle)
            || contains_ignore_case(resource->GetResourceFilePath(), needle);
    }
}

ResourceViewer::ResourceViewer(Editor* editor) : Widget(editor)
{
    m_title         = "Resource Viewer";
    m_visible       = false;
    m_toolbar_order = 2;
    m_toolbar_icon  = static_cast<int>(IconType::ResourceCache);
    m_size_initial  = Vector2(1100, 700);
}

void ResourceViewer::OnTickVisible()
{
    auto resources = ResourceCache::GetResourcesSnapshot();

    // per type totals feed the composition bar and the chips, one pass over the cache
    uint32_t counts[type_count + 1] = {};
    uint64_t bytes[type_count + 1]  = {};
    uint64_t total_bytes            = 0;
    uint64_t largest                = 0;
    for (const shared_ptr<IResource>& resource : resources)
    {
        const uint32_t index = min(static_cast<uint32_t>(resource->GetResourceType()), type_count);
        counts[index]++;
        bytes[index] += resource->GetObjectSize();
        total_bytes  += resource->GetObjectSize();
        largest       = max(largest, resource->GetObjectSize());
    }

    // what is loaded and what it weighs, split by kind so the heavy kind is obvious before reading any row
    vector<editor_ui::Segment> segments;
    vector<ResourceType> segment_types;
    for (uint32_t i = 0; i < type_count; i++)
    {
        if (counts[i] == 0)
        {
            continue;
        }
        const ResourceType type = static_cast<ResourceType>(i);
        segments.push_back({ type_name(type), static_cast<double>(bytes[i]), type_tint(type), editor_ui::format::bytes(static_cast<double>(bytes[i])) });
        segment_types.push_back(type);
    }

    editor_ui::stat_strip("##resource_stats", {
        { to_string(resources.size()), "Resources" },
        { editor_ui::format::bytes(static_cast<double>(total_bytes)), "In memory" },
        { to_string(segments.size()), "Kinds" },
        { editor_ui::format::bytes(static_cast<double>(largest)), "Largest" }
    });
    ImGui::Dummy(ImVec2(0, ImGui::EditorUi::scaled(2.0f)));
    const int hovered_segment = editor_ui::stacked_bar("##resource_composition", segments, static_cast<double>(total_bytes), ImGui::EditorUi::scaled(8.0f));
    ImGui::Dummy(ImVec2(0, ImGui::EditorUi::scaled(2.0f)));

    // kind chips double as the legend, a click filters the table to that kind, a second click shows every kind again
    const float gap = ImGui::EditorUi::scaled(4.0f);
    bool first_chip = true;
    const bool all_types = type_mask == 0xffffffffu;
    for (size_t s = 0; s < segment_types.size(); s++)
    {
        const ResourceType type = segment_types[s];
        const uint32_t bit      = 1u << static_cast<uint32_t>(type);
        char label[64];
        snprintf(label, sizeof(label), "%s %u###type_%u", type_name(type), counts[static_cast<uint32_t>(type)], static_cast<uint32_t>(type));
        if (!first_chip)
        {
            ImGui::SameLine(0, gap);
        }
        first_chip = false;
        const bool active = !all_types && (type_mask & bit) != 0;
        const bool lit    = active || hovered_segment == static_cast<int>(s);
        if (editor_ui::toolbar::pill(label, lit, type_tint(type), all_types ? "Show only this kind" : (active ? "Click again to show every kind" : "Show only this kind"), true))
        {
            type_mask = active ? 0xffffffffu : bit;
        }
    }

    // the search takes what is left of the row, a table this long is navigated by typing
    const float search_x = first_chip ? 0.0f : ImGui::EditorUi::scaled(10.0f);
    if (!first_chip)
    {
        ImGui::SameLine(0, search_x);
    }

    vector<IResource*> rows;
    rows.reserve(resources.size());
    for (const shared_ptr<IResource>& resource : resources)
    {
        const uint32_t bit = 1u << min(static_cast<uint32_t>(resource->GetResourceType()), 31u);
        if ((type_mask & bit) == 0)
        {
            continue;
        }
        if (search_filter.IsActive() && !matches_search(resource.get(), search_filter.InputBuf))
        {
            continue;
        }
        rows.push_back(resource.get());
    }

    char count[48];
    snprintf(count, sizeof(count), "%zu of %zu", rows.size(), resources.size());
    editor_ui::toolbar::search("##resource_viewer_search", "Search by name, path, kind or id", search_filter, max(ImGui::EditorUi::scaled(160.0f), ImGui::GetContentRegionAvail().x), count, rows.empty());
    ImGui::Dummy(ImVec2(0, ImGui::EditorUi::scaled(2.0f)));

    if (rows.empty())
    {
        const bool filtered = search_filter.IsActive() || type_mask != 0xffffffffu;
        if (editor_ui::empty_state(resources.empty() ? "Nothing is loaded" : "No resources match", resources.empty() ? "Textures, meshes and materials appear here as a world loads them." : "Try another search, or show every kind again.", filtered ? "Clear filters" : nullptr))
        {
            search_filter.Clear();
            type_mask = 0xffffffffu;
        }
        return;
    }

    static ImGuiTableFlags flags =
        ImGuiTableFlags_BordersInnerV     |
        ImGuiTableFlags_BordersOuterH     |
        ImGuiTableFlags_RowBg             |
        ImGuiTableFlags_Resizable         |
        ImGuiTableFlags_Reorderable       |
        ImGuiTableFlags_Hideable          |
        ImGuiTableFlags_Sortable          |
        ImGuiTableFlags_ContextMenuInBody |
        ImGuiTableFlags_ScrollY;

    ImGui::EditorUi::push_table_style();
    if (ImGui::BeginTable("##resource_table", 5, flags, ImVec2(-1.0f, -1.0f)))
    {
        // name and size are what people scan for, the id is only needed when chasing a bug, so it starts hidden
        const float dpi = Window::GetDpiScale();
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 110.0f * dpi);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending, 120.0f * dpi);
        ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultHide, 170.0f * dpi);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        int sorted_column              = 2;
        ImGuiSortDirection direction   = ImGuiSortDirection_Descending;
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs())
        {
            if (specs->SpecsCount > 0)
            {
                sorted_column = specs->Specs[0].ColumnIndex;
                direction     = specs->Specs[0].SortDirection;
            }
        }

        const bool ascending = direction == ImGuiSortDirection_Ascending;
        stable_sort(rows.begin(), rows.end(), [&](IResource* a, IResource* b)
        {
            IResource* left  = ascending ? a : b;
            IResource* right = ascending ? b : a;
            switch (sorted_column)
            {
                case 0:  return left->GetObjectName() < right->GetObjectName();
                case 1:  return left->GetResourceType() < right->GetResourceType();
                case 2:  return left->GetObjectSize() < right->GetObjectSize();
                case 3:  return left->GetResourceFilePath() < right->GetResourceFilePath();
                default: return left->GetObjectId() < right->GetObjectId();
            }
        });

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
            {
                IResource* resource     = rows[i];
                const ResourceType type = resource->GetResourceType();
                const string path       = readable_path(resource->GetResourceFilePath());
                ImGui::PushID(resource);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::Selectable(resource->GetObjectName().c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
                if (ImGui::BeginPopupContextItem("##resource_menu"))
                {
                    if (ImGui::MenuItem("Copy name"))
                    {
                        ImGui::SetClipboardText(resource->GetObjectName().c_str());
                    }
                    if (ImGui::MenuItem("Copy path", nullptr, false, !resource->GetResourceFilePath().empty()))
                    {
                        ImGui::SetClipboardText(resource->GetResourceFilePath().c_str());
                    }
                    if (ImGui::MenuItem("Copy id"))
                    {
                        ImGui::SetClipboardText(to_string(resource->GetObjectId()).c_str());
                    }
                    ImGui::EndPopup();
                }

                ImGui::TableSetColumnIndex(1);
                {
                    const ImVec2 pos   = ImGui::GetCursorScreenPos();
                    const float radius = ImGui::EditorUi::scaled(3.0f);
                    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos.x + radius + 1.0f, pos.y + ImGui::GetTextLineHeight() * 0.5f), radius, ImGui::EditorUi::color(type_tint(type)));
                    ImGui::SetCursorScreenPos(ImVec2(pos.x + radius * 2.0f + ImGui::EditorUi::scaled(6.0f), pos.y));
                    ImGui::TextColored(ImGui::Style::color_text_muted, "%s", resource->GetResourceTypeCstr());
                }

                ImGui::TableSetColumnIndex(2);
                const uint64_t size = resource->GetObjectSize();
                editor_ui::cell_bar(size > 0 ? editor_ui::format::bytes(static_cast<double>(size)).c_str() : "-", largest > 0 ? static_cast<float>(static_cast<double>(size) / static_cast<double>(largest)) : 0.0f, type_tint(type));

                ImGui::TableSetColumnIndex(3);
                ImGui::TextColored(ImGui::Style::color_text_muted, "%s", path.empty() ? "generated at runtime" : path.c_str());
                if (!path.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                {
                    ImGui::SetTooltip("%s", resource->GetResourceFilePath().c_str());
                }

                ImGui::TableSetColumnIndex(4);
                ImGui::TextColored(ImGui::Style::color_text_faint, "%llu", static_cast<unsigned long long>(resource->GetObjectId()));
                ImGui::PopID();
            }
        }

        ImGui::EndTable();
    }
    ImGui::EditorUi::pop_table_style();
}
