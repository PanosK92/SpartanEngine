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

#pragma once

//= INCLUDES ==================
#include "ImGui/Source/imgui.h"
//=============================

// float[16] matches spartan::math::Matrix column-major memory
// (m00,m10,m20,m30, ...), translation at indices 3, 7, 11

namespace TransformGizmo
{
    enum class Operation
    {
        Translate,
        Rotate,
        Scale,
        Universal
    };

    enum class Space
    {
        Local,
        World
    };

    enum class Pivot
    {
        Median,
        Active,
        Individual
    };

    enum class Color
    {
        DirectionX,
        DirectionY,
        DirectionZ,
        PlaneX,
        PlaneY,
        PlaneZ,
        Selection,
        Inactive,
        Text,
        Count
    };

    struct Style
    {
        ImVec4 colors[static_cast<int>(Color::Count)];
        float translation_line_thickness   = 5.0f;
        float translation_line_arrow_size  = 22.0f;
        float rotation_line_thickness      = 4.5f;
        float scale_line_thickness         = 5.0f;
        float scale_line_circle_size       = 7.0f;
        float center_circle_size           = 7.0f;
        float plane_size                   = 0.28f;
        float hit_radius_pixels            = 16.0f;
        float gizmo_size_clip_space        = 0.135f;
        bool show_delta_hud                = true;

        // presentation
        // halo is a soft dark falloff around the handles, it replaces hard outlines
        float halo_thickness               = 8.0f;
        float halo_alpha                   = 0.35f;
        float ambient_glow                 = 0.06f;
        float glow_thickness               = 8.0f;
        float hover_animation_speed        = 16.0f;
        float inactive_dim                 = 0.30f;
        bool show_halo                     = true;
        bool show_axis_labels              = true;
    };

    // last drag readout for hud / external display
    struct DeltaInfo
    {
        Operation operation = Operation::Translate;
        float values[3]     = { 0.0f, 0.0f, 0.0f };
        bool active         = false;
    };

    void begin_frame();
    void set_rect(float x, float y, float w, float h);
    void set_draw_list(ImDrawList* list);
    void set_orthographic(bool ortho);

    // matrix_16 is read/written in place, returns true while dragging
    bool manipulate(
        const float* view,
        const float* proj,
        Operation operation,
        Space space,
        float* matrix_16,
        float* delta_matrix_16 = nullptr,
        const float* snap = nullptr
    );

    bool is_using();
    bool is_over();
    Style& get_style();
    const DeltaInfo& get_delta_info();
}
