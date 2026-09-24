/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include "Widget.h"
#include "profiling/TimeBlock.h"
#include <deque>
#include <string>
#include <vector>

class Profiler : public Widget
{
public:
    Profiler(Editor* editor);
    void OnTick() override;
    void OnTickVisible() override;

private:
    // Own labels and timings only: history must not retain timestamp queries or command lists.
    struct Scope
    {
        std::string name;
        float start;
        float end;
        float duration;
        uint32_t depth;
        int lane;
        uint32_t id;
        uint32_t parent_id;
        float self = 0.0f;
    };

    struct Capture
    {
        uint64_t revision = 0;
        float wall = 0.0f;
        float cpu = 0.0f;
        float gpu = 0.0f;
        float pacing = 0.0f;
        float wait = 0.0f;
        float gpu_covered = 0.0f;
        double calibration_deviation_ms = 0.0;
        bool calibrated = true;
        bool has_gpu = false;
        uint32_t invalid_gpu = 0;
        uint32_t incomplete = 0;
        uint32_t dropped = 0;
        std::vector<Scope> scopes;
    };

    void CaptureLatest();
    void DrawHistory();
    void DrawTimeline(const Capture& capture, float height);
    void DrawDetails(const Capture& capture, float height);
    void SelectCapture(int index);

    std::deque<Capture> m_history;
    uint64_t m_last_revision = 0;
    int m_selected_capture = -1;
    int m_selected_scope = -1;
    bool m_paused = false;
    bool m_show_cpu = true;
    bool m_show_gpu = true;
    bool m_inspect_narrow = false;
    bool m_collapsed[6] = {};
    bool m_fit = true;
    bool m_auto_fit = true;
    float m_offset_ms = 0.0f;
    float m_range_ms = 16.667f;
    ImGuiTextFilter m_filter;
};
