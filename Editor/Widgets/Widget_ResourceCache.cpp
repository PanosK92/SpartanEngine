/*
Copyright(c) 2016-2021 Panos Karabelas

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

#pragma once

//= INCLUDES ======================
#include "Widget_ResourceCache.h"
#include "Resource/ResourceCache.h"
#include "../ImGui/Source/imgui.h"
#include "Core/SpartanObject.h"
//=================================

//= NAMESPACES ==========
using namespace std;
using namespace Spartan;
//=======================

Widget_ResourceCache::Widget_ResourceCache(Editor* editor) : Widget(editor)
{
    m_title         = "Resource Cache";
    m_size          = ImVec2(1366, 768);
    m_is_visible    = false;
    m_position      = k_widget_position_screen_center;
}

inline void print_memory(uint64_t memory)
{
    if (memory == 0)
    {
        ImGui::Text("0 Mb");
    }
    else if (memory < 1024)
    {
        ImGui::Text("%.4f Mb", static_cast<float>(memory) / 1000.0f / 1000.0f);
    }
    else
    {
        ImGui::Text("%.1f Mb", static_cast<float>(memory) / 1000.0f / 1000.0f);
    }
}

void Widget_ResourceCache::TickVisible()
{
    auto resource_cache             = m_context->GetSubsystem<ResourceCache>();
    auto resources                  = resource_cache->GetByType();
    const float memory_usage_cpu    = resource_cache->GetMemoryUsageCpu() / 1000.0f / 1000.0f;
    const float memory_usage_gpu    = resource_cache->GetMemoryUsageGpu() / 1000.0f / 1000.0f;

    ImGui::Text("Resource count: %d, Memory usage cpu: %d Mb, Memory usage gpu: %d Mb", static_cast<uint32_t>(resources.size()), static_cast<uint32_t>(memory_usage_cpu), static_cast<uint32_t>(memory_usage_gpu));
    ImGui::Separator();

    static ImGuiTableFlags flags =
        ImGuiTableFlags_Borders             | // Draw all borders.
        ImGuiTableFlags_RowBg               | // Set each RowBg color with ImGuiCol_TableRowBg or ImGuiCol_TableRowBgAlt (equivalent of calling TableSetBgColor with ImGuiTableBgFlags_RowBg0 on each row manually)
        ImGuiTableFlags_Resizable           | // Allow resizing columns.
        ImGuiTableFlags_ContextMenuInBody   | // Right-click on columns body/contents will display table context menu. By default it is available in TableHeadersRow().
        ImGuiTableFlags_ScrollX             | // Enable horizontal scrolling. Require 'outer_size' parameter of BeginTable() to specify the container size. Changes default sizing policy. Because this create a child window, ScrollY is currently generally recommended when using ScrollX.
        ImGuiTableFlags_ScrollY;              // Enable vertical scrolling. Require 'outer_size' parameter of BeginTable() to specify the container size.

    static ImVec2 size = ImVec2(-1.0f);
    if (ImGui::BeginTable("##Widget_ResourceCache", 7, flags, size))
    {
        // Headers
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("ID");
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Path");
        ImGui::TableSetupColumn("Path (native)");
        ImGui::TableSetupColumn("Size CPU");
        ImGui::TableSetupColumn("Size GPU");
        ImGui::TableHeadersRow();

        for (const shared_ptr<IResource>& resource : resources)
        {
            if (SpartanObject* object = dynamic_cast<SpartanObject*>(resource.get()))
            {
                // Switch row
                ImGui::TableNextRow();

                // Type
                ImGui::TableSetColumnIndex(0);
                ImGui::Text(resource->GetResourceTypeCstr());

                // ID
                ImGui::TableSetColumnIndex(1);
                ImGui::Text(to_string(object->GetObjectId()).c_str());

                // Name
                ImGui::TableSetColumnIndex(2);
                ImGui::Text(resource->GetResourceName().c_str());

                // Path
                ImGui::TableSetColumnIndex(3);
                ImGui::Text(resource->GetResourceFilePath().c_str());

                // Path (native)
                ImGui::TableSetColumnIndex(4);
                ImGui::Text(resource->GetResourceFilePathNative().c_str());
                
                // Memory CPU
                ImGui::TableSetColumnIndex(5);
                print_memory(object->GetObjectSizeCpu());
                
                // Memory GPU
                ImGui::TableSetColumnIndex(6);
                print_memory(object->GetObjectSizeGpu());
            }
        }

        ImGui::EndTable();
    }
}
