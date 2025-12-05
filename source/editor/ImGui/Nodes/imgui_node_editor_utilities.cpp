/*
Copyright(c) 2015-2025 Panos Karabelas

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

//------------------------------------------------------------------------------
// Layout utilities for imgui-node-editor - Implementation
//------------------------------------------------------------------------------

//= INCLUDES ========================
#include "pch.h"
#include "imgui_node_editor_utilities.h"
#include "../Source/imgui_internal.h"
//===================================

namespace ImGui 
{
    
    struct LayoutItem
    {
        enum Type : uint8_t
        {
            Horizontal,
            Vertical
        };
    
        Type        item_type;
        ImGuiID     id;
        ImRect      rect;
        ImVec2      cursor_pos;
        ImVec2      size;
        float       vertical_align;
        int         spring_count;
        float       spring_size;
        float       current_line_max_height;
        ImVec2      current_line_start_pos;
    };
    
    static ImVector<LayoutItem> s_LayoutStack;
    
    void BeginHorizontal(const char* str_id, const ImVec2& size)
    {
        BeginHorizontal(GetID(str_id), size);
    }
    
    void BeginHorizontal(ImGuiID id, const ImVec2& size)
    {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return;
    
        LayoutItem item;
        item.item_type = LayoutItem::Horizontal;
        item.id = id;
        item.cursor_pos = window->DC.CursorPos;
        item.size = size;
        item.spring_count = 0;
        item.spring_size = 0.0f;
        item.current_line_max_height = 0.0f;
        item.current_line_start_pos = window->DC.CursorPos;
        item.vertical_align = -1.0f;
        item.rect.Min = window->DC.CursorPos;
        item.rect.Max = window->DC.CursorPos;
    
        s_LayoutStack.push_back(item);
    
        PushID(id);
        BeginGroup();
    }
    
    void EndHorizontal()
    {
        ImGuiWindow* window = GetCurrentWindow();
        
        EndGroup();
        PopID();
    
        if (s_LayoutStack.Size == 0)
            return;
    
        LayoutItem& item = s_LayoutStack.back();
        
        // Calculate the final size
        ImVec2 itemSize = GetItemRectSize();
        item.rect.Max = ImVec2(item.rect.Min.x + itemSize.x, item.rect.Min.y + itemSize.y);
        
        // Distribute spring spacing
        if (item.spring_count > 0 && item.spring_size > 0.0f)
        {
            float springSize = item.spring_size / (float)item.spring_count;
            // Springs have been handled during layout
        }
    
        // Update cursor position
        window->DC.CursorPos = ImVec2(item.rect.Max.x, item.cursor_pos.y);
        
        s_LayoutStack.pop_back();
        
        ItemSize(item.rect.GetSize());
        ItemAdd(item.rect, item.id);
    }
    
    void BeginVertical(const char* str_id, const ImVec2& size, float align)
    {
        BeginVertical(GetID(str_id), size, align);
    }
    
    void BeginVertical(ImGuiID id, const ImVec2& size, float align)
    {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return;
    
        LayoutItem item;
        item.item_type = LayoutItem::Vertical;
        item.id = id;
        item.cursor_pos = window->DC.CursorPos;
        item.size = size;
        item.vertical_align = align;
        item.spring_count = 0;
        item.spring_size = 0.0f;
        item.current_line_max_height = 0.0f;
        item.current_line_start_pos = window->DC.CursorPos;
        item.rect.Min = window->DC.CursorPos;
        item.rect.Max = window->DC.CursorPos;
    
        s_LayoutStack.push_back(item);
    
        PushID(id);
        BeginGroup();
    }
    
    void EndVertical()
    {
        ImGuiWindow* window = GetCurrentWindow();
        
        EndGroup();
        PopID();
    
        if (s_LayoutStack.Size == 0)
            return;
    
        LayoutItem& item = s_LayoutStack.back();
        
        // Calculate the final size
        ImVec2 itemSize = GetItemRectSize();
        item.rect.Max = ImVec2(item.rect.Min.x + itemSize.x, item.rect.Min.y + itemSize.y);
        
        // Apply vertical alignment if specified
        if (item.vertical_align >= 0.0f && item.size.y > 0.0f)
        {
            float offset = (item.size.y - itemSize.y) * item.vertical_align;
            if (offset > 0.0f)
            {
                // Adjust position based on alignment
            }
        }
    
        // Update cursor position
        window->DC.CursorPos = ImVec2(item.cursor_pos.x, item.rect.Max.y);
        
        s_LayoutStack.pop_back();
        
        ItemSize(item.rect.GetSize());
        ItemAdd(item.rect, item.id);
    }
    
    void Spring(float weight, float spacing)
    {
        ImGuiWindow* window = GetCurrentWindow();
        
        if (s_LayoutStack.Size == 0)
        {
            // No layout active, just add spacing
            if (spacing >= 0.0f)
                Dummy(ImVec2(spacing, spacing));
            return;
        }
    
        LayoutItem& item = s_LayoutStack.back();
        
        if (spacing < 0.0f)
            spacing = GetStyle().ItemSpacing.x;
    
        if (item.item_type == LayoutItem::Horizontal)
        {
            // Horizontal spring - add flexible spacing
            item.spring_count++;
            item.spring_size += spacing * weight;
            
            // Add the spacing now
            if (spacing > 0.0f)
                Dummy(ImVec2(spacing * weight, 0.0f));
        }
        else
        {
            // Vertical spring - add flexible spacing
            item.spring_count++;
            item.spring_size += spacing * weight;
            
            // Add the spacing now
            if (spacing > 0.0f)
                Dummy(ImVec2(0.0f, spacing * weight));
        }
    }
    
}
