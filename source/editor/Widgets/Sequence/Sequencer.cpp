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

//= INCLUDES ===========================
#include "pch.h"
#include "Sequencer.h"
#include "World/World.h"
#include "World/Entity.h"
#include "World/Components/Camera.h"
#include "../../ImGui/ImGui_Extension.h"
#include "../../ImGui/ImGui_Style.h"
#include "FileSystem/FileSystem.h"
SP_WARNINGS_OFF
#include "IO/pugixml.hpp"
SP_WARNINGS_ON
//======================================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
using namespace math;
//======================

namespace
{
    // layout
    const float ruler_height   = 26.0f;
    const float minimap_height = 20.0f;

    // track type accent hues (blended with theme at runtime)
    const ImVec4 hue_camera    = ImVec4(0.35f, 0.55f, 0.85f, 1.0f);
    const ImVec4 hue_transform = ImVec4(0.85f, 0.70f, 0.25f, 1.0f);
    const ImVec4 hue_event     = ImVec4(0.85f, 0.35f, 0.35f, 1.0f);
    const ImVec4 hue_sequence  = ImVec4(0.75f, 0.40f, 0.65f, 1.0f);

    // theme-derived colors, recomputed each frame
    struct TimelineColors
    {
        ImVec4 camera_accent;
        ImVec4 transform_accent;
        ImVec4 event_accent;
        ImVec4 sequence_accent;
        ImU32  ruler_bg;
        ImU32  ruler_text;
        ImU32  ruler_tick;
        ImU32  playhead;
        ImU32  track_bg_a;
        ImU32  track_bg_b;
        ImU32  group_bg;
        ImU32  selected;
        ImU32  text_primary;
        ImU32  text_dim;
        ImU32  separator;
        ImU32  snap_grid;
    };

    TimelineColors tc;

    ImVec4 blend_accent(const ImVec4& hue, float strength = 0.7f)
    {
        ImVec4 base = ImGui::Style::bg_color_2;
        return ImVec4(
            base.x + (hue.x - base.x) * strength,
            base.y + (hue.y - base.y) * strength,
            base.z + (hue.z - base.z) * strength,
            1.0f
        );
    }

    void compute_theme_colors()
    {
        const ImVec4& bg1 = ImGui::Style::bg_color_1;
        const ImVec4& bg2 = ImGui::Style::bg_color_2;
        const ImVec4& txt = ImGui::Style::h_color_1;

        tc.camera_accent    = blend_accent(hue_camera);
        tc.transform_accent = blend_accent(hue_transform);
        tc.event_accent     = blend_accent(hue_event);
        tc.sequence_accent  = blend_accent(hue_sequence);

        auto to_u32 = [](float r, float g, float b, float a) -> ImU32 {
            return IM_COL32(
                static_cast<int>(r * 255), static_cast<int>(g * 255),
                static_cast<int>(b * 255), static_cast<int>(a * 255));
        };

        ImVec4 deep = ImGui::Style::lerp(bg1, ImVec4(0, 0, 0, 1), 0.15f);
        ImVec4 mid  = ImGui::Style::lerp(bg1, bg2, 0.15f);

        tc.ruler_bg    = to_u32(deep.x, deep.y, deep.z, 1.0f);
        tc.ruler_text  = to_u32(txt.x * 0.7f, txt.y * 0.7f, txt.z * 0.7f, 1.0f);
        tc.ruler_tick  = to_u32(mid.x, mid.y, mid.z, 1.0f);
        tc.playhead    = IM_COL32(220, 60, 60, 255);
        tc.track_bg_a  = to_u32(bg1.x * 1.05f, bg1.y * 1.05f, bg1.z * 1.05f, 1.0f);
        tc.track_bg_b  = to_u32(bg1.x * 1.15f, bg1.y * 1.15f, bg1.z * 1.15f, 1.0f);
        tc.group_bg    = to_u32(deep.x, deep.y, deep.z, 1.0f);
        tc.selected    = IM_COL32(255, 255, 100, 255);
        tc.text_primary = to_u32(txt.x, txt.y, txt.z, 1.0f);
        tc.text_dim    = to_u32(txt.x * 0.5f, txt.y * 0.5f, txt.z * 0.5f, 1.0f);
        tc.separator   = to_u32(mid.x, mid.y, mid.z, 0.6f);
        tc.snap_grid   = to_u32(mid.x, mid.y, mid.z, 0.15f);
    }

    ImU32 to_imu32(const ImVec4& c, float alpha = 1.0f)
    {
        return IM_COL32(
            static_cast<int>(c.x * 255), static_cast<int>(c.y * 255),
            static_cast<int>(c.z * 255), static_cast<int>(alpha * 255));
    }

    const ImVec4& track_type_color(SequenceTrackType type, const SequenceTrack* track = nullptr)
    {
        if (track && (track->custom_color.x != 0.0f || track->custom_color.y != 0.0f || track->custom_color.z != 0.0f))
        {
            static ImVec4 custom;
            custom = ImVec4(track->custom_color.x, track->custom_color.y, track->custom_color.z, track->custom_color.w);
            return custom;
        }

        switch (type)
        {
            case SequenceTrackType::CameraCut: return tc.camera_accent;
            case SequenceTrackType::Transform: return tc.transform_accent;
            case SequenceTrackType::Event:     return tc.event_accent;
            default:                           return tc.sequence_accent;
        }
    }

    const char* track_type_badge(SequenceTrackType type)
    {
        switch (type)
        {
            case SequenceTrackType::CameraCut: return "CAM";
            case SequenceTrackType::Transform: return "TFM";
            case SequenceTrackType::Event:     return "EVT";
            default:                           return "???";
        }
    }

    const char* track_type_label(SequenceTrackType type)
    {
        switch (type)
        {
            case SequenceTrackType::CameraCut: return "Camera Cut";
            case SequenceTrackType::Transform: return "Transform";
            case SequenceTrackType::Event:     return "Event";
            default:                           return "Unknown";
        }
    }

    const char* event_action_label(SequenceEventAction action)
    {
        switch (action)
        {
            case SequenceEventAction::CarEnter:               return "Car Enter";
            case SequenceEventAction::CarExit:                return "Car Exit";
            case SequenceEventAction::SetSplineFollowerSpeed: return "Set Spline Speed";
            case SequenceEventAction::PlayAudio:              return "Play Audio";
            case SequenceEventAction::StopAudio:              return "Stop Audio";
            default:                                          return "Unknown";
        }
    }

    const char* interpolation_mode_label(InterpolationMode mode)
    {
        switch (mode)
        {
            case InterpolationMode::Linear:     return "linear";
            case InterpolationMode::CatmullRom: return "catmull-rom";
            case InterpolationMode::EaseIn:     return "ease in";
            case InterpolationMode::EaseOut:    return "ease out";
            case InterpolationMode::EaseInOut:  return "ease in/out";
            default:                            return "catmull-rom";
        }
    }

    void toolbar_separator()
    {
        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();
    }

    // adaptive ruler step: pick a "nice" interval so labels don't overlap
    float compute_ruler_step(float pixels_per_sec)
    {
        float min_label_spacing = 60.0f;
        float raw_step = min_label_spacing / pixels_per_sec;

        // snap to a nice value from the sequence: 0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60...
        static const float nice_steps[] = { 0.05f, 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 15.0f, 30.0f, 60.0f, 120.0f, 300.0f };
        for (float s : nice_steps)
        {
            if (s >= raw_step)
            {
                return s;
            }
        }
        return 300.0f;
    }
}

Sequencer::Sequencer(Editor* editor) : Widget(editor)
{
    m_title   = "Sequencer";
    m_visible = false;
    m_flags   = ImGuiWindowFlags_NoCollapse;
}

string Sequencer::GetFilePath() const
{
    const string& world_path = World::GetFilePath();
    if (world_path.empty())
    {
        return "";
    }

    return FileSystem::GetDirectoryFromFilePath(world_path) + "sequencer.xml";
}

void Sequencer::Save()
{
    string file_path = GetFilePath();
    if (file_path.empty())
    {
        return;
    }

    pugi::xml_document doc;
    pugi::xml_node root = doc.append_child("Sequencer");

    for (Sequence& seq : m_sequences)
    {
        pugi::xml_node seq_node = root.append_child("Sequence");
        seq.Save(seq_node);
    }

    if (!doc.save_file(file_path.c_str(), " ", pugi::format_indent))
    {
        SP_LOG_ERROR("Failed to save sequencer file: %s", file_path.c_str());
    }
}

void Sequencer::Load()
{
    m_sequences.clear();
    m_collapsed_sequences.clear();

    string file_path = GetFilePath();
    if (file_path.empty())
    {
        return;
    }

    pugi::xml_document doc;
    if (!doc.load_file(file_path.c_str()))
    {
        return;
    }

    pugi::xml_node root = doc.child("Sequencer");
    if (!root)
    {
        return;
    }

    for (pugi::xml_node seq_node = root.child("Sequence"); seq_node; seq_node = seq_node.next_sibling("Sequence"))
    {
        Sequence seq;
        seq.Load(seq_node);
        m_sequences.push_back(move(seq));
    }
}

float Sequencer::SnapTime(float time) const
{
    if (!m_snap_enabled || m_snap_interval <= 0.0f)
    {
        return time;
    }

    return roundf(time / m_snap_interval) * m_snap_interval;
}

float Sequencer::ComputeTotalHeight() const
{
    float height = 0.0f;
    for (uint32_t s = 0; s < static_cast<uint32_t>(m_sequences.size()); s++)
    {
        height += m_group_height;
        bool collapsed = m_collapsed_sequences.count(m_sequences[s].GetId()) > 0;
        if (!collapsed)
        {
            height += m_track_height * static_cast<float>(m_sequences[s].GetTracks().size());
        }
    }
    return height;
}

void Sequencer::ClearSelection()
{
    m_selected_sequence = -1;
    m_selected_track    = -1;
    m_selected_keyframe = -1;
    m_selected_clip     = -1;
    m_selected_keyframes.clear();
}

void Sequencer::DeleteSelection()
{
    if (m_selected_sequence < 0 || m_selected_sequence >= static_cast<int32_t>(m_sequences.size()))
    {
        return;
    }

    Sequence& seq = m_sequences[m_selected_sequence];
    auto& tracks = seq.GetTracks();

    if (m_selected_track < 0 || m_selected_track >= static_cast<int32_t>(tracks.size()))
    {
        return;
    }

    SequenceTrack& track = tracks[m_selected_track];

    // multi-select delete
    if (!m_selected_keyframes.empty() && track.type == SequenceTrackType::Transform)
    {
        vector<uint32_t> sorted_indices(m_selected_keyframes.begin(), m_selected_keyframes.end());
        sort(sorted_indices.rbegin(), sorted_indices.rend());
        for (uint32_t idx : sorted_indices)
        {
            if (idx < static_cast<uint32_t>(track.keyframes.size()))
            {
                track.keyframes.erase(track.keyframes.begin() + idx);
            }
        }
        m_selected_keyframes.clear();
        m_selected_keyframe = -1;
        return;
    }

    if (track.type == SequenceTrackType::Transform && m_selected_keyframe >= 0 && m_selected_keyframe < static_cast<int32_t>(track.keyframes.size()))
    {
        track.keyframes.erase(track.keyframes.begin() + m_selected_keyframe);
        m_selected_keyframe = -1;
    }
    else if (track.type == SequenceTrackType::CameraCut && m_selected_clip >= 0 && m_selected_clip < static_cast<int32_t>(track.camera_clips.size()))
    {
        track.camera_clips.erase(track.camera_clips.begin() + m_selected_clip);
        m_selected_clip = -1;
    }
    else if (track.type == SequenceTrackType::Event && m_selected_keyframe >= 0 && m_selected_keyframe < static_cast<int32_t>(track.event_clips.size()))
    {
        track.event_clips.erase(track.event_clips.begin() + m_selected_keyframe);
        m_selected_keyframe = -1;
    }
}

