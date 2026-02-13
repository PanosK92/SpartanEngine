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
#include "LightAuditor.h"

#include "ButtonColorPicker.h"
#include "../ImGui/ImGui_Extension.h"
#include "World/Entity.h"
#include "World/Components/Light.h"
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan;
using namespace spartan::math;
//============================

namespace
{
    int resource_search_count      = 0;
    vector<string> light_types     = { "Directional", "Point", "Spot", "Area" };
    vector<string> intensity_types = {
        "Stadium",
        "500W Bulb",
        "150W Bulb",
        "100W Bulb",
        "60W Bulb",
        "25W Bulb",
        "Flashlight",
        "Black Hole",
        "Custom"
    };

    bool contains_search_ignore_case(const char* cstr_haystack, const char* cstr_needle)
    {
        string_view str_h = cstr_haystack;
        string_view str_n = cstr_needle;

        const auto it = ranges::search(str_h, str_n,
                                 [](unsigned char a, unsigned char b)
                                 {
                                     return std::tolower(a) == std::tolower(b);
                                 }).begin();

        return it != str_h.end();
    }

    bool is_resource_searched(const Light* light, const char* cstr_needle)
    {
        return contains_search_ignore_case(light->GetEntity()->GetObjectName().c_str(), cstr_needle);
    }
}

LightAuditor::LightAuditor(Editor* editor) : Widget(editor)
{
    m_title                 = "Light Auditor";
    m_visible               = false;
    m_alpha                 = 1.0f;
    m_size_initial          = Vector2(Display::GetWidth() * 0.25f, Display::GetHeight() * 0.5f);
}

