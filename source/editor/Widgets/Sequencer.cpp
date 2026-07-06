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
#include <algorithm>
#include "World/World.h"
#include "World/Entity.h"
#include "World/Components/Camera.h"
#include "World/Components/SplineFollower.h"
#include "MCP/McpCommands.h"
#include "../ImGui/ImGui_Extension.h"
#include "../ImGui/ImGui_Style.h"
#include "FileSystem/FileSystem.h"
SP_WARNINGS_OFF
#include "IO/pugixml.hpp"
SP_WARNINGS_ON
//======================================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
//======================

namespace
{
    const float ruler_height  = 24.0f;
    const float track_height  = 40.0f;
    const float min_event_gap = 0.05f;

    string format_time(float seconds)
    {
        int minutes = static_cast<int>(seconds) / 60;
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%02d:%04.1f", minutes, seconds - static_cast<float>(minutes * 60));
        return buffer;
    }

    string get_entity_name(uint64_t entity_id)
    {
        Entity* entity = World::GetEntityById(entity_id);
        return entity ? entity->GetObjectName() : "missing";
    }

    // list every camera entity in the world as menu items, returns the clicked one
    Entity* draw_camera_menu_items()
    {
        Entity* clicked = nullptr;
        bool found      = false;
        for (Entity* entity : World::GetEntities())
        {
            if (entity->GetComponent<Camera>())
            {
                found = true;
                if (ImGui::MenuItem(entity->GetObjectName().c_str()))
                {
                    clicked = entity;
                }
            }
        }
        if (!found)
        {
            ImGui::TextDisabled("no cameras in the world");
        }
        return clicked;
    }

    // list active root entities as lock targets, returns true when a choice was made
    bool draw_target_menu_items(uint64_t& target_id)
    {
        bool changed = false;
        if (ImGui::MenuItem("(none)", nullptr, target_id == 0))
        {
            target_id = 0;
            changed   = true;
        }
        vector<Entity*> roots;
        World::GetRootEntities(roots);
        for (Entity* entity : roots)
        {
            if (entity->GetActive() && ImGui::MenuItem(entity->GetObjectName().c_str(), nullptr, target_id == entity->GetObjectId()))
            {
                target_id = entity->GetObjectId();
                changed   = true;
            }
        }
        return changed;
    }

    const char* mcp_command_names[] = { "sequencer_get", "sequencer_set", "sequencer_playback", "sequencer_event_add", "sequencer_event_update", "sequencer_event_remove" };

    string json_escape(const string& value)
    {
        string out;
        for (char c : value)
        {
            if (c == '"' || c == '\\')
            {
                out += '\\';
            }
            out += c;
        }
        return out;
    }

    string mcp_error(const string& message)
    {
        return "{\"ok\":false,\"error\":\"" + json_escape(message) + "\"}";
    }

    const string* get_arg(const McpRequest& request, const string& name)
    {
        const auto it = request.arguments.find(name);
        return it != request.arguments.end() ? &it->second : nullptr;
    }

    // accepts an entity id or an entity name, must have a camera component
    Entity* resolve_camera(const string& value)
    {
        if (!value.empty() && value.find_first_not_of("0123456789") == string::npos)
        {
            Entity* entity = World::GetEntityById(strtoull(value.c_str(), nullptr, 10));
            if (entity && entity->GetComponent<Camera>())
            {
                return entity;
            }
        }
        for (Entity* entity : World::GetEntities())
        {
            if (entity->GetComponent<Camera>() && entity->GetObjectName() == value)
            {
                return entity;
            }
        }
        return nullptr;
    }

    // accepts an entity id or an entity name, any entity qualifies as a lock target
    Entity* resolve_entity(const string& value)
    {
        if (!value.empty() && value.find_first_not_of("0123456789") == string::npos)
        {
            if (Entity* entity = World::GetEntityById(strtoull(value.c_str(), nullptr, 10)))
            {
                return entity;
            }
        }
        for (Entity* entity : World::GetEntities())
        {
            if (entity->GetObjectName() == value)
            {
                return entity;
            }
        }
        return nullptr;
    }
}