void Sequencer::DuplicateSelection()
{
    if (m_selected_sequence < 0 || m_selected_sequence >= static_cast<int32_t>(m_sequences.size()))
    {
        return;
    }

    Sequence& seq = m_sequences[m_selected_sequence];
    auto& tracks = seq.GetTracks();

    if (m_selected_track < 0 || m_selected_track >= static_cast<int32_t>(tracks.size()))
    {
        return;
    }

    SequenceTrack& track = tracks[m_selected_track];

    if (track.type == SequenceTrackType::Transform && m_selected_keyframe >= 0 && m_selected_keyframe < static_cast<int32_t>(track.keyframes.size()))
    {
        SequenceKeyframe kf = track.keyframes[m_selected_keyframe];
        kf.time += 0.5f;
        if (kf.time > seq.GetDuration())
        {
            kf.time = seq.GetDuration();
        }
        track.keyframes.push_back(kf);
        sort(track.keyframes.begin(), track.keyframes.end(),
            [](const SequenceKeyframe& a, const SequenceKeyframe& b) { return a.time < b.time; });
    }
    else if (track.type == SequenceTrackType::CameraCut && m_selected_clip >= 0 && m_selected_clip < static_cast<int32_t>(track.camera_clips.size()))
    {
        SequenceCameraCutClip clip = track.camera_clips[m_selected_clip];
        float duration = clip.end_time - clip.start_time;
        clip.start_time = clip.end_time;
        clip.end_time   = min(clip.start_time + duration, seq.GetDuration());
        if (clip.start_time < seq.GetDuration())
        {
            track.camera_clips.push_back(clip);
        }
    }
}

void Sequencer::HandleInput()
{
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))
    {
        return;
    }

    // don't consume keys while renaming
    if (m_renaming_sequence >= 0 || m_renaming_track >= 0)
    {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

    if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
    {
        bool any_playing = false;
        for (const Sequence& seq : m_sequences)
        {
            if (seq.IsPlaying() && !seq.IsPaused())
            {
                any_playing = true;
            }
        }

        if (any_playing)
        {
            for (Sequence& seq : m_sequences)
                seq.Pause();
        }
        else
        {
            for (Sequence& seq : m_sequences)
                seq.Play();
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
    {
        DeleteSelection();
    }

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
    {
        Save();
    }

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
    {
        DuplicateSelection();
    }

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false))
    {
        if (m_selected_sequence >= 0 && m_selected_track >= 0)
        {
            auto& tracks = m_sequences[m_selected_sequence].GetTracks();
            if (m_selected_track < static_cast<int32_t>(tracks.size()))
            {
                m_selected_keyframes.clear();
                for (uint32_t k = 0; k < static_cast<uint32_t>(tracks[m_selected_track].keyframes.size()); k++)
                    m_selected_keyframes.insert(k);
            }
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Home, false))
    {
        for (Sequence& seq : m_sequences)
            seq.SetPlaybackTime(0.0f);
    }

    if (ImGui::IsKeyPressed(ImGuiKey_End, false))
    {
        for (Sequence& seq : m_sequences)
            seq.SetPlaybackTime(seq.GetDuration());
    }

    // nudge selected keyframe with arrow keys
    float nudge = m_snap_enabled ? m_snap_interval : 0.05f;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true) && m_selected_sequence >= 0 && m_selected_track >= 0)
    {
        auto& tracks = m_sequences[m_selected_sequence].GetTracks();
        if (m_selected_track < static_cast<int32_t>(tracks.size()))
        {
            if (m_selected_keyframe >= 0 && m_selected_keyframe < static_cast<int32_t>(tracks[m_selected_track].keyframes.size()))
            {
                float& t = tracks[m_selected_track].keyframes[m_selected_keyframe].time;
                t = max(0.0f, t - nudge);
            }
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true) && m_selected_sequence >= 0 && m_selected_track >= 0)
    {
        auto& tracks = m_sequences[m_selected_sequence].GetTracks();
        if (m_selected_track < static_cast<int32_t>(tracks.size()))
        {
            if (m_selected_keyframe >= 0 && m_selected_keyframe < static_cast<int32_t>(tracks[m_selected_track].keyframes.size()))
            {
                float& t = tracks[m_selected_track].keyframes[m_selected_keyframe].time;
                t = min(m_sequences[m_selected_sequence].GetDuration(), t + nudge);
            }
        }
    }
}

void Sequencer::OnTickVisible()
{
    compute_theme_colors();

    for (Sequence& seq : m_sequences)
    {
        seq.Tick();
    }

    if (m_sequences.empty())
    {
        DrawEmptyState();
        return;
    }

    HandleInput();

    m_global_duration = 1.0f;
    for (const Sequence& seq : m_sequences)
    {
        m_global_duration = max(m_global_duration, seq.GetDuration());
    }

    DrawToolbar();
    ImGui::Separator();
    DrawTimeline();
    DrawSelectionProperties();
    DrawAddTrackPopup();
}

void Sequencer::DrawEmptyState()
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float center_x = avail.x * 0.5f;

    ImGui::Dummy(ImVec2(0, avail.y * 0.2f));

    ImGui::PushFont(Editor::font_bold, 0.0f);
    ImVec2 title_size = ImGui::CalcTextSize("cinematic sequencer");
    ImGui::SetCursorPosX(center_x - title_size.x * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(tc.sequence_accent.x, tc.sequence_accent.y, tc.sequence_accent.z, 1.0f));
    ImGui::TextUnformatted("cinematic sequencer");
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0, 8.0f));

    const char* desc = "create timeline-based cinematics with camera cuts,\nentity animations, and scripted events.";
    ImVec2 desc_size = ImGui::CalcTextSize(desc);
    ImGui::SetCursorPosX(center_x - desc_size.x * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(desc);
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, 20.0f));

    const char* btn_label = "create new sequence";
    ImVec2 btn_size = ImVec2(ImGui::CalcTextSize(btn_label).x + 32.0f, 32.0f);

    ImGui::SetCursorPosX(center_x - btn_size.x * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(tc.sequence_accent.x * 0.35f, tc.sequence_accent.y * 0.35f, tc.sequence_accent.z * 0.35f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(tc.sequence_accent.x * 0.5f, tc.sequence_accent.y * 0.5f, tc.sequence_accent.z * 0.5f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(tc.sequence_accent.x * 0.25f, tc.sequence_accent.y * 0.25f, tc.sequence_accent.z * 0.25f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

    if (ImGui::Button(btn_label, btn_size))
    {
        m_sequences.emplace_back();
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    ImGui::Dummy(ImVec2(0, 16.0f));

    ImGui::SetCursorPosX(center_x - 140.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted("1. click the button above to create a sequence");
    ImGui::SetCursorPosX(center_x - 140.0f);
    ImGui::TextUnformatted("2. add tracks for cameras, animations, and events");
    ImGui::SetCursorPosX(center_x - 140.0f);
    ImGui::TextUnformatted("3. press play to preview your cinematic");
    ImGui::PopStyleColor();
}

void Sequencer::DrawToolbar()
{
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);

    bool any_playing = false;
    bool any_looping = false;
    for (const Sequence& seq : m_sequences)
    {
        if (seq.IsPlaying() && !seq.IsPaused())
        {
            any_playing = true;
        }
        if (seq.IsLooping())
        {
            any_looping = true;
        }
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    {
        if (any_playing)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.35f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.45f, 0.2f, 1.0f));
            if (ImGui::Button("||", ImVec2(28, 0)))
            {
                for (Sequence& seq : m_sequences)
                    seq.Pause();
            }
            ImGui::PopStyleColor(2);
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.55f, 0.25f, 1.0f));
            if (ImGui::Button(">", ImVec2(28, 0)))
            {
                for (Sequence& seq : m_sequences)
                    seq.Play();
            }
            ImGui::PopStyleColor(2);
        }

        ImGui::SameLine();
        if (ImGui::Button("[]", ImVec2(28, 0)))
        {
            for (Sequence& seq : m_sequences)
                seq.StopPlayback();
        }

        ImGui::SameLine();
        if (any_looping)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.55f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.65f, 1.0f));
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Button));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
        }
        if (ImGui::Button("loop", ImVec2(0, 0)))
        {
            bool new_loop = !any_looping;
            for (Sequence& seq : m_sequences)
                seq.SetLooping(new_loop);
        }
        ImGui::PopStyleColor(2);
    }
    ImGui::PopStyleVar();

    toolbar_separator();
    {
        float current_time = 0.0f;
        for (const Sequence& seq : m_sequences)
            current_time = max(current_time, seq.GetPlaybackTime());

        int cur_min = static_cast<int>(current_time) / 60;
        int cur_sec = static_cast<int>(current_time) % 60;
        int cur_fra = static_cast<int>((current_time - floorf(current_time)) * 100.0f);
        int dur_min = static_cast<int>(m_global_duration) / 60;
        int dur_sec = static_cast<int>(m_global_duration) % 60;
        int dur_fra = static_cast<int>((m_global_duration - floorf(m_global_duration)) * 100.0f);

        char timecode[64];
        snprintf(timecode, sizeof(timecode), "%02d:%02d.%02d / %02d:%02d.%02d",
            cur_min, cur_sec, cur_fra, dur_min, dur_sec, dur_fra);

        ImGui::PushFont(Editor::font_bold, 0.0f);
        ImGui::TextUnformatted(timecode);
        ImGui::PopFont();
    }

    toolbar_separator();
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(tc.sequence_accent.x * 0.35f, tc.sequence_accent.y * 0.35f, tc.sequence_accent.z * 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(tc.sequence_accent.x * 0.5f, tc.sequence_accent.y * 0.5f, tc.sequence_accent.z * 0.5f, 1.0f));
        if (ImGui::Button("+ sequence"))
        {
            m_sequences.emplace_back();
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();

        ImGui::SameLine();
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        if (ImGui::Button("save"))
        {
            Save();
        }
        ImGui::PopStyleVar();

        ImGui::SameLine();
        if (m_snap_enabled)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.55f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.65f, 1.0f));
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Button));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        if (ImGui::Button("snap"))
        {
            m_snap_enabled = !m_snap_enabled;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::SliderFloat("##zoom", &m_pixels_per_sec, 20.0f, 400.0f, "zoom: %.0f");
    }

    toolbar_separator();
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        if (ImGui::Button("settings"))
        {
            ImGui::OpenPopup("##seq_settings");
        }
        ImGui::PopStyleVar();

        if (ImGui::BeginPopup("##seq_settings"))
        {
            ImGui::Text("sequencer settings");
            ImGui::Separator();

            ImGui::SetNextItemWidth(100.0f);
            ImGui::DragFloat("snap interval", &m_snap_interval, 0.05f, 0.05f, 5.0f, "%.2f s");

            ImGui::SetNextItemWidth(100.0f);
            ImGui::DragFloat("track height", &m_track_height, 1.0f, 20.0f, 60.0f, "%.0f px");

            ImGui::SetNextItemWidth(100.0f);
            ImGui::DragFloat("group height", &m_group_height, 1.0f, 20.0f, 50.0f, "%.0f px");

            ImGui::EndPopup();
        }
    }
}

