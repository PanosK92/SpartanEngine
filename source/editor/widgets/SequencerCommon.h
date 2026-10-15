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
//=================

// helpers shared by the sequencer translation units
namespace sequencer_common
{
    // timeline layout
    inline constexpr float ruler_height  = 24.0f;
    inline constexpr float track_height  = 40.0f;
    inline constexpr float lane_height   = 30.0f; // keyed tracks are thinner than event tracks
    inline constexpr float min_event_gap = 0.05f;
    inline constexpr float edge_grab_px  = 6.0f;
    inline constexpr float label_width   = 72.0f;

    std::string format_time(float seconds);
    std::string get_entity_name(uint64_t entity_id);
    float apply_ease(Sequencer::ShotEase ease, float u);

    // inspector layout
    void inspector_label(const char* label, float full_width);
    void inspector_section(const char* title);
    bool target_combo(const char* label, uint64_t& target_id);
    bool spline_combo(const char* label, uint64_t& spline_id);
}