Sequencer::Sequencer(Editor* editor) : Widget(editor)
{
    m_title   = "Sequencer";
    m_visible = false;

    m_world_loaded_handle = SP_SUBSCRIBE_TO_EVENT(EventType::WorldLoaded, SP_EVENT_HANDLER(Load));

    RegisterMcpCommands();
}

Sequencer::~Sequencer()
{
    for (const char* name : mcp_command_names)
    {
        UnregisterMcpCommand(name);
    }

    SP_UNSUBSCRIBE_FROM_EVENT(EventType::WorldLoaded, m_world_loaded_handle);
}

void Sequencer::RegisterMcpCommands()
{
    RegisterMcpCommand("sequencer_get", [this](const McpRequest&)
    {
        return GetMcpState();
    });

    RegisterMcpCommand("sequencer_set", [this](const McpRequest& request)
    {
        if (const string* duration = get_arg(request, "duration"))
        {
            m_duration = clamp(strtof(duration->c_str(), nullptr), 1.0f, 3600.0f);
            for (CameraEvent& event : m_events)
            {
                event.time = min(event.time, m_duration);
            }
        }
        if (const string* loop = get_arg(request, "loop"))
        {
            m_loop = *loop == "true" || *loop == "1";
        }
        if (const string* time = get_arg(request, "time"))
        {
            m_time = clamp(strtof(time->c_str(), nullptr), 0.0f, m_duration);
        }
        if (const string* visible = get_arg(request, "visible"))
        {
            SetVisible(*visible == "true" || *visible == "1");
        }
        m_time = min(m_time, m_duration);
        Save();
        return GetMcpState();
    });

    RegisterMcpCommand("sequencer_playback", [this](const McpRequest& request)
    {
        const string* action = get_arg(request, "action");
        if (!action)
        {
            return mcp_error("missing action");
        }
        if (*action == "play")
        {
            if (m_time >= m_duration)
            {
                m_time = 0.0f;
            }
            m_playing = true;
        }
        else if (*action == "pause")
        {
            m_playing = false;
        }
        else if (*action == "stop")
        {
            m_playing = false;
            m_time    = 0.0f;
        }
        else
        {
            return mcp_error("action must be play, pause or stop");
        }
        return GetMcpState();
    });

    RegisterMcpCommand("sequencer_event_add", [this](const McpRequest& request)
    {
        const string* time   = get_arg(request, "time");
        const string* camera = get_arg(request, "camera");
        if (!time || !camera)
        {
            return mcp_error("missing time or camera");
        }
        Entity* entity = resolve_camera(*camera);
        if (!entity)
        {
            return mcp_error("no camera entity matches '" + *camera + "'");
        }
        CameraEvent event;
        event.time             = clamp(strtof(time->c_str(), nullptr), 0.0f, m_duration);
        event.camera_entity_id = entity->GetObjectId();
        if (const string* target = get_arg(request, "target"))
        {
            Entity* target_entity = resolve_entity(*target);
            if (!target_entity)
            {
                return mcp_error("no entity matches '" + *target + "'");
            }
            event.target_entity_id = target_entity->GetObjectId();
        }
        m_events.push_back(event);
        sort(m_events.begin(), m_events.end(), [](const CameraEvent& a, const CameraEvent& b) { return a.time < b.time; });
        m_selected = -1;
        Save();
        return GetMcpState();
    });

    RegisterMcpCommand("sequencer_event_update", [this](const McpRequest& request)
    {
        const string* index_arg = get_arg(request, "index");
        if (!index_arg)
        {
            return mcp_error("missing index");
        }
        const int index = atoi(index_arg->c_str());
        if (index < 0 || index >= static_cast<int>(m_events.size()))
        {
            return mcp_error("index is out of range");
        }
        if (const string* camera = get_arg(request, "camera"))
        {
            Entity* entity = resolve_camera(*camera);
            if (!entity)
            {
                return mcp_error("no camera entity matches '" + *camera + "'");
            }
            m_events[index].camera_entity_id = entity->GetObjectId();
        }
        if (const string* target = get_arg(request, "target"))
        {
            if (target->empty() || *target == "none")
            {
                m_events[index].target_entity_id = 0;
            }
            else
            {
                Entity* target_entity = resolve_entity(*target);
                if (!target_entity)
                {
                    return mcp_error("no entity matches '" + *target + "'");
                }
                m_events[index].target_entity_id = target_entity->GetObjectId();
            }
        }
        if (const string* time = get_arg(request, "time"))
        {
            m_events[index].time = clamp(strtof(time->c_str(), nullptr), 0.0f, m_duration);
            sort(m_events.begin(), m_events.end(), [](const CameraEvent& a, const CameraEvent& b) { return a.time < b.time; });
        }
        m_selected = -1;
        Save();
        return GetMcpState();
    });

    RegisterMcpCommand("sequencer_event_remove", [this](const McpRequest& request)
    {
        if (const string* all = get_arg(request, "all"))
        {
            if (*all == "true" || *all == "1")
            {
                m_events.clear();
                m_selected = -1;
                Save();
                return GetMcpState();
            }
        }
        const string* index_arg = get_arg(request, "index");
        if (!index_arg)
        {
            return mcp_error("missing index, pass all=true to clear every event");
        }
        const int index = atoi(index_arg->c_str());
        if (index < 0 || index >= static_cast<int>(m_events.size()))
        {
            return mcp_error("index is out of range");
        }
        m_events.erase(m_events.begin() + index);
        m_selected = -1;
        Save();
        return GetMcpState();
    });
}