void Sequencer::DrawTimeline()
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 10.0f || avail.y < 10.0f)
    {
        return;
    }

    float total_track_h = ComputeTotalHeight();
    float content_height = ruler_height + minimap_height + total_track_h + 20.0f;
    float timeline_height = min(content_height, avail.y * 0.75f);
    timeline_height = max(timeline_height, 80.0f);

    ImGui::BeginChild("##timeline_region", ImVec2(avail.x, timeline_height), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // left pane: track list
    ImGui::BeginChild("##track_list", ImVec2(m_track_list_width, 0), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
        ImGui::Dummy(ImVec2(0, ruler_height + minimap_height));
        DrawTrackList();
        ImGui::Dummy(ImVec2(0, 20.0f));
        ImGui::SetScrollY(m_scroll_y);
    }
    ImGui::EndChild();

    // splitter handle
    ImGui::SameLine(0, 0);
    {
        ImVec2 splitter_pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##splitter", ImVec2(4.0f, timeline_height));
        bool splitter_hovered = ImGui::IsItemHovered();

        if (ImGui::IsItemActive())
        {
            m_resizing_splitter = true;
            m_track_list_width += ImGui::GetIO().MouseDelta.x;
            m_track_list_width = max(120.0f, min(m_track_list_width, 400.0f));
        }
        else
        {
            m_resizing_splitter = false;
        }

        if (splitter_hovered || m_resizing_splitter)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            ImGui::GetWindowDrawList()->AddRectFilled(
                splitter_pos,
                ImVec2(splitter_pos.x + 2.0f, splitter_pos.y + timeline_height),
                to_imu32(tc.sequence_accent, 0.6f)
            );
        }
    }

    ImGui::SameLine(0, 0);

    // right pane: timeline content
    ImGui::BeginChild("##timeline_content", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    {
        // zoom toward mouse with ctrl+wheel
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
        {
            ImGuiIO& io = ImGui::GetIO();
            if (io.KeyCtrl && io.MouseWheel != 0.0f)
            {
                float mouse_x = io.MousePos.x;
                float scroll_x = ImGui::GetScrollX();
                float canvas_x = ImGui::GetCursorScreenPos().x;
                float time_under_mouse = (mouse_x - canvas_x + scroll_x) / m_pixels_per_sec;

                float zoom_factor = (io.MouseWheel > 0) ? 1.15f : (1.0f / 1.15f);
                m_pixels_per_sec *= zoom_factor;
                m_pixels_per_sec = max(20.0f, min(m_pixels_per_sec, 400.0f));

                float new_scroll = time_under_mouse * m_pixels_per_sec - (mouse_x - canvas_x);
                ImGui::SetScrollX(max(0.0f, new_scroll));
            }
            else if (!io.KeyCtrl && io.MouseWheel != 0.0f)
            {
                ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseWheel * 50.0f);
            }
        }

        DrawTimelineContent();
        m_scroll_y = ImGui::GetScrollY();
    }
    ImGui::EndChild();

    ImGui::EndChild();
}

void Sequencer::DrawTrackList()
{
    ImDrawList* dl  = ImGui::GetWindowDrawList();
    float row_width = ImGui::GetContentRegionAvail().x;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

    for (uint32_t s = 0; s < static_cast<uint32_t>(m_sequences.size()); s++)
    {
        Sequence& seq = m_sequences[s];
        bool collapsed = m_collapsed_sequences.count(seq.GetId()) > 0;
        bool is_playing = seq.IsPlaying() && !seq.IsPaused();

        ImGui::PushID(static_cast<int>(s));

        ImVec2 header_min = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##seq_header", ImVec2(row_width, m_group_height));
        ImVec2 header_max = ImVec2(header_min.x + row_width, header_min.y + m_group_height);
        bool header_hovered = ImGui::IsItemHovered();

        ImU32 header_bg = header_hovered ? ImGui::GetColorU32(ImGuiCol_HeaderHovered) : tc.group_bg;
        dl->AddRectFilled(header_min, header_max, header_bg);

        // playing indicator: pulsing accent bar
        float accent_alpha = is_playing ? (0.6f + 0.4f * sinf(static_cast<float>(ImGui::GetTime()) * 4.0f)) : 0.8f;
        dl->AddRectFilled(ImVec2(header_min.x, header_min.y), ImVec2(header_min.x + 3.0f, header_max.y), to_imu32(tc.sequence_accent, accent_alpha));

        // collapse triangle
        {
            float tri_x  = header_min.x + 10.0f;
            float tri_cy = header_min.y + m_group_height * 0.5f;
            float tri_s  = 4.0f;

            if (collapsed)
            {
                dl->AddTriangleFilled(
                    ImVec2(tri_x, tri_cy - tri_s), ImVec2(tri_x + tri_s, tri_cy), ImVec2(tri_x, tri_cy + tri_s),
                    tc.text_dim);
            }
            else
            {
                dl->AddTriangleFilled(
                    ImVec2(tri_x - tri_s * 0.5f, tri_cy - tri_s * 0.5f),
                    ImVec2(tri_x + tri_s * 0.5f, tri_cy - tri_s * 0.5f),
                    ImVec2(tri_x, tri_cy + tri_s * 0.5f),
                    tc.text_dim);
            }
        }

        float name_x = header_min.x + 22.0f;
        ImGui::PushFont(Editor::font_bold, 0.0f);
        dl->PushClipRect(ImVec2(name_x, header_min.y), ImVec2(header_max.x - 50.0f, header_max.y));
        dl->AddText(ImVec2(name_x, header_min.y + 5.0f), to_imu32(tc.sequence_accent), seq.GetName().c_str());
        dl->PopClipRect();
        ImGui::PopFont();

        dl->AddLine(ImVec2(header_min.x, header_max.y - 1.0f), ImVec2(header_max.x, header_max.y - 1.0f), tc.separator);

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            m_renaming_sequence = static_cast<int32_t>(s);
            m_renaming_track    = -1;
            snprintf(m_rename_buf, sizeof(m_rename_buf), "%s", seq.GetName().c_str());
        }
        else if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            if (m_collapsed_sequences.count(seq.GetId()))
            {
                m_collapsed_sequences.erase(seq.GetId());
            }
            else
            {
                m_collapsed_sequences.insert(seq.GetId());
            }
        }

        if (m_renaming_sequence == static_cast<int32_t>(s) && m_renaming_track == -1)
        {
            ImGui::SetCursorScreenPos(ImVec2(name_x, header_min.y + 3.0f));
            ImGui::SetNextItemWidth(row_width - 60.0f);
            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##rename_seq", m_rename_buf, sizeof(m_rename_buf),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
            {
                seq.SetName(m_rename_buf);
                m_renaming_sequence = -1;
            }
            if (!ImGui::IsItemActive() && m_renaming_sequence == static_cast<int32_t>(s))
            {
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                {
                    m_renaming_sequence = -1;
                }
            }
        }

        if (header_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            ImGui::OpenPopup("seq_header_context");
        }

        if (ImGui::BeginPopup("seq_header_context"))
        {
            if (ImGui::MenuItem("rename"))
            {
                m_renaming_sequence = static_cast<int32_t>(s);
                m_renaming_track    = -1;
                snprintf(m_rename_buf, sizeof(m_rename_buf), "%s", seq.GetName().c_str());
            }

            float dur = seq.GetDuration();
            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::DragFloat("duration", &dur, 0.1f, 0.1f, 600.0f, "%.1f s"))
            {
                seq.SetDuration(dur);
            }

            float spd = seq.GetPlaybackSpeed();
            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::DragFloat("speed", &spd, 0.01f, 0.1f, 10.0f, "%.2fx"))
            {
                seq.SetPlaybackSpeed(spd);
            }

            ImGui::Separator();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(tc.camera_accent.x, tc.camera_accent.y, tc.camera_accent.z, 1.0f));
            if (ImGui::MenuItem("add camera cut track"))
            {
                seq.AddTrack(SequenceTrackType::CameraCut, 0, "camera cut");
                m_collapsed_sequences.erase(seq.GetId());
            }
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(tc.transform_accent.x, tc.transform_accent.y, tc.transform_accent.z, 1.0f));
            if (ImGui::BeginMenu("add transform track"))
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
                for (Entity* entity : World::GetEntities())
                {
                    ImGui::PushID(static_cast<int>(entity->GetObjectId()));
                    if (ImGui::MenuItem(entity->GetObjectName().c_str()))
                    {
                        seq.AddTrack(SequenceTrackType::Transform, entity->GetObjectId(), entity->GetObjectName());
                        m_collapsed_sequences.erase(seq.GetId());
                    }
                    ImGui::PopID();
                }
                ImGui::PopStyleColor();
                ImGui::EndMenu();
            }
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(tc.event_accent.x, tc.event_accent.y, tc.event_accent.z, 1.0f));
            if (ImGui::MenuItem("add event track"))
            {
                seq.AddTrack(SequenceTrackType::Event, 0, "events");
                m_collapsed_sequences.erase(seq.GetId());
            }
            ImGui::PopStyleColor();

            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ImGui::Style::color_error.x, ImGui::Style::color_error.y, ImGui::Style::color_error.z, 1.0f));
            if (ImGui::MenuItem("delete sequence"))
            {
                m_sequences.erase(m_sequences.begin() + s);
                if (m_selected_sequence == static_cast<int32_t>(s))
                {
                    ClearSelection();
                }
                ImGui::PopStyleColor();
                ImGui::EndPopup();
                ImGui::PopID();
                ImGui::PopStyleVar();
                return;
            }
            ImGui::PopStyleColor();

            ImGui::EndPopup();
        }

        if (!collapsed)
        {
            auto& tracks = seq.GetTracks();
            for (uint32_t t = 0; t < static_cast<uint32_t>(tracks.size()); t++)
            {
                SequenceTrack& track = tracks[t];
                ImGui::PushID(static_cast<int>(t + 1000));

                bool selected = (m_selected_sequence == static_cast<int32_t>(s) && m_selected_track == static_cast<int32_t>(t));
                const ImVec4& accent = track_type_color(track.type, &track);

                ImVec2 row_min = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("##track_row", ImVec2(row_width, m_track_height));
                ImVec2 row_max = ImVec2(row_min.x + row_width, row_min.y + m_track_height);
                bool row_hovered = ImGui::IsItemHovered();

                ImU32 row_bg = selected
                    ? IM_COL32(static_cast<int>(accent.x * 60), static_cast<int>(accent.y * 60), static_cast<int>(accent.z * 60), 80)
                    : (row_hovered ? ImGui::GetColorU32(ImGuiCol_HeaderHovered) : ImGui::GetColorU32(ImGuiCol_FrameBg));
                dl->AddRectFilled(row_min, row_max, row_bg);

                // mute indicator: dim the row
                if (track.muted)
                {
                    dl->AddRectFilled(row_min, row_max, IM_COL32(0, 0, 0, 80));
                }

                dl->AddRectFilled(ImVec2(row_min.x, row_min.y), ImVec2(row_min.x + 3.0f, row_max.y), to_imu32(accent));

                // mute/solo buttons
                float btn_x = row_max.x - 30.0f;
                float btn_y = row_min.y + (m_track_height - ImGui::GetTextLineHeight()) * 0.5f;
                {
                    ImVec2 m_min = ImVec2(btn_x, btn_y - 1.0f);
                    ImVec2 m_max = ImVec2(btn_x + 12.0f, btn_y + ImGui::GetTextLineHeight() + 1.0f);
                    bool m_hov = ImGui::IsMouseHoveringRect(m_min, m_max);
                    dl->AddRectFilled(m_min, m_max, track.muted ? IM_COL32(180, 60, 60, 200) : (m_hov ? IM_COL32(80, 80, 80, 150) : IM_COL32(0, 0, 0, 0)), 2.0f);
                    dl->AddText(ImVec2(btn_x + 2.0f, btn_y), track.muted ? IM_COL32(255, 255, 255, 230) : tc.text_dim, "M");
                    if (m_hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        track.muted = !track.muted;
                    }
                }
                {
                    float s_x = btn_x - 16.0f;
                    ImVec2 s_min = ImVec2(s_x, btn_y - 1.0f);
                    ImVec2 s_max = ImVec2(s_x + 12.0f, btn_y + ImGui::GetTextLineHeight() + 1.0f);
                    bool s_hov = ImGui::IsMouseHoveringRect(s_min, s_max);
                    dl->AddRectFilled(s_min, s_max, track.solo ? IM_COL32(60, 120, 180, 200) : (s_hov ? IM_COL32(80, 80, 80, 150) : IM_COL32(0, 0, 0, 0)), 2.0f);
                    dl->AddText(ImVec2(s_x + 2.0f, btn_y), track.solo ? IM_COL32(255, 255, 255, 230) : tc.text_dim, "S");
                    if (s_hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        track.solo = !track.solo;
                    }
                }

                float text_x = row_min.x + 14.0f;
                float text_y = row_min.y + (m_track_height - ImGui::GetTextLineHeight()) * 0.5f;
                {
                    const char* badge = track_type_badge(track.type);
                    ImVec2 badge_size = ImGui::CalcTextSize(badge);
                    ImVec2 badge_min  = ImVec2(text_x, text_y - 1.0f);
                    ImVec2 badge_max  = ImVec2(badge_min.x + badge_size.x + 8.0f, badge_min.y + badge_size.y + 2.0f);
                    dl->AddRectFilled(badge_min, badge_max, to_imu32(accent, 0.7f), 2.0f);
                    dl->AddText(ImVec2(badge_min.x + 4.0f, badge_min.y + 1.0f), IM_COL32(255, 255, 255, 230), badge);
                    text_x = badge_max.x + 4.0f;
                }

                dl->PushClipRect(ImVec2(text_x, row_min.y), ImVec2(row_max.x - 50.0f, row_max.y));
                dl->AddText(ImVec2(text_x, text_y), tc.text_primary, track.name.c_str());
                dl->PopClipRect();

                dl->AddLine(ImVec2(row_min.x, row_max.y - 1.0f), ImVec2(row_max.x, row_max.y - 1.0f), tc.separator);

                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                {
                    m_selected_sequence = static_cast<int32_t>(s);
                    m_selected_track    = static_cast<int32_t>(t);
                    m_selected_keyframe = -1;
                    m_selected_clip     = -1;
                    m_selected_keyframes.clear();
                }

                if (row_hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    m_renaming_track    = static_cast<int32_t>(t);
                    m_renaming_sequence = static_cast<int32_t>(s);
                    snprintf(m_rename_buf, sizeof(m_rename_buf), "%s", track.name.c_str());
                }

                if (m_renaming_track == static_cast<int32_t>(t) && m_renaming_sequence == static_cast<int32_t>(s))
                {
                    ImGui::SetCursorScreenPos(ImVec2(text_x, text_y));
                    ImGui::SetNextItemWidth(row_width - (text_x - row_min.x) - 50.0f);
                    ImGui::SetKeyboardFocusHere();
                    if (ImGui::InputText("##rename_trk", m_rename_buf, sizeof(m_rename_buf),
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
                    {
                        track.name = m_rename_buf;
                        m_renaming_track    = -1;
                        m_renaming_sequence = -1;
                    }
                    if (!ImGui::IsItemActive() && m_renaming_track == static_cast<int32_t>(t) && m_renaming_sequence == static_cast<int32_t>(s))
                    {
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                        {
                            m_renaming_track    = -1;
                            m_renaming_sequence = -1;
                        }
                    }
                }

                // track reorder drag
                if (row_hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 8.0f) && m_renaming_track == -1)
                {
                    if (!m_reorder_active)
                    {
                        m_reorder_active  = true;
                        m_reorder_seq     = static_cast<int32_t>(s);
                        m_reorder_track   = static_cast<int32_t>(t);
                        m_reorder_start_y = ImGui::GetMousePos().y;
                    }
                }

                if (row_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                {
                    ImGui::OpenPopup("track_context");
                }

                if (ImGui::BeginPopup("track_context"))
                {
                    if (track.type == SequenceTrackType::Transform || track.type == SequenceTrackType::Event)
                    {
                        if (ImGui::BeginMenu("set target entity"))
                        {
                            for (Entity* entity : World::GetEntities())
                            {
                                ImGui::PushID(static_cast<int>(entity->GetObjectId()));
                                bool is_current = (entity->GetObjectId() == track.target_entity_id);
                                if (ImGui::MenuItem(entity->GetObjectName().c_str(), nullptr, is_current))
                                {
                                    track.target_entity_id = entity->GetObjectId();
                                }
                                ImGui::PopID();
                            }
                            ImGui::EndMenu();
                        }
                    }

                    if (ImGui::MenuItem("rename"))
                    {
                        m_renaming_track    = static_cast<int32_t>(t);
                        m_renaming_sequence = static_cast<int32_t>(s);
                        snprintf(m_rename_buf, sizeof(m_rename_buf), "%s", track.name.c_str());
                    }

                    // track color picker
                    float col[4] = { track.custom_color.x, track.custom_color.y, track.custom_color.z, track.custom_color.w };
                    if (ImGui::ColorEdit4("track color", col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel))
                    {
                        track.custom_color = Vector4(col[0], col[1], col[2], col[3]);
                    }
                    ImGui::SameLine();
                    ImGui::TextUnformatted("track color");

                    if (ImGui::MenuItem("reset color"))
                    {
                        track.custom_color = Vector4::Zero;
                    }

                    ImGui::Separator();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ImGui::Style::color_error.x, ImGui::Style::color_error.y, ImGui::Style::color_error.z, 1.0f));
                    if (ImGui::MenuItem("delete track"))
                    {
                        seq.RemoveTrack(t);
                        if (m_selected_sequence == static_cast<int32_t>(s) && m_selected_track == static_cast<int32_t>(t))
                        {
                            m_selected_track = -1;
                        }
                        ImGui::PopStyleColor();
                        ImGui::EndPopup();
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopStyleColor();

                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }
        }

        ImGui::PopID();
    }

    // handle track reorder drop
    if (m_reorder_active && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        if (m_reorder_seq >= 0 && m_reorder_seq < static_cast<int32_t>(m_sequences.size()))
        {
            float delta_y = ImGui::GetMousePos().y - m_reorder_start_y;
            int move_by = static_cast<int>(delta_y / m_track_height);
            if (move_by != 0)
            {
                auto& trks = m_sequences[m_reorder_seq].GetTracks();
                int from = m_reorder_track;
                int to = max(0, min(static_cast<int>(trks.size()) - 1, from + move_by));
                if (from != to && from >= 0 && from < static_cast<int>(trks.size()))
                {
                    if (to > from)
                    {
                        rotate(trks.begin() + from, trks.begin() + from + 1, trks.begin() + to + 1);
                    }
                    else
                    {
                        rotate(trks.begin() + to, trks.begin() + from, trks.begin() + from + 1);
                    }

                    if (m_selected_sequence == m_reorder_seq && m_selected_track == from)
                    {
                        m_selected_track = to;
                    }
                }
            }
        }
        m_reorder_active = false;
        m_reorder_seq    = -1;
        m_reorder_track  = -1;
    }

    ImGui::PopStyleVar();
}