void LightAuditor::OnTickVisible()
{
    auto lights = World::GetEntitiesLights();

    static char search_buffer[128] = "";
    ImGui::InputTextWithHint("##light_auditor_search", "Search light entity by name", search_buffer, IM_ARRAYSIZE(search_buffer));
    if (search_buffer[0] != '\0')
    {
        ImGui::SameLine();
        ImGui::Text("%d result%s", resource_search_count, resource_search_count > 1 ? "s" : "");
        resource_search_count = 0;
    }
    ImGui::Separator();

    static ImGuiTableFlags flags =
        ImGuiTableFlags_Borders           | // Draw all borders.
        ImGuiTableFlags_RowBg             | // Set each RowBg color with ImGuiCol_TableRowBg or ImGuiCol_TableRowBgAlt (equivalent of calling TableSetBgColor with ImGuiTableBgFlags_RowBg0 on each row manually)
        ImGuiTableFlags_SizingFixedFit    | // Match column width with its content's maximum width.
        ImGuiTableFlags_Reorderable       | // Allow reordering columns.
        ImGuiTableFlags_Sortable          | // Allow sorting rows.
        ImGuiTableFlags_ContextMenuInBody | // Right-click on columns body/contents will display table context menu. By default it is available in TableHeadersRow().
        ImGuiTableFlags_ScrollX           | // Enable horizontal scrolling. Require 'outer_size' parameter of BeginTable() to specify the container size. Changes default sizing policy. Because this create a child window, ScrollY is currently generally recommended when using ScrollX.
        ImGuiTableFlags_ScrollY;            // Enable vertical scrolling. Require 'outer_size' parameter of BeginTable() to specify the container size.

    static ImVec2 size = ImGui::GetContentRegionAvail();
    if (ImGui::BeginTable("##Widget_LightAuditor", 16, flags, size))
    {
        // Headers
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Active");
        ImGui::TableSetupColumn("Color");
        ImGui::TableSetupColumn("Temperature");
        ImGui::TableSetupColumn("Intensity");
        ImGui::TableSetupColumn("Preset");
        ImGui::TableSetupColumn("Shadows");
        ImGui::TableSetupColumn("Screen Space");
        ImGui::TableSetupColumn("Volumetric");
        ImGui::TableSetupColumn("Cycle");
        ImGui::TableSetupColumn("Real Time");
        ImGui::TableSetupColumn("Range");
        ImGui::TableSetupColumn("Angle");
        ImGui::TableSetupColumn("Width");
        ImGui::TableSetupColumn("Height");
        ImGui::TableHeadersRow();

        // --- Sorting logic on column header click ---
        static int sorted_column                 = 1; // default sorting method by ID
        static ImGuiSortDirection sort_direction = ImGuiSortDirection_Ascending;

        if (ImGuiTableSortSpecs* table_sort_specs = ImGui::TableGetSortSpecs())
        {
            if (table_sort_specs->SpecsDirty)
            {
                const ImGuiTableColumnSortSpecs* spec = &table_sort_specs->Specs[0];
                sorted_column                         = spec->ColumnIndex;
                sort_direction                        = spec->SortDirection;
                table_sort_specs->SpecsDirty          = false;
            }
        }

        ranges::sort(lights, [](Entity* a, Entity* b)
        {
            Light* light_a = a->GetComponent<Light>();
            Light* light_b = b->GetComponent<Light>();
            if (!light_a || !light_b) return false;

            switch (sorted_column)
            {
                case 0: return sort_direction == ImGuiSortDirection_Ascending // Name
                                ? a->GetObjectName() < b->GetObjectName()
                                : a->GetObjectName() > b->GetObjectName();
                case 1: return sort_direction == ImGuiSortDirection_Ascending // Type
                                ? light_a->GetLightType() < light_b->GetLightType()
                                : light_a->GetLightType() > light_b->GetLightType();
                case 2: return sort_direction == ImGuiSortDirection_Ascending // Active
                                ? a->GetActive() < b->GetActive()
                                : a->GetActive() > b->GetActive();
                case 3: return sort_direction == ImGuiSortDirection_Ascending // Color
                                ? light_a->GetColor() < light_b->GetColor()
                                : light_a->GetColor() > light_b->GetColor();
                case 4: return sort_direction == ImGuiSortDirection_Ascending // Temperature
                                ? light_a->GetTemperature() < light_b->GetTemperature()
                                : light_a->GetTemperature() > light_b->GetTemperature();
                case 5: return sort_direction == ImGuiSortDirection_Ascending // Intensity
                                ? light_a->GetIntensity() < light_b->GetIntensity()
                                : light_a->GetIntensity() > light_b->GetIntensity();
                case 6: return sort_direction == ImGuiSortDirection_Ascending // Preset
                                ? light_a->GetPreset() < light_b->GetPreset()
                                : light_a->GetPreset() > light_b->GetPreset();
                case 7: return sort_direction == ImGuiSortDirection_Ascending // Shadows Enabled
                                ? light_a->GetFlag(spartan::LightFlags::Shadows) < light_b->GetFlag(spartan::LightFlags::Shadows)
                                : light_a->GetFlag(spartan::LightFlags::Shadows) > light_b->GetFlag(spartan::LightFlags::Shadows);
                case 8: return sort_direction == ImGuiSortDirection_Ascending // Shadows Screen Space
                                ? light_a->GetFlag(spartan::LightFlags::ShadowsScreenSpace) < light_b->GetFlag(spartan::LightFlags::ShadowsScreenSpace)
                                : light_a->GetFlag(spartan::LightFlags::ShadowsScreenSpace) > light_b->GetFlag(spartan::LightFlags::ShadowsScreenSpace);
                case 9: return sort_direction == ImGuiSortDirection_Ascending // Volumetric
                                ? light_a->GetFlag(spartan::LightFlags::Volumetric) < light_b->GetFlag(spartan::LightFlags::Volumetric)
                                : light_a->GetFlag(spartan::LightFlags::Volumetric) > light_b->GetFlag(spartan::LightFlags::Volumetric);
                case 10: return sort_direction == ImGuiSortDirection_Ascending // Day/Night Cycle
                                ? light_a->GetFlag(spartan::LightFlags::DayNightCycle) < light_b->GetFlag(spartan::LightFlags::DayNightCycle)
                                : light_a->GetFlag(spartan::LightFlags::DayNightCycle) > light_b->GetFlag(spartan::LightFlags::DayNightCycle);
                case 11: return sort_direction == ImGuiSortDirection_Ascending // Real-Time Cycle
                                ? light_a->GetFlag(spartan::LightFlags::RealTimeCycle) < light_b->GetFlag(spartan::LightFlags::RealTimeCycle)
                                : light_a->GetFlag(spartan::LightFlags::RealTimeCycle) > light_b->GetFlag(spartan::LightFlags::RealTimeCycle);
                case 12: return sort_direction == ImGuiSortDirection_Ascending // Range
                                ? light_a->GetRange() < light_b->GetRange()
                                : light_a->GetRange() > light_b->GetRange();
                case 13: return sort_direction == ImGuiSortDirection_Ascending // Angle
                                ? light_a->GetAngle() < light_b->GetAngle()
                                : light_a->GetAngle() > light_b->GetAngle();
                case 14: return sort_direction == ImGuiSortDirection_Ascending // Width
                                ? light_a->GetAreaWidth() < light_b->GetAreaWidth()
                                : light_a->GetAreaWidth() > light_b->GetAreaWidth();
                case 15: return sort_direction == ImGuiSortDirection_Ascending // Height
                                ? light_a->GetAreaWidth() < light_b->GetAreaWidth()
                                : light_a->GetAreaWidth() > light_b->GetAreaWidth();
                default: return true;
            }
        });

        // --- Draw Row Data ---
        for (auto& light_entity : lights)
        {
            if (Light* light = light_entity->GetComponent<Light>())
            {
                if (search_buffer[0] != '\0')
                {
                    if (!is_resource_searched(light, search_buffer))
                        continue;

                    resource_search_count++;
                }

                const bool is_directional = light->GetLightType() == LightType::Directional;
                const bool is_spot        = light->GetLightType() == LightType::Spot;
                const bool is_area        = light->GetLightType() == LightType::Area;

                // Switch row
                ImGui::TableNextRow();

                // Name
                ImGui::TableSetColumnIndex(0);
                ImGui::Text(light_entity->GetObjectName().c_str());

                // Type
                ImGui::TableSetColumnIndex(1);
                const uint32_t light_type_index = static_cast<uint32_t>(light->GetLightType());
                ImGui::Text(light_types[light_type_index].c_str());

                auto light_id_str = string(to_string(light_entity->GetObjectId()));

                // Active
                ImGui::TableSetColumnIndex(2);
                bool is_active = light_entity->IsActive();
                ImGui::BeginDisabled();
                ImGuiSp::toggle_switch(("##Active_" + light_id_str).c_str(), &is_active);
                ImGui::EndDisabled();

                // Color
                ImGui::TableSetColumnIndex(3);
                ImGui::BeginDisabled();
                const Color light_color = light->GetColor();
                ImVec4 light_color_im   = ImVec4(light_color.r, light_color.g, light_color.b, light_color.a);
                ImGui::ColorButton(("##Color_" + light_id_str).c_str(), light_color_im);
                ImGui::EndDisabled();

                // Temperature
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%.0f K", light->GetTemperature());

                // Intensity
                ImGui::TableSetColumnIndex(5);
                ImGui::Text(is_directional ? "%.0f lux" : "%.0f lm", light->GetIntensityLumens());

                // Presets
                ImGui::TableSetColumnIndex(6);
                const uint32_t intensity_type_index = static_cast<uint32_t>(light->GetIntensity());
                ImGui::Text(!is_directional ? intensity_types[intensity_type_index].c_str() : "");

                // Shadows Enabled
                ImGui::TableSetColumnIndex(7);
                bool shadows_enabled = light->GetFlag(spartan::LightFlags::Shadows);
                ImGui::BeginDisabled();
                ImGuiSp::toggle_switch(("##Shadows_" + light_id_str).c_str(), &shadows_enabled);
                ImGui::EndDisabled();

                // Screen Space
                ImGui::TableSetColumnIndex(8);
                bool screen_space = light->GetFlag(spartan::LightFlags::ShadowsScreenSpace);
                ImGui::BeginDisabled();
                ImGuiSp::toggle_switch(("##ScreenSpace_" + light_id_str).c_str(), &screen_space);
                ImGui::EndDisabled();

                // Volumetric
                ImGui::TableSetColumnIndex(9);
                bool volumetric = light->GetFlag(spartan::LightFlags::Volumetric);
                ImGui::BeginDisabled();
                ImGuiSp::toggle_switch(("##Volumetric_" + light_id_str).c_str(), &volumetric);
                ImGui::EndDisabled();

                // Day/Night Cycle
                ImGui::TableSetColumnIndex(10);
                if (is_directional)
                {
                    bool time_cycle = light->GetFlag(spartan::LightFlags::DayNightCycle);
                    ImGui::BeginDisabled();
                    ImGuiSp::toggle_switch(("##DayNightCycle_" + light_id_str).c_str(), &time_cycle);
                    ImGui::EndDisabled();
                }
                else
                {
                    ImGui::Text("");
                }

                // Real-Time
                ImGui::TableSetColumnIndex(11);
                if (is_directional)
                {
                    bool real_time = light->GetFlag(spartan::LightFlags::RealTimeCycle);
                    ImGui::BeginDisabled();
                    ImGuiSp::toggle_switch(("##RealTime_" + light_id_str).c_str(), &real_time);
                    ImGui::EndDisabled();
                }
                else
                {
                    ImGui::Text("");
                }

                // Range
                ImGui::TableSetColumnIndex(12);
                ImGui::Text(!is_directional ? "%.1f m" : "", light->GetRange());

                // Angle
                ImGui::TableSetColumnIndex(13);
                ImGui::Text(is_spot ? "%.1f d" : "", light->GetRange());

                // Width
                ImGui::TableSetColumnIndex(14);
                ImGui::Text(is_area ? "%.2f m" : "", light->GetAreaWidth());

                // Height
                ImGui::TableSetColumnIndex(15);
                ImGui::Text(is_area ? "%.2f m" : "", light->GetAreaHeight());
            }
        }

        ImGui::EndTable();
    }
}