string Sequencer::GetMcpState() const
{
    string json = "{\"ok\":true";
    json += ",\"duration\":" + to_string(m_duration);
    json += ",\"time\":" + to_string(m_time);
    json += string(",\"playing\":") + (m_playing ? "true" : "false");
    json += string(",\"loop\":") + (m_loop ? "true" : "false");
    json += ",\"events\":[";
    for (size_t i = 0; i < m_events.size(); i++)
    {
        if (i > 0)
        {
            json += ",";
        }
        json += "{\"index\":" + to_string(i);
        json += ",\"time\":" + to_string(m_events[i].time);
        json += ",\"camera_entity_id\":\"" + to_string(m_events[i].camera_entity_id) + "\"";
        json += ",\"camera_name\":\"" + json_escape(get_entity_name(m_events[i].camera_entity_id)) + "\"";
        json += ",\"target_entity_id\":\"" + to_string(m_events[i].target_entity_id) + "\"";
        json += ",\"target_name\":\"" + (m_events[i].target_entity_id != 0 ? json_escape(get_entity_name(m_events[i].target_entity_id)) : "") + "\"}";
    }
    json += "]}";
    return json;
}

void Sequencer::OnTick()
{
    if (m_playing)
    {
        m_time += static_cast<float>(Timer::GetDeltaTimeSec());
        if (m_time >= m_duration)
        {
            m_time    = m_loop ? fmodf(m_time, m_duration) : m_duration;
            m_playing = m_loop;
        }
    }

    // while playing or scrubbing the event under the playhead drives the render camera
    bool previewing = m_playing || m_scrubbing;
    if (previewing)
    {
        int index = GetEventIndexAtTime(m_time);
        World::SetActiveCamera(index != -1 ? World::GetEntityById(m_events[index].camera_entity_id) : nullptr);

        // the timeline drives every spline follower so the sequence stays deterministic when scrubbing
        for (Entity* entity : World::GetEntities())
        {
            if (SplineFollower* follower = entity->GetComponent<SplineFollower>())
            {
                follower->SetTime(m_time);
            }
        }

        // a locked camera pans to keep its target in view
        if (index != -1 && m_events[index].target_entity_id != 0)
        {
            Entity* camera = World::GetEntityById(m_events[index].camera_entity_id);
            Entity* target = World::GetEntityById(m_events[index].target_entity_id);
            if (camera && target)
            {
                math::Vector3 direction = target->GetPosition() - camera->GetPosition();
                if (direction.LengthSquared() > 0.0f)
                {
                    direction.Normalize();
                    camera->SetRotation(math::Quaternion::FromLookRotation(direction, math::Vector3::Up));
                }
            }
        }
    }
    else if (m_was_previewing)
    {
        World::SetActiveCamera(nullptr);
    }
    m_was_previewing = previewing;
}