void Sequencer::DrawTimelineContent()
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    float content_width  = m_global_duration * m_pixels_per_sec;
    float total_track_h  = ComputeTotalHeight();
    ImVec2 canvas_pos    = ImGui::GetCursorScreenPos();
    float timeline_x     = canvas_pos.x;
    float timeline_y     = canvas_pos.y;
    m_timeline_origin_x  = timeline_x;

    ImGui::Dummy(ImVec2(content_width, ruler_height + minimap_height + total_track_h + 20.0f));

    // ruler
    {
        draw_list->AddRectFilled(
            ImVec2(timeline_x, timeline_y),
            ImVec2(timeline_x + content_width, timeline_y + ruler_height),
            tc.ruler_bg
        );

        float step = compute_ruler_step(m_pixels_per_sec);

        for (float t = 0.0f; t <= m_global_duration; t += step)
        {
            float x = timeline_x + t * m_pixels_per_sec;

            draw_list->AddLine(
                ImVec2(x, timeline_y + ruler_height - 12.0f),
                ImVec2(x, timeline_y + ruler_height),
                tc.ruler_tick, 1.0f
            );

            char time_str[16];
            int mins = static_cast<int>(t) / 60;
            float secs = t - static_cast<float>(mins * 60);
            if (mins > 0)
            {
                snprintf(time_str, sizeof(time_str), "%d:%04.1f", mins, secs);
            }
            else if (step < 1.0f)
            {
                snprintf(time_str, sizeof(time_str), "%.2fs", t);
            }
            else
            {
                snprintf(time_str, sizeof(time_str), "%.0fs", t);
            }
            draw_list->AddText(ImVec2(x + 2.0f, timeline_y + 2.0f), tc.ruler_text, time_str);

            // sub-ticks
            int sub_count = (step >= 1.0f) ? 4 : 2;
            float sub_step = step / static_cast<float>(sub_count);
            for (int si = 1; si < sub_count; si++)
            {
                float sub_x = timeline_x + (t + si * sub_step) * m_pixels_per_sec;
                if (sub_x < timeline_x + content_width)
                {
                    draw_list->AddLine(
                        ImVec2(sub_x, timeline_y + ruler_height - 5.0f),
                        ImVec2(sub_x, timeline_y + ruler_height),
                        tc.ruler_tick
                    );
                }
            }
        }

        if (m_snap_enabled)
        {
            for (float t = 0.0f; t <= m_global_duration; t += m_snap_interval)
            {
                float x = timeline_x + t * m_pixels_per_sec;
                draw_list->AddLine(
                    ImVec2(x, timeline_y + ruler_height + minimap_height),
                    ImVec2(x, timeline_y + ruler_height + minimap_height + total_track_h),
                    tc.snap_grid
                );
            }
        }

        // ruler hover tooltip
        if (ImGui::IsMouseHoveringRect(ImVec2(timeline_x, timeline_y), ImVec2(timeline_x + content_width, timeline_y + ruler_height)))
        {
            float hover_time = (ImGui::GetMousePos().x - timeline_x) / m_pixels_per_sec;
            hover_time = max(0.0f, min(hover_time, m_global_duration));
            ImGui::BeginTooltip();
            ImGui::Text("%.3fs", hover_time);
            ImGui::EndTooltip();
        }
    }

    // ruler scrubbing
    {
        ImGui::SetCursorScreenPos(ImVec2(timeline_x, timeline_y));
        ImGui::InvisibleButton("##ruler_scrub", ImVec2(content_width, ruler_height));

        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            m_scrubbing = true;
        }

        if (m_scrubbing)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                float mouse_x = ImGui::GetMousePos().x;
                float time = (mouse_x - timeline_x) / m_pixels_per_sec;
                time = max(0.0f, min(time, m_global_duration));
                time = SnapTime(time);
                for (Sequence& seq : m_sequences)
                    seq.SetPlaybackTime(min(time, seq.GetDuration()));
            }
            else
            {
                m_scrubbing = false;
            }
        }
    }

    // minimap
    DrawMinimap(timeline_x, content_width, timeline_y);

    // draw all sequences' tracks
    float y_offset = timeline_y + ruler_height + minimap_height;
    uint32_t global_row = 0;

    for (uint32_t s = 0; s < static_cast<uint32_t>(m_sequences.size()); s++)
    {
        Sequence& seq = m_sequences[s];
        bool collapsed = m_collapsed_sequences.count(seq.GetId()) > 0;
        float seq_duration = seq.GetDuration();

        // sequence group header row on the timeline
        {
            draw_list->AddRectFilled(
                ImVec2(timeline_x, y_offset),
                ImVec2(timeline_x + content_width, y_offset + m_group_height),
                tc.group_bg
            );

            draw_list->AddLine(
                ImVec2(timeline_x, y_offset + m_group_height - 1.0f),
                ImVec2(timeline_x + content_width, y_offset + m_group_height - 1.0f),
                to_imu32(tc.sequence_accent, 0.3f)
            );

            float bar_x1 = timeline_x + seq_duration * m_pixels_per_sec;
            draw_list->AddRectFilled(
                ImVec2(timeline_x, y_offset + 2.0f),
                ImVec2(bar_x1, y_offset + m_group_height - 2.0f),
                to_imu32(tc.sequence_accent, 0.08f),
                3.0f
            );

            draw_list->AddLine(
                ImVec2(bar_x1, y_offset),
                ImVec2(bar_x1, y_offset + m_group_height),
                to_imu32(tc.sequence_accent, 0.4f),
                1.5f
            );
        }

        y_offset += m_group_height;

        if (collapsed)
        {
            continue;
        }

        auto& tracks = seq.GetTracks();
        for (uint32_t i = 0; i < static_cast<uint32_t>(tracks.size()); i++)
        {
            SequenceTrack& track = tracks[i];
            float row_y = y_offset;
            const ImVec4& accent = track_type_color(track.type, &track);

            ImU32 base_color = (global_row % 2 == 0) ? tc.track_bg_a : tc.track_bg_b;
            draw_list->AddRectFilled(
                ImVec2(timeline_x, row_y),
                ImVec2(timeline_x + content_width, row_y + m_track_height),
                base_color
            );

            draw_list->AddRectFilled(
                ImVec2(timeline_x, row_y),
                ImVec2(timeline_x + content_width, row_y + m_track_height),
                to_imu32(accent, 0.04f)
            );

            // dim muted tracks
            if (track.muted)
            {
                draw_list->AddRectFilled(
                    ImVec2(timeline_x, row_y),
                    ImVec2(timeline_x + content_width, row_y + m_track_height),
                    IM_COL32(0, 0, 0, 60)
                );
            }

            if (seq_duration < m_global_duration)
            {
                float dim_x = timeline_x + seq_duration * m_pixels_per_sec;
                draw_list->AddRectFilled(
                    ImVec2(dim_x, row_y),
                    ImVec2(timeline_x + content_width, row_y + m_track_height),
                    IM_COL32(0, 0, 0, 40)
                );
            }

            // empty track hint
            bool track_empty = track.keyframes.empty() && track.camera_clips.empty() && track.event_clips.empty();
            if (track_empty)
            {
                const char* hint = "right-click to add";
                ImVec2 hint_size = ImGui::CalcTextSize(hint);
                float hint_x = timeline_x + (content_width - hint_size.x) * 0.5f;
                float hint_y = row_y + (m_track_height - hint_size.y) * 0.5f;
                draw_list->AddText(ImVec2(hint_x, hint_y), tc.text_dim, hint);
            }

            switch (track.type)
            {
                case SequenceTrackType::CameraCut:
                {
                    ImU32 clip_base  = to_imu32(accent, 0.6f);
                    ImU32 clip_hover = to_imu32(accent, 0.8f);

                    for (uint32_t c = 0; c < static_cast<uint32_t>(track.camera_clips.size()); c++)
                    {
                        auto& clip = track.camera_clips[c];
                        float x0 = timeline_x + clip.start_time * m_pixels_per_sec;
                        float x1 = timeline_x + clip.end_time * m_pixels_per_sec;
                        float y0 = row_y + 3.0f;
                        float y1 = row_y + m_track_height - 3.0f;

                        bool hovered = ImGui::IsMouseHoveringRect(ImVec2(x0, y0), ImVec2(x1, y1));
                        bool sel = (m_selected_sequence == static_cast<int32_t>(s) && m_selected_track == static_cast<int32_t>(i) && m_selected_clip == static_cast<int32_t>(c));
                        ImU32 cc = sel ? tc.selected : (hovered ? clip_hover : clip_base);

                        draw_list->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), cc, 4.0f);

                        if (sel)
                        {
                            draw_list->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(255, 255, 255, 120), 4.0f, 1.5f);
                        }

                        Entity* cam_ent = World::GetEntityById(clip.camera_entity_id);
                        const char* cam_name = cam_ent ? cam_ent->GetObjectName().c_str() : "?";
                        ImVec2 text_size = ImGui::CalcTextSize(cam_name);
                        float clip_width = x1 - x0;
                        float text_cx = x0 + max(4.0f, (clip_width - text_size.x) * 0.5f);
                        float text_cy = y0 + (y1 - y0 - text_size.y) * 0.5f;

                        draw_list->PushClipRect(ImVec2(x0 + 2.0f, y0), ImVec2(x1 - 2.0f, y1));
                        draw_list->AddText(ImVec2(text_cx, text_cy), IM_COL32(255, 255, 255, 230), cam_name);
                        draw_list->PopClipRect();

                        // edge resize hit-test
                        float edge_zone = 6.0f;
                        bool left_edge  = ImGui::IsMouseHoveringRect(ImVec2(x0 - 2, y0), ImVec2(x0 + edge_zone, y1));
                        bool right_edge = ImGui::IsMouseHoveringRect(ImVec2(x1 - edge_zone, y0), ImVec2(x1 + 2, y1));

                        if ((left_edge || right_edge) && m_resizing_clip == -1)
                        {
                            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                        }

                        if (left_edge && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        {
                            m_resizing_clip     = static_cast<int32_t>(c);
                            m_resizing_clip_seq = static_cast<int32_t>(s);
                            m_resizing_clip_trk = static_cast<int32_t>(i);
                            m_resizing_left     = true;
                        }
                        else if (right_edge && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        {
                            m_resizing_clip     = static_cast<int32_t>(c);
                            m_resizing_clip_seq = static_cast<int32_t>(s);
                            m_resizing_clip_trk = static_cast<int32_t>(i);
                            m_resizing_left     = false;
                        }

                        // tooltip
                        if (hovered && !left_edge && !right_edge)
                        {
                            ImGui::BeginTooltip();
                            ImGui::Text("%s", cam_name);
                            ImGui::Text("%.2fs - %.2fs (%.2fs)", clip.start_time, clip.end_time, clip.end_time - clip.start_time);
                            if (clip.transition_in > 0.0f)
                            {
                                ImGui::Text("blend: %.2fs", clip.transition_in);
                            }
                            ImGui::EndTooltip();
                        }

                        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !left_edge && !right_edge)
                        {
                            m_selected_sequence = static_cast<int32_t>(s);
                            m_selected_track    = static_cast<int32_t>(i);
                            m_selected_clip     = static_cast<int32_t>(c);
                            m_selected_keyframe = -1;
                            m_selected_keyframes.clear();
                        }

                        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                        {
                            m_selected_sequence = static_cast<int32_t>(s);
                            m_selected_track    = static_cast<int32_t>(i);
                            m_selected_clip     = static_cast<int32_t>(c);
                            m_popup_sequence    = static_cast<int32_t>(s);
                            m_popup_track       = static_cast<int32_t>(i);
                            ImGui::OpenPopup("clip_context");
                        }
                    }

                    if (ImGui::IsMouseHoveringRect(ImVec2(timeline_x, row_y), ImVec2(timeline_x + content_width, row_y + m_track_height)))
                    {
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && m_selected_clip == -1)
                        {
                            m_popup_sequence = static_cast<int32_t>(s);
                            m_popup_track    = static_cast<int32_t>(i);
                            m_popup_mouse_x  = ImGui::GetMousePos().x;
                            ImGui::OpenPopup("add_camera_clip");
                        }
                    }
                } break;

                case SequenceTrackType::Transform:
                {
                    if (track.keyframes.size() >= 2)
                    {
                        float bar_x0 = timeline_x + track.keyframes.front().time * m_pixels_per_sec;
                        float bar_x1 = timeline_x + track.keyframes.back().time * m_pixels_per_sec;
                        draw_list->AddRectFilled(
                            ImVec2(bar_x0, row_y + 6.0f),
                            ImVec2(bar_x1, row_y + m_track_height - 6.0f),
                            to_imu32(accent, 0.2f),
                            3.0f
                        );
                    }

                    ImU32 line_color = to_imu32(accent, 0.4f);
                    ImU32 kf_base    = to_imu32(accent, 0.9f);
                    ImU32 kf_hover   = to_imu32(accent, 1.0f);

                    for (uint32_t k = 0; k + 1 < static_cast<uint32_t>(track.keyframes.size()); k++)
                    {
                        float x0 = timeline_x + track.keyframes[k].time * m_pixels_per_sec;
                        float x1 = timeline_x + track.keyframes[k + 1].time * m_pixels_per_sec;
                        float cy = row_y + m_track_height * 0.5f;
                        draw_list->AddLine(ImVec2(x0, cy), ImVec2(x1, cy), line_color, 2.0f);
                    }

                    for (uint32_t k = 0; k < static_cast<uint32_t>(track.keyframes.size()); k++)
                    {
                        auto& kf = track.keyframes[k];
                        float cx = timeline_x + kf.time * m_pixels_per_sec;
                        float cy = row_y + m_track_height * 0.5f;
                        float size = 6.0f;

                        ImVec2 diamond[4] = {
                            ImVec2(cx, cy - size),
                            ImVec2(cx + size, cy),
                            ImVec2(cx, cy + size),
                            ImVec2(cx - size, cy)
                        };

                        bool hovered = ImGui::IsMouseHoveringRect(ImVec2(cx - size - 1, cy - size - 1), ImVec2(cx + size + 1, cy + size + 1));
                        bool sel = (m_selected_sequence == static_cast<int32_t>(s) && m_selected_track == static_cast<int32_t>(i) && m_selected_keyframe == static_cast<int32_t>(k));
                        bool multi_sel = m_selected_keyframes.count(k) > 0 && m_selected_sequence == static_cast<int32_t>(s) && m_selected_track == static_cast<int32_t>(i);
                        ImU32 kf_color = (sel || multi_sel) ? tc.selected : (hovered ? kf_hover : kf_base);

                        draw_list->AddConvexPolyFilled(diamond, 4, kf_color);

                        if (sel || multi_sel)
                        {
                            draw_list->AddPolyline(diamond, 4, IM_COL32(255, 255, 255, 180), 1.5f, ImDrawFlags_Closed);
                        }

                        // tooltip
                        if (hovered)
                        {
                            ImGui::BeginTooltip();
                            ImGui::Text("%.3fs", kf.time);
                            ImGui::Text("pos: %.2f, %.2f, %.2f", kf.position.x, kf.position.y, kf.position.z);
                            ImGui::Text("rot: %.3f, %.3f, %.3f, %.3f", kf.rotation.x, kf.rotation.y, kf.rotation.z, kf.rotation.w);
                            ImGui::Text("interp: %s", interpolation_mode_label(kf.interpolation));
                            ImGui::EndTooltip();
                        }

                        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        {
                            ImGuiIO& io = ImGui::GetIO();

                            m_selected_sequence = static_cast<int32_t>(s);
                            m_selected_track    = static_cast<int32_t>(i);
                            m_selected_clip     = -1;

                            if (io.KeyCtrl)
                            {
                                if (m_selected_keyframes.count(k))
                                {
                                    m_selected_keyframes.erase(k);
                                }
                                else
                                {
                                    m_selected_keyframes.insert(k);
                                }
                                m_selected_keyframe = static_cast<int32_t>(k);
                            }
                            else if (io.KeyShift && m_selected_keyframe >= 0)
                            {
                                uint32_t from = static_cast<uint32_t>(m_selected_keyframe);
                                uint32_t to = k;
                                if (from > to)
                                {
                                    swap(from, to);
                                }
                                m_selected_keyframes.clear();
                                for (uint32_t ki = from; ki <= to; ki++)
                                    m_selected_keyframes.insert(ki);
                                m_selected_keyframe = static_cast<int32_t>(k);
                            }
                            else
                            {
                                m_selected_keyframes.clear();
                                m_selected_keyframe = static_cast<int32_t>(k);
                            }

                            m_dragging_keyframe = static_cast<int32_t>(k);
                            m_dragging_track    = static_cast<int32_t>(i);
                            m_dragging_sequence = static_cast<int32_t>(s);
                        }
                    }

                    // box select start on empty area
                    if (ImGui::IsMouseHoveringRect(ImVec2(timeline_x, row_y), ImVec2(timeline_x + content_width, row_y + m_track_height)))
                    {
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m_dragging_keyframe == -1 && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift)
                        {
                            m_box_selecting  = true;
                            m_box_start_x    = ImGui::GetMousePos().x;
                            m_box_start_y    = ImGui::GetMousePos().y;
                            m_box_select_seq = static_cast<int32_t>(s);
                            m_box_select_trk = static_cast<int32_t>(i);
                            m_selected_keyframes.clear();
                            m_selected_sequence = static_cast<int32_t>(s);
                            m_selected_track    = static_cast<int32_t>(i);
                        }

                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                        {
                            m_popup_sequence = static_cast<int32_t>(s);
                            m_popup_track    = static_cast<int32_t>(i);
                            m_popup_mouse_x  = ImGui::GetMousePos().x;
                            ImGui::OpenPopup("add_transform_keyframe");
                        }
                    }
                } break;

                case SequenceTrackType::Event:
                {
                    ImU32 evt_base  = to_imu32(accent, 0.8f);
                    ImU32 evt_hover = to_imu32(accent, 1.0f);

                    for (uint32_t e = 0; e < static_cast<uint32_t>(track.event_clips.size()); e++)
                    {
                        auto& evt = track.event_clips[e];
                        float cx = timeline_x + evt.time * m_pixels_per_sec;
                        float cy = row_y + m_track_height * 0.5f;
                        float size = 6.0f;

                        bool hovered = ImGui::IsMouseHoveringRect(ImVec2(cx - size, cy - size), ImVec2(cx + size, cy + size));
                        bool sel = (m_selected_sequence == static_cast<int32_t>(s) && m_selected_track == static_cast<int32_t>(i) && m_selected_keyframe == static_cast<int32_t>(e));
                        ImU32 evt_color = sel ? tc.selected : (hovered ? evt_hover : evt_base);

                        draw_list->AddLine(ImVec2(cx, row_y + 4.0f), ImVec2(cx, row_y + m_track_height - 4.0f), evt_color, 2.0f);
                        ImVec2 flag[3] = {
                            ImVec2(cx, row_y + 4.0f),
                            ImVec2(cx + 8.0f, row_y + 8.0f),
                            ImVec2(cx, row_y + 12.0f)
                        };
                        draw_list->AddTriangleFilled(flag[0], flag[1], flag[2], evt_color);

                        if (m_pixels_per_sec > 60.0f)
                        {
                            const char* label = event_action_label(evt.action);
                            draw_list->AddText(ImVec2(cx + 10.0f, row_y + 2.0f), tc.text_dim, label);
                        }

                        if (hovered)
                        {
                            ImGui::BeginTooltip();
                            ImGui::Text("%s @ %.2fs", event_action_label(evt.action), evt.time);
                            Entity* target = World::GetEntityById(evt.target_entity_id);
                            if (target)
                            {
                                ImGui::Text("target: %s", target->GetObjectName().c_str());
                            }
                            if (evt.parameter != 0.0f)
                            {
                                ImGui::Text("param: %.2f", evt.parameter);
                            }
                            ImGui::EndTooltip();
                        }

                        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        {
                            m_selected_sequence = static_cast<int32_t>(s);
                            m_selected_track    = static_cast<int32_t>(i);
                            m_selected_keyframe = static_cast<int32_t>(e);
                            m_selected_clip     = -1;
                            m_selected_keyframes.clear();
                            m_dragging_event    = static_cast<int32_t>(e);
                            m_dragging_track    = static_cast<int32_t>(i);
                            m_dragging_sequence = static_cast<int32_t>(s);
                        }

                        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                        {
                            m_popup_sequence = static_cast<int32_t>(s);
                            m_popup_track    = static_cast<int32_t>(i);
                            ImGui::OpenPopup("event_context");
                        }
                    }

                    if (ImGui::IsMouseHoveringRect(ImVec2(timeline_x, row_y), ImVec2(timeline_x + content_width, row_y + m_track_height)))
                    {
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                        {
                            m_popup_sequence = static_cast<int32_t>(s);
                            m_popup_track    = static_cast<int32_t>(i);
                            m_popup_mouse_x  = ImGui::GetMousePos().x;
                            ImGui::OpenPopup("add_event");
                        }
                    }
                } break;

                default:
                    break;
            }

            y_offset += m_track_height;
            global_row++;
        }
    }

    // keyframe dragging (transform)
    if (m_dragging_keyframe >= 0 && m_dragging_track >= 0 && m_dragging_sequence >= 0 && m_dragging_event == -1)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (m_dragging_sequence < static_cast<int32_t>(m_sequences.size()))
            {
                auto& drag_tracks = m_sequences[m_dragging_sequence].GetTracks();
                if (m_dragging_track < static_cast<int32_t>(drag_tracks.size()))
                {
                    auto& kfs = drag_tracks[m_dragging_track].keyframes;
                    if (m_dragging_keyframe < static_cast<int32_t>(kfs.size()))
                    {
                        float mouse_x = ImGui::GetMousePos().x;
                        float new_time = (mouse_x - timeline_x) / m_pixels_per_sec;
                        float seq_dur = m_sequences[m_dragging_sequence].GetDuration();
                        new_time = max(0.0f, min(new_time, seq_dur));
                        new_time = SnapTime(new_time);

                        float delta = new_time - kfs[m_dragging_keyframe].time;

                        if (!m_selected_keyframes.empty() && m_selected_keyframes.count(static_cast<uint32_t>(m_dragging_keyframe)))
                        {
                            for (uint32_t idx : m_selected_keyframes)
                            {
                                if (idx < static_cast<uint32_t>(kfs.size()))
                                {
                                    kfs[idx].time = max(0.0f, min(kfs[idx].time + delta, seq_dur));
                                }
                            }
                        }
                        else
                        {
                            kfs[m_dragging_keyframe].time = new_time;
                        }
                    }
                }
            }
        }
        else
        {
            m_dragging_keyframe = -1;
            m_dragging_track    = -1;
            m_dragging_sequence = -1;
        }
    }

    // event dragging
    if (m_dragging_event >= 0 && m_dragging_track >= 0 && m_dragging_sequence >= 0)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (m_dragging_sequence < static_cast<int32_t>(m_sequences.size()))
            {
                auto& drag_tracks = m_sequences[m_dragging_sequence].GetTracks();
                if (m_dragging_track < static_cast<int32_t>(drag_tracks.size()))
                {
                    auto& evts = drag_tracks[m_dragging_track].event_clips;
                    if (m_dragging_event < static_cast<int32_t>(evts.size()))
                    {
                        float mouse_x = ImGui::GetMousePos().x;
                        float new_time = (mouse_x - timeline_x) / m_pixels_per_sec;
                        float seq_dur = m_sequences[m_dragging_sequence].GetDuration();
                        new_time = max(0.0f, min(new_time, seq_dur));
                        new_time = SnapTime(new_time);
                        evts[m_dragging_event].time = new_time;
                    }
                }
            }
        }
        else
        {
            // re-sort after drag
            if (m_dragging_sequence < static_cast<int32_t>(m_sequences.size()))
            {
                auto& drag_tracks = m_sequences[m_dragging_sequence].GetTracks();
                if (m_dragging_track < static_cast<int32_t>(drag_tracks.size()))
                {
                    sort(drag_tracks[m_dragging_track].event_clips.begin(), drag_tracks[m_dragging_track].event_clips.end(),
                        [](const SequenceEventClip& a, const SequenceEventClip& b) { return a.time < b.time; });
                }
            }
            m_dragging_event    = -1;
            m_dragging_track    = -1;
            m_dragging_sequence = -1;
        }
    }

    // clip edge resizing
    if (m_resizing_clip >= 0)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (m_resizing_clip_seq < static_cast<int32_t>(m_sequences.size()))
            {
                auto& trks = m_sequences[m_resizing_clip_seq].GetTracks();
                if (m_resizing_clip_trk < static_cast<int32_t>(trks.size()))
                {
                    auto& clips = trks[m_resizing_clip_trk].camera_clips;
                    if (m_resizing_clip < static_cast<int32_t>(clips.size()))
                    {
                        float mouse_x = ImGui::GetMousePos().x;
                        float new_time = (mouse_x - timeline_x) / m_pixels_per_sec;
                        float seq_dur = m_sequences[m_resizing_clip_seq].GetDuration();
                        new_time = max(0.0f, min(new_time, seq_dur));
                        new_time = SnapTime(new_time);

                        if (m_resizing_left)
                        {
                            clips[m_resizing_clip].start_time = min(new_time, clips[m_resizing_clip].end_time - 0.01f);
                        }
                        else
                        {
                            clips[m_resizing_clip].end_time = max(new_time, clips[m_resizing_clip].start_time + 0.01f);
                        }
                    }
                }
            }
        }
        else
        {
            m_resizing_clip     = -1;
            m_resizing_clip_seq = -1;
            m_resizing_clip_trk = -1;
        }
    }

    // box selection
    if (m_box_selecting)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            float cur_x = ImGui::GetMousePos().x;
            float cur_y = ImGui::GetMousePos().y;
            float x0 = min(m_box_start_x, cur_x);
            float x1 = max(m_box_start_x, cur_x);
            float y0 = min(m_box_start_y, cur_y);
            float y1 = max(m_box_start_y, cur_y);

            draw_list->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(100, 150, 255, 30));
            draw_list->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(100, 150, 255, 120));

            if (m_box_select_seq >= 0 && m_box_select_seq < static_cast<int32_t>(m_sequences.size()) &&
                m_box_select_trk >= 0)
            {
                auto& trks = m_sequences[m_box_select_seq].GetTracks();
                if (m_box_select_trk < static_cast<int32_t>(trks.size()))
                {
                    m_selected_keyframes.clear();
                    for (uint32_t k = 0; k < static_cast<uint32_t>(trks[m_box_select_trk].keyframes.size()); k++)
                    {
                        float kx = timeline_x + trks[m_box_select_trk].keyframes[k].time * m_pixels_per_sec;
                        if (kx >= x0 && kx <= x1)
                        {
                            m_selected_keyframes.insert(k);
                        }
                    }
                }
            }
        }
        else
        {
            m_box_selecting = false;
        }
    }

    // auto-scroll during playback
    {
        bool any_playing = false;
        float playback_time = 0.0f;
        for (const Sequence& seq : m_sequences)
        {
            if (seq.IsPlaying() && !seq.IsPaused())
            {
                any_playing = true;
            }
            playback_time = max(playback_time, seq.GetPlaybackTime());
        }

        if (any_playing)
        {
            float playhead_screen_x = timeline_x + playback_time * m_pixels_per_sec;
            float visible_left  = timeline_x + ImGui::GetScrollX();
            float visible_right = visible_left + ImGui::GetWindowWidth();

            if (playhead_screen_x > visible_right - 50.0f || playhead_screen_x < visible_left + 50.0f)
            {
                float target_scroll = playback_time * m_pixels_per_sec - ImGui::GetWindowWidth() * 0.5f;
                float current_scroll = ImGui::GetScrollX();
                ImGui::SetScrollX(current_scroll + (target_scroll - current_scroll) * 0.1f);
            }
        }
    }

    // playhead
    DrawPlayhead(timeline_x, content_width, timeline_y, ruler_height + minimap_height + total_track_h);

    // context menus
    if (ImGui::BeginPopup("add_camera_clip"))
    {
        if (m_popup_sequence >= 0 && m_popup_sequence < static_cast<int32_t>(m_sequences.size()))
        {
            Sequence& seq = m_sequences[m_popup_sequence];
            auto& tracks = seq.GetTracks();
            if (m_popup_track >= 0 && m_popup_track < static_cast<int32_t>(tracks.size()))
            {
                ImGui::Text("add camera cut clip");
                ImGui::Separator();

                for (Entity* entity : World::GetEntities())
                {
                    if (entity->GetComponent<Camera>())
                    {
                        ImGui::PushID(static_cast<int>(entity->GetObjectId()));
                        if (ImGui::MenuItem(entity->GetObjectName().c_str()))
                        {
                            float mouse_time = (m_popup_mouse_x - timeline_x) / m_pixels_per_sec;
                            mouse_time = max(0.0f, min(mouse_time, seq.GetDuration()));
                            mouse_time = SnapTime(mouse_time);

                            SequenceCameraCutClip clip;
                            clip.start_time       = mouse_time;
                            clip.end_time         = min(mouse_time + 3.0f, seq.GetDuration());
                            clip.camera_entity_id = entity->GetObjectId();
                            tracks[m_popup_track].camera_clips.push_back(clip);
                        }
                        ImGui::PopID();
                    }
                }
            }
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("add_transform_keyframe"))
    {
        if (m_popup_sequence >= 0 && m_popup_sequence < static_cast<int32_t>(m_sequences.size()))
        {
            Sequence& seq = m_sequences[m_popup_sequence];
            auto& tracks = seq.GetTracks();
            if (m_popup_track >= 0 && m_popup_track < static_cast<int32_t>(tracks.size()))
            {
                SequenceTrack& track = tracks[m_popup_track];

                if (ImGui::MenuItem("add keyframe here"))
                {
                    float mouse_time = (m_popup_mouse_x - timeline_x) / m_pixels_per_sec;
                    mouse_time = max(0.0f, min(mouse_time, seq.GetDuration()));
                    mouse_time = SnapTime(mouse_time);

                    SequenceKeyframe kf;
                    kf.time = mouse_time;

                    Entity* target = World::GetEntityById(track.target_entity_id);
                    if (target)
                    {
                        kf.position = target->GetPosition();
                        kf.rotation = target->GetRotation();
                    }

                    track.keyframes.push_back(kf);
                    sort(track.keyframes.begin(), track.keyframes.end(),
                        [](const SequenceKeyframe& a, const SequenceKeyframe& b) { return a.time < b.time; });
                }

                if (ImGui::MenuItem("add keyframe at playback time"))
                {
                    SequenceKeyframe kf;
                    kf.time = SnapTime(seq.GetPlaybackTime());

                    Entity* target = World::GetEntityById(track.target_entity_id);
                    if (target)
                    {
                        kf.position = target->GetPosition();
                        kf.rotation = target->GetRotation();
                    }

                    track.keyframes.push_back(kf);
                    sort(track.keyframes.begin(), track.keyframes.end(),
                        [](const SequenceKeyframe& a, const SequenceKeyframe& b) { return a.time < b.time; });
                }
            }
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("add_event"))
    {
        if (m_popup_sequence >= 0 && m_popup_sequence < static_cast<int32_t>(m_sequences.size()))
        {
            Sequence& seq = m_sequences[m_popup_sequence];
            auto& tracks = seq.GetTracks();
            if (m_popup_track >= 0 && m_popup_track < static_cast<int32_t>(tracks.size()))
            {
                ImGui::Text("add event");
                ImGui::Separator();

                float mouse_time = (m_popup_mouse_x - timeline_x) / m_pixels_per_sec;
                mouse_time = max(0.0f, min(mouse_time, seq.GetDuration()));
                mouse_time = SnapTime(mouse_time);

                for (uint8_t a = 0; a < static_cast<uint8_t>(SequenceEventAction::Max); a++)
                {
                    SequenceEventAction action = static_cast<SequenceEventAction>(a);
                    if (ImGui::MenuItem(event_action_label(action)))
                    {
                        SequenceEventClip evt;
                        evt.time   = mouse_time;
                        evt.action = action;
                        tracks[m_popup_track].event_clips.push_back(evt);

                        sort(tracks[m_popup_track].event_clips.begin(), tracks[m_popup_track].event_clips.end(),
                            [](const SequenceEventClip& a, const SequenceEventClip& b) { return a.time < b.time; });
                    }
                }
            }
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("clip_context"))
    {
        if (m_popup_sequence >= 0 && m_popup_sequence < static_cast<int32_t>(m_sequences.size()))
        {
            Sequence& seq = m_sequences[m_popup_sequence];
            auto& tracks = seq.GetTracks();
            if (m_popup_track >= 0 && m_popup_track < static_cast<int32_t>(tracks.size()) && m_selected_clip >= 0)
            {
                auto& track = tracks[m_popup_track];
                if (track.type == SequenceTrackType::CameraCut && m_selected_clip < static_cast<int32_t>(track.camera_clips.size()))
                {
                    auto& clip = track.camera_clips[m_selected_clip];

                    ImGui::DragFloat("start", &clip.start_time, 0.05f, 0.0f, clip.end_time - 0.01f, "%.2fs");
                    ImGui::DragFloat("end", &clip.end_time, 0.05f, clip.start_time + 0.01f, seq.GetDuration(), "%.2fs");
                    ImGui::DragFloat("transition in", &clip.transition_in, 0.01f, 0.0f, 2.0f, "%.2fs");

                    if (ImGui::BeginMenu("camera"))
                    {
                        for (Entity* entity : World::GetEntities())
                        {
                            if (entity->GetComponent<Camera>())
                            {
                                ImGui::PushID(static_cast<int>(entity->GetObjectId()));
                                bool is_current = (entity->GetObjectId() == clip.camera_entity_id);
                                if (ImGui::MenuItem(entity->GetObjectName().c_str(), nullptr, is_current))
                                {
                                    clip.camera_entity_id = entity->GetObjectId();
                                }
                                ImGui::PopID();
                            }
                        }
                        ImGui::EndMenu();
                    }

                    ImGui::Separator();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ImGui::Style::color_error.x, ImGui::Style::color_error.y, ImGui::Style::color_error.z, 1.0f));
                    if (ImGui::MenuItem("delete clip"))
                    {
                        track.camera_clips.erase(track.camera_clips.begin() + m_selected_clip);
                        m_selected_clip = -1;
                    }
                    ImGui::PopStyleColor();
                }
            }
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("event_context"))
    {
        if (m_popup_sequence >= 0 && m_popup_sequence < static_cast<int32_t>(m_sequences.size()))
        {
            Sequence& seq = m_sequences[m_popup_sequence];
            auto& tracks = seq.GetTracks();
            if (m_popup_track >= 0 && m_popup_track < static_cast<int32_t>(tracks.size()))
            {
                auto& track = tracks[m_popup_track];
                if (track.type == SequenceTrackType::Event)
                {
                    for (uint32_t e = 0; e < static_cast<uint32_t>(track.event_clips.size()); e++)
                    {
                        auto& evt = track.event_clips[e];
                        ImGui::PushID(static_cast<int>(e));

                        ImGui::Text("%s @ %.2fs", event_action_label(evt.action), evt.time);

                        if (ImGui::BeginMenu("target"))
                        {
                            for (Entity* entity : World::GetEntities())
                            {
                                ImGui::PushID(static_cast<int>(entity->GetObjectId()));
                                bool is_current = (entity->GetObjectId() == evt.target_entity_id);
                                if (ImGui::MenuItem(entity->GetObjectName().c_str(), nullptr, is_current))
                                {
                                    evt.target_entity_id = entity->GetObjectId();
                                }
                                ImGui::PopID();
                            }
                            ImGui::EndMenu();
                        }

                        ImGui::DragFloat("param", &evt.parameter, 0.1f);

                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ImGui::Style::color_error.x, ImGui::Style::color_error.y, ImGui::Style::color_error.z, 1.0f));
                        if (ImGui::MenuItem("delete"))
                        {
                            track.event_clips.erase(track.event_clips.begin() + e);
                            ImGui::PopStyleColor();
                            ImGui::PopID();
                            break;
                        }
                        ImGui::PopStyleColor();

                        ImGui::Separator();
                        ImGui::PopID();
                    }
                }
            }
        }
        ImGui::EndPopup();
    }
}

