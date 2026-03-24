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

//= INCLUDES =========================
#include "pch.h"
#include "Sequencer.h"
#include "World/World.h"
#include "World/Entity.h"
#include "World/Components/Camera.h"
#include "../../ImGui/ImGui_Extension.h"
#include "FileSystem/FileSystem.h"
SP_WARNINGS_OFF
#include "IO/pugixml.hpp"
SP_WARNINGS_ON
//====================================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
using namespace math;
//======================

namespace
{
    // layout
    const float track_list_width = 220.0f;
    const float ruler_height     = 26.0f;
    const float toolbar_height   = 32.0f;
    const float type_badge_width = 32.0f;

    // track type accent colors
    const ImVec4 color_camera_accent    = ImVec4(0.35f, 0.55f, 0.85f, 1.0f);
    const ImVec4 color_transform_accent = ImVec4(0.85f, 0.70f, 0.25f, 1.0f);
    const ImVec4 color_event_accent     = ImVec4(0.85f, 0.35f, 0.35f, 1.0f);
    const ImVec4 color_sequence_accent  = ImVec4(0.75f, 0.40f, 0.65f, 1.0f);

    // timeline colors
    const ImU32 color_ruler_bg   = IM_COL32(30, 30, 34, 255);
    const ImU32 color_ruler_text = IM_COL32(170, 170, 175, 255);
    const ImU32 color_ruler_tick = IM_COL32(70, 70, 78, 255);
    const ImU32 color_playhead   = IM_COL32(220, 60, 60, 255);
    const ImU32 color_track_bg_a = IM_COL32(38, 38, 42, 255);
    const ImU32 color_track_bg_b = IM_COL32(44, 44, 48, 255);
    const ImU32 color_selected   = IM_COL32(255, 255, 100, 255);

    ImU32 to_imu32(const ImVec4& c, float alpha = 1.0f)
    {
        return IM_COL32(
            static_cast<int>(c.x * 255), static_cast<int>(c.y * 255),
            static_cast<int>(c.z * 255), static_cast<int>(alpha * 255));
    }

    const ImVec4& track_type_color(SequenceTrackType type)
    {
        switch (type)
        {
            case SequenceTrackType::CameraCut: return color_camera_accent;
            case SequenceTrackType::Transform: return color_transform_accent;
            case SequenceTrackType::Event:     return color_event_accent;
            default:                           return color_sequence_accent;
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

    void toolbar_separator()
    {
        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();
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
        return "";

    return FileSystem::GetDirectoryFromFilePath(world_path) + "sequencer.xml";
}

void Sequencer::Save()
{
    string file_path = GetFilePath();
    if (file_path.empty())
        return;

    pugi::xml_document doc;
    pugi::xml_node root = doc.append_child("Sequencer");

    for (Sequence& seq : m_sequences)
    {
        pugi::xml_node seq_node = root.append_child("Sequence");
        seq.Save(seq_node);
    }

    doc.save_file(file_path.c_str(), " ", pugi::format_indent);
}

void Sequencer::Load()
{
    m_sequences.clear();
    m_active_sequence_id = 0;

    string file_path = GetFilePath();
    if (file_path.empty())
        return;

    pugi::xml_document doc;
    if (!doc.load_file(file_path.c_str()))
        return;

    pugi::xml_node root = doc.child("Sequencer");
    if (!root)
        return;

    for (pugi::xml_node seq_node = root.child("Sequence"); seq_node; seq_node = seq_node.next_sibling("Sequence"))
    {
        Sequence seq;
        seq.Load(seq_node);
        m_sequences.push_back(move(seq));
    }

    if (!m_sequences.empty())
        m_active_sequence_id = m_sequences[0].GetId();
}

Sequence* Sequencer::ResolveSequence()
{
    if (m_active_sequence_id != 0)
    {
        for (Sequence& seq : m_sequences)
        {
            if (seq.GetId() == m_active_sequence_id)
                return &seq;
        }
        m_active_sequence_id = 0;
    }

    if (!m_sequences.empty())
    {
        m_active_sequence_id = m_sequences[0].GetId();
        return &m_sequences[0];
    }

    return nullptr;
}

float Sequencer::SnapTime(float time) const
{
    if (!m_snap_enabled || m_snap_interval <= 0.0f)
        return time;

    return roundf(time / m_snap_interval) * m_snap_interval;
}

void Sequencer::OnTickVisible()
{
    // tick all playing sequences
    for (Sequence& seq : m_sequences)
    {
        seq.Tick();
    }

    DrawSequenceSelector();

    Sequence* sequence = ResolveSequence();
    if (!sequence)
        return;

    DrawToolbar(sequence);
    ImGui::Separator();

    if (sequence->GetTracks().empty())
    {
        DrawEmptyTracksState(sequence);
    }
    else
    {
        DrawTimeline(sequence);
        DrawSelectionProperties(sequence);
    }

    DrawAddTrackPopup(sequence);
}

void Sequencer::DrawSequenceSelector()
{
    if (m_sequences.empty())
    {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float center_x = avail.x * 0.5f;

        ImGui::Dummy(ImVec2(0, avail.y * 0.2f));

        // title
        ImGui::PushFont(Editor::font_bold);
        ImVec2 title_size = ImGui::CalcTextSize("cinematic sequencer");
        ImGui::SetCursorPosX(center_x - title_size.x * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(color_sequence_accent.x, color_sequence_accent.y, color_sequence_accent.z, 1.0f));
        ImGui::TextUnformatted("cinematic sequencer");
        ImGui::PopStyleColor();
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(0, 8.0f));

        // description
        const char* desc = "create timeline-based cinematics with camera cuts,\nentity animations, and scripted events.";
        ImVec2 desc_size = ImGui::CalcTextSize(desc);
        ImGui::SetCursorPosX(center_x - desc_size.x * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.58f, 1.0f));
        ImGui::TextUnformatted(desc);
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, 20.0f));