void Sequencer::OnTickVisible()
{
    DrawToolbar();
    DrawTimeline();
    DrawPopups();

    if (m_selected != -1 && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        m_events.erase(m_events.begin() + m_selected);
        m_selected = -1;
        Save();
    }
}

void Sequencer::DrawToolbar()
{
    if (ImGuiSp::button(m_playing ? "||" : ">", ImVec2(30.0f, 0.0f)))
    {
        if (!m_playing && m_time >= m_duration)
        {
            m_time = 0.0f;
        }
        m_playing = !m_playing;
    }

    ImGui::SameLine();
    if (ImGuiSp::button("[]", ImVec2(30.0f, 0.0f)))
    {
        m_playing = false;
        m_time    = 0.0f;
    }

    ImGui::SameLine();
    const bool loop_highlighted = m_loop;
    if (loop_highlighted)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::Style::color_accent_2);
    }
    if (ImGuiSp::button("loop"))
    {
        m_loop = !m_loop;
        Save();
    }
    if (loop_highlighted)
    {
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s / %s", format_time(m_time).c_str(), format_time(m_duration).c_str());

    // duration, right aligned
    const float input_width = 80.0f;
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - input_width);
    ImGui::SetNextItemWidth(input_width);
    ImGui::DragFloat("##duration", &m_duration, 0.1f, 1.0f, 3600.0f, "%.1f s");
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        for (CameraEvent& event : m_events)
        {
            event.time = min(event.time, m_duration);
        }
        m_time = min(m_time, m_duration);
        Save();
    }
    ImGuiSp::tooltip("duration in seconds");
}