void Sequencer::DrawPlayhead(float timeline_x, float content_width, float timeline_y, float total_height)
{
    if (total_height <= 0.0f)
    {
        return;
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    float playback_time = 0.0f;
    bool any_playing = false;
    for (const Sequence& seq : m_sequences)
    {
        playback_time = max(playback_time, seq.GetPlaybackTime());
        if (seq.IsPlaying() && !seq.IsPaused())
        {
            any_playing = true;
        }
    }

    float playhead_x = timeline_x + playback_time * m_pixels_per_sec;

    // glow lines
    draw_list->AddLine(
        ImVec2(playhead_x - 2.0f, timeline_y),
        ImVec2(playhead_x - 2.0f, timeline_y + total_height),
        IM_COL32(220, 60, 60, 25), 1.0f
    );
    draw_list->AddLine(
        ImVec2(playhead_x + 2.0f, timeline_y),
        ImVec2(playhead_x + 2.0f, timeline_y + total_height),
        IM_COL32(220, 60, 60, 25), 1.0f
    );

    // shadow
    draw_list->AddLine(
        ImVec2(playhead_x + 1.0f, timeline_y),
        ImVec2(playhead_x + 1.0f, timeline_y + total_height),
        IM_COL32(0, 0, 0, 60), 3.0f
    );

    // animated color when playing
    ImU32 playhead_color = tc.playhead;
    if (any_playing)
    {
        float pulse = 0.7f + 0.3f * sinf(static_cast<float>(ImGui::GetTime()) * 6.0f);
        int r = static_cast<int>(220 * pulse + 255 * (1.0f - pulse));
        playhead_color = IM_COL32(r, 60, 60, 255);
    }

    draw_list->AddLine(
        ImVec2(playhead_x, timeline_y),
        ImVec2(playhead_x, timeline_y + total_height),
        playhead_color, 2.5f
    );

    float tri_size = 7.0f;
    ImVec2 tri[3] = {
        ImVec2(playhead_x - tri_size, timeline_y),
        ImVec2(playhead_x + tri_size, timeline_y),
        ImVec2(playhead_x, timeline_y + tri_size * 1.5f)
    };
    draw_list->AddTriangleFilled(tri[0], tri[1], tri[2], playhead_color);

    // smart label placement
    char time_label[16];
    snprintf(time_label, sizeof(time_label), "%.2f", playback_time);
    ImVec2 label_size = ImGui::CalcTextSize(time_label);

    float label_x, label_y;
    label_y = timeline_y + 1.0f;

    float right_space = (timeline_x + content_width) - (playhead_x + tri_size + 2.0f + label_size.x + 4.0f);
    if (right_space > 0)
    {
        label_x = playhead_x + tri_size + 2.0f;
    }
    else
    {
        label_x = playhead_x - tri_size - label_size.x - 4.0f;
    }

    draw_list->AddRectFilled(
        ImVec2(label_x - 1.0f, label_y),
        ImVec2(label_x + label_size.x + 3.0f, label_y + label_size.y + 1.0f),
        IM_COL32(30, 30, 34, 200), 2.0f
    );
    draw_list->AddText(ImVec2(label_x, label_y), playhead_color, time_label);
}

void Sequencer::DrawMinimap(float timeline_x, float content_width, float timeline_y)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    float mm_y = timeline_y + ruler_height;

    draw_list->AddRectFilled(
        ImVec2(timeline_x, mm_y),
        ImVec2(timeline_x + content_width, mm_y + minimap_height),
        tc.group_bg
    );

    // draw simplified track content
    for (const Sequence& seq : m_sequences)
    {
        float seq_dur = seq.GetDuration();
        for (const SequenceTrack& track : seq.GetTracks())
        {
            const ImVec4& accent = track_type_color(track.type, &track);
            ImU32 col = to_imu32(accent, 0.5f);

            for (const auto& clip : track.camera_clips)
            {
                float x0 = timeline_x + (clip.start_time / m_global_duration) * content_width;
                float x1 = timeline_x + (clip.end_time / m_global_duration) * content_width;
                draw_list->AddRectFilled(ImVec2(x0, mm_y + 2), ImVec2(x1, mm_y + minimap_height - 2), col, 1.0f);
            }

            for (const auto& kf : track.keyframes)
            {
                float x = timeline_x + (kf.time / m_global_duration) * content_width;
                draw_list->AddCircleFilled(ImVec2(x, mm_y + minimap_height * 0.5f), 2.0f, col);
            }

            for (const auto& evt : track.event_clips)
            {
                float x = timeline_x + (evt.time / m_global_duration) * content_width;
                draw_list->AddLine(ImVec2(x, mm_y + 3), ImVec2(x, mm_y + minimap_height - 3), col, 1.0f);
            }
        }
    }

    // visible viewport overlay
    float scroll_x = ImGui::GetScrollX();
    float window_w = ImGui::GetWindowWidth();
    float vis_start = scroll_x / (m_global_duration * m_pixels_per_sec);
    float vis_end   = (scroll_x + window_w) / (m_global_duration * m_pixels_per_sec);
    vis_start = max(0.0f, min(vis_start, 1.0f));
    vis_end   = max(0.0f, min(vis_end, 1.0f));

    float vx0 = timeline_x + vis_start * content_width;
    float vx1 = timeline_x + vis_end * content_width;
    draw_list->AddRectFilled(ImVec2(vx0, mm_y), ImVec2(vx1, mm_y + minimap_height), IM_COL32(255, 255, 255, 20));
    draw_list->AddRect(ImVec2(vx0, mm_y), ImVec2(vx1, mm_y + minimap_height), IM_COL32(255, 255, 255, 60));

    // click on minimap to scroll
    ImGui::SetCursorScreenPos(ImVec2(timeline_x, mm_y));
    ImGui::InvisibleButton("##minimap", ImVec2(content_width, minimap_height));
    if (ImGui::IsItemActive())
    {
        float click_ratio = (ImGui::GetMousePos().x - timeline_x) / content_width;
        click_ratio = max(0.0f, min(click_ratio, 1.0f));
        float target_scroll = click_ratio * m_global_duration * m_pixels_per_sec - window_w * 0.5f;
        ImGui::SetScrollX(max(0.0f, target_scroll));
    }

    draw_list->AddLine(ImVec2(timeline_x, mm_y + minimap_height), ImVec2(timeline_x + content_width, mm_y + minimap_height), tc.separator);
}

