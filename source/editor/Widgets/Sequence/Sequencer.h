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

//= INCLUDES ===========
#include "../Widget.h"
#include "Sequence.h"
#include <vector>
//======================

class Sequencer : public Widget
{
public:
    Sequencer(Editor* editor);

    void OnTickVisible() override;

    void Save();
    void Load();

private:
    void DrawSequenceSelector();
    void DrawEmptyTracksState(spartan::Sequence* sequence);
    void DrawToolbar(spartan::Sequence* sequence);
    void DrawTimeline(spartan::Sequence* sequence);
    void DrawTrackList(spartan::Sequence* sequence);
    void DrawTimelineContent(spartan::Sequence* sequence);
    void DrawPlayhead(spartan::Sequence* sequence, float timeline_x, float timeline_width, float timeline_y, float timeline_height);
    void DrawAddTrackPopup(spartan::Sequence* sequence);
    void DrawSelectionProperties(spartan::Sequence* sequence);

    spartan::Sequence* ResolveSequence();
    float SnapTime(float time) const;
    std::string GetFilePath() const;

    // the sequencer owns all sequences
    std::vector<spartan::Sequence> m_sequences;
    uint64_t m_active_sequence_id = 0;

    // timeline view state
    float m_scroll_x       = 0.0f;
    float m_pixels_per_sec = 100.0f;
    float m_track_height   = 32.0f;

    // snap
    bool  m_snap_enabled  = false;
    float m_snap_interval = 0.25f;

    // interaction state
    int32_t m_selected_track    = -1;
    int32_t m_selected_keyframe = -1;
    int32_t m_selected_clip     = -1;
    int32_t m_dragging_keyframe = -1;
    int32_t m_dragging_track    = -1;
    bool    m_scrubbing         = false;

    // inline rename
    int32_t m_renaming_track = -1;
    char    m_rename_buf[128] = {};

    // right-click position (stored when opening timeline context popups)
    float m_popup_mouse_x = 0.0f;

    // settings popup
    bool m_show_settings = false;
};
