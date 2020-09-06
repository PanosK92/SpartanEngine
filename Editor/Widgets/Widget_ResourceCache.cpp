/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "Core/Spartan_Object.h"
//=================================

//= NAMESPACES ==========
using namespace std;
using namespace Spartan;
//=======================

Widget_ResourceCache::Widget_ResourceCache(Editor* editor) : Widget(editor)
{
    m_title            = "Resource Cache";
    m_flags            |= ImGuiWindowFlags_HorizontalScrollbar;
    m_size          = ImVec2(1366, 768);
    m_is_visible    = false;
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

void Widget_ResourceCache::Tick()
{
    auto resource_cache            = m_context->GetSubsystem<ResourceCache>();
    auto resources                = resource_cache->GetByType();
    const auto memory_usage_cpu    = resource_cache->GetMemoryUsageCpu() / 1000.0f / 1000.0f;
    const auto memory_usage_gpu = resource_cache->GetMemoryUsageGpu() / 1000.0f / 1000.0f;

    ImGui::Text("Resource count: %d, Memory usage cpu: %d Mb, Memory usage gpu: %d Mb", static_cast<uint32_t>(resources.size()), static_cast<uint32_t>(memory_usage_cpu), static_cast<uint32_t>(memory_usage_gpu));
    ImGui::Separator();
    ImGui::Columns(7, "##Widget_ResourceCache");

    // Set column width - Has to be done only once in order to allow for the user to resize them
    if (!m_column_width_set)
    {
        ImGui::SetColumnWidth(0, m_size.x * 0.15f);
        ImGui::SetColumnWidth(1, m_size.x * 0.05f);
        ImGui::SetColumnWidth(2, m_size.x * 0.15f);
        ImGui::SetColumnWidth(3, m_size.x * 0.3f);
        ImGui::SetColumnWidth(4, m_size.x * 0.3f);
        ImGui::SetColumnWidth(5, m_size.x * 0.05f);
        m_column_width_set = true;
    }

    // Set column titles
    ImGui::Text("Type");            ImGui::NextColumn();
    ImGui::Text("ID");              ImGui::NextColumn();
    ImGui::Text("Name");            ImGui::NextColumn();
    ImGui::Text("Path");            ImGui::NextColumn();
    ImGui::Text("Path (native)");   ImGui::NextColumn();
    ImGui::Text("Size CPU");        ImGui::NextColumn();
    ImGui::Text("Size GPU");        ImGui::NextColumn();
    ImGui::Separator();

    // Fill rows with resource information
    for (const shared_ptr<IResource>& resource : resources)
    {
        if (Spartan_Object* object = dynamic_cast<Spartan_Object*>(resource.get()))
        {
            // Type
            ImGui::Text(resource->GetResourceTypeCstr());                    ImGui::NextColumn();
            // ID
            ImGui::Text(to_string(object->GetId()).c_str());                ImGui::NextColumn();
            // Name
            ImGui::Text(resource->GetResourceName().c_str());                ImGui::NextColumn();
            // Path
            ImGui::Text(resource->GetResourceFilePath().c_str());            ImGui::NextColumn();
            // Path (native)
            ImGui::Text(resource->GetResourceFilePathNative().c_str());        ImGui::NextColumn();
            // Memory CPU
            print_memory(object->GetSizeCpu());                             ImGui::NextColumn();
            // Memory GPU
            print_memory(object->GetSizeGpu());                             ImGui::NextColumn();
        }
    }
    ImGui::Columns(1);
}
