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

//= INCLUDES =========
#include "Widget.h"
#include <vector>
#include <string>
#include <cstdint>
//====================

class Sequencer : public Widget
{
public:
    Sequencer(Editor* editor);
    ~Sequencer() override;

    void OnTick() override;
    void OnTickVisible() override;

    // a cut to a camera, active from its time until the next event
    struct CameraEvent
    {
        float time                = 0.0f;
        uint64_t camera_entity_id = 0;
        uint64_t target_entity_id = 0; // optional, the camera pans to look at this entity
        bool operator==(const CameraEvent& other) const { return time == other.time && camera_entity_id == other.camera_entity_id && target_entity_id == other.target_entity_id; }
    };

    // a window during which an entity follows its spline, driven by the timeline
    struct SplineEvent
    {
        float start_time            = 0.0f;
        float end_time              = 0.0f;
        uint64_t follower_entity_id = 0; // entity with a spline follower component
        bool operator==(const SplineEvent& other) const { return start_time == other.start_time && end_time == other.end_time && follower_entity_id == other.follower_entity_id; }
    };

    // a full snapshot of the editable state, used by the undo/redo command
    struct State
    {
        float duration = 10.0f;
        bool loop      = false;
        std::vector<CameraEvent> events;
        std::vector<SplineEvent> spline_events;
    };

    // restores a snapshot into the widget and persists it, used by the undo/redo command
    void ApplyState(const State& state);

private:
    void DrawToolbar();
    void DrawTimeline();
    void DrawCameraTrack(float origin_x, float track_y, float width, float pixels_per_sec);
    void DrawSplineTrack(float origin_x, float track_y, float width, float pixels_per_sec);
    void DrawPopups();
    void RegisterMcpCommands();
    std::string GetMcpState() const;
    int GetEventIndexAtTime(float time) const;
    std::string GetFilePath() const;
    void Save() const;
    void Load();
    State CaptureState() const;
    void CommitState(const State& before); // pushes an undo step when the state changed, then saves

    std::vector<CameraEvent> m_events; // always sorted by time
    std::vector<SplineEvent> m_spline_events;
    float m_duration      = 10.0f;
    float m_time          = 0.0f;
    bool m_playing        = false;
    bool m_loop           = false;
    bool m_scrubbing      = false;
    bool m_was_previewing = false;
    int m_selected        = -1;
    int m_dragging        = -1;
    float m_drag_offset   = 0.0f;
    bool m_drag_moved     = false;
    float m_popup_time    = 0.0f;

    // spline track interaction state
    int m_spline_selected      = -1;
    int m_spline_dragging      = -1;
    int m_spline_drag_edge     = 0; // -1 left edge, 0 body, +1 right edge
    float m_spline_drag_offset = 0.0f;
    bool m_spline_drag_moved   = false;
    float m_spline_popup_time  = 0.0f;

    State m_drag_undo_state; // captured when a drag begins, committed when it ends

    uint64_t m_world_loaded_handle = 0;
};
