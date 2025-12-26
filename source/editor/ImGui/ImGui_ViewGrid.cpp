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

//= INCLUDES =====================
#include "pch.h"
#include "ImGui_ViewGrid.h"
#include "../Widgets/NodeWidget.h"
//================================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
//======================

void Grid::Draw()
{
    if (!m_settings.enabled)
        return;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 canvas_pos = GetCanvasPos();
    const ImVec2 canvas_size = GetCanvasSize();
    
    // Draw background
    draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), 
        m_settings.colors.grid_background);

    DrawGridLines();
}

void Grid::DrawGridLines()
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 canvas_pos = GetCanvasPos();
    const ImVec2 canvas_size = GetCanvasSize();
    
    const float grid_step = m_settings.grid_scale * m_zoom;
    
    // Calculate grid offset based on scroll
    const float offset_x = fmodf(m_scroll.x * m_zoom, grid_step);
    const float offset_y = fmodf(m_scroll.y * m_zoom, grid_step);
    
    // Draw vertical lines
    for (float x = offset_x; x < canvas_size.x; x += grid_step)
    {
        const bool is_thick_line = fmodf(x - offset_x, grid_step * 10.0f) < 0.1f;
        const ImU32 color = is_thick_line ? m_settings.colors.grid_lines_thick : m_settings.colors.grid_lines_thin;
        draw_list->AddLine(
            ImVec2(canvas_pos.x + x, canvas_pos.y),
            ImVec2(canvas_pos.x + x, canvas_pos.y + canvas_size.y),
            color);
    }
    
    // Draw horizontal lines
    for (float y = offset_y; y < canvas_size.y; y += grid_step)
    {
        const bool is_thick_line = fmodf(y - offset_y, grid_step * 10.0f) < 0.1f;
        const ImU32 color = is_thick_line ? m_settings.colors.grid_lines_thick : m_settings.colors.grid_lines_thin;
        draw_list->AddLine(
            ImVec2(canvas_pos.x, canvas_pos.y + y),
            ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + y),
            color);
    }
}

ImVec2 Grid::ScreenToGrid(const ImVec2& screen_pos) const
{
    const ImVec2 canvas_pos = GetCanvasPos();
    return ImVec2(
        (screen_pos.x - canvas_pos.x) / m_zoom - m_scroll.x,
        (screen_pos.y - canvas_pos.y) / m_zoom - m_scroll.y
    );
}

ImVec2 Grid::GridToScreen(const ImVec2& grid_pos) const
{
    const ImVec2 canvas_pos = GetCanvasPos();
    return ImVec2(
        (grid_pos.x + m_scroll.x) * m_zoom + canvas_pos.x,
        (grid_pos.y + m_scroll.y) * m_zoom + canvas_pos.y
    );
}

void Grid::HandleInput()
{
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 canvas_pos = GetCanvasPos();
    const ImVec2 canvas_size = GetCanvasSize();
    const ImVec2 mouse_pos = ImGui::GetMousePos();
    
    const bool is_mouse_in_canvas = 
        mouse_pos.x >= canvas_pos.x && mouse_pos.x <= canvas_pos.x + canvas_size.x &&
        mouse_pos.y >= canvas_pos.y && mouse_pos.y <= canvas_pos.y + canvas_size.y;
    
    if (!is_mouse_in_canvas)
        return;
    
    // Handle zoom
    if (io.MouseWheel != 0.0f)
    {
        const float zoom_delta = io.MouseWheel * 0.1f;
        float new_zoom = m_target_zoom + zoom_delta;
        
        // Clamp zoom
        if (new_zoom < m_settings.min_zoom)
            new_zoom = m_settings.min_zoom;
        if (new_zoom > m_settings.max_zoom)
            new_zoom = m_settings.max_zoom;
            
        m_target_zoom = new_zoom;
    }
    
    // Smooth zoom interpolation
    if (fabsf(m_zoom - m_target_zoom) > 0.01f)
    {
        // Linear interpolation
        m_zoom = m_zoom + (m_target_zoom - m_zoom) * io.DeltaTime * m_settings.zoom_smoothness;
    }
    else
    {
        m_zoom = m_target_zoom;
    }
    
    // Handle panning (middle mouse button or Alt+Left mouse button)
    const bool pan_button = ImGui::IsMouseDown(ImGuiMouseButton_Middle) || 
        (ImGui::IsMouseDown(ImGuiMouseButton_Left) && io.KeyAlt);
    
    if (pan_button && !m_is_panning)
    {
        m_is_panning = true;
        m_pan_start_pos = mouse_pos;
    }
    else if (!pan_button && m_is_panning)
    {
        m_is_panning = false;
    }
    
    if (m_is_panning)
    {
        const ImVec2 delta = ImVec2(mouse_pos.x - m_pan_start_pos.x, mouse_pos.y - m_pan_start_pos.y);
        m_scroll.x += delta.x / m_zoom;
        m_scroll.y += delta.y / m_zoom;
        m_pan_start_pos = mouse_pos;
    }
}

void Grid::SetZoom(float zoom)
{
    // Clamp zoom
    if (zoom < m_settings.min_zoom)
        zoom = m_settings.min_zoom;
    if (zoom > m_settings.max_zoom)
        zoom = m_settings.max_zoom;
        
    m_zoom = zoom;
    m_target_zoom = m_zoom;
}

ImVec2 Grid::GetCanvasSize() const
{
    return ImGui::GetContentRegionAvail();
}

ImVec2 Grid::GetCanvasPos() const
{
    return ImGui::GetCursorScreenPos();
}