        // create button
        const char* btn_label = "create new sequence";
        ImVec2 btn_size = ImVec2(ImGui::CalcTextSize(btn_label).x + 32.0f, 32.0f);

        ImGui::SetCursorPosX(center_x - btn_size.x * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color_sequence_accent.x * 0.35f, color_sequence_accent.y * 0.35f, color_sequence_accent.z * 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color_sequence_accent.x * 0.5f, color_sequence_accent.y * 0.5f, color_sequence_accent.z * 0.5f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(color_sequence_accent.x * 0.25f, color_sequence_accent.y * 0.25f, color_sequence_accent.z * 0.25f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

        if (ImGui::Button(btn_label, btn_size))
        {
            m_sequences.emplace_back();
            m_active_sequence_id = m_sequences.back().GetId();
        }

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

        ImGui::Dummy(ImVec2(0, 16.0f));

        // quick-start steps
        ImGui::SetCursorPosX(center_x - 140.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.48f, 1.0f));
        ImGui::TextUnformatted("1. click the button above to create a sequence");
        ImGui::SetCursorPosX(center_x - 140.0f);
        ImGui::TextUnformatted("2. add tracks for cameras, animations, and events");
        ImGui::SetCursorPosX(center_x - 140.0f);
        ImGui::TextUnformatted("3. press play to preview your cinematic");
        ImGui::PopStyleColor();

        return;
    }

    // context bar - inline, no child window
    ImGui::PushFont(Editor::font_bold);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(color_sequence_accent.x, color_sequence_accent.y, color_sequence_accent.z, 1.0f));
    ImGui::TextUnformatted("sequence:");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SameLine();

    Sequence* current = ResolveSequence();
    const char* current_name = current ? current->GetName().c_str() : "none";

    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::BeginCombo("##seq_picker", current_name, ImGuiComboFlags_NoArrowButton))
    {
        for (Sequence& seq : m_sequences)
        {
            ImGui::PushID(static_cast<int>(seq.GetId() & 0xFFFFFFFF));
            bool is_selected = (seq.GetId() == m_active_sequence_id);
            if (ImGui::Selectable(seq.GetName().c_str(), is_selected))
            {
                m_active_sequence_id = seq.GetId();
                m_selected_track    = -1;
                m_selected_keyframe = -1;
                m_selected_clip     = -1;
            }
            if (is_selected)
                ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    if (current)
    {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.55f, 1.0f));
        ImGui::Text("(%u tracks, %.1fs)", static_cast<uint32_t>(current->GetTracks().size()), current->GetDuration());
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.4f, 0.55f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.5f, 0.65f, 1.0f));
    if (ImGui::Button("+"))
    {
        m_sequences.emplace_back();
        m_active_sequence_id = m_sequences.back().GetId();
    }
    ImGui::PopStyleColor(2);

    ImGui::SameLine();
    if (ImGui::Button("save"))
        Save();

    if (current && m_sequences.size() > 0)
    {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("del"))
        {
            uint64_t id_to_remove = m_active_sequence_id;
            m_sequences.erase(
                remove_if(m_sequences.begin(), m_sequences.end(),
                    [id_to_remove](const Sequence& s) { return s.GetId() == id_to_remove; }),
                m_sequences.end()
            );
            m_active_sequence_id = m_sequences.empty() ? 0 : m_sequences[0].GetId();
            m_selected_track    = -1;
            m_selected_keyframe = -1;
            m_selected_clip     = -1;
        }
        ImGui::PopStyleColor(2);
    }

    ImGui::PopStyleVar();
    ImGui::Separator();
}