void Sequencer::DrawSelectionProperties()
{
    if (m_selected_sequence < 0 || m_selected_sequence >= static_cast<int32_t>(m_sequences.size()))
    {
        return;
    }

    Sequence& seq = m_sequences[m_selected_sequence];
    auto& tracks = seq.GetTracks();

    if (m_selected_track < 0 || m_selected_track >= static_cast<int32_t>(tracks.size()))
    {
        return;
    }

    SequenceTrack& track = tracks[m_selected_track];

    bool has_selection = false;
    if (track.type == SequenceTrackType::Transform && m_selected_keyframe >= 0 && m_selected_keyframe < static_cast<int32_t>(track.keyframes.size()))
    {
        has_selection = true;
    }
    if (track.type == SequenceTrackType::CameraCut && m_selected_clip >= 0 && m_selected_clip < static_cast<int32_t>(track.camera_clips.size()))
    {
        has_selection = true;
    }
    if (track.type == SequenceTrackType::Event && m_selected_keyframe >= 0 && m_selected_keyframe < static_cast<int32_t>(track.event_clips.size()))
    {
        has_selection = true;
    }

    if (!has_selection)
    {
        return;
    }

    ImGui::SetNextItemOpen(m_properties_open);
    if (ImGui::CollapsingHeader("properties", &m_properties_open))
    {
        const ImVec4& accent = track_type_color(track.type, &track);
        float label_col = 80.0f;

        ImGui::PushFont(Editor::font_bold, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, accent);
        ImGui::TextUnformatted(track_type_label(track.type));
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::Text("- %s [%s] (#%d)", track.name.c_str(), seq.GetName().c_str(),
            (track.type == SequenceTrackType::CameraCut) ? m_selected_clip : m_selected_keyframe);
        ImGui::PopStyleColor();

        switch (track.type)
        {
            case SequenceTrackType::Transform:
            {
                SequenceKeyframe& kf = track.keyframes[m_selected_keyframe];

                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("time");
                ImGui::SameLine(label_col);
                ImGui::SetNextItemWidth(150.0f);
                ImGui::DragFloat("##kf_time", &kf.time, 0.01f, 0.0f, seq.GetDuration(), "%.3f s");

                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("interp");
                ImGui::SameLine(label_col);
                ImGui::SetNextItemWidth(150.0f);
                if (ImGui::BeginCombo("##kf_interp", interpolation_mode_label(kf.interpolation)))
                {
                    for (uint8_t m = 0; m < static_cast<uint8_t>(InterpolationMode::Max); m++)
                    {
                        InterpolationMode mode = static_cast<InterpolationMode>(m);
                        bool is_selected = (kf.interpolation == mode);
                        if (ImGui::Selectable(interpolation_mode_label(mode), is_selected))
                        {
                            kf.interpolation = mode;
                        }
                        if (is_selected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                // easing curve preview
                ImGui::SameLine();
                {
                    ImVec2 preview_pos = ImGui::GetCursorScreenPos();
                    float preview_size = 20.0f;
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddRect(preview_pos, ImVec2(preview_pos.x + preview_size, preview_pos.y + preview_size), tc.text_dim);

                    ImVec2 p0 = ImVec2(preview_pos.x, preview_pos.y + preview_size);
                    ImVec2 p3 = ImVec2(preview_pos.x + preview_size, preview_pos.y);
                    ImVec2 cp1, cp2;
                    switch (kf.interpolation)
                    {
                        case InterpolationMode::EaseIn:
                            cp1 = ImVec2(p0.x + preview_size * 0.4f, p0.y);
                            cp2 = ImVec2(p3.x, p3.y + preview_size * 0.0f);
                            break;
                        case InterpolationMode::EaseOut:
                            cp1 = ImVec2(p0.x, p0.y - preview_size * 0.0f);
                            cp2 = ImVec2(p3.x - preview_size * 0.4f, p3.y);
                            break;
                        case InterpolationMode::EaseInOut:
                            cp1 = ImVec2(p0.x + preview_size * 0.4f, p0.y);
                            cp2 = ImVec2(p3.x - preview_size * 0.4f, p3.y);
                            break;
                        default:
                            cp1 = ImVec2(p0.x + preview_size * 0.33f, p0.y - preview_size * 0.33f);
                            cp2 = ImVec2(p3.x - preview_size * 0.33f, p3.y + preview_size * 0.33f);
                            break;
                    }
                    dl->AddBezierCubic(p0, cp1, cp2, p3, to_imu32(accent), 1.5f);
                    ImGui::Dummy(ImVec2(preview_size, preview_size));
                }

                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("position");
                ImGui::SameLine(label_col);
                float pos[3] = { kf.position.x, kf.position.y, kf.position.z };
                ImGui::SetNextItemWidth(300.0f);
                if (ImGui::DragFloat3("##kf_pos", pos, 0.05f, 0.0f, 0.0f, "%.3f"))
                {
                    kf.position.x = pos[0];
                    kf.position.y = pos[1];
                    kf.position.z = pos[2];
                }

                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("rotation");
                ImGui::SameLine(label_col);
                float rot[4] = { kf.rotation.x, kf.rotation.y, kf.rotation.z, kf.rotation.w };
                ImGui::SetNextItemWidth(400.0f);
                if (ImGui::DragFloat4("##kf_rot", rot, 0.005f, -1.0f, 1.0f, "%.4f"))
                {
                    kf.rotation.x = rot[0];
                    kf.rotation.y = rot[1];
                    kf.rotation.z = rot[2];
                    kf.rotation.w = rot[3];
                }

                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

                Entity* target = World::GetEntityById(track.target_entity_id);
                if (target)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(accent.x * 0.3f, accent.y * 0.3f, accent.z * 0.3f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accent.x * 0.5f, accent.y * 0.5f, accent.z * 0.5f, 1.0f));

                    if (ImGui::Button("capture from entity"))
                    {
                        kf.position = target->GetPosition();
                        kf.rotation = target->GetRotation();
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("apply to entity"))
                    {
                        target->SetPosition(kf.position);
                        target->SetRotation(kf.rotation);
                    }

                    ImGui::PopStyleColor(2);
                }

                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(ImGui::Style::color_error.x * 0.5f, ImGui::Style::color_error.y * 0.5f, ImGui::Style::color_error.z * 0.5f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(ImGui::Style::color_error.x * 0.7f, ImGui::Style::color_error.y * 0.7f, ImGui::Style::color_error.z * 0.7f, 1.0f));
                if (ImGui::Button("delete keyframe"))
                {
                    track.keyframes.erase(track.keyframes.begin() + m_selected_keyframe);
                    m_selected_keyframe = -1;
                }
                ImGui::PopStyleColor(2);

                ImGui::PopStyleVar();
            } break;

            case SequenceTrackType::CameraCut:
            {
                SequenceCameraCutClip& clip = track.camera_clips[m_selected_clip];

                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("start");
                ImGui::SameLine(label_col);
                ImGui::SetNextItemWidth(120.0f);
                ImGui::DragFloat("##clip_start", &clip.start_time, 0.05f, 0.0f, clip.end_time - 0.01f, "%.2f s");
                ImGui::SameLine();
                ImGui::TextUnformatted("end");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120.0f);
                ImGui::DragFloat("##clip_end", &clip.end_time, 0.05f, clip.start_time + 0.01f, seq.GetDuration(), "%.2f s");
                ImGui::SameLine();
                ImGui::TextUnformatted("blend");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(80.0f);
                ImGui::DragFloat("##clip_blend", &clip.transition_in, 0.01f, 0.0f, 2.0f, "%.2f s");

                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("camera");
                ImGui::SameLine(label_col);
                Entity* cam_ent = World::GetEntityById(clip.camera_entity_id);
                const char* cam_label = cam_ent ? cam_ent->GetObjectName().c_str() : "none";
                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::BeginCombo("##clip_cam", cam_label))
                {
                    for (Entity* entity : World::GetEntities())
                    {
                        if (entity->GetComponent<Camera>())
                        {
                            ImGui::PushID(static_cast<int>(entity->GetObjectId()));
                            bool is_current = (entity->GetObjectId() == clip.camera_entity_id);
                            if (ImGui::Selectable(entity->GetObjectName().c_str(), is_current))
                            {
                                clip.camera_entity_id = entity->GetObjectId();
                            }
                            if (is_current)
                            {
                                ImGui::SetItemDefaultFocus();
                            }
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndCombo();
                }

                ImGui::SameLine();
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(ImGui::Style::color_error.x * 0.5f, ImGui::Style::color_error.y * 0.5f, ImGui::Style::color_error.z * 0.5f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(ImGui::Style::color_error.x * 0.7f, ImGui::Style::color_error.y * 0.7f, ImGui::Style::color_error.z * 0.7f, 1.0f));
                if (ImGui::Button("delete clip"))
                {
                    track.camera_clips.erase(track.camera_clips.begin() + m_selected_clip);
                    m_selected_clip = -1;
                }
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar();
            } break;

            default:
                break;
        }
    }
}

void Sequencer::DrawAddTrackPopup()
{
    if (ImGui::BeginPopup("add_track_popup"))
    {
        ImGui::Text("add track to sequence");
        ImGui::Separator();

        for (uint32_t s = 0; s < static_cast<uint32_t>(m_sequences.size()); s++)
        {
            Sequence& seq = m_sequences[s];
            ImGui::PushID(static_cast<int>(s));

            if (ImGui::BeginMenu(seq.GetName().c_str()))
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(tc.camera_accent.x, tc.camera_accent.y, tc.camera_accent.z, 1.0f));
                if (ImGui::MenuItem("camera cut"))
                {
                    seq.AddTrack(SequenceTrackType::CameraCut, 0, "camera cut");
                    m_collapsed_sequences.erase(seq.GetId());
                }
                ImGui::PopStyleColor();

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(tc.transform_accent.x, tc.transform_accent.y, tc.transform_accent.z, 1.0f));
                if (ImGui::BeginMenu("transform"))
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
                    for (Entity* entity : World::GetEntities())
                    {
                        ImGui::PushID(static_cast<int>(entity->GetObjectId()));
                        if (ImGui::MenuItem(entity->GetObjectName().c_str()))
                        {
                            seq.AddTrack(SequenceTrackType::Transform, entity->GetObjectId(), entity->GetObjectName());
                            m_collapsed_sequences.erase(seq.GetId());
                        }
                        ImGui::PopID();
                    }
                    ImGui::PopStyleColor();
                    ImGui::EndMenu();
                }
                ImGui::PopStyleColor();

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(tc.event_accent.x, tc.event_accent.y, tc.event_accent.z, 1.0f));
                if (ImGui::MenuItem("event"))
                {
                    seq.AddTrack(SequenceTrackType::Event, 0, "events");
                    m_collapsed_sequences.erase(seq.GetId());
                }
                ImGui::PopStyleColor();

                ImGui::EndMenu();
            }

            ImGui::PopID();
        }

        ImGui::EndPopup();
    }
}