void Sequencer::DrawTimeline()
{
    ImDrawList* draw    = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width   = ImGui::GetContentRegionAvail().x;
    if (width < 50.0f)
    {
        return;
    }
    const float pixels_per_sec = width / m_duration;

    const ImU32 col_bg       = ImGui::ColorConvertFloat4ToU32(ImGui::Style::bg_color_1);
    const ImU32 col_tick     = ImGui::ColorConvertFloat4ToU32(ImGui::Style::h_color_2);
    const ImU32 col_text     = ImGui::ColorConvertFloat4ToU32(ImGui::Style::color_info);
    const ImU32 col_playhead = ImGui::ColorConvertFloat4ToU32(ImGui::Style::color_accent_1);

    // ruler, click or drag to scrub
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##sequencer_ruler", ImVec2(width, ruler_height));
    if (ImGui::IsItemActive())
    {
        m_playing   = false;
        m_scrubbing = true;
        m_time      = clamp((ImGui::GetIO().MousePos.x - origin.x) / pixels_per_sec, 0.0f, m_duration);
    }
    else
    {
        m_scrubbing = false;
    }

    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + ruler_height), col_bg, 3.0f);

    // pick a tick step so labels never crowd
    float tick_step = 0.1f;
    for (float step : { 0.1f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 30.0f, 60.0f })
    {
        tick_step = step;
        if (step * pixels_per_sec >= 50.0f)
        {
            break;
        }
    }
    for (float t = 0.0f; t <= m_duration + 0.001f; t += tick_step)
    {
        const float x = origin.x + t * pixels_per_sec;
        draw->AddLine(ImVec2(x, origin.y + ruler_height * 0.5f), ImVec2(x, origin.y + ruler_height), col_tick);
        char label[16];
        snprintf(label, sizeof(label), tick_step < 1.0f ? "%.1f" : "%.0f", t);
        draw->AddText(ImVec2(x + 3.0f, origin.y + 2.0f), col_text, label);
    }

    // track
    const ImVec2 track_min = ImVec2(origin.x, origin.y + ruler_height + 2.0f);
    const ImVec2 track_max = ImVec2(origin.x + width, track_min.y + track_height);
    ImGui::SetCursorScreenPos(track_min);
    ImGui::InvisibleButton("##sequencer_track", ImVec2(width, track_height));
    const float mouse_time = clamp((ImGui::GetIO().MousePos.x - track_min.x) / pixels_per_sec, 0.0f, m_duration);
    const int hovered_event = ImGui::IsItemHovered() ? GetEventIndexAtTime(mouse_time) : -1;

    draw->AddRectFilled(track_min, track_max, col_bg, 3.0f);

    // interactions
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        m_popup_time = mouse_time;
        ImGui::OpenPopup("##sequencer_add");
    }
    else if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        m_selected = hovered_event;
        if (m_selected != -1)
        {
            m_dragging    = m_selected;
            m_drag_offset = mouse_time - m_events[m_selected].time;
            m_drag_moved  = false;
        }
    }
    else if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        m_selected = hovered_event;
        if (m_selected != -1)
        {
            ImGui::OpenPopup("##sequencer_event");
        }
    }

    // dragging a segment moves its cut point, clamped between its neighbors
    if (m_dragging != -1)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const float time_min = m_dragging > 0 ? m_events[m_dragging - 1].time + min_event_gap : 0.0f;
            const float time_max = m_dragging < static_cast<int>(m_events.size()) - 1 ? m_events[m_dragging + 1].time - min_event_gap : m_duration;
            const float new_time = clamp(mouse_time - m_drag_offset, time_min, min(time_max, m_duration));
            if (new_time != m_events[m_dragging].time)
            {
                m_events[m_dragging].time = new_time;
                m_drag_moved              = true;
            }
        }
        else
        {
            if (m_drag_moved)
            {
                Save();
            }
            m_dragging = -1;
        }
    }

    // segments, each spans from its event to the next
    for (int i = 0; i < static_cast<int>(m_events.size()); i++)
    {
        const float t0 = m_events[i].time;
        const float t1 = i + 1 < static_cast<int>(m_events.size()) ? m_events[i + 1].time : m_duration;
        const ImVec2 seg_min = ImVec2(track_min.x + t0 * pixels_per_sec + 1.0f, track_min.y + 2.0f);
        const ImVec2 seg_max = ImVec2(track_min.x + t1 * pixels_per_sec - 1.0f, track_max.y - 2.0f);

        ImVec4 fill = ImGui::Style::color_accent_2;
        fill.w      = (i % 2 == 0) ? 0.85f : 0.6f;
        draw->AddRectFilled(seg_min, seg_max, ImGui::ColorConvertFloat4ToU32(fill), 3.0f);
        if (i == m_selected || i == hovered_event)
        {
            draw->AddRect(seg_min, seg_max, col_playhead, 3.0f, i == m_selected ? 2.0f : 1.0f);
        }

        string label = get_entity_name(m_events[i].camera_entity_id);
        if (m_events[i].target_entity_id != 0)
        {
            label += " @ " + get_entity_name(m_events[i].target_entity_id);
        }
        draw->PushClipRect(seg_min, seg_max, true);
        draw->AddText(ImVec2(seg_min.x + 6.0f, seg_min.y + (seg_max.y - seg_min.y - ImGui::GetFontSize()) * 0.5f), col_text, label.c_str());
        draw->PopClipRect();
    }

    if (m_events.empty())
    {
        const char* hint  = "double click to add a camera event";
        const ImVec2 size = ImGui::CalcTextSize(hint);
        draw->AddText(ImVec2(track_min.x + (width - size.x) * 0.5f, track_min.y + (track_height - size.y) * 0.5f), col_tick, hint);
    }

    // playhead
    const float playhead_x = origin.x + m_time * pixels_per_sec;
    draw->AddLine(ImVec2(playhead_x, origin.y), ImVec2(playhead_x, track_max.y), col_playhead, 2.0f);
    draw->AddTriangleFilled(ImVec2(playhead_x - 5.0f, origin.y), ImVec2(playhead_x + 5.0f, origin.y), ImVec2(playhead_x, origin.y + 8.0f), col_playhead);
}