void Sequencer::DrawEmptyTracksState(Sequence* sequence)
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float center_x = avail.x * 0.5f;

    ImGui::Dummy(ImVec2(0, max(20.0f, avail.y * 0.1f)));

    // header
    ImGui::PushFont(Editor::font_bold);
    const char* header = "add tracks to build your cinematic";
    ImVec2 header_size = ImGui::CalcTextSize(header);
    ImGui::SetCursorPosX(center_x - header_size.x * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.75f, 0.78f, 1.0f));
    ImGui::TextUnformatted(header);
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0, 6.0f));

    const char* sub = "each track type controls a different aspect of the sequence:";
    ImVec2 sub_size = ImGui::CalcTextSize(sub);
    ImGui::SetCursorPosX(center_x - sub_size.x * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.53f, 1.0f));
    ImGui::TextUnformatted(sub);
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, 20.0f));

    float line_height = ImGui::GetTextLineHeightWithSpacing();

    struct TrackCard
    {
        const char*       badge;
        const char*       title;
        const char*       desc;
        const char*       btn;
        const ImVec4*     color;
        SequenceTrackType type;
    };

    TrackCard cards[3] = {
        { "CAM", "camera cuts",  "switch between cameras during playback", "add camera track",    &color_camera_accent,    SequenceTrackType::CameraCut },
        { "TFM", "transform",    "animate entity position and rotation",   "add transform track", &color_transform_accent, SequenceTrackType::Transform },
        { "EVT", "events",       "trigger actions at specific times",      "add event track",     &color_event_accent,     SequenceTrackType::Event     },
    };

    float row_height = line_height * 2.0f + 16.0f;
    float list_width = min(500.0f, avail.x - 40.0f);
    float list_x     = (avail.x - list_width) * 0.5f;

    for (int c = 0; c < 3; c++)
    {
        TrackCard& card = cards[c];
        ImGui::PushID(c);

        ImGui::SetCursorPosX(list_x);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(card.color->x * 0.1f, card.color->y * 0.1f, card.color->z * 0.1f, 0.6f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::BeginChild("##track_row", ImVec2(list_width, row_height), true);
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 row_start = ImGui::GetCursorScreenPos();

            // left accent bar
            dl->AddRectFilled(
                row_start,
                ImVec2(row_start.x + 4.0f, row_start.y + row_height),
                to_imu32(*card.color, 0.8f)
            );

            // badge
            ImVec2 badge_pos = ImVec2(row_start.x + 12.0f, row_start.y + 8.0f);
            ImVec2 badge_text_size = ImGui::CalcTextSize(card.badge);
            dl->AddRectFilled(
                badge_pos,
                ImVec2(badge_pos.x + badge_text_size.x + 10.0f, badge_pos.y + badge_text_size.y + 4.0f),
                to_imu32(*card.color, 0.5f), 3.0f
            );
            dl->AddText(ImVec2(badge_pos.x + 5.0f, badge_pos.y + 2.0f), IM_COL32(255, 255, 255, 230), card.badge);

            // title
            float title_x = badge_pos.x + badge_text_size.x + 18.0f;
            dl->AddText(ImVec2(title_x, row_start.y + 10.0f), to_imu32(*card.color), card.title);

            // description
            dl->AddText(ImVec2(title_x, row_start.y + 10.0f + line_height), IM_COL32(140, 140, 145, 200), card.desc);

            // button on the right side, vertically centered
            float btn_width = ImGui::CalcTextSize(card.btn).x + 20.0f;
            float btn_x = list_width - btn_width - 10.0f;
            float btn_y = (row_height - ImGui::GetFrameHeight()) * 0.5f;

            ImGui::SetCursorPos(ImVec2(btn_x, btn_y));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(card.color->x * 0.3f, card.color->y * 0.3f, card.color->z * 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(card.color->x * 0.5f, card.color->y * 0.5f, card.color->z * 0.5f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(card.color->x * 0.2f, card.color->y * 0.2f, card.color->z * 0.2f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

            if (card.type == SequenceTrackType::Transform)
            {
                if (ImGui::Button(card.btn))
                    ImGui::OpenPopup("##empty_add_transform");

                if (ImGui::BeginPopup("##empty_add_transform"))
                {
                    ImGui::Text("pick target entity");
                    ImGui::Separator();
                    for (Entity* entity : World::GetEntities())
                    {
                        ImGui::PushID(static_cast<int>(entity->GetObjectId()));
                        if (ImGui::MenuItem(entity->GetObjectName().c_str()))
                            sequence->AddTrack(SequenceTrackType::Transform, entity->GetObjectId(), entity->GetObjectName());
                        ImGui::PopID();
                    }
                    ImGui::EndPopup();
                }
            }
            else
            {
                if (ImGui::Button(card.btn))
                {
                    const char* default_name = (card.type == SequenceTrackType::CameraCut) ? "camera cut" : "events";
                    sequence->AddTrack(card.type, 0, default_name);
                }
            }

            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, 4.0f));

        ImGui::PopID();
    }
}

void Sequencer::DrawToolbar(Sequence* sequence)
{
    float duration = sequence->GetDuration();
    float speed    = sequence->GetPlaybackSpeed();
    bool  looping  = sequence->IsLooping();
    bool  playing  = sequence->IsPlaying() && !sequence->IsPaused();

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);

    // transport group
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    {
        if (playing)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.35f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.45f, 0.2f, 1.0f));
            if (ImGui::Button("||", ImVec2(28, 0)))
                sequence->Pause();
            ImGui::PopStyleColor(2);
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.55f, 0.25f, 1.0f));
            if (ImGui::Button(">", ImVec2(28, 0)))
                sequence->Play();
            ImGui::PopStyleColor(2);
        }

        ImGui::SameLine();
        if (ImGui::Button("[]", ImVec2(28, 0)))
            sequence->StopPlayback();

        ImGui::SameLine();
        if (looping)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.55f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.65f, 1.0f));
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.22f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
        }
        if (ImGui::Button("loop", ImVec2(0, 0)))
            sequence->SetLooping(!looping);
        ImGui::PopStyleColor(2);
    }
    ImGui::PopStyleVar();

    // timecode display
    toolbar_separator();
    {
        float current_time = sequence->GetPlaybackTime();
        int cur_min = static_cast<int>(current_time) / 60;
        int cur_sec = static_cast<int>(current_time) % 60;
        int cur_fra = static_cast<int>((current_time - floorf(current_time)) * 100.0f);
        int dur_min = static_cast<int>(duration) / 60;
        int dur_sec = static_cast<int>(duration) % 60;
        int dur_fra = static_cast<int>((duration - floorf(duration)) * 100.0f);

        char timecode[64];
        snprintf(timecode, sizeof(timecode), "%02d:%02d.%02d / %02d:%02d.%02d",
            cur_min, cur_sec, cur_fra, dur_min, dur_sec, dur_fra);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
        ImGui::PushFont(Editor::font_bold);
        ImGui::TextUnformatted(timecode);
        ImGui::PopFont();
        ImGui::PopStyleColor();
    }

    // editing group
    toolbar_separator();
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.4f, 0.55f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.5f, 0.65f, 1.0f));
        if (ImGui::Button("+ track"))
            ImGui::OpenPopup("add_track_popup");
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();

        ImGui::SameLine();
        if (m_snap_enabled)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.55f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.65f, 1.0f));
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.22f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        if (ImGui::Button("snap"))
            m_snap_enabled = !m_snap_enabled;
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::SliderFloat("##zoom", &m_pixels_per_sec, 20.0f, 400.0f, "zoom: %.0f");
    }

    // settings gear
    toolbar_separator();
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        if (ImGui::Button("settings"))
            ImGui::OpenPopup("##seq_settings");
        ImGui::PopStyleVar();

        if (ImGui::BeginPopup("##seq_settings"))
        {
            ImGui::Text("sequence settings");
            ImGui::Separator();

            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::DragFloat("duration", &duration, 0.1f, 0.1f, 600.0f, "%.1f s"))
                sequence->SetDuration(duration);

            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::DragFloat("speed", &speed, 0.01f, 0.1f, 10.0f, "%.2fx"))
                sequence->SetPlaybackSpeed(speed);

            ImGui::Separator();

            ImGui::SetNextItemWidth(100.0f);
            ImGui::DragFloat("snap interval", &m_snap_interval, 0.05f, 0.05f, 5.0f, "%.2f s");

            ImGui::SetNextItemWidth(100.0f);
            ImGui::DragFloat("track height", &m_track_height, 1.0f, 20.0f, 60.0f, "%.0f px");

            // sequence name
            ImGui::Separator();
            char name_buf[128];
            snprintf(name_buf, sizeof(name_buf), "%s", sequence->GetName().c_str());
            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::InputText("name", name_buf, sizeof(name_buf), ImGuiInputTextFlags_EnterReturnsTrue))
                sequence->SetName(name_buf);

            ImGui::EndPopup();
        }
    }
}

