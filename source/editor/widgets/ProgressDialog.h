/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ====================
#include "Widget.h"
#include "core/ProgressTracker.h"
#include <array>
#include <string>
//===============================

class ProgressDialog : public Widget
{
public:
    ProgressDialog(Editor* editor);
    ~ProgressDialog() = default;

    void OnTick() override;
    void OnTickVisible() override;
    void OnPreBegin() override;

private:
    struct OsdEntry
    {
        uint64_t id = 0;
        uint64_t step_id = 0;
        std::string title;
        std::string step;
        std::string detail;
        std::string badge;
        double elapsed_seconds = 0.0;
        float fraction = -1.0f;
        ImVec4 signal = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
        bool finished = false;
    };

    struct BarState
    {
        uint64_t id = 0;
        uint64_t step_id = 0;
        float fraction = 0.0f;
        float max_fraction = 0.0f;
        double start_time = 0.0;
        double eta_seconds = -1.0;
    };

    struct CompileBucket
    {
        uint32_t archived = 0;
        uint32_t last_submitted = 0;
        uint32_t last_completed = 0;
        bool was_active = false;
    };

    std::array<OsdEntry, 2> m_entries;
    uint32_t m_entry_count = 0;
    uint32_t m_overflow_count = 0;
    std::array<BarState, 2> m_bars;

    std::array<CompileBucket, 4> m_compile_buckets;
    double m_tracker_visible_since = 0.0;
    double m_compile_busy_sec = 0.0;
    double m_compile_done_hold_sec = 0.0;
    bool m_compile_was_shown = false;
    bool m_compile_last_was_pass2 = false;
    float m_alpha_anim = 0.0f;
    float m_bar_x = 0.0f;
    float m_bar_y = 0.0f;
    float m_bar_w = 0.0f;
    float m_bar_h = 0.0f;
};
