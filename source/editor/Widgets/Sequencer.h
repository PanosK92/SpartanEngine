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

private:
    // a cut to a camera, active from its time until the next event
    struct CameraEvent
    {
        float time                = 0.0f;
        uint64_t camera_entity_id = 0;
        uint64_t target_entity_id = 0; // optional, the camera pans to look at this entity
    };

    void DrawToolbar();
    void DrawTimeline();
    void DrawPopups();
    void RegisterMcpCommands();
    std::string GetMcpState() const;
    int GetEventIndexAtTime(float time) const;
    std::string GetFilePath() const;
    void Save() const;
    void Load();

    std::vector<CameraEvent> m_events; // always sorted by time
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
    uint64_t m_world_loaded_handle = 0;
};