void Sequencer::DrawTimeline(Sequence* sequence)
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 10.0f || avail.y < 10.0f)
        return;

    // height based on actual content: ruler + tracks + some padding
    float track_count    = max(1.0f, static_cast<float>(sequence->GetTracks().size()));
    float content_height = ruler_height + track_count * m_track_height + 20.0f;
    float timeline_height = min(content_height, avail.y * 0.75f);
    timeline_height = max(timeline_height, 80.0f);

    ImGui::BeginChild("##timeline_region", ImVec2(avail.x, timeline_height), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::BeginChild("##track_list", ImVec2(track_list_width, 0), true, ImGuiWindowFlags_NoScrollbar);
    {
        ImGui::Dummy(ImVec2(0, ruler_height));
        ImGui::Separator();
        DrawTrackList(sequence);
    }
    ImGui::EndChild();

    ImGui::SameLine(0, 0);

    ImGui::BeginChild("##timeline_content", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    {
        DrawTimelineContent(sequence);
    }
    ImGui::EndChild();

    ImGui::EndChild();
}

void Sequencer::DrawTrackList(Sequence* sequence)
{
    auto& tracks = sequence->GetTracks();

    for (uint32_t i = 0; i < static_cast<uint32_t>(tracks.size()); i++)
    {
        SequenceTrack& track = tracks[i];
        ImGui::PushID(static_cast<int>(i));

        bool selected = (m_selected_track == static_cast<int32_t>(i));
        const ImVec4& accent = track_type_color(track.type);

        // track row with fixed height
        ImVec2 row_start = ImGui::GetCursorScreenPos();
        float row_width = track_list_width - 16.0f;

        // colored left-edge indicator bar
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(
            ImVec2(row_start.x - 4.0f, row_start.y),
            ImVec2(row_start.x, row_start.y + m_track_height),
            to_imu32(accent)
        );

        // selection background
        if (selected)
        {
            dl->AddRectFilled(
                row_start,
                ImVec2(row_start.x + row_width, row_start.y + m_track_height),
                IM_COL32(accent.x * 60, accent.y * 60, accent.z * 60, 80)
            );
        }

        // invisible selectable for the full row
        if (ImGui::Selectable("##track_sel", selected, ImGuiSelectableFlags_None, ImVec2(row_width, m_track_height)))
        {
            m_selected_track    = static_cast<int32_t>(i);
            m_selected_keyframe = -1;
            m_selected_clip     = -1;
        }

        // double-click to rename
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            m_renaming_track = static_cast<int32_t>(i);
            snprintf(m_rename_buf, sizeof(m_rename_buf), "%s", track.name.c_str());
        }

        // overlay: badge + name + target on top of the selectable
        ImVec2 text_pos = ImVec2(row_start.x + 4.0f, row_start.y + 2.0f);

        // type badge
        {
            const char* badge = track_type_badge(track.type);
            ImVec2 badge_size = ImGui::CalcTextSize(badge);
            ImVec2 badge_min = ImVec2(text_pos.x, text_pos.y);
            ImVec2 badge_max = ImVec2(badge_min.x + badge_size.x + 8.0f, badge_min.y + badge_size.y + 2.0f);
            dl->AddRectFilled(badge_min, badge_max, to_imu32(accent, 0.7f), 2.0f);
            dl->AddText(ImVec2(badge_min.x + 4.0f, badge_min.y + 1.0f), IM_COL32(255, 255, 255, 230), badge);
            text_pos.x = badge_max.x + 4.0f;
        }

        // track name (or inline rename input)
        if (m_renaming_track == static_cast<int32_t>(i))
        {
            ImGui::SetCursorScreenPos(ImVec2(text_pos.x, text_pos.y));
            ImGui::SetNextItemWidth(row_width - (text_pos.x - row_start.x) - 24.0f);
            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##rename", m_rename_buf, sizeof(m_rename_buf),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
            {
                track.name = m_rename_buf;
                m_renaming_track = -1;
            }
            if (!ImGui::IsItemActive() && m_renaming_track == static_cast<int32_t>(i))
            {
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                    m_renaming_track = -1;
            }
        }
        else
        {
            // clip text to avoid overflow into delete button area
            float max_text_x = row_start.x + row_width - 22.0f;
            dl->PushClipRect(ImVec2(text_pos.x, row_start.y), ImVec2(max_text_x, row_start.y + m_track_height));
            dl->AddText(text_pos, IM_COL32(220, 220, 220, 255), track.name.c_str());
            dl->PopClipRect();
        }

        // delete button (right-aligned)
        {
            float btn_x = row_start.x + row_width - 18.0f;
            float btn_y = row_start.y + (m_track_height - 14.0f) * 0.5f;
            ImGui::SetCursorScreenPos(ImVec2(btn_x, btn_y));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.2f, 0.2f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            if (ImGui::SmallButton("x"))
            {
                sequence->RemoveTrack(i);
                if (m_selected_track == static_cast<int32_t>(i))
                    m_selected_track = -1;
                if (m_renaming_track == static_cast<int32_t>(i))
                    m_renaming_track = -1;
                ImGui::PopStyleColor(3);
                ImGui::PopID();
                break;
            }
            ImGui::PopStyleColor(3);
        }

        // right-click context menu
        ImGui::SetCursorScreenPos(ImVec2(row_start.x, row_start.y));
        ImGui::InvisibleButton("##ctx_area", ImVec2(row_width, m_track_height));
        if (ImGui::BeginPopupContextItem("track_context"))
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
                            track.target_entity_id = entity->GetObjectId();
                        ImGui::PopID();
                    }
                    ImGui::EndMenu();
                }
            }

            if (ImGui::MenuItem("rename"))
            {
                m_renaming_track = static_cast<int32_t>(i);
                snprintf(m_rename_buf, sizeof(m_rename_buf), "%s", track.name.c_str());
            }

            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
            if (ImGui::MenuItem("delete track"))
            {
                sequence->RemoveTrack(i);
                if (m_selected_track == static_cast<int32_t>(i))
                    m_selected_track = -1;
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

void Sequencer::DrawTimelineContent(Sequence* sequence)
{
    auto& tracks = sequence->GetTracks();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    float content_width = sequence->GetDuration() * m_pixels_per_sec;
    ImVec2 canvas_pos   = ImGui::GetCursorScreenPos();
    float timeline_x    = canvas_pos.x;
    float timeline_y    = canvas_pos.y;

    ImGui::Dummy(ImVec2(content_width, ruler_height + m_track_height * static_cast<float>(tracks.size()) + 20.0f));

    // ruler
    {
        draw_list->AddRectFilled(
            ImVec2(timeline_x, timeline_y),
            ImVec2(timeline_x + content_width, timeline_y + ruler_height),
            color_ruler_bg
        );

        float step = 1.0f;
        if (m_pixels_per_sec < 40.0f)  step = 5.0f;
        if (m_pixels_per_sec > 200.0f) step = 0.5f;

        for (float t = 0.0f; t <= sequence->GetDuration(); t += step)
        {
            float x = timeline_x + t * m_pixels_per_sec;

            draw_list->AddLine(
                ImVec2(x, timeline_y + ruler_height - 12.0f),
                ImVec2(x, timeline_y + ruler_height),
                color_ruler_tick, 1.0f
            );

            char time_str[16];
            int mins = static_cast<int>(t) / 60;
            float secs = t - static_cast<float>(mins * 60);
            if (mins > 0)
                snprintf(time_str, sizeof(time_str), "%d:%04.1f", mins, secs);
            else
                snprintf(time_str, sizeof(time_str), "%.1fs", t);
            draw_list->AddText(ImVec2(x + 2.0f, timeline_y + 2.0f), color_ruler_text, time_str);

            if (step >= 1.0f)
            {
                for (float sub = step * 0.25f; sub < step; sub += step * 0.25f)
                {
                    float sub_x = timeline_x + (t + sub) * m_pixels_per_sec;
                    if (sub_x < timeline_x + content_width)
                    {
                        draw_list->AddLine(
                            ImVec2(sub_x, timeline_y + ruler_height - 5.0f),
                            ImVec2(sub_x, timeline_y + ruler_height),
                            color_ruler_tick
                        );
                    }
                }
            }
        }

        // snap grid lines extending into track area
        if (m_snap_enabled)
        {
            float total_h = m_track_height * static_cast<float>(tracks.size());
            for (float t = 0.0f; t <= sequence->GetDuration(); t += m_snap_interval)
            {
                float x = timeline_x + t * m_pixels_per_sec;
                draw_list->AddLine(
                    ImVec2(x, timeline_y + ruler_height),
                    ImVec2(x, timeline_y + ruler_height + total_h),
                    IM_COL32(60, 60, 70, 40)
                );
            }
        }
    }

    // ruler scrubbing
    {
        ImVec2 ruler_min = ImVec2(timeline_x, timeline_y);
        ImVec2 ruler_max = ImVec2(timeline_x + content_width, timeline_y + ruler_height);

        if (ImGui::IsMouseHoveringRect(ruler_min, ruler_max) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            m_scrubbing = true;

        if (m_scrubbing)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                float mouse_x = ImGui::GetMousePos().x;
                float time = (mouse_x - timeline_x) / m_pixels_per_sec;
                time = max(0.0f, min(time, sequence->GetDuration()));
                time = SnapTime(time);
                sequence->SetPlaybackTime(time);
            }
            else
            {
                m_scrubbing = false;
            }
        }
    }

    // draw tracks
    float track_start_y = timeline_y + ruler_height;
    for (uint32_t i = 0; i < static_cast<uint32_t>(tracks.size()); i++)
    {
        SequenceTrack& track = tracks[i];
        float row_y = track_start_y + static_cast<float>(i) * m_track_height;
        const ImVec4& accent = track_type_color(track.type);

        // row background with subtle type tint
        ImU32 base_color = (i % 2 == 0) ? color_track_bg_a : color_track_bg_b;
        draw_list->AddRectFilled(
            ImVec2(timeline_x, row_y),
            ImVec2(timeline_x + content_width, row_y + m_track_height),
            base_color
        );

        // subtle accent tint overlay
        draw_list->AddRectFilled(
            ImVec2(timeline_x, row_y),
            ImVec2(timeline_x + content_width, row_y + m_track_height),
            to_imu32(accent, 0.04f)
        );

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
                    bool sel = (m_selected_track == static_cast<int32_t>(i) && m_selected_clip == static_cast<int32_t>(c));
                    ImU32 cc = sel ? color_selected : (hovered ? clip_hover : clip_base);

                    draw_list->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), cc, 4.0f);

                    if (sel)
                        draw_list->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(255, 255, 255, 120), 4.0f, 0, 1.5f);

                    Entity* cam_ent = World::GetEntityById(clip.camera_entity_id);
                    const char* cam_name = cam_ent ? cam_ent->GetObjectName().c_str() : "?";
                    ImVec2 text_size = ImGui::CalcTextSize(cam_name);
                    float clip_width = x1 - x0;
                    float text_x = x0 + max(4.0f, (clip_width - text_size.x) * 0.5f);
                    float text_y = y0 + (y1 - y0 - text_size.y) * 0.5f;

                    draw_list->PushClipRect(ImVec2(x0 + 2.0f, y0), ImVec2(x1 - 2.0f, y1));
                    draw_list->AddText(ImVec2(text_x, text_y), IM_COL32(255, 255, 255, 230), cam_name);
                    draw_list->PopClipRect();

                    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        m_selected_track    = static_cast<int32_t>(i);
                        m_selected_clip     = static_cast<int32_t>(c);
                        m_selected_keyframe = -1;
                    }

                    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                    {
                        m_selected_track = static_cast<int32_t>(i);
                        m_selected_clip  = static_cast<int32_t>(c);
                        ImGui::OpenPopup("clip_context");
                    }
                }

                if (ImGui::IsMouseHoveringRect(ImVec2(timeline_x, row_y), ImVec2(timeline_x + content_width, row_y + m_track_height)))
                {
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && m_selected_clip == -1)
                    {
                        m_selected_track  = static_cast<int32_t>(i);
                        m_popup_mouse_x   = ImGui::GetMousePos().x;
                        ImGui::OpenPopup("add_camera_clip");
                    }
                }
            } break;

            case SequenceTrackType::Transform:
            {
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
                    bool sel = (m_selected_track == static_cast<int32_t>(i) && m_selected_keyframe == static_cast<int32_t>(k));
                    ImU32 kf_color = sel ? color_selected : (hovered ? kf_hover : kf_base);

                    draw_list->AddConvexPolyFilled(diamond, 4, kf_color);

                    if (sel)
                        draw_list->AddPolyline(diamond, 4, IM_COL32(255, 255, 255, 180), ImDrawFlags_Closed, 1.5f);

                    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        m_selected_track    = static_cast<int32_t>(i);
                        m_selected_keyframe = static_cast<int32_t>(k);
                        m_selected_clip     = -1;
                        m_dragging_keyframe = static_cast<int32_t>(k);
                        m_dragging_track    = static_cast<int32_t>(i);
                    }
                }

                if (ImGui::IsMouseHoveringRect(ImVec2(timeline_x, row_y), ImVec2(timeline_x + content_width, row_y + m_track_height)))
                {
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                    {
                        m_selected_track = static_cast<int32_t>(i);
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
                    ImU32 evt_color = hovered ? evt_hover : evt_base;

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
                        draw_list->AddText(ImVec2(cx + 10.0f, row_y + 2.0f), IM_COL32(180, 180, 180, 180), label);
                    }

                    if (hovered)
                    {
                        ImGui::BeginTooltip();
                        ImGui::Text("%s @ %.2fs", event_action_label(evt.action), evt.time);
                        Entity* target = World::GetEntityById(evt.target_entity_id);
                        if (target)
                            ImGui::Text("target: %s", target->GetObjectName().c_str());
                        ImGui::EndTooltip();
                    }

                    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                    {
                        m_selected_track = static_cast<int32_t>(i);
                        ImGui::OpenPopup("event_context");
                    }
                }

                if (ImGui::IsMouseHoveringRect(ImVec2(timeline_x, row_y), ImVec2(timeline_x + content_width, row_y + m_track_height)))
                {
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                    {
                        m_selected_track = static_cast<int32_t>(i);
                        m_popup_mouse_x  = ImGui::GetMousePos().x;
                        ImGui::OpenPopup("add_event");
                    }
                }
            } break;

            default:
                break;
        }
    }

    // keyframe dragging with snap support
    if (m_dragging_keyframe >= 0 && m_dragging_track >= 0)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            auto& tracks_ref = sequence->GetTracks();
            if (m_dragging_track < static_cast<int32_t>(tracks_ref.size()))
            {
                auto& kfs = tracks_ref[m_dragging_track].keyframes;
                if (m_dragging_keyframe < static_cast<int32_t>(kfs.size()))
                {
                    float mouse_x = ImGui::GetMousePos().x;
                    float new_time = (mouse_x - timeline_x) / m_pixels_per_sec;
                    new_time = max(0.0f, min(new_time, sequence->GetDuration()));
                    new_time = SnapTime(new_time);
                    kfs[m_dragging_keyframe].time = new_time;
                }
            }
        }
        else
        {
            m_dragging_keyframe = -1;
            m_dragging_track    = -1;
        }
    }

    // playhead
    float total_track_height = m_track_height * static_cast<float>(tracks.size());
    DrawPlayhead(sequence, timeline_x, content_width, timeline_y, ruler_height + total_track_height);

    // context menus
    if (ImGui::BeginPopup("add_camera_clip"))
    {
        if (m_selected_track >= 0 && m_selected_track < static_cast<int32_t>(tracks.size()))
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
                        mouse_time = max(0.0f, min(mouse_time, sequence->GetDuration()));
                        mouse_time = SnapTime(mouse_time);

                        SequenceCameraCutClip clip;
                        clip.start_time       = mouse_time;
                        clip.end_time         = min(mouse_time + 3.0f, sequence->GetDuration());
                        clip.camera_entity_id = entity->GetObjectId();
                        tracks[m_selected_track].camera_clips.push_back(clip);
                    }
                    ImGui::PopID();
                }
            }
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("add_transform_keyframe"))
    {
        if (m_selected_track >= 0 && m_selected_track < static_cast<int32_t>(tracks.size()))
        {
            SequenceTrack& track = tracks[m_selected_track];

            if (ImGui::MenuItem("add keyframe here"))
            {
                float mouse_time = (m_popup_mouse_x - timeline_x) / m_pixels_per_sec;
                mouse_time = max(0.0f, min(mouse_time, sequence->GetDuration()));
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
                kf.time = SnapTime(sequence->GetPlaybackTime());

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
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("add_event"))
    {
        if (m_selected_track >= 0 && m_selected_track < static_cast<int32_t>(tracks.size()))
        {
            ImGui::Text("add event");
            ImGui::Separator();

            float mouse_time = (m_popup_mouse_x - timeline_x) / m_pixels_per_sec;
            mouse_time = max(0.0f, min(mouse_time, sequence->GetDuration()));
            mouse_time = SnapTime(mouse_time);

            for (uint8_t a = 0; a < static_cast<uint8_t>(SequenceEventAction::Max); a++)
            {
                SequenceEventAction action = static_cast<SequenceEventAction>(a);
                if (ImGui::MenuItem(event_action_label(action)))
                {
                    SequenceEventClip evt;
                    evt.time   = mouse_time;
                    evt.action = action;
                    tracks[m_selected_track].event_clips.push_back(evt);

                    sort(tracks[m_selected_track].event_clips.begin(), tracks[m_selected_track].event_clips.end(),
                        [](const SequenceEventClip& a, const SequenceEventClip& b) { return a.time < b.time; });
                }
            }
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("clip_context"))
    {
        if (m_selected_track >= 0 && m_selected_clip >= 0)
        {
            auto& track = tracks[m_selected_track];
            if (track.type == SequenceTrackType::CameraCut && m_selected_clip < static_cast<int32_t>(track.camera_clips.size()))
            {
                auto& clip = track.camera_clips[m_selected_clip];

                ImGui::DragFloat("start", &clip.start_time, 0.05f, 0.0f, clip.end_time - 0.01f, "%.2fs");
                ImGui::DragFloat("end", &clip.end_time, 0.05f, clip.start_time + 0.01f, sequence->GetDuration(), "%.2fs");
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
                                clip.camera_entity_id = entity->GetObjectId();
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndMenu();
                }

                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
                if (ImGui::MenuItem("delete clip"))
                {
                    track.camera_clips.erase(track.camera_clips.begin() + m_selected_clip);
                    m_selected_clip = -1;
                }
                ImGui::PopStyleColor();
            }
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("event_context"))
    {
        if (m_selected_track >= 0 && m_selected_track < static_cast<int32_t>(tracks.size()))
        {
            auto& track = tracks[m_selected_track];
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
                                evt.target_entity_id = entity->GetObjectId();
                            ImGui::PopID();
                        }
                        ImGui::EndMenu();
                    }

                    ImGui::DragFloat("param", &evt.parameter, 0.1f);

                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
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
        ImGui::EndPopup();
    }
}

void Sequencer::DrawPlayhead(Sequence* sequence, float timeline_x, float timeline_width, float timeline_y, float timeline_height)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    float playhead_x = timeline_x + sequence->GetPlaybackTime() * m_pixels_per_sec;

    // subtle shadow
    draw_list->AddLine(
        ImVec2(playhead_x + 1.0f, timeline_y),
        ImVec2(playhead_x + 1.0f, timeline_y + timeline_height),
        IM_COL32(0, 0, 0, 60), 3.0f
    );

    // main line
    draw_list->AddLine(
        ImVec2(playhead_x, timeline_y),
        ImVec2(playhead_x, timeline_y + timeline_height),
        color_playhead, 2.5f
    );

    // triangle at the top
    float tri_size = 7.0f;
    ImVec2 tri[3] = {
        ImVec2(playhead_x - tri_size, timeline_y),
        ImVec2(playhead_x + tri_size, timeline_y),
        ImVec2(playhead_x, timeline_y + tri_size * 1.5f)
    };
    draw_list->AddTriangleFilled(tri[0], tri[1], tri[2], color_playhead);

    // time label next to the playhead triangle
    char time_label[16];
    snprintf(time_label, sizeof(time_label), "%.2f", sequence->GetPlaybackTime());
    ImVec2 label_size = ImGui::CalcTextSize(time_label);
    float label_x = playhead_x + tri_size + 2.0f;
    float label_y = timeline_y + 1.0f;

    draw_list->AddRectFilled(
        ImVec2(label_x - 1.0f, label_y),
        ImVec2(label_x + label_size.x + 3.0f, label_y + label_size.y + 1.0f),
        IM_COL32(30, 30, 34, 200), 2.0f
    );
    draw_list->AddText(ImVec2(label_x, label_y), color_playhead, time_label);
}

