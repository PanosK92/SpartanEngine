/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ======
#include <cstdint>
#include <string>
#include "Sequencer.h"
#include "../imgui/ImGui_Style.h"
//=================

// helpers shared by the sequencer translation units
namespace sequencer_common
{
    // timeline layout
    inline constexpr float ruler_height   = 26.0f;
    inline constexpr float marker_height  = 20.0f;
    inline constexpr float track_height   = 46.0f;
    inline constexpr float lane_height    = 34.0f; // keyed tracks are thinner than event tracks
    inline constexpr float track_gap      = 1.0f;
    inline constexpr float zoom_bar_height = 10.0f;
    inline constexpr float min_event_gap  = 0.05f;
    inline constexpr float edge_grab_px   = 6.0f;
    inline constexpr float snap_px        = 8.0f;
    inline constexpr float label_width    = 156.0f;
    inline constexpr float min_view_span  = 0.25f;

    std::string format_time(float seconds);
    std::string get_entity_name(uint64_t entity_id);
    float apply_ease(Sequencer::ShotEase ease, float u);

    // timeline drawing, shared by the event tracks and the keyed lanes
    enum class Glyph : uint8_t
    {
        Play,
        Pause,
        Stop,
        ToStart,
        ToEnd,
        PrevCut,
        NextCut,
        Loop,
        Plus,
        Fit,
        Eye,
        Camera,
        Rig,
        Orbit,
        DollyZoom,
        Path,
        Spline,
        Drive,
        Record
    };
    ImU32 to_color(const ImVec4& color, float alpha = 1.0f);
    ImVec4 channel_color(Sequencer::Channel channel);
    std::string shot_title(const Sequencer::CameraEvent& event);
    Glyph shot_glyph(const Sequencer::CameraEvent& event);
    ImVec4 shot_hue(int index);
    ImVec4 track_hue(int track);
    void draw_glyph(ImDrawList* draw, Glyph glyph, const ImVec2& center, float size, ImU32 color);
    bool glyph_button(const char* id, Glyph glyph, bool active, const char* tooltip, float size = 26.0f);
    void draw_clip(ImDrawList* draw, const ImVec2& min, const ImVec2& max, const ImVec4& hue, bool selected, bool hovered);
    void draw_clip_text(ImDrawList* draw, const ImVec2& min, const ImVec2& max, const char* title, const char* subtitle, const char* right, bool has_glyph, Glyph glyph);
    void draw_empty_hint(ImDrawList* draw, const ImVec2& min, const ImVec2& max, const char* hint);
    void draw_time_readout(ImDrawList* draw, const ImVec2& anchor, float time);

    // inspector layout
    void inspector_label(const char* label, float full_width);
    void inspector_section(const char* title);
    void inspector_header(const char* kicker, const char* title, const char* subtitle, const ImVec4& hue);
    bool danger_button(const char* label, const ImVec2& size);
    bool target_combo(const char* label, uint64_t& target_id);
    bool spline_combo(const char* label, uint64_t& spline_id);
}
