/*
Copyright(c) 2015-2025 Panos Karabelas & Thomas Ray

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
// Layout utilities for imgui-node-editor
// These functions provide layout helpers for building node UI
//------------------------------------------------------------------------------

#pragma once

//= INCLUDES ========================
#include "../Source/imgui.h"
//===================================

namespace ImGui 
{

    // Begin a horizontal layout group
    void BeginHorizontal(const char* str_id, const ImVec2& size = ImVec2(0, 0));
    void BeginHorizontal(ImGuiID id, const ImVec2& size = ImVec2(0, 0));
    
    // End a horizontal layout group
    void EndHorizontal();
    
    // Begin a vertical layout group
    void BeginVertical(const char* str_id, const ImVec2& size = ImVec2(0, 0), float align = -1.0f);
    void BeginVertical(ImGuiID id, const ImVec2& size = ImVec2(0, 0), float align = -1.0f);
    
    // End a vertical layout group
    void EndVertical();
    
    // Add flexible spacing (spring)
    void Spring(float weight = 1.0f, float spacing = -1.0f);

} // namespace ImGui