void Sequencer::DrawSelectionProperties(Sequence* sequence)
{
    auto& tracks = sequence->GetTracks();

    bool has_selection = false;
    if (m_selected_track >= 0 && m_selected_track < static_cast<int32_t>(tracks.size()))
    {
        SequenceTrack& track = tracks[m_selected_track];

        if (track.type == SequenceTrackType::Transform && m_selected_keyframe >= 0 && m_selected_keyframe < static_cast<int32_t>(track.keyframes.size()))
            has_selection = true;
        if (track.type == SequenceTrackType::CameraCut && m_selected_clip >= 0 && m_selected_clip < static_cast<int32_t>(track.camera_clips.size()))
            has_selection = true;
        if (track.type == SequenceTrackType::Event && m_selected_keyframe >= 0 && m_selected_keyframe < static_cast<int32_t>(track.event_clips.size()))
            has_selection = true;
    }

    if (!has_selection)
        return;

    ImGui::Separator();

    SequenceTrack& track = tracks[m_selected_track];
    const ImVec4& accent = track_type_color(track.type);
    float label_col = 70.0f;

    // header
    ImGui::PushFont(Editor::font_bold);
    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::TextUnformatted(track_type_label(track.type));
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.53f, 1.0f));
    ImGui::Text("- %s (keyframe %d)", track.name.c_str(), (track.type == SequenceTrackType::CameraCut) ? m_selected_clip : m_selected_keyframe);
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
            ImGui::DragFloat("##kf_time", &kf.time, 0.01f, 0.0f, sequence->GetDuration(), "%.3f s");

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
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.2f, 0.2f, 1.0f));
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
            ImGui::DragFloat("##clip_end", &clip.end_time, 0.05f, clip.start_time + 0.01f, sequence->GetDuration(), "%.2f s");
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
                            clip.camera_entity_id = entity->GetObjectId();
                        if (is_current)
                            ImGui::SetItemDefaultFocus();
                        ImGui::PopID();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.2f, 0.2f, 1.0f));
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

