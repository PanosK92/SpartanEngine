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

#pragma once

//= INCLUDES ======
#include "Source/imgui.h"
//=================

//= FORWARD DECLARATIONS =
class NodeWidget;
//========================

struct GridColors
{
    ImU32 grid_lines_thick = IM_COL32(200, 200, 200, 40);
    ImU32 grid_lines_thin  = IM_COL32(200, 200, 200, 10);
    ImU32 grid_background  = IM_COL32(33, 41, 45, 255);
};

struct GridSettings
{
    bool  enabled         = true;
    bool  snap_to_grid    = false;
    float grid_scale      = 50.0f;
    float min_zoom        = 0.3f;
    float max_zoom        = 2.0f;
    float zoom_smoothness = 5.0f;
    GridColors colors;
};

class Grid
{
public:
    Grid() = default;
    ~Grid() = default;

    void SetWidgetContext(NodeWidget* widget) { m_widget_context = widget; }

    // Rendering
    void Draw();
    
    // Coordinate transforms
    ImVec2 ScreenToGrid(const ImVec2& screen_pos) const;
    ImVec2 GridToScreen(const ImVec2& grid_pos) const;

    // Input handling
    void HandleInput();
    
    // Accessors
    ImVec2 GetScroll() const { return m_scroll; }
    float GetZoom() const { return m_zoom; }
    ImVec2 GetCanvasSize() const;
    ImVec2 GetCanvasPos() const;
    
    void SetScroll(const ImVec2& scroll) { m_scroll = scroll; }
    void SetZoom(float zoom);

    GridSettings& GetSettings() { return m_settings; }
    const GridSettings& GetSettings() const { return m_settings; }

private:
    void DrawGridLines();

    NodeWidget* m_widget_context = nullptr;
    GridSettings m_settings;
    ImVec2 m_scroll = ImVec2(0, 0);
    float m_zoom = 1.0f;
    float m_target_zoom = 1.0f;
    bool m_is_panning = false;
    ImVec2 m_pan_start_pos = ImVec2(0, 0);
};
