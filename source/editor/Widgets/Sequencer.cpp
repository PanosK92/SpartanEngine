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
#include "Commands/Command.h"
#include "Commands/CommandStack.h"
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
    const float edge_grab_px  = 6.0f;
    const float label_width   = 72.0f;

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

    // list every entity that has a spline follower as menu items, returns the clicked one
    Entity* draw_follower_menu_items()
    {
        Entity* clicked = nullptr;
        bool found      = false;
        for (Entity* entity : World::GetEntities())
        {
            if (entity->GetComponent<SplineFollower>())
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
            ImGui::TextDisabled("no spline followers in the world");
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

    // the first camera entity in the world, used when adding a cut from the toolbar
    Entity* first_camera()
    {
        for (Entity* entity : World::GetEntities())
        {
            if (entity->GetComponent<Camera>())
            {
                return entity;
            }
        }
        return nullptr;
    }

    // the first spline follower entity in the world, used when adding a motion from the toolbar
    Entity* first_follower()
    {
        for (Entity* entity : World::GetEntities())
        {
            if (entity->GetComponent<SplineFollower>())
            {
                return entity;
            }
        }
        return nullptr;
    }

    const float inspector_label_ratio = 0.40f;

    // draws a dimmed label then places the next widget in the value column
    void inspector_label(const char* label, float full_width)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.70f, 0.70f, 1.0f));
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        const float column = full_width * inspector_label_ratio;
        ImGui::SameLine(column);
        ImGui::SetNextItemWidth(full_width - column);
    }

    // bold sub heading inside the inspector
    void inspector_section(const char* title)
    {
        ImGui::PushFont(Editor::font_bold, 0.0f);
        ImGui::TextUnformatted(title);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }

    // combo listing every camera entity, returns true when the choice changed
    bool camera_combo(const char* label, uint64_t& camera_id)
    {
        bool changed         = false;
        const string preview = camera_id != 0 ? get_entity_name(camera_id) : "(none)";
        if (ImGui::BeginCombo(label, preview.c_str()))
        {
            for (Entity* entity : World::GetEntities())
            {
                if (!entity->GetComponent<Camera>())
                {
                    continue;
                }
                const bool selected = entity->GetObjectId() == camera_id;
                if (ImGui::Selectable(entity->GetObjectName().c_str(), selected))
                {
                    camera_id = entity->GetObjectId();
                    changed   = true;
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    // combo listing every spline follower entity, returns true when the choice changed
    bool follower_combo(const char* label, uint64_t& follower_id)
    {
        bool changed         = false;
        const string preview = follower_id != 0 ? get_entity_name(follower_id) : "(none)";
        if (ImGui::BeginCombo(label, preview.c_str()))
        {
            for (Entity* entity : World::GetEntities())
            {
                if (!entity->GetComponent<SplineFollower>())
                {
                    continue;
                }
                const bool selected = entity->GetObjectId() == follower_id;
                if (ImGui::Selectable(entity->GetObjectName().c_str(), selected))
                {
                    follower_id = entity->GetObjectId();
                    changed     = true;
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    // combo listing none plus every active root entity as a look at target
    bool target_combo(const char* label, uint64_t& target_id)
    {
        bool changed         = false;
        const string preview = target_id != 0 ? get_entity_name(target_id) : "(none)";
        if (ImGui::BeginCombo(label, preview.c_str()))
        {
            if (ImGui::Selectable("(none)", target_id == 0))
            {
                target_id = 0;
                changed   = true;
            }
            vector<Entity*> roots;
            World::GetRootEntities(roots);
            for (Entity* entity : roots)
            {
                if (!entity->GetActive())
                {
                    continue;
                }
                const bool selected = entity->GetObjectId() == target_id;
                if (ImGui::Selectable(entity->GetObjectName().c_str(), selected))
                {
                    target_id = entity->GetObjectId();
                    changed   = true;
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    const char* mcp_command_names[] = { "sequencer_get", "sequencer_set", "sequencer_playback", "sequencer_event_add", "sequencer_event_update", "sequencer_event_remove", "sequencer_spline_add", "sequencer_spline_update", "sequencer_spline_remove" };

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

    // accepts an entity id or an entity name, must have a spline follower component
    Entity* resolve_follower(const string& value)
    {
        if (!value.empty() && value.find_first_not_of("0123456789") == string::npos)
        {
            Entity* entity = World::GetEntityById(strtoull(value.c_str(), nullptr, 10));
            if (entity && entity->GetComponent<SplineFollower>())
            {
                return entity;
            }
        }
        for (Entity* entity : World::GetEntities())
        {
            if (entity->GetComponent<SplineFollower>() && entity->GetObjectName() == value)
            {
                return entity;
            }
        }
        return nullptr;
    }
}

namespace
{
    // snapshot based undo step, restores the whole sequencer state on apply or revert
    class SequencerCommand : public Command
    {
    public:
        SequencerCommand(Sequencer* sequencer, Sequencer::State before, Sequencer::State after)
            : m_sequencer(sequencer), m_before(move(before)), m_after(move(after))
        {
        }

        void OnApply() override  { m_sequencer->ApplyState(m_after); }
        void OnRevert() override { m_sequencer->ApplyState(m_before); }

    private:
        Sequencer* m_sequencer;
        Sequencer::State m_before;
        Sequencer::State m_after;
    };
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
        const State before = CaptureState();
        if (const string* duration = get_arg(request, "duration"))
        {
            m_duration = clamp(strtof(duration->c_str(), nullptr), 1.0f, 3600.0f);
            for (CameraEvent& event : m_events)
            {
                event.time = min(event.time, m_duration);
            }
            for (SplineEvent& event : m_spline_events)
            {
                event.start_time = min(event.start_time, m_duration);
                event.end_time   = min(event.end_time, m_duration);
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
        CommitState(before);
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
        const State before = CaptureState();
        m_events.push_back(event);
        sort(m_events.begin(), m_events.end(), [](const CameraEvent& a, const CameraEvent& b) { return a.time < b.time; });
        m_selected = -1;
        CommitState(before);
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
        const State before = CaptureState();
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
        CommitState(before);
        return GetMcpState();
    });

    RegisterMcpCommand("sequencer_event_remove", [this](const McpRequest& request)
    {
        if (const string* all = get_arg(request, "all"))
        {
            if (*all == "true" || *all == "1")
            {
                const State before = CaptureState();
                m_events.clear();
                m_selected = -1;
                CommitState(before);
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
        const State before = CaptureState();
        m_events.erase(m_events.begin() + index);
        m_selected = -1;
        CommitState(before);
        return GetMcpState();
    });

    RegisterMcpCommand("sequencer_spline_add", [this](const McpRequest& request)
    {
        const string* start    = get_arg(request, "start");
        const string* end      = get_arg(request, "end");
        const string* follower = get_arg(request, "follower");
        if (!start || !end || !follower)
        {
            return mcp_error("missing start, end or follower");
        }
        Entity* entity = resolve_follower(*follower);
        if (!entity)
        {
            return mcp_error("no spline follower entity matches '" + *follower + "'");
        }
        SplineEvent event;
        event.start_time = clamp(strtof(start->c_str(), nullptr), 0.0f, m_duration);
        event.end_time   = clamp(strtof(end->c_str(), nullptr), 0.0f, m_duration);
        if (event.end_time < event.start_time + min_event_gap)
        {
            event.end_time = min(event.start_time + min_event_gap, m_duration);
        }
        event.follower_entity_id = entity->GetObjectId();
        const State before = CaptureState();
        m_spline_events.push_back(event);
        m_spline_selected = -1;
        CommitState(before);
        return GetMcpState();
    });

    RegisterMcpCommand("sequencer_spline_update", [this](const McpRequest& request)
    {
        const string* index_arg = get_arg(request, "index");
        if (!index_arg)
        {
            return mcp_error("missing index");
        }
        const int index = atoi(index_arg->c_str());
        if (index < 0 || index >= static_cast<int>(m_spline_events.size()))
        {
            return mcp_error("index is out of range");
        }
        const State before = CaptureState();
        if (const string* follower = get_arg(request, "follower"))
        {
            Entity* entity = resolve_follower(*follower);
            if (!entity)
            {
                return mcp_error("no spline follower entity matches '" + *follower + "'");
            }
            m_spline_events[index].follower_entity_id = entity->GetObjectId();
        }
        if (const string* start = get_arg(request, "start"))
        {
            m_spline_events[index].start_time = clamp(strtof(start->c_str(), nullptr), 0.0f, m_duration);
        }
        if (const string* end = get_arg(request, "end"))
        {
            m_spline_events[index].end_time = clamp(strtof(end->c_str(), nullptr), 0.0f, m_duration);
        }
        if (m_spline_events[index].end_time < m_spline_events[index].start_time + min_event_gap)
        {
            m_spline_events[index].end_time = min(m_spline_events[index].start_time + min_event_gap, m_duration);
        }
        m_spline_selected = -1;
        CommitState(before);
        return GetMcpState();
    });

    RegisterMcpCommand("sequencer_spline_remove", [this](const McpRequest& request)
    {
        if (const string* all = get_arg(request, "all"))
        {
            if (*all == "true" || *all == "1")
            {
                const State before = CaptureState();
                m_spline_events.clear();
                m_spline_selected = -1;
                CommitState(before);
                return GetMcpState();
            }
        }
        const string* index_arg = get_arg(request, "index");
        if (!index_arg)
        {
            return mcp_error("missing index, pass all=true to clear every spline event");
        }
        const int index = atoi(index_arg->c_str());
        if (index < 0 || index >= static_cast<int>(m_spline_events.size()))
        {
            return mcp_error("index is out of range");
        }
        const State before = CaptureState();
        m_spline_events.erase(m_spline_events.begin() + index);
        m_spline_selected = -1;
        CommitState(before);
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
    json += "],\"spline_events\":[";
    for (size_t i = 0; i < m_spline_events.size(); i++)
    {
        if (i > 0)
        {
            json += ",";
        }
        json += "{\"index\":" + to_string(i);
        json += ",\"start_time\":" + to_string(m_spline_events[i].start_time);
        json += ",\"end_time\":" + to_string(m_spline_events[i].end_time);
        json += ",\"follower_entity_id\":\"" + to_string(m_spline_events[i].follower_entity_id) + "\"";
        json += ",\"follower_name\":\"" + json_escape(get_entity_name(m_spline_events[i].follower_entity_id)) + "\"}";
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

        // each spline event drives its follower within its window, clamped at the edges, so scrubbing stays deterministic
        for (const SplineEvent& event : m_spline_events)
        {
            Entity* entity = World::GetEntityById(event.follower_entity_id);
            if (!entity)
            {
                continue;
            }
            if (SplineFollower* follower = entity->GetComponent<SplineFollower>())
            {
                follower->SetTime(clamp(m_time, event.start_time, event.end_time));
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
    ImGui::Dummy(ImVec2(0.0f, 2.0f));

    // the timeline fills the window and reserves a fixed panel on the right for the inspector
    const float spacing       = 6.0f;
    const float inspector_w   = 280.0f;
    const float avail         = ImGui::GetContentRegionAvail().x;
    const bool show_inspector = avail - inspector_w - spacing > 220.0f;
    const float timeline_w    = show_inspector ? avail - inspector_w - spacing : 0.0f;

    // popups live in the same child that opens them so their id scope matches
    ImGui::BeginChild("##seq_timeline", ImVec2(timeline_w, 0.0f), ImGuiChildFlags_None);
    DrawTimeline();
    DrawPopups();
    ImGui::EndChild();

    if (show_inspector)
    {
        ImGui::SameLine(0.0f, spacing);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
        ImGui::BeginChild("##seq_inspector", ImVec2(inspector_w, 0.0f), ImGuiChildFlags_Borders);
        DrawInspector();
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        if (m_selected != -1)
        {
            DeleteSelectedCamera();
        }
        else if (m_spline_selected != -1)
        {
            DeleteSelectedSpline();
        }
    }
}

void Sequencer::DrawToolbar()
{
    const float icon_size = ImGui::GetFontSize();
    bool transport_toggle = false;
    if (m_playing)
    {
        // pause glyph drawn to match the main transport toolbar
        const ImVec2 pad = ImGui::GetStyle().FramePadding;
        ImGui::PushID("##seq_pause");
        transport_toggle = ImGui::InvisibleButton("##pause", ImVec2(icon_size + pad.x * 2.0f, icon_size + pad.y * 2.0f));
        ImGui::PopID();

        const ImVec2 min_pos = ImGui::GetItemRectMin();
        const ImVec2 max_pos = ImGui::GetItemRectMax();
        const float cx       = (min_pos.x + max_pos.x) * 0.5f;
        const float cy       = (min_pos.y + max_pos.y) * 0.5f;
        const float bar_h    = icon_size * 0.56f;
        const float bar_w    = max(2.0f, icon_size * 0.13f);
        const float gap      = icon_size * 0.18f;
        const ImU32 col      = ImGui::GetColorU32(ImGui::Style::color_accent_1);
        ImDrawList* draw     = ImGui::GetWindowDrawList();
        draw->AddRectFilled(ImVec2(cx - gap - bar_w, cy - bar_h * 0.5f), ImVec2(cx - gap, cy + bar_h * 0.5f), col);
        draw->AddRectFilled(ImVec2(cx + gap, cy - bar_h * 0.5f), ImVec2(cx + gap + bar_w, cy + bar_h * 0.5f), col);
        ImGuiSp::tooltip("pause");
    }
    else
    {
        transport_toggle = ImGuiSp::image_button(IconType::Play, math::Vector2(icon_size, icon_size), false, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
        ImGuiSp::tooltip("play");
    }
    if (transport_toggle)
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
    const ImVec4 loop_tint = m_loop ? ImGui::Style::color_accent_1 : ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
    if (ImGuiSp::image_button(IconType::Refresh, math::Vector2(icon_size, icon_size), false, loop_tint))
    {
        const State before = CaptureState();
        m_loop = !m_loop;
        CommitState(before);
    }
    ImGuiSp::tooltip("loop");

    ImGui::SameLine();
    ImGui::BeginDisabled(first_camera() == nullptr);
    if (ImGuiSp::button("+ camera"))
    {
        AddCameraAtPlayhead();
    }
    ImGui::EndDisabled();
    ImGuiSp::tooltip("add a camera cut at the playhead");

    ImGui::SameLine();
    ImGui::BeginDisabled(first_follower() == nullptr);
    if (ImGuiSp::button("+ motion"))
    {
        AddMotionAtPlayhead();
    }
    ImGui::EndDisabled();
    ImGuiSp::tooltip("add a spline motion at the playhead");

    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s / %s", format_time(m_time).c_str(), format_time(m_duration).c_str());

    // duration, right aligned
    const float input_width = 80.0f;
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - input_width);
    ImGui::SetNextItemWidth(input_width);
    ImGui::DragFloat("##duration", &m_duration, 0.1f, 1.0f, 3600.0f, "%.1f s");
    if (ImGui::IsItemActivated())
    {
        m_drag_undo_state = CaptureState();
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        ClampToDuration();
        CommitState(m_drag_undo_state);
    }
    ImGuiSp::tooltip("total sequence length in seconds");
}

void Sequencer::DrawTimeline()
{
    ImDrawList* draw    = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width   = ImGui::GetContentRegionAvail().x;
    if (width - label_width < 50.0f)
    {
        return;
    }

    // reserve a gutter on the left for track labels, the timeline occupies the rest
    const float timeline_x     = origin.x + label_width;
    const float timeline_width = width - label_width;
    const float pixels_per_sec = timeline_width / m_duration;

    const ImU32 col_bg       = ImGui::ColorConvertFloat4ToU32(ImGui::Style::bg_color_1);
    const ImU32 col_tick     = ImGui::ColorConvertFloat4ToU32(ImGui::Style::h_color_2);
    const ImU32 col_text     = ImGui::ColorConvertFloat4ToU32(ImGui::Style::color_info);
    const ImU32 col_playhead = ImGui::ColorConvertFloat4ToU32(ImGui::Style::color_accent_1);

    // ruler, click or drag to scrub
    ImGui::SetCursorScreenPos(ImVec2(timeline_x, origin.y));
    ImGui::InvisibleButton("##sequencer_ruler", ImVec2(timeline_width, ruler_height));
    if (ImGui::IsItemActive())
    {
        m_playing   = false;
        m_scrubbing = true;
        m_time      = clamp((ImGui::GetIO().MousePos.x - timeline_x) / pixels_per_sec, 0.0f, m_duration);
    }
    else
    {
        m_scrubbing = false;
    }

    draw->AddRectFilled(ImVec2(timeline_x, origin.y), ImVec2(timeline_x + timeline_width, origin.y + ruler_height), col_bg, 3.0f);

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
        const float x = timeline_x + t * pixels_per_sec;
        draw->AddLine(ImVec2(x, origin.y + ruler_height * 0.5f), ImVec2(x, origin.y + ruler_height), col_tick);
        char label[16];
        snprintf(label, sizeof(label), tick_step < 1.0f ? "%.1f" : "%.0f", t);
        draw->AddText(ImVec2(x + 3.0f, origin.y + 2.0f), col_text, label);
    }

    // one track per event kind, stacked below the ruler
    const float camera_track_y = origin.y + ruler_height + 2.0f;
    const float spline_track_y = camera_track_y + track_height + 2.0f;
    const float label_offset_y = (track_height - ImGui::GetFontSize()) * 0.5f;
    draw->AddText(ImVec2(origin.x, camera_track_y + label_offset_y), col_text, "camera");
    draw->AddText(ImVec2(origin.x, spline_track_y + label_offset_y), col_text, "spline");
    DrawCameraTrack(timeline_x, camera_track_y, timeline_width, pixels_per_sec);
    DrawSplineTrack(timeline_x, spline_track_y, timeline_width, pixels_per_sec);

    // playhead, spans both tracks
    const float playhead_x = timeline_x + m_time * pixels_per_sec;
    const float tracks_bottom = spline_track_y + track_height;
    draw->AddLine(ImVec2(playhead_x, origin.y), ImVec2(playhead_x, tracks_bottom), col_playhead, 2.0f);
    draw->AddTriangleFilled(ImVec2(playhead_x - 5.0f, origin.y), ImVec2(playhead_x + 5.0f, origin.y), ImVec2(playhead_x, origin.y + 8.0f), col_playhead);
}

void Sequencer::DrawCameraTrack(float origin_x, float track_y, float width, float pixels_per_sec)
{
    ImDrawList* draw         = ImGui::GetWindowDrawList();
    const ImU32 col_bg       = ImGui::ColorConvertFloat4ToU32(ImGui::Style::bg_color_1);
    const ImU32 col_tick     = ImGui::ColorConvertFloat4ToU32(ImGui::Style::h_color_2);
    const ImU32 col_text     = ImGui::ColorConvertFloat4ToU32(ImGui::Style::color_info);
    const ImU32 col_playhead = ImGui::ColorConvertFloat4ToU32(ImGui::Style::color_accent_1);

    const ImVec2 track_min = ImVec2(origin_x, track_y);
    const ImVec2 track_max = ImVec2(origin_x + width, track_y + track_height);
    ImGui::SetCursorScreenPos(track_min);
    ImGui::InvisibleButton("##sequencer_track", ImVec2(width, track_height));
    const float mouse_time  = clamp((ImGui::GetIO().MousePos.x - track_min.x) / pixels_per_sec, 0.0f, m_duration);
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
        m_selected        = hovered_event;
        m_spline_selected = -1;
        if (m_selected != -1)
        {
            m_dragging        = m_selected;
            m_drag_offset     = mouse_time - m_events[m_selected].time;
            m_drag_moved      = false;
            m_drag_undo_state = CaptureState();
        }
    }
    else if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        m_selected = hovered_event;
        if (m_selected != -1)
        {
            m_spline_selected = -1;
            ImGui::OpenPopup("##sequencer_event");
        }
        else
        {
            m_popup_time = mouse_time;
            ImGui::OpenPopup("##sequencer_add");
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
                CommitState(m_drag_undo_state);
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
        const char* hint  = "double click or right click to add a camera cut";
        const ImVec2 size = ImGui::CalcTextSize(hint);
        draw->AddText(ImVec2(track_min.x + (width - size.x) * 0.5f, track_min.y + (track_height - size.y) * 0.5f), col_tick, hint);
    }
}

void Sequencer::DrawSplineTrack(float origin_x, float track_y, float width, float pixels_per_sec)
{
    ImDrawList* draw         = ImGui::GetWindowDrawList();
    const ImU32 col_bg       = ImGui::ColorConvertFloat4ToU32(ImGui::Style::bg_color_1);
    const ImU32 col_tick     = ImGui::ColorConvertFloat4ToU32(ImGui::Style::h_color_2);
    const ImU32 col_text     = ImGui::ColorConvertFloat4ToU32(ImGui::Style::color_info);
    const ImU32 col_playhead = ImGui::ColorConvertFloat4ToU32(ImGui::Style::color_accent_1);

    const ImVec2 track_min = ImVec2(origin_x, track_y);
    const ImVec2 track_max = ImVec2(origin_x + width, track_y + track_height);
    ImGui::SetCursorScreenPos(track_min);
    ImGui::InvisibleButton("##sequencer_spline_track", ImVec2(width, track_height));
    const bool track_hovered = ImGui::IsItemHovered();
    const float mouse_time   = clamp((ImGui::GetIO().MousePos.x - track_min.x) / pixels_per_sec, 0.0f, m_duration);

    // find the hovered event and whether the cursor is over one of its resize edges
    int hovered_event      = -1;
    int hovered_edge       = 0;
    const float edge_time  = edge_grab_px / pixels_per_sec;
    if (track_hovered)
    {
        for (int i = 0; i < static_cast<int>(m_spline_events.size()); i++)
        {
            if (mouse_time >= m_spline_events[i].start_time && mouse_time <= m_spline_events[i].end_time)
            {
                hovered_event = i;
                if (mouse_time - m_spline_events[i].start_time <= edge_time)
                {
                    hovered_edge = -1;
                }
                else if (m_spline_events[i].end_time - mouse_time <= edge_time)
                {
                    hovered_edge = 1;
                }
                break;
            }
        }
    }

    draw->AddRectFilled(track_min, track_max, col_bg, 3.0f);

    // interactions
    if (track_hovered && hovered_event == -1 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        m_spline_popup_time = mouse_time;
        ImGui::OpenPopup("##sequencer_spline_add");
    }
    else if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        m_spline_selected = hovered_event;
        m_selected        = -1;
        if (m_spline_selected != -1)
        {
            m_spline_dragging    = m_spline_selected;
            m_spline_drag_edge   = hovered_edge;
            m_spline_drag_offset = mouse_time - (hovered_edge == 1 ? m_spline_events[m_spline_selected].end_time : m_spline_events[m_spline_selected].start_time);
            m_spline_drag_moved  = false;
            m_drag_undo_state    = CaptureState();
        }
    }
    else if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        m_spline_selected = hovered_event;
        if (m_spline_selected != -1)
        {
            m_selected = -1;
            ImGui::OpenPopup("##sequencer_spline_event");
        }
        else
        {
            m_spline_popup_time = mouse_time;
            ImGui::OpenPopup("##sequencer_spline_add");
        }
    }

    // dragging moves the whole window or resizes one edge, clamped to the timeline
    if (m_spline_dragging != -1)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            SplineEvent& event = m_spline_events[m_spline_dragging];
            if (m_spline_drag_edge == -1)
            {
                const float new_start = clamp(mouse_time - m_spline_drag_offset, 0.0f, event.end_time - min_event_gap);
                if (new_start != event.start_time)
                {
                    event.start_time    = new_start;
                    m_spline_drag_moved = true;
                }
            }
            else if (m_spline_drag_edge == 1)
            {
                const float new_end = clamp(mouse_time - m_spline_drag_offset, event.start_time + min_event_gap, m_duration);
                if (new_end != event.end_time)
                {
                    event.end_time      = new_end;
                    m_spline_drag_moved = true;
                }
            }
            else
            {
                const float span      = event.end_time - event.start_time;
                const float new_start = clamp(mouse_time - m_spline_drag_offset, 0.0f, m_duration - span);
                if (new_start != event.start_time)
                {
                    event.start_time    = new_start;
                    event.end_time      = new_start + span;
                    m_spline_drag_moved = true;
                }
            }
        }
        else
        {
            if (m_spline_drag_moved)
            {
                CommitState(m_drag_undo_state);
            }
            m_spline_dragging = -1;
        }
    }

    // segments, each spans its own start to end window
    for (int i = 0; i < static_cast<int>(m_spline_events.size()); i++)
    {
        const float t0       = m_spline_events[i].start_time;
        const float t1       = m_spline_events[i].end_time;
        const ImVec2 seg_min = ImVec2(track_min.x + t0 * pixels_per_sec + 1.0f, track_min.y + 2.0f);
        const ImVec2 seg_max = ImVec2(track_min.x + t1 * pixels_per_sec - 1.0f, track_max.y - 2.0f);

        ImVec4 fill = ImGui::Style::color_ok;
        fill.w      = 0.7f;
        draw->AddRectFilled(seg_min, seg_max, ImGui::ColorConvertFloat4ToU32(fill), 3.0f);
        if (i == m_spline_selected || i == hovered_event)
        {
            draw->AddRect(seg_min, seg_max, col_playhead, 3.0f, i == m_spline_selected ? 2.0f : 1.0f);
        }

        const string label = get_entity_name(m_spline_events[i].follower_entity_id);
        draw->PushClipRect(seg_min, seg_max, true);
        draw->AddText(ImVec2(seg_min.x + 6.0f, seg_min.y + (seg_max.y - seg_min.y - ImGui::GetFontSize()) * 0.5f), col_text, label.c_str());
        draw->PopClipRect();
    }

    if (m_spline_events.empty())
    {
        const char* hint  = "double click or right click to add a spline motion";
        const ImVec2 size = ImGui::CalcTextSize(hint);
        draw->AddText(ImVec2(track_min.x + (width - size.x) * 0.5f, track_min.y + (track_height - size.y) * 0.5f), col_tick, hint);
    }
}

void Sequencer::DrawPopups()
{
    if (ImGui::BeginPopup("##sequencer_add"))
    {
        ImGui::TextDisabled("cut to camera at %s", format_time(m_popup_time).c_str());
        ImGui::Separator();
        if (Entity* entity = draw_camera_menu_items())
        {
            const State before     = CaptureState();
            CameraEvent event;
            event.time             = m_popup_time;
            event.camera_entity_id = entity->GetObjectId();
            m_events.push_back(event);
            sort(m_events.begin(), m_events.end(), [](const CameraEvent& a, const CameraEvent& b) { return a.time < b.time; });
            m_selected        = GetEventIndexAtTime(m_popup_time);
            m_spline_selected = -1;
            CommitState(before);
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
                    const State before                    = CaptureState();
                    m_events[m_selected].camera_entity_id = entity->GetObjectId();
                    CommitState(before);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("look at"))
            {
                const State before = CaptureState();
                if (draw_target_menu_items(m_events[m_selected].target_entity_id))
                {
                    CommitState(before);
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("duplicate"))
            {
                DuplicateSelectedCamera();
            }
            if (ImGui::MenuItem("delete"))
            {
                DeleteSelectedCamera();
            }
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("##sequencer_spline_add"))
    {
        ImGui::TextDisabled("follow spline from %s", format_time(m_spline_popup_time).c_str());
        ImGui::Separator();
        if (Entity* entity = draw_follower_menu_items())
        {
            const State before       = CaptureState();
            SplineEvent event;
            event.start_time         = m_spline_popup_time;
            event.end_time           = min(m_spline_popup_time + 5.0f, m_duration);
            event.follower_entity_id = entity->GetObjectId();
            m_spline_events.push_back(event);
            m_spline_selected = static_cast<int>(m_spline_events.size()) - 1;
            m_selected        = -1;
            CommitState(before);
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("##sequencer_spline_event"))
    {
        if (m_spline_selected != -1 && m_spline_selected < static_cast<int>(m_spline_events.size()))
        {
            if (ImGui::BeginMenu("follower"))
            {
                if (Entity* entity = draw_follower_menu_items())
                {
                    const State before                                   = CaptureState();
                    m_spline_events[m_spline_selected].follower_entity_id = entity->GetObjectId();
                    CommitState(before);
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("duplicate"))
            {
                DuplicateSelectedSpline();
            }
            if (ImGui::MenuItem("delete"))
            {
                DeleteSelectedSpline();
            }
        }
        ImGui::EndPopup();
    }
}

void Sequencer::DrawInspector()
{
    const float width = ImGui::GetContentRegionAvail().x;

    // a camera shot is selected
    if (m_selected != -1 && m_selected < static_cast<int>(m_events.size()))
    {
        inspector_section("camera shot");
        CameraEvent& event = m_events[m_selected];

        // camera
        {
            uint64_t camera_id = event.camera_entity_id;
            inspector_label("camera", width);
            if (camera_combo("##seq_i_camera", camera_id))
            {
                const State before     = CaptureState();
                event.camera_entity_id = camera_id;
                CommitState(before);
            }
        }

        // look at target, the camera pans to keep it in view while this shot is live
        {
            uint64_t target_id = event.target_entity_id;
            inspector_label("look at", width);
            if (target_combo("##seq_i_target", target_id))
            {
                const State before     = CaptureState();
                event.target_entity_id = target_id;
                CommitState(before);
            }
        }

        // start time, clamped between the neighboring cuts so the order never changes
        {
            const float t_min = m_selected > 0 ? m_events[m_selected - 1].time + min_event_gap : 0.0f;
            const float t_max = max(t_min, m_selected < static_cast<int>(m_events.size()) - 1 ? m_events[m_selected + 1].time - min_event_gap : m_duration);
            float start_time  = event.time;
            inspector_label("start", width);
            if (ImGui::DragFloat("##seq_i_start", &start_time, 0.05f, t_min, t_max, "%.2f s"))
            {
                event.time = clamp(start_time, t_min, t_max);
            }
            if (ImGui::IsItemActivated())
            {
                m_drag_undo_state = CaptureState();
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                CommitState(m_drag_undo_state);
            }
        }

        // duration, the last shot extends the sequence end, others push the next cut
        {
            const bool is_last   = m_selected == static_cast<int>(m_events.size()) - 1;
            const float end_time = is_last ? m_duration : m_events[m_selected + 1].time;
            float duration       = end_time - event.time;
            inspector_label("duration", width);
            if (ImGui::DragFloat("##seq_i_duration", &duration, 0.05f, min_event_gap, 3600.0f, "%.2f s"))
            {
                duration = max(duration, min_event_gap);
                if (is_last)
                {
                    m_duration = event.time + duration;
                    ClampToDuration();
                }
                else
                {
                    const float lower             = event.time + min_event_gap;
                    const float upper             = max(lower, m_selected + 2 < static_cast<int>(m_events.size()) ? m_events[m_selected + 2].time - min_event_gap : m_duration);
                    m_events[m_selected + 1].time = clamp(event.time + duration, lower, upper);
                }
            }
            if (ImGui::IsItemActivated())
            {
                m_drag_undo_state = CaptureState();
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                CommitState(m_drag_undo_state);
            }
        }

        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        const float button_w = (width - 6.0f) * 0.5f;
        if (ImGuiSp::button("duplicate", ImVec2(button_w, 0.0f)))
        {
            DuplicateSelectedCamera();
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.18f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.60f, 0.22f, 0.22f, 1.0f));
        if (ImGuiSp::button("delete", ImVec2(button_w, 0.0f)))
        {
            DeleteSelectedCamera();
        }
        ImGui::PopStyleColor(2);
        return;
    }

    // a spline motion is selected
    if (m_spline_selected != -1 && m_spline_selected < static_cast<int>(m_spline_events.size()))
    {
        inspector_section("motion");
        SplineEvent& event = m_spline_events[m_spline_selected];

        // follower
        {
            uint64_t follower_id = event.follower_entity_id;
            inspector_label("follower", width);
            if (follower_combo("##seq_i_follower", follower_id))
            {
                const State before       = CaptureState();
                event.follower_entity_id = follower_id;
                CommitState(before);
            }
        }

        // start
        {
            float start_time = event.start_time;
            inspector_label("start", width);
            if (ImGui::DragFloat("##seq_i_sstart", &start_time, 0.05f, 0.0f, event.end_time - min_event_gap, "%.2f s"))
            {
                event.start_time = clamp(start_time, 0.0f, event.end_time - min_event_gap);
            }
            if (ImGui::IsItemActivated())
            {
                m_drag_undo_state = CaptureState();
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                CommitState(m_drag_undo_state);
            }
        }

        // end
        {
            const float e_min = event.start_time + min_event_gap;
            const float e_max = max(e_min, m_duration);
            float end_time    = event.end_time;
            inspector_label("end", width);
            if (ImGui::DragFloat("##seq_i_send", &end_time, 0.05f, e_min, e_max, "%.2f s"))
            {
                event.end_time = clamp(end_time, e_min, e_max);
            }
            if (ImGui::IsItemActivated())
            {
                m_drag_undo_state = CaptureState();
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                CommitState(m_drag_undo_state);
            }
        }

        // duration, moves the end while keeping the start fixed
        {
            float duration    = event.end_time - event.start_time;
            const float d_min = event.start_time + min_event_gap;
            const float d_max = max(d_min, m_duration);
            inspector_label("duration", width);
            if (ImGui::DragFloat("##seq_i_sduration", &duration, 0.05f, min_event_gap, m_duration, "%.2f s"))
            {
                event.end_time = clamp(event.start_time + max(duration, min_event_gap), d_min, d_max);
            }
            if (ImGui::IsItemActivated())
            {
                m_drag_undo_state = CaptureState();
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                CommitState(m_drag_undo_state);
            }
        }

        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        const float button_w = (width - 6.0f) * 0.5f;
        if (ImGuiSp::button("duplicate", ImVec2(button_w, 0.0f)))
        {
            DuplicateSelectedSpline();
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.18f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.60f, 0.22f, 0.22f, 1.0f));
        if (ImGuiSp::button("delete", ImVec2(button_w, 0.0f)))
        {
            DeleteSelectedSpline();
        }
        ImGui::PopStyleColor(2);
        return;
    }

    // nothing selected
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("select a shot on the timeline to edit it, or use + camera and + motion to add one");
    ImGui::PopTextWrapPos();
}

void Sequencer::ClampToDuration()
{
    for (CameraEvent& event : m_events)
    {
        event.time = min(event.time, m_duration);
    }
    for (SplineEvent& event : m_spline_events)
    {
        event.start_time = min(event.start_time, m_duration);
        event.end_time   = min(event.end_time, m_duration);
    }
    m_time = min(m_time, m_duration);
}

void Sequencer::AddCameraAtPlayhead()
{
    Entity* camera = first_camera();
    if (!camera)
    {
        return;
    }
    const State before     = CaptureState();
    CameraEvent event;
    event.time             = m_time;
    event.camera_entity_id = camera->GetObjectId();
    m_events.push_back(event);
    sort(m_events.begin(), m_events.end(), [](const CameraEvent& a, const CameraEvent& b) { return a.time < b.time; });
    m_selected        = GetEventIndexAtTime(m_time);
    m_spline_selected = -1;
    CommitState(before);
}

void Sequencer::AddMotionAtPlayhead()
{
    Entity* follower = first_follower();
    if (!follower)
    {
        return;
    }
    const State before       = CaptureState();
    SplineEvent event;
    event.start_time         = m_time;
    event.end_time           = min(m_time + 5.0f, m_duration);
    if (event.end_time < event.start_time + min_event_gap)
    {
        event.end_time = min(event.start_time + min_event_gap, m_duration);
    }
    event.follower_entity_id = follower->GetObjectId();
    m_spline_events.push_back(event);
    m_spline_selected = static_cast<int>(m_spline_events.size()) - 1;
    m_selected        = -1;
    CommitState(before);
}

void Sequencer::DeleteSelectedCamera()
{
    if (m_selected < 0 || m_selected >= static_cast<int>(m_events.size()))
    {
        return;
    }
    const State before = CaptureState();
    m_events.erase(m_events.begin() + m_selected);
    m_selected = -1;
    CommitState(before);
}

void Sequencer::DeleteSelectedSpline()
{
    if (m_spline_selected < 0 || m_spline_selected >= static_cast<int>(m_spline_events.size()))
    {
        return;
    }
    const State before = CaptureState();
    m_spline_events.erase(m_spline_events.begin() + m_spline_selected);
    m_spline_selected = -1;
    CommitState(before);
}

void Sequencer::DuplicateSelectedCamera()
{
    if (m_selected < 0 || m_selected >= static_cast<int>(m_events.size()))
    {
        return;
    }
    const State before    = CaptureState();
    const float next_time = m_selected < static_cast<int>(m_events.size()) - 1 ? m_events[m_selected + 1].time : m_duration;
    CameraEvent copy      = m_events[m_selected];
    copy.time             = clamp((m_events[m_selected].time + next_time) * 0.5f, 0.0f, m_duration);
    m_events.push_back(copy);
    sort(m_events.begin(), m_events.end(), [](const CameraEvent& a, const CameraEvent& b) { return a.time < b.time; });
    m_selected        = GetEventIndexAtTime(copy.time);
    m_spline_selected = -1;
    CommitState(before);
}

void Sequencer::DuplicateSelectedSpline()
{
    if (m_spline_selected < 0 || m_spline_selected >= static_cast<int>(m_spline_events.size()))
    {
        return;
    }
    const State before = CaptureState();
    SplineEvent copy   = m_spline_events[m_spline_selected];
    const float span   = copy.end_time - copy.start_time;
    copy.start_time    = min(copy.end_time, m_duration);
    copy.end_time      = min(copy.start_time + span, m_duration);
    if (copy.end_time < copy.start_time + min_event_gap)
    {
        copy.end_time = min(copy.start_time + min_event_gap, m_duration);
    }
    m_spline_events.push_back(copy);
    m_spline_selected = static_cast<int>(m_spline_events.size()) - 1;
    m_selected        = -1;
    CommitState(before);
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

Sequencer::State Sequencer::CaptureState() const
{
    State state;
    state.duration      = m_duration;
    state.loop          = m_loop;
    state.events        = m_events;
    state.spline_events = m_spline_events;
    return state;
}

void Sequencer::ApplyState(const State& state)
{
    m_duration        = state.duration;
    m_loop            = state.loop;
    m_events          = state.events;
    m_spline_events   = state.spline_events;
    m_selected        = -1;
    m_spline_selected = -1;
    m_dragging        = -1;
    m_spline_dragging = -1;
    m_time            = min(m_time, m_duration);
    Save();
}

void Sequencer::CommitState(const State& before)
{
    // skip when nothing actually changed so undo steps stay meaningful
    if (before.duration == m_duration && before.loop == m_loop && before.events == m_events && before.spline_events == m_spline_events)
    {
        return;
    }
    CommandStack::Push(make_shared<SequencerCommand>(this, before, CaptureState()));
    Save();
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
    for (const SplineEvent& event : m_spline_events)
    {
        pugi::xml_node node = root.append_child("spline_event");
        node.append_attribute("start")              = event.start_time;
        node.append_attribute("end")                = event.end_time;
        node.append_attribute("follower_entity_id") = event.follower_entity_id;
    }
    doc.save_file(file_path.c_str());
}

void Sequencer::Load()
{
    m_events.clear();
    m_spline_events.clear();
    m_time            = 0.0f;
    m_playing         = false;
    m_selected        = -1;
    m_spline_selected = -1;

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

    for (pugi::xml_node node : root.children("spline_event"))
    {
        SplineEvent event;
        event.start_time         = node.attribute("start").as_float(0.0f);
        event.end_time           = node.attribute("end").as_float(0.0f);
        event.follower_entity_id = node.attribute("follower_entity_id").as_ullong(0);
        m_spline_events.push_back(event);
    }
}