void Sequencer::DrawPopups()
{
    if (ImGui::BeginPopup("##sequencer_add"))
    {
        ImGui::TextDisabled("cut to camera at %s", format_time(m_popup_time).c_str());
        ImGui::Separator();
        if (Entity* entity = draw_camera_menu_items())
        {
            CameraEvent event;
            event.time             = m_popup_time;
            event.camera_entity_id = entity->GetObjectId();
            m_events.push_back(event);
            sort(m_events.begin(), m_events.end(), [](const CameraEvent& a, const CameraEvent& b) { return a.time < b.time; });
            m_selected = GetEventIndexAtTime(m_popup_time);
            Save();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("##sequencer_event"))
    {
        if (m_selected != -1 && m_selected < static_cast<int>(m_events.size()))
        {
            if (ImGui::BeginMenu("camera"))
            {
                if (Entity* entity = draw_camera_menu_items())
                {
                    m_events[m_selected].camera_entity_id = entity->GetObjectId();
                    Save();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("look at"))
            {
                if (draw_target_menu_items(m_events[m_selected].target_entity_id))
                {
                    Save();
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("delete"))
            {
                m_events.erase(m_events.begin() + m_selected);
                m_selected = -1;
                Save();
            }
        }
        ImGui::EndPopup();
    }
}

int Sequencer::GetEventIndexAtTime(float time) const
{
    int index = -1;
    for (int i = 0; i < static_cast<int>(m_events.size()); i++)
    {
        if (m_events[i].time <= time)
        {
            index = i;
        }
    }
    return index;
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

void Sequencer::Save() const
{
    const string file_path = GetFilePath();
    if (file_path.empty())
    {
        return;
    }

    pugi::xml_document doc;
    pugi::xml_node root               = doc.append_child("sequencer");
    root.append_attribute("duration") = m_duration;
    root.append_attribute("loop")     = m_loop;
    for (const CameraEvent& event : m_events)
    {
        pugi::xml_node node = root.append_child("event");
        node.append_attribute("time")             = event.time;
        node.append_attribute("camera_entity_id") = event.camera_entity_id;
        node.append_attribute("target_entity_id") = event.target_entity_id;
    }
    doc.save_file(file_path.c_str());
}

void Sequencer::Load()
{
    m_events.clear();
    m_time     = 0.0f;
    m_playing  = false;
    m_selected = -1;

    const string file_path = GetFilePath();
    if (file_path.empty() || !FileSystem::Exists(file_path))
    {
        return;
    }

    pugi::xml_document doc;
    if (!doc.load_file(file_path.c_str()))
    {
        return;
    }

    pugi::xml_node root = doc.child("sequencer");
    m_duration          = root.attribute("duration").as_float(10.0f);
    m_loop              = root.attribute("loop").as_bool(false);
    for (pugi::xml_node node : root.children("event"))
    {
        CameraEvent event;
        event.time             = node.attribute("time").as_float(0.0f);
        event.camera_entity_id = node.attribute("camera_entity_id").as_ullong(0);
        event.target_entity_id = node.attribute("target_entity_id").as_ullong(0);
        m_events.push_back(event);
    }
    sort(m_events.begin(), m_events.end(), [](const CameraEvent& a, const CameraEvent& b) { return a.time < b.time; });
}