void Sequencer::DrawAddTrackPopup(Sequence* sequence)
{
    if (ImGui::BeginPopup("add_track_popup"))
    {
        ImGui::Text("add track");
        ImGui::Separator();

        // camera cut
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(color_camera_accent.x, color_camera_accent.y, color_camera_accent.z, 1.0f));
        if (ImGui::MenuItem("camera cut"))
        {
            sequence->AddTrack(SequenceTrackType::CameraCut, 0, "camera cut");
        }
        ImGui::PopStyleColor();

        // transform (with entity picker submenu)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(color_transform_accent.x, color_transform_accent.y, color_transform_accent.z, 1.0f));
        if (ImGui::BeginMenu("transform"))
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
            for (Entity* entity : World::GetEntities())
            {
                ImGui::PushID(static_cast<int>(entity->GetObjectId()));
                if (ImGui::MenuItem(entity->GetObjectName().c_str()))
                {
                    sequence->AddTrack(SequenceTrackType::Transform, entity->GetObjectId(), entity->GetObjectName());
                }
                ImGui::PopID();
            }
            ImGui::PopStyleColor();
            ImGui::EndMenu();
        }
        ImGui::PopStyleColor();

        // event
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(color_event_accent.x, color_event_accent.y, color_event_accent.z, 1.0f));
        if (ImGui::MenuItem("event"))
        {
            sequence->AddTrack(SequenceTrackType::Event, 0, "events");
        }
        ImGui::PopStyleColor();

        ImGui::EndPopup();
    }
}
