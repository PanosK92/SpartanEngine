/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===========================
#include "pch.h"
#ifdef SP_GAME
#include "../../car/CarPhysics.h"
#endif
#include "Sequencer.h"
#include "SequencerCommon.h"
#include <algorithm>
#include <thread>
#include <chrono>
#include <filesystem>
#include "world/World.h"
#include "world/Entity.h"
#include "world/components/Camera.h"
#include "world/components/Physics.h"
#include "world/components/Spline.h"
#include "world/components/SplineFollower.h"
#include "car/Car.h"
#include "physics/PhysicsWorld.h"
#include "core/ProgressTracker.h"
#include "rendering/Renderer.h"
#include "commands/Command.h"
#include "commands/CommandStack.h"
#include "../imgui/ImGui_EditorUi.h"
#include "../imgui/ImGui_Extension.h"
#include "../imgui/ImGui_Style.h"
#include "file_system/FileSystem.h"
#include "resource/ResourceCache.h"
SP_WARNINGS_OFF
#include "io/pugixml.hpp"
SP_WARNINGS_ON
//======================================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
using namespace spartan::math;
//======================

namespace sequencer_common
{
    // below this speed the handbrake holds the car, the automatic gearbox reverses on the brake pedal at rest
    const float drive_hold_speed = 1.5f;
    // how many png encodes may queue before a recording waits for them
    const uint32_t max_saves_in_flight = 8;
    const char* shot_camera_name       = "sequencer_camera";

    const char* space_names[] = { "world", "anchor", "heading" };
    const char* ease_names[]  = { "linear", "in_out", "in", "out" };

    string format_time(float seconds)
    {
        const bool negative = seconds < 0.0f;
        seconds             = fabsf(seconds);
        int minutes         = static_cast<int>(seconds) / 60;
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%s%02d:%04.1f", negative ? "-" : "", minutes, seconds - static_cast<float>(minutes * 60));
        return buffer;
    }

    string get_entity_name(uint64_t entity_id)
    {
        Entity* entity = World::GetEntityById(entity_id);
        return entity ? entity->GetObjectName() : "missing";
    }

    float apply_ease(Sequencer::ShotEase ease, float u)
    {
        u = clamp(u, 0.0f, 1.0f);
        switch (ease)
        {
            case Sequencer::ShotEase::InOut: return u * u * u * (u * (u * 6.0f - 15.0f) + 10.0f);
            case Sequencer::ShotEase::In:    return u * u;
            case Sequencer::ShotEase::Out:   return 1.0f - (1.0f - u) * (1.0f - u);
            default:                         return u;
        }
    }

    float heading_yaw(const Vector3& forward)
    {
        return atan2f(forward.x, forward.z);
    }

    // smooth, deterministic handheld drift, three incommensurate sines per axis never visibly repeat
    float sway(float t, float seed)
    {
        return sinf(t * 0.93f + seed) * 0.5f + sinf(t * 1.71f + seed * 2.13f) * 0.3f + sinf(t * 3.37f + seed * 3.71f) * 0.2f;
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

    Car* first_drivable_car()
    {
        for (Car* car : Car::GetAll())
        {
            if (car && car->IsDrivable() && car->GetRootEntity())
            {
                return car;
            }
        }
        return nullptr;
    }

    Entity* first_road_spline()
    {
        for (Entity* entity : World::GetEntities())
        {
            if (Spline* spline = entity->GetComponent<Spline>())
            {
                if (spline->GetControlPointCount() >= 2)
                {
                    return entity;
                }
            }
        }
        return nullptr;
    }

    const float inspector_label_ratio = 0.40f;

    // draws a dimmed label then places the next widget in the value column
    void inspector_label(const char* label, float full_width)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImGui::Style::color_text_muted
        );
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

    // panel title with a subtle underline, labels the timeline and properties panels
    void panel_header(const char* title)
    {
        ImGui::EditorUi::panel_header(
            title,
            nullptr,
            Editor::font_bold
        );
    }

    // combo over entities accepted by a filter, with an optional none entry, returns true when the choice changed
    template<typename Filter>
    bool entity_combo(const char* label, uint64_t& entity_id, bool allow_none, Filter filter)
    {
        bool changed         = false;
        const string preview = entity_id != 0 ? get_entity_name(entity_id) : "(none)";
        if (ImGui::BeginCombo(label, preview.c_str()))
        {
            if (allow_none && ImGui::Selectable("(none)", entity_id == 0))
            {
                entity_id = 0;
                changed   = true;
            }
            for (Entity* entity : World::GetEntities())
            {
                if (!filter(entity))
                {
                    continue;
                }
                const bool selected = entity->GetObjectId() == entity_id;
                ImGui::PushID(entity);
                if (ImGui::Selectable(entity->GetObjectName().c_str(), selected))
                {
                    entity_id = entity->GetObjectId();
                    changed   = true;
                }
                ImGui::PopID();
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    bool camera_combo(const char* label, uint64_t& camera_id)
    {
        return entity_combo(label, camera_id, false, [](Entity* entity) { return entity->GetComponent<Camera>() != nullptr; });
    }

    bool follower_combo(const char* label, uint64_t& follower_id)
    {
        return entity_combo(label, follower_id, false, [](Entity* entity) { return entity->GetComponent<SplineFollower>() != nullptr; });
    }

    // none plus every active root entity and every car, a car is listed once through the entity that keeps its id across loads
    bool target_combo(const char* label, uint64_t& target_id)
    {
        return entity_combo(label, target_id, true, [](Entity* entity)
        {
            return entity->GetActive() && (!entity->GetParent() || Sequencer::FindCar(entity) != nullptr && Sequencer::GetPersistentEntity(entity) == entity);
        });
    }

    bool car_combo(const char* label, uint64_t& car_id)
    {
        return entity_combo(label, car_id, false, [](Entity* entity)
        {
            Car* car = Sequencer::FindCar(entity);
            return car && car->IsDrivable() && Sequencer::GetPersistentEntity(car->GetRootEntity()) == entity;
        });
    }

    bool spline_combo(const char* label, uint64_t& spline_id)
    {
        return entity_combo(label, spline_id, false, [](Entity* entity)
        {
            Spline* spline = entity->GetComponent<Spline>();
            return spline && spline->GetControlPointCount() >= 2;
        });
    }
}

using namespace sequencer_common;

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

bool Sequencer::CameraEvent::operator==(const CameraEvent& other) const
{
    return time == other.time && camera_entity_id == other.camera_entity_id && target_entity_id == other.target_entity_id &&
        rig == other.rig && anchor_entity_id == other.anchor_entity_id && space == other.space &&
        position_start == other.position_start && position_end == other.position_end &&
        look_start == other.look_start && look_end == other.look_end &&
        fov_start == other.fov_start && fov_end == other.fov_end && aperture == other.aperture &&
        roll == other.roll && shake == other.shake && lag == other.lag && ease == other.ease &&
        move == other.move && orbit_radius == other.orbit_radius && orbit_height == other.orbit_height &&
        orbit_angle_start == other.orbit_angle_start && orbit_angle_end == other.orbit_angle_end && orbit_rise == other.orbit_rise &&
        path_entity_id == other.path_entity_id && path_start == other.path_start && path_end == other.path_end && blend_in == other.blend_in;
}

bool Sequencer::DriveEvent::operator==(const DriveEvent& other) const
{
    return start_time == other.start_time && end_time == other.end_time && car_entity_id == other.car_entity_id &&
        spline_entity_id == other.spline_entity_id && start_distance == other.start_distance && lane_offset == other.lane_offset &&
        max_lateral_g == other.max_lateral_g && reverse == other.reverse && speed_keys == other.speed_keys;
}

Sequencer::Sequencer(Editor* editor) : Widget(editor)
{
    m_title         = "Sequencer";
    m_visible       = false;
    m_dock          = WidgetDock::Down;
    m_toolbar_order = 10;
    m_toolbar_icon  = static_cast<int>(IconType::Sequencer);

    m_world_loaded_handle = SP_SUBSCRIBE_TO_EVENT(EventType::WorldLoaded, SP_EVENT_HANDLER(Load));
    m_world_ticked_handle = SP_SUBSCRIBE_TO_EVENT(EventType::WorldTicked, SP_EVENT_HANDLER(OnWorldTicked));
}

Sequencer::~Sequencer()
{
    SP_UNSUBSCRIBE_FROM_EVENT(EventType::WorldLoaded, m_world_loaded_handle);
    SP_UNSUBSCRIBE_FROM_EVENT(EventType::WorldTicked, m_world_ticked_handle);
}

Car* Sequencer::FindCar(Entity* entity)
{
    if (!entity)
    {
        return nullptr;
    }
    for (Car* car : Car::GetAll())
    {
        if (!car)
        {
            continue;
        }
        Entity* root = car->GetRootEntity();
        if (!root)
        {
            continue;
        }
        // the car root, its prefab owner, or anything inside the car
        if (entity == root || entity == root->GetParent() || entity == car->GetBodyEntity())
        {
            return car;
        }
        for (Entity* parent = entity->GetParent(); parent; parent = parent->GetParent())
        {
            if (parent == root)
            {
                return car;
            }
        }
    }
    return nullptr;
}

Entity* Sequencer::GetMovingEntity(Entity* entity)
{
    if (!entity)
    {
        return nullptr;
    }
    // a car prefab's owner stays where it was spawned, the vehicle child is what physics moves
    if (Car* car = FindCar(entity))
    {
        Entity* root = car->GetRootEntity();
        if (entity == root->GetParent())
        {
            return root;
        }
    }
    return entity;
}

Entity* Sequencer::GetOrCreateShotCamera()
{
    for (Entity* entity : World::GetEntities())
    {
        if (entity->GetObjectName() == shot_camera_name && entity->GetComponent<Camera>())
        {
            return entity;
        }
    }

    // copies the lens and exposure of whatever camera is live so the look matches the rest of the game
    Entity* entity = World::CreateEntity();
    entity->SetObjectName(shot_camera_name);
    Camera* component = entity->AddComponent<Camera>();
    if (Camera* source = World::GetCamera())
    {
        pugi::xml_document doc;
        pugi::xml_node node = doc.append_child("camera");
        source->Save(node);
        component->Load(node);
    }
    component->SetFlag(CameraFlags::CanBeControlled, false);
    component->SetFlag(CameraFlags::IsControlled, false);
    component->SetFlag(CameraFlags::PhysicalBodyAnimation, false);
    component->SetFlag(CameraFlags::Flashlight, false);
    return entity;
}

Entity* Sequencer::GetPersistentEntity(Entity* entity)
{
    if (Car* car = FindCar(entity))
    {
        Entity* root = car->GetRootEntity();
        if (entity == root || entity == root->GetParent())
        {
            return root->GetParent() ? root->GetParent() : root;
        }
    }
    return entity;
}

void Sequencer::SortCameraEvents()
{
    stable_sort(m_events.begin(), m_events.end(), [](const CameraEvent& a, const CameraEvent& b) { return a.time < b.time; });
}

void Sequencer::SetTimeline(const TimelineRequest& request)
{
    const State before = CaptureState();
    if (request.duration)
    {
        m_duration = clamp(*request.duration, 1.0f, 3600.0f);
        ClampToDuration();
    }
    if (request.loop)
    {
        m_loop = *request.loop;
    }
    if (request.time)
    {
        m_time = clamp(*request.time, 0.0f, m_duration);
    }
    if (request.visible)
    {
        SetVisible(*request.visible);
    }
    if (request.preview)
    {
        m_preview = *request.preview;
    }
    m_time = min(m_time, m_duration);
    CommitState(before);
}

void Sequencer::SetPlayback(Playback action)
{
    switch (action)
    {
        case Playback::Play:
            if (m_time >= m_duration)
            {
                m_time = 0.0f;
            }
            m_playing            = true;
            m_drive_wait         = 0.0f;
            m_drive_wait_expired = false;
            StageDrives();
            break;
        case Playback::Pause:
            m_playing = false;
            break;
        case Playback::Stop:
            if (m_render_status.active)
            {
                FinishRender();
            }
            m_playing            = false;
            m_time               = 0.0f;
            m_drive_wait         = 0.0f;
            m_drive_wait_expired = false;
            ReleaseDrives();
            break;
    }
}

void Sequencer::AddCameraEvent(const CameraEvent& event_in)
{
    CameraEvent event = event_in;
    event.time        = clamp(event.time, 0.0f, m_duration);

    const State before = CaptureState();
    m_events.push_back(event);
    SortCameraEvents();
    m_selected = -1;
    CommitState(before);
}

bool Sequencer::UpdateCameraEvent(int index, const CameraEvent& event)
{
    if (index < 0 || index >= static_cast<int>(m_events.size()))
    {
        return false;
    }

    const State before = CaptureState();
    m_events[index]      = event;
    m_events[index].time = clamp(event.time, 0.0f, m_duration);
    SortCameraEvents();
    m_selected = -1;
    CommitState(before);
    return true;
}

bool Sequencer::RemoveCameraEvent(int index)
{
    if (index < 0 || index >= static_cast<int>(m_events.size()))
    {
        return false;
    }

    const State before = CaptureState();
    m_events.erase(m_events.begin() + index);
    m_selected = -1;
    CommitState(before);
    return true;
}

void Sequencer::ClearCameraEvents()
{
    const State before = CaptureState();
    m_events.clear();
    m_selected = -1;
    CommitState(before);
}

void Sequencer::AddSplineEvent(float start_time, float end_time, uint64_t follower_entity_id)
{
    SplineEvent event;
    event.start_time = clamp(start_time, 0.0f, m_duration);
    event.end_time   = clamp(end_time, 0.0f, m_duration);
    if (event.end_time < event.start_time + min_event_gap)
    {
        event.end_time = min(event.start_time + min_event_gap, m_duration);
    }
    event.follower_entity_id = follower_entity_id;

    const State before = CaptureState();
    m_spline_events.push_back(event);
    m_spline_selected = -1;
    CommitState(before);
}

bool Sequencer::UpdateSplineEvent(int index, optional<float> start_time, optional<float> end_time, optional<uint64_t> follower_entity_id)
{
    if (index < 0 || index >= static_cast<int>(m_spline_events.size()))
    {
        return false;
    }

    const State before = CaptureState();
    if (follower_entity_id)
    {
        m_spline_events[index].follower_entity_id = *follower_entity_id;
    }
    if (start_time)
    {
        m_spline_events[index].start_time = clamp(*start_time, 0.0f, m_duration);
    }
    if (end_time)
    {
        m_spline_events[index].end_time = clamp(*end_time, 0.0f, m_duration);
    }
    if (m_spline_events[index].end_time < m_spline_events[index].start_time + min_event_gap)
    {
        m_spline_events[index].end_time = min(m_spline_events[index].start_time + min_event_gap, m_duration);
    }
    m_spline_selected = -1;
    CommitState(before);
    return true;
}

bool Sequencer::RemoveSplineEvent(int index)
{
    if (index < 0 || index >= static_cast<int>(m_spline_events.size()))
    {
        return false;
    }

    const State before = CaptureState();
    m_spline_events.erase(m_spline_events.begin() + index);
    m_spline_selected = -1;
    CommitState(before);
    return true;
}

void Sequencer::ClearSplineEvents()
{
    const State before = CaptureState();
    m_spline_events.clear();
    m_spline_selected = -1;
    CommitState(before);
}

void Sequencer::AddDriveEvent(const DriveEvent& event_in)
{
    DriveEvent event = event_in;
    event.start_time = clamp(event.start_time, 0.0f, m_duration);
    event.end_time   = clamp(event.end_time, event.start_time + min_event_gap, m_duration);
    sort(event.speed_keys.begin(), event.speed_keys.end(), [](const SpeedKey& a, const SpeedKey& b) { return a.time < b.time; });

    const State before = CaptureState();
    m_drive_events.push_back(event);
    m_drive_selected = -1;
    CommitState(before);
}

bool Sequencer::UpdateDriveEvent(int index, const DriveEvent& event_in)
{
    if (index < 0 || index >= static_cast<int>(m_drive_events.size()))
    {
        return false;
    }

    DriveEvent event = event_in;
    event.start_time = clamp(event.start_time, 0.0f, m_duration);
    event.end_time   = clamp(event.end_time, event.start_time + min_event_gap, m_duration);
    sort(event.speed_keys.begin(), event.speed_keys.end(), [](const SpeedKey& a, const SpeedKey& b) { return a.time < b.time; });

    const State before    = CaptureState();
    m_drive_events[index] = event;
    CommitState(before);
    return true;
}

bool Sequencer::RemoveDriveEvent(int index)
{
    if (index < 0 || index >= static_cast<int>(m_drive_events.size()))
    {
        return false;
    }

    ReleaseDrives();
    const State before = CaptureState();
    m_drive_events.erase(m_drive_events.begin() + index);
    m_drive_selected = -1;
    CommitState(before);
    return true;
}

void Sequencer::ClearDriveEvents()
{
    ReleaseDrives();
    const State before = CaptureState();
    m_drive_events.clear();
    m_drive_selected = -1;
    CommitState(before);
}

Sequencer::Snapshot Sequencer::GetSnapshot() const
{
    Snapshot snapshot;
    snapshot.events        = m_events;
    snapshot.spline_events = m_spline_events;
    snapshot.drive_events  = m_drive_events;
    snapshot.cinematic     = m_cinematic;
    snapshot.time_scale    = m_applied_time_scale;
    snapshot.render        = m_render_status;
    snapshot.duration      = m_duration;
    snapshot.time          = m_time;
    snapshot.playing       = m_playing;
    snapshot.loop          = m_loop;
    snapshot.preview       = m_preview;
    for (size_t i = 0; i < m_drive_events.size(); i++)
    {
        snapshot.drive_telemetry.push_back(i < m_drive_runtime.size() ? m_drive_runtime[i].telemetry : DriveTelemetry());
    }
    return snapshot;
}

void Sequencer::OnTick()
{
    // evaluation runs from the world ticked event, after physics and before rendering, so cameras mounted on
    // moving cars see the pose that is about to be drawn rather than last frame's
}

void Sequencer::OnWorldTicked()
{
    if (ProgressTracker::IsLoading())
    {
        return;
    }

    const float delta_time = static_cast<float>(Timer::GetDeltaTimeRealSec());
    const bool frozen      = Engine::IsFlagSet(EngineMode::Playing) && Engine::IsFlagSet(EngineMode::Paused);
    const bool waiting     = !frozen && WaitForDrives(delta_time);
    if (m_playing && !m_skip_advance && !frozen && !waiting)
    {
        m_time += delta_time;
        if (m_time >= m_duration)
        {
            if (m_loop && !m_render_status.active)
            {
                m_time = fmodf(m_time, m_duration);
                StageDrives();
            }
            else
            {
                m_time    = m_duration;
                m_playing = false;
            }
        }
    }
    m_skip_advance = false;

    Evaluate(frozen ? 0.0f : delta_time);
    if (!waiting)
    {
        TickRender();
    }
    TickScrubCapture();

    // playback that ran off the end hands the cars back
    if (!m_playing && !m_render_status.active && any_of(m_drive_runtime.begin(), m_drive_runtime.end(), [](const DriveRuntime& r) { return r.staged; }))
    {
        ReleaseDrives();
    }
}

void Sequencer::Evaluate(float delta_time)
{
    const bool previewing = m_playing || m_scrubbing || m_preview || m_render_status.active;
    EvaluateTracks(previewing);
    if (!previewing)
    {
        if (m_was_previewing)
        {
            World::SetActiveCamera(nullptr);
        }
        m_was_previewing = false;
        m_last_event     = -1;
        return;
    }

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

    // the autopilot integrates in simulation time so slow motion slows its steering and speed tracking with the cars
    EvaluateDrives(delta_time * m_applied_time_scale);

    const int index = GetEventIndexAtTime(max(m_time, 0.0f));
    const bool cut  = index != m_last_event;
    if (index != -1)
    {
        EvaluateCamera(index, cut);
    }
    else
    {
        World::SetActiveCamera(nullptr);
    }
    m_last_event     = index;
    m_was_previewing = true;
}

void Sequencer::EvaluateCamera(int index, bool cut)
{
    const CameraEvent& event = m_events[index];
    const bool animated      = IsAnimated(event);
    Entity* camera           = World::GetEntityById(event.camera_entity_id);
    if (!camera && animated)
    {
        // an animated shot owns no pose of its own, so any camera will do when the one it was authored with is gone
        camera = GetOrCreateShotCamera();
    }
    World::SetActiveCamera(camera);
    if (!camera)
    {
        return;
    }

    // a blended shot eases in from wherever the previous shot is, so there is no cut for the renderer's history to drop
    const bool blending = animated && event.blend_in > 0.0f && index > 0 && m_time < event.time + event.blend_in;
    if (cut && (!blending || m_last_event == -1))
    {
        Renderer::NotifyCameraCut();
    }

    // legacy cut, the camera keeps its pose and pans to keep its target in view
    if (!animated)
    {
        if (Entity* target = GetMovingEntity(World::GetEntityById(event.target_entity_id)))
        {
            Vector3 direction = target->GetPosition() - camera->GetPosition();
            if (direction.LengthSquared() > 0.0f)
            {
                direction.Normalize();
                camera->SetRotation(Quaternion::FromLookRotation(direction, Vector3::Up));
            }
        }
        if (Camera* component = camera->GetComponent<Camera>())
        {
            component->RefreshMatrices();
        }
        return;
    }

    ShotPose pose = EvaluateShotPose(event, index, m_time, cut, true, camera);
    if (blending)
    {
        // the previous shot keeps moving through the blend, sampled at the current time past its own end
        const CameraEvent& previous = m_events[index - 1];
        Entity* previous_camera     = World::GetEntityById(previous.camera_entity_id);
        ShotPose from;
        bool from_valid = false;
        if (IsAnimated(previous))
        {
            from       = EvaluateShotPose(previous, index - 1, m_time, false, false, previous_camera ? previous_camera : camera);
            from_valid = true;
        }
        else if (previous_camera)
        {
            from.position = previous_camera->GetPosition();
            from.rotation = previous_camera->GetRotation();
            if (Entity* target = GetMovingEntity(World::GetEntityById(previous.target_entity_id)))
            {
                const Vector3 direction = target->GetPosition() - from.position;
                if (direction.LengthSquared() > 1e-6f)
                {
                    from.rotation = Quaternion::FromLookRotation(direction.Normalized(), Vector3::Up);
                    from.look     = target->GetPosition();
                    from.has_look = true;
                }
            }
            if (Camera* component = previous_camera->GetComponent<Camera>())
            {
                from.fov = component->GetFovHorizontalDeg();
            }
            from_valid = true;
        }
        if (from_valid)
        {
            const float w = apply_ease(ShotEase::InOut, (m_time - event.time) / event.blend_in);
            const Vector3 position = Vector3::Lerp(from.position, pose.position, w);
            const Vector3 look     = Vector3::Lerp(from.look, pose.look, w);
            const Vector3 aim      = look - position;
            if (from.has_look && pose.has_look && aim.LengthSquared() > 1e-6f)
            {
                // aiming at a blended look point keeps the subject framed even when the two shots face opposite ways,
                // where a rotation blend swings through the empty side; shake and roll ride on top as offsets
                const Quaternion offset_from = Quaternion::FromLookRotation((from.look - from.position).Normalized(), Vector3::Up).Inverse() * from.rotation;
                const Quaternion offset_to   = Quaternion::FromLookRotation((pose.look - pose.position).Normalized(), Vector3::Up).Inverse() * pose.rotation;
                pose.rotation = Quaternion::FromLookRotation(aim.Normalized(), Vector3::Up) * Quaternion::Lerp(offset_from, offset_to, w);
            }
            else
            {
                pose.rotation = Quaternion::Lerp(from.rotation, pose.rotation, w);
            }
            pose.position = position;
            if (from.fov > 0.0f && pose.fov > 0.0f)
            {
                pose.fov = from.fov + (pose.fov - from.fov) * w;
            }
        }
    }

    camera->SetPosition(pose.position);
    camera->SetRotation(pose.rotation);
    if (Camera* component = camera->GetComponent<Camera>())
    {
        if (pose.fov > 0.0f)
        {
            component->SetFovHorizontalDeg(pose.fov);
        }
        if (event.aperture > 0.0f)
        {
            component->SetAperture(event.aperture);
        }
        component->RefreshMatrices();
    }
}

Sequencer::ShotPose Sequencer::EvaluateShotPose(const CameraEvent& event, int index, float time, bool cut, bool stateful, Entity* camera)
{
    const float next_time = index + 1 < static_cast<int>(m_events.size()) ? m_events[index + 1].time : m_duration;
    const float span      = max(next_time - event.time, 0.001f);
    const float u         = apply_ease(event.ease, (time - event.time) / span);
    Entity* target        = GetMovingEntity(World::GetEntityById(event.target_entity_id));

    // the frame positions are authored in
    Vector3 origin      = Vector3::Zero;
    Quaternion frame    = Quaternion::Identity;
    Entity* anchor      = GetMovingEntity(World::GetEntityById(event.anchor_entity_id));
    if (anchor && event.space != ShotSpace::World)
    {
        origin = anchor->GetPosition();
        if (event.space == ShotSpace::Anchor)
        {
            frame = anchor->GetRotation();
        }
        else
        {
            // a camera car follows the heading with some slack, swinging out on corner entry
            float yaw = heading_yaw(anchor->GetForward());
            if (stateful)
            {
                const float delta_time = static_cast<float>(Timer::GetDeltaTimeRealSec());
                if (event.lag > 0.0f && !cut)
                {
                    const float k = 1.0f - expf(-delta_time / event.lag);
                    m_lag_yaw    += remainderf(yaw - m_lag_yaw, 2.0f * pi) * k;
                }
                else
                {
                    m_lag_yaw = yaw;
                }
                yaw = m_lag_yaw;
            }
            frame = Quaternion::FromAxisAngle(Vector3::Up, yaw);
        }
    }

    const Vector3 offset_local = Vector3::Lerp(event.position_start, event.position_end, u);
    const Vector3 look_local   = Vector3::Lerp(event.look_start, event.look_end, u);
    Vector3 position           = origin + frame * offset_local;
    Vector3 look               = target ? target->GetPosition() + target->GetRotation() * look_local : origin + frame * look_local;
    float fov                  = 0.0f;
    if (event.fov_start > 0.0f)
    {
        const float fov_end = event.fov_end > 0.0f ? event.fov_end : event.fov_start;
        fov                 = event.fov_start + (fov_end - event.fov_start) * u;
    }

    if (event.move == ShotMove::Orbit)
    {
        // circles a center that follows the target's heading, so a frozen car can be swept around like bullet time
        float base_yaw = 0.0f;
        Vector3 center = Vector3::Zero;
        if (target)
        {
            base_yaw = heading_yaw(target->GetForward());
            center   = target->GetPosition() + Quaternion::FromAxisAngle(Vector3::Up, base_yaw) * look_local;
        }
        else
        {
            base_yaw = anchor && event.space != ShotSpace::World ? heading_yaw(frame * Vector3::Forward) : 0.0f;
            center   = origin + frame * look_local;
        }
        const float angle = base_yaw - (event.orbit_angle_start + (event.orbit_angle_end - event.orbit_angle_start) * u) * deg_to_rad;
        position          = center + Vector3(sinf(angle), 0.0f, cosf(angle)) * event.orbit_radius + Vector3::Up * (event.orbit_height + event.orbit_rise * u);
        look              = center;
    }
    else if (event.move == ShotMove::DollyZoom)
    {
        // the fov follows the subject distance so the subject keeps its framing while the background stretches
        const Vector3 position_first = origin + frame * event.position_start;
        const Vector3 look_first     = target ? target->GetPosition() + target->GetRotation() * event.look_start : origin + frame * event.look_start;
        const float distance_first   = Vector3::Distance(position_first, look_first);
        const float distance         = Vector3::Distance(position, look);
        const float fov_first        = (event.fov_start > 0.0f ? event.fov_start : 60.0f) * deg_to_rad;
        if (distance_first > 0.01f && distance > 0.01f)
        {
            fov = clamp(2.0f * atanf(distance_first * tanf(fov_first * 0.5f) / distance) * rad_to_deg, 5.0f, 150.0f);
        }
    }
    else if (event.move == ShotMove::Path)
    {
        Entity* path_entity = World::GetEntityById(event.path_entity_id);
        Spline* spline      = path_entity ? path_entity->GetComponent<Spline>() : nullptr;
        if (spline && spline->GetControlPointCount() >= 2)
        {
            const float t   = clamp(event.path_start + (event.path_end - event.path_start) * u, 0.0f, 1.0f);
            Vector3 tangent = spline->GetTangent(t);
            if (event.path_end < event.path_start)
            {
                tangent = tangent * -1.0f;
            }
            tangent.y = 0.0f;
            tangent   = tangent.LengthSquared() > 1e-8f ? tangent.Normalized() : Vector3::Forward;
            const Quaternion path_frame = Quaternion::FromLookRotation(tangent, Vector3::Up);
            position = spline->GetPoint(t) + path_frame * offset_local;
            if (!target)
            {
                look = look_local == Vector3::Zero ? position + tangent * 10.0f : position + path_frame * look_local;
            }
        }
    }

    Vector3 direction = look - position;
    Quaternion rotation = camera->GetRotation();
    if (direction.LengthSquared() > 1e-6f)
    {
        rotation = Quaternion::FromLookRotation(direction.Normalized(), Vector3::Up);
    }

    // handheld sway plus a fine vibration when bolted onto a moving car
    float yaw_offset   = 0.0f;
    float pitch_offset = 0.0f;
    float roll_offset  = event.roll * deg_to_rad;
    if (event.shake > 0.0f)
    {
        const float t = time;
        yaw_offset   += sway(t, 1.3f) * event.shake * 1.4f * deg_to_rad;
        pitch_offset += sway(t * 1.13f, 4.1f) * event.shake * 0.9f * deg_to_rad;
        roll_offset  += sway(t * 0.71f, 7.7f) * event.shake * 0.7f * deg_to_rad;
        if (event.space == ShotSpace::Anchor)
        {
            pitch_offset += sinf(t * 41.0f) * sinf(t * 2.3f) * event.shake * 0.12f * deg_to_rad;
            yaw_offset   += sinf(t * 53.0f + 1.7f) * event.shake * 0.06f * deg_to_rad;
        }
    }
    if (yaw_offset != 0.0f || pitch_offset != 0.0f || roll_offset != 0.0f)
    {
        rotation = rotation * Quaternion::FromYawPitchRoll(yaw_offset, pitch_offset, roll_offset);
    }

    ShotPose pose;
    pose.position = position;
    pose.rotation = rotation;
    pose.fov      = fov;
    pose.look     = look;
    pose.has_look = direction.LengthSquared() > 1e-6f;
    return pose;
}

bool Sequencer::BuildDrivePath(const DriveEvent& event, DriveRuntime& runtime)
{
    runtime.points.clear();
    runtime.distances.clear();
    runtime.curvatures.clear();

    Entity* entity = World::GetEntityById(event.spline_entity_id);
    Spline* spline = entity ? entity->GetComponent<Spline>() : nullptr;
    if (!spline || spline->GetControlPointCount() < 2)
    {
        return false;
    }

    const float lane = event.reverse ? -event.lane_offset : event.lane_offset;
    auto offset_to_lane = [lane](Vector3 point, Vector3 tangent)
    {
        tangent.y = 0.0f;
        if (tangent.LengthSquared() > 1e-8f)
        {
            tangent.Normalize();
            point += Vector3::Up.Cross(tangent) * lane;
        }
        return point;
    };

    // the generated deck is what the wheels touch, control points can sit meters off a road that conforms to terrain
    vector<Vector3> dense;
    const vector<SplineFrame>& frames = spline->GetRoadFrames();
    if (frames.size() >= 2)
    {
        // frames are in the spline entity's space
        const Matrix world_matrix = entity->GetMatrix();
        dense.reserve(frames.size());
        for (const SplineFrame& frame : frames)
        {
            const Vector3 position = world_matrix * frame.position;
            const Vector3 tangent  = world_matrix * (frame.position + frame.tangent) - position;
            dense.push_back(offset_to_lane(position, tangent));
        }
    }
    else
    {
        // dense parametric samples, resampled to even arc length below so lookahead is in meters
        const float length   = spline->GetLength(20);
        const uint32_t count = clamp(static_cast<uint32_t>(length / 0.5f), 16u, 200000u);
        dense.resize(count + 1);
        for (uint32_t i = 0; i <= count; i++)
        {
            const float t = static_cast<float>(i) / static_cast<float>(count);
            dense[i]      = offset_to_lane(spline->GetPoint(t), spline->GetTangent(t));
        }
    }
    if (event.reverse)
    {
        reverse(dense.begin(), dense.end());
    }

    const float spacing = 1.0f;
    runtime.points.push_back(dense[0]);
    runtime.distances.push_back(0.0f);
    float carried = 0.0f;
    float total   = 0.0f;
    for (size_t i = 1; i < dense.size(); i++)
    {
        Vector3 a           = dense[i - 1];
        const Vector3 b     = dense[i];
        float segment       = Vector3::Distance(a, b);
        while (carried + segment >= spacing && segment > 0.0f)
        {
            const float step = spacing - carried;
            a                = Vector3::Lerp(a, b, step / segment);
            segment         -= step;
            carried          = 0.0f;
            total           += spacing;
            runtime.points.push_back(a);
            runtime.distances.push_back(total);
        }
        carried += segment;
    }
    if (runtime.points.size() < 8)
    {
        return false;
    }

    // curvature from the heading change across +-10 m, wide enough that a kink in the centerline does not read as a hairpin
    const size_t n    = runtime.points.size();
    const size_t half = 10;
    runtime.curvatures.assign(n, 0.0f);
    for (size_t i = half; i + half < n; i++)
    {
        Vector3 d0 = runtime.points[i] - runtime.points[i - half];
        Vector3 d1 = runtime.points[i + half] - runtime.points[i];
        d0.y = d1.y = 0.0f;
        const float a0 = atan2f(d0.x, d0.z);
        const float a1 = atan2f(d1.x, d1.z);
        runtime.curvatures[i] = fabsf(remainderf(a1 - a0, 2.0f * pi)) / (static_cast<float>(half) * spacing);
    }

    runtime.telemetry.path_length = runtime.distances.back();
    return true;
}

void Sequencer::StageDrives()
{
    m_drive_runtime.resize(m_drive_events.size());
    if (!Engine::IsFlagSet(EngineMode::Playing))
    {
        return;
    }

    for (size_t i = 0; i < m_drive_events.size(); i++)
    {
        const DriveEvent& event = m_drive_events[i];
        DriveRuntime& runtime   = m_drive_runtime[i];
        runtime                 = DriveRuntime();

        Car* car = FindCar(World::GetEntityById(event.car_entity_id));
        if (!car || !car->IsDrivable() || !BuildDrivePath(event, runtime))
        {
            continue;
        }

        // the car starts at rest at the staging distance, facing down the road
        const auto& distances = runtime.distances;
        size_t start          = static_cast<size_t>(lower_bound(distances.begin(), distances.end(), max(event.start_distance, 0.0f)) - distances.begin());
        start                 = min(start, runtime.points.size() - 3);
        Vector3 forward       = runtime.points[start + 2] - runtime.points[start];
        forward.y             = 0.0f;
        forward.Normalize();

        if (!car->IsOccupied())
        {
            car->SetExternallyControlled(false);
            car->Enter();
        }
        car->SetExternallyControlled(true);
        car->SetCinematic(true);

        Vector3 ground = runtime.points[start];
        Vector3 hit;
        if (PhysicsWorld::RaycastStatic(ground + Vector3(0.0f, 20.0f, 0.0f), Vector3::Down, 60.0f, hit))
        {
            ground = hit;
        }
        car->PlaceAt(ground, Quaternion::FromLookRotation(forward, Vector3::Up));
        car->SetThrottle(0.0f);
        car->SetBrake(0.0f);
        car->SetSteering(0.0f);
        car->SetHandbrake(1.0f);

        runtime.nearest          = start;
        runtime.staged           = true;
        runtime.telemetry.staged = true;
    }
}

// playback started in edit mode, or in the frames before the cars exist at play entry, leaves drive events
// unstaged and the cars parked, so restage every tick and hold the timeline until they are placed
bool Sequencer::WaitForDrives(float delta_time)
{
    if (!Engine::IsFlagSet(EngineMode::Playing))
    {
        m_drive_wait         = 0.0f;
        m_drive_wait_expired = false;
        return false;
    }
    if (!m_playing || m_drive_events.empty() || m_drive_wait_expired)
    {
        return false;
    }

    bool pending    = false;
    bool any_staged = false;
    for (size_t i = 0; i < m_drive_events.size(); i++)
    {
        const bool staged = i < m_drive_runtime.size() && m_drive_runtime[i].staged;
        any_staged       |= staged;
        if (!staged && World::GetEntityById(m_drive_events[i].car_entity_id))
        {
            pending = true;
        }
    }
    if (!pending)
    {
        m_drive_wait = 0.0f;
        return false;
    }

    // nothing staged yet means playback ran without the cars, start the shot over once they are placed
    if (m_drive_wait == 0.0f && !any_staged)
    {
        m_time = 0.0f;
    }

    StageDrives();
    m_drive_wait += delta_time;
    if (m_drive_wait > 10.0f)
    {
        m_drive_wait_expired = true;
        SP_LOG_WARNING("sequencer: drive cars could not be staged after 10 s (missing vehicle physics or drive path), playing without them");
        return false;
    }
    return true;
}

void Sequencer::ReleaseDrives()
{
    for (size_t i = 0; i < m_drive_runtime.size(); i++)
    {
        DriveRuntime& runtime = m_drive_runtime[i];
        if (!runtime.staged)
        {
            continue;
        }
        runtime.staged           = false;
        runtime.telemetry.staged = false;
        runtime.telemetry.active = false;

        if (i >= m_drive_events.size())
        {
            continue;
        }
        if (Car* car = FindCar(World::GetEntityById(m_drive_events[i].car_entity_id)))
        {
            car->SetThrottle(0.0f);
            car->SetSteering(0.0f);
            car->SetCinematic(false);
            car->SetExternallyControlled(false);
        }
    }
}

void Sequencer::EvaluateDrives(float delta_time)
{
    if (!Engine::IsFlagSet(EngineMode::Playing) || Engine::IsFlagSet(EngineMode::Paused))
    {
        return;
    }

    for (size_t i = 0; i < m_drive_events.size() && i < m_drive_runtime.size(); i++)
    {
        const DriveEvent& event = m_drive_events[i];
        DriveRuntime& runtime   = m_drive_runtime[i];
        if (!runtime.staged)
        {
            continue;
        }

        Car* car        = FindCar(World::GetEntityById(event.car_entity_id));
        Entity* vehicle = car ? car->GetRootEntity() : nullptr;
        Physics* physics = vehicle ? vehicle->GetComponent<Physics>() : nullptr;
        float wheelbase = 2.6f, max_steer = 0.6f, linearity = 1.0f;
        if (!physics || !car->GetSteeringGeometry(wheelbase, max_steer, linearity))
        {
            continue;
        }

        const vector<Vector3>& points = runtime.points;
        const vector<float>& distances = runtime.distances;
        const size_t n                 = points.size();

        const Vector3 position = vehicle->GetPosition();
        Vector3 forward        = vehicle->GetForward();
        const Vector3 velocity = physics->GetLinearVelocity();
        const float speed      = velocity.Dot(forward);

        // project the car onto the path near where it was last frame
        size_t best_index = runtime.nearest;
        float best_t      = 0.0f;
        float best_dist   = numeric_limits<float>::max();
        const size_t lo   = runtime.nearest > 30 ? runtime.nearest - 30 : 0;
        const size_t hi   = min(runtime.nearest + 300, n - 1);
        for (size_t j = lo; j < hi; j++)
        {
            Vector3 a = points[j];
            Vector3 b = points[j + 1];
            Vector3 p = position;
            a.y = b.y = p.y = 0.0f;
            const Vector3 ab     = b - a;
            const float len_sq   = max(ab.LengthSquared(), 1e-6f);
            const float t        = clamp((p - a).Dot(ab) / len_sq, 0.0f, 1.0f);
            const float dist_sq  = (a + ab * t - p).LengthSquared();
            if (dist_sq < best_dist)
            {
                best_dist  = dist_sq;
                best_index = j;
                best_t     = t;
            }
        }
        runtime.nearest = best_index;
        const float s   = distances[best_index] + (distances[min(best_index + 1, n - 1)] - distances[best_index]) * best_t;

        Vector3 path_dir = points[min(best_index + 1, n - 1)] - points[best_index];
        path_dir.y       = 0.0f;
        path_dir.Normalize();
        Vector3 offset   = position - points[best_index];
        offset.y         = 0.0f;
        const float lateral = Vector3::Up.Cross(path_dir).Dot(offset);

        auto sample = [&](float distance) -> Vector3
        {
            distance = clamp(distance, 0.0f, distances.back());
            const size_t k = min(static_cast<size_t>(lower_bound(distances.begin(), distances.end(), distance) - distances.begin()), n - 1);
            if (k == 0)
            {
                return points[0];
            }
            const float span = max(distances[k] - distances[k - 1], 1e-4f);
            return Vector3::Lerp(points[k - 1], points[k], (distance - distances[k - 1]) / span);
        };

        float throttle  = 0.0f;
        float brake     = 0.0f;
        float handbrake = 0.0f;
        float target_ms = 0.0f;

        if (m_time < event.start_time)
        {
            handbrake       = 1.0f;
            runtime.steering = 0.0f;
        }
        else
        {
            // pure pursuit, the bicycle model gives the road wheel angle that arcs onto the lookahead point
            // and the simulation's own steering curve maps that angle back to an input
            const float lookahead = clamp(5.0f + 0.45f * fabsf(speed), 7.0f, 34.0f);
            const Vector3 target  = sample(s + lookahead);
            const Vector3 local   = vehicle->GetRotation().Inverse() * (target - position);
            const float alpha     = atan2f(local.x, local.z);
            const float tan_steer = 2.0f * wheelbase * sinf(alpha) / lookahead;
            const float curved    = tan_steer / max(tanf(max_steer), 0.05f);
            const float input     = copysignf(powf(min(fabsf(curved), 1.0f), 1.0f / max(linearity, 0.1f)), curved);
            runtime.steering     += (input - runtime.steering) * min(1.0f, delta_time * 14.0f);

            const bool in_window = m_time <= event.end_time;
            if (in_window)
            {
                // keyed speed, linear between keys and held past the ends
                float target_kmh = 0.0f;
                const vector<SpeedKey>& keys = event.speed_keys;
                if (!keys.empty())
                {
                    target_kmh = keys.front().speed_kmh;
                    for (size_t k = 0; k < keys.size(); k++)
                    {
                        if (m_time >= keys[k].time)
                        {
                            target_kmh = keys[k].speed_kmh;
                            if (k + 1 < keys.size())
                            {
                                const float span = max(keys[k + 1].time - keys[k].time, 1e-3f);
                                const float f    = clamp((m_time - keys[k].time) / span, 0.0f, 1.0f);
                                target_kmh       = keys[k].speed_kmh + (keys[k + 1].speed_kmh - keys[k].speed_kmh) * f;
                            }
                        }
                    }
                }
                target_ms = target_kmh / 3.6f;

                // corners ahead cap the speed, with enough room to brake down to each one
                const float decel      = 5.5f;
                const float lateral_a  = max(event.max_lateral_g, 0.1f) * 9.81f;
                const float horizon    = speed * speed / (2.0f * decel) + 25.0f;
                for (size_t k = best_index; k < n && distances[k] - s < horizon; k++)
                {
                    const float kappa = runtime.curvatures[k];
                    if (kappa > 1e-4f)
                    {
                        const float corner  = sqrtf(lateral_a / kappa);
                        const float allowed = sqrtf(corner * corner + 2.0f * decel * max(distances[k] - s, 0.0f));
                        target_ms           = min(target_ms, allowed);
                    }
                }
                // the road ends, stop before it does
                target_ms = min(target_ms, sqrtf(2.0f * decel * max(distances.back() - s - 10.0f, 0.0f)));

                const float error = target_ms - speed;
                if (fabsf(error) < 4.0f)
                {
                    runtime.integral = clamp(runtime.integral + error * delta_time, -6.0f, 6.0f);
                }
                const float command = 0.32f * error + 0.07f * runtime.integral;
                throttle            = clamp(command, 0.0f, 1.0f);

                // the tires share one grip budget, power only gets what the corner under the car leaves over
                const float lateral_use = min(speed * speed * runtime.curvatures[best_index] / lateral_a, 1.0f);
                throttle *= max(sqrtf(1.0f - lateral_use), 0.2f);

                // back off while the driven wheels spin or the rear steps out, a wet road has little to give
                float spin  = 0.0f;
                float slide = 0.0f;
                for (uint32_t w = 0; w < static_cast<uint32_t>(WheelIndex::Count); w++)
                {
                    const WheelIndex wheel = static_cast<WheelIndex>(w);
                    spin = max(spin, CarPhysics::Get(*physics).GetWheelSlipRatio(wheel));
                    if (wheel == WheelIndex::RearLeft || wheel == WheelIndex::RearRight)
                    {
                        slide = max(slide, fabsf(CarPhysics::Get(*physics).GetWheelSlipAngle(wheel)));
                    }
                }
                const float excess = max(spin - 0.12f, 0.0f) * 4.0f + max(slide - 0.1f, 0.0f) * 5.0f;
                throttle *= clamp(1.0f - excess, 0.1f, 1.0f);

                // squeeze the pedal in, lift it at once
                const float rate  = throttle > runtime.throttle ? 2.5f : 20.0f;
                runtime.throttle += (throttle - runtime.throttle) * min(1.0f, delta_time * rate);
                throttle          = runtime.throttle;
                if (command < -0.12f && speed > drive_hold_speed)
                {
                    brake = clamp(-command * 0.7f, 0.0f, 1.0f);
                }
                if (target_ms < 0.5f && speed < drive_hold_speed)
                {
                    throttle  = 0.0f;
                    handbrake = 1.0f;
                }
            }
            else
            {
                brake     = speed > drive_hold_speed ? 0.5f : 0.0f;
                handbrake = speed > drive_hold_speed ? 0.0f : 1.0f;
            }
        }

        car->SetThrottle(throttle);
        car->SetBrake(brake);
        car->SetSteering(clamp(runtime.steering, -1.0f, 1.0f));
        car->SetHandbrake(handbrake);

        DriveTelemetry& telemetry = runtime.telemetry;
        telemetry.active          = m_time >= event.start_time && m_time <= event.end_time;
        telemetry.distance        = s;
        telemetry.speed_kmh       = speed * 3.6f;
        telemetry.target_kmh      = target_ms * 3.6f;
        telemetry.lateral_error   = lateral;
        telemetry.throttle        = throttle;
        telemetry.brake           = brake;
        telemetry.steering        = runtime.steering;
    }
}

bool Sequencer::StartRender(const RenderRequest& request, string& error)
{
    if (m_render_status.active)
    {
        error = "a render is already running";
        return false;
    }
    if (!m_drive_events.empty() && (!Engine::IsFlagSet(EngineMode::Playing) || Engine::IsFlagSet(EngineMode::Paused)))
    {
        error = "drive events need physics, enter play mode (unpaused) first";
        return false;
    }
    if (m_events.empty())
    {
        error = "there are no camera events to render";
        return false;
    }

    m_render_request         = request;
    m_render_request.fps     = clamp(request.fps, 1.0f, 120.0f);
    m_render_request.preroll = clamp(request.preroll, 0.0f, 30.0f);
    m_render_request.stride  = max(request.stride, 1u);
    m_render_request.start   = clamp(request.start, 0.0f, m_duration);
    const float end          = request.end < 0.0f ? m_duration : clamp(request.end, m_render_request.start, m_duration);
    m_render_request.end     = end;
    const bool capture       = !m_render_request.capture_times.empty();
    if (capture)
    {
        // stills replay the take from the start and only write at the requested moments
        for (float& time : m_render_request.capture_times)
        {
            time = clamp(time, 0.0f, m_duration);
        }
        sort(m_render_request.capture_times.begin(), m_render_request.capture_times.end());
        m_render_request.start  = m_render_request.capture_times.front();
        m_render_request.end    = m_render_request.capture_times.back();
        m_render_request.stride = 1;
    }
    if (m_render_request.directory.empty())
    {
        m_render_request.directory = "sequencer_renders/" + FileSystem::GetFileNameWithoutExtensionFromFilePath(World::GetFilePath());
    }

    // a previous take in the same folder would leave frames past the new end
    if (!FileSystem::Exists(m_render_request.directory))
    {
        FileSystem::CreateDirectory_(m_render_request.directory);
    }
    const char* stale_prefix = capture ? "capture_" : "frame_";
    for (const string& file : FileSystem::GetFilesInDirectory(m_render_request.directory))
    {
        const string name = FileSystem::GetFileNameFromFilePath(file);
        if (name.rfind(stale_prefix, 0) == 0 && FileSystem::GetExtensionFromFilePath(file) == ".png")
        {
            FileSystem::Delete(file);
        }
    }

    const uint32_t steps = static_cast<uint32_t>(floorf((m_render_request.end - m_render_request.start) * m_render_request.fps + 0.5f)) + 1;

    m_render_status                = RenderStatus();
    m_render_status.active         = true;
    m_render_status.capture        = capture;
    m_render_status.fps            = m_render_request.fps;
    m_render_status.directory      = m_render_request.directory;
    m_render_status.frames_total   = capture ? static_cast<uint32_t>(m_render_request.capture_times.size()) : (steps + m_render_request.stride - 1) / m_render_request.stride;
    m_render_frame_index           = 0;
    m_render_finish_wait           = 0;
    m_render_save_failures_start   = Renderer::GetScreenshotSaveFailures();

    Timer::SetFixedDeltaSec(1.0 / m_render_request.fps);
    Renderer::SetCleanCapture(true);

    ReleaseDrives();
    m_time         = -m_render_request.preroll;
    m_playing      = true;
    m_preview      = false;
    m_skip_advance = true;
    m_last_event   = -1;
    StageDrives();
    return true;
}

void Sequencer::StopRender()
{
    if (m_render_status.active)
    {
        m_render_status.last_error = "stopped";
        FinishRender();
    }
}

bool Sequencer::HasSimulatedMotion() const
{
    // drives are the only thing a jumped playhead cannot reproduce, spline followers and every track are pure functions of time
    return !m_drive_events.empty();
}

bool Sequencer::StartCapture(const CaptureRequest& request, string& error)
{
    if (m_render_status.active)
    {
        error = "a render or capture is already running";
        return false;
    }
    if (request.times.empty())
    {
        error = "no capture times were given";
        return false;
    }
    if (request.times.size() > 64)
    {
        error = "at most 64 stills per capture";
        return false;
    }
    if (m_events.empty())
    {
        error = "there are no camera events to capture";
        return false;
    }

    bool simulate = HasSimulatedMotion();
    if (request.mode == "simulate")
    {
        simulate = true;
    }
    else if (request.mode == "scrub")
    {
        simulate = false;
    }
    else if (request.mode != "auto")
    {
        error = "mode must be auto, simulate or scrub";
        return false;
    }

    const string directory = request.directory.empty() ? "sequencer_renders/" + FileSystem::GetFileNameWithoutExtensionFromFilePath(World::GetFilePath()) + "_capture" : request.directory;
    if (simulate)
    {
        RenderRequest render;
        render.fps           = request.fps;
        render.directory     = directory;
        render.capture_times = request.times;
        return StartRender(render, error);
    }

    if (!FileSystem::Exists(directory))
    {
        FileSystem::CreateDirectory_(directory);
    }
    for (const string& file : FileSystem::GetFilesInDirectory(directory))
    {
        const string name = FileSystem::GetFileNameFromFilePath(file);
        if (name.rfind("capture_", 0) == 0 && FileSystem::GetExtensionFromFilePath(file) == ".png")
        {
            FileSystem::Delete(file);
        }
    }

    m_scrub_times = request.times;
    for (float& time : m_scrub_times)
    {
        time = clamp(time, 0.0f, m_duration);
    }
    m_render_status              = RenderStatus();
    m_render_status.active       = true;
    m_render_status.capture      = true;
    m_render_status.scrubbed     = true;
    m_render_status.directory    = directory;
    m_render_status.frames_total = static_cast<uint32_t>(m_scrub_times.size());
    m_render_save_failures_start = Renderer::GetScreenshotSaveFailures();
    m_render_request             = RenderRequest();
    m_render_request.directory   = directory;
    m_scrub_index                = 0;
    m_scrub_wait                 = -1;
    m_scrub_was_preview          = m_preview;
    m_scrub_was_time             = m_time;
    m_playing                    = false;
    m_preview                    = true;
    Renderer::SetCleanCapture(true);
    return true;
}

void Sequencer::TickScrubCapture()
{
    if (!m_render_status.active || !m_render_status.scrubbed)
    {
        return;
    }
    if (m_scrub_index >= m_scrub_times.size())
    {
        FinishScrubCapture();
        return;
    }

    // jump, then give temporal upscaling and history a few frames to converge on the new view before the still
    const int settle_frames = 8;
    if (m_scrub_wait < 0)
    {
        m_time       = m_scrub_times[m_scrub_index];
        m_last_event = -1;
        m_scrub_wait = settle_frames;
        return;
    }
    if (m_scrub_wait > 0)
    {
        m_scrub_wait--;
        return;
    }

    char name[32];
    snprintf(name, sizeof(name), "/capture_%02u.png", static_cast<uint32_t>(m_scrub_index));
    const string path = m_render_request.directory + name;
    if (Renderer::Screenshot(path))
    {
        m_render_status.frames_written++;
        m_render_status.files.push_back(filesystem::absolute(path).generic_string());
        m_render_status.times.push_back(m_time);
        m_scrub_index++;
        m_scrub_wait = -1;
    }
}

void Sequencer::FinishScrubCapture()
{
    Renderer::SetCleanCapture(false);
    m_render_status.active = false;
    m_preview              = m_scrub_was_preview;
    m_time                 = m_scrub_was_time;
    m_last_event           = -1;

    const auto wait_start = chrono::steady_clock::now();
    while (Renderer::GetScreenshotSavesInFlight() > 0 && chrono::steady_clock::now() - wait_start < chrono::seconds(30))
    {
        this_thread::sleep_for(chrono::milliseconds(5));
    }
    const uint32_t failures = Renderer::GetScreenshotSaveFailures() - m_render_save_failures_start;
    if (failures > 0)
    {
        m_render_status.last_error = to_string(failures) + " stills failed to save, check free disk space";
    }
}

void Sequencer::TickRender()
{
    if (!m_render_status.active || m_render_status.scrubbed)
    {
        return;
    }

    // clean capture stays on until the queued final screenshot has rendered, or the editor overlay lands in it
    if (m_render_finish_wait > 0)
    {
        if (--m_render_finish_wait == 0)
        {
            FinishRender();
        }
        return;
    }

    const float half_frame = 0.5f / m_render_request.fps;
    if (m_render_status.capture)
    {
        // one still per frame, a second time inside the same frame lands on the next one
        const vector<float>& times = m_render_request.capture_times;
        if (m_render_frame_index < times.size() && m_time >= times[m_render_frame_index] - half_frame)
        {
            char name[32];
            snprintf(name, sizeof(name), "/capture_%02u.png", m_render_frame_index);
            const string path = m_render_request.directory + name;
            if (Renderer::Screenshot(path))
            {
                m_render_status.frames_written++;
                m_render_status.files.push_back(filesystem::absolute(path).generic_string());
                m_render_status.times.push_back(m_time);
            }
            else
            {
                m_render_status.last_error = "a still was dropped because a screenshot was still pending";
            }
            m_render_frame_index++;
        }
        if (m_render_frame_index >= times.size() || !m_playing)
        {
            m_render_finish_wait = 2;
        }
        return;
    }

    // png encoding is slower than rendering, cap the queue so staging buffers do not pile up
    const auto wait_start = chrono::steady_clock::now();
    while (Renderer::GetScreenshotSavesInFlight() > max_saves_in_flight && chrono::steady_clock::now() - wait_start < chrono::seconds(20))
    {
        this_thread::sleep_for(chrono::milliseconds(2));
    }

    if (m_time >= m_render_request.start - half_frame)
    {
        if (m_render_frame_index % m_render_request.stride == 0)
        {
            char name[32];
            snprintf(name, sizeof(name), "/frame_%05u.png", m_render_status.frames_written);
            if (Renderer::Screenshot(m_render_request.directory + name))
            {
                m_render_status.frames_written++;
            }
            else
            {
                m_render_status.last_error = "a frame was dropped because a screenshot was still pending";
            }
        }
        m_render_frame_index++;
    }

    if (m_time >= m_render_request.end - half_frame || !m_playing || m_render_status.frames_written >= m_render_status.frames_total)
    {
        m_render_finish_wait = 2;
    }
}

void Sequencer::FinishRender()
{
    if (m_render_status.scrubbed)
    {
        FinishScrubCapture();
        return;
    }
    Timer::SetFixedDeltaSec(0.0);
    Renderer::SetCleanCapture(false);
    m_render_status.active = false;
    m_playing              = false;
    ReleaseDrives();

    // the last pngs are still encoding, wait for them so a failed write is reported with the take
    const auto wait_start = chrono::steady_clock::now();
    while (Renderer::GetScreenshotSavesInFlight() > 0 && chrono::steady_clock::now() - wait_start < chrono::seconds(30))
    {
        this_thread::sleep_for(chrono::milliseconds(5));
    }
    const uint32_t failures = Renderer::GetScreenshotSaveFailures() - m_render_save_failures_start;
    if (failures > 0)
    {
        m_render_status.last_error = to_string(failures) + " frames failed to save, check free disk space";
    }
}

void Sequencer::OnTickVisible()
{
    DrawToolbar();
    ImGui::Dummy(ImVec2(0.0f, 2.0f));

    // the timeline fills the window and reserves a fixed panel on the right for the inspector
    const float spacing       = 6.0f;
    const float inspector_w   = 320.0f;
    const float avail         = ImGui::GetContentRegionAvail().x;
    const bool show_inspector = avail - inspector_w - spacing > 220.0f;
    const float timeline_w    = show_inspector ? avail - inspector_w - spacing : 0.0f;

    // popups live in the same child that opens them so their id scope matches
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    ImGui::BeginChild("##seq_timeline", ImVec2(timeline_w, 0.0f), ImGuiChildFlags_Borders);
    panel_header("timeline");
    DrawTimeline();
    DrawPopups();
    ImGui::EndChild();

    if (show_inspector)
    {
        ImGui::SameLine(0.0f, spacing);
        ImGui::BeginChild("##seq_inspector", ImVec2(inspector_w, 0.0f), ImGuiChildFlags_Borders);
        panel_header("properties");
        DrawInspector();
        ImGui::EndChild();
    }
    ImGui::PopStyleVar();

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
        else if (m_drive_selected != -1)
        {
            DeleteSelectedDrive();
        }
        else if (m_curve_channel >= 0 && m_curve_selected >= 0)
        {
            RemoveCurveKey(static_cast<Channel>(m_curve_channel), m_curve_selected);
        }
        else if (m_title_selected >= 0)
        {
            RemoveTitle(m_title_selected);
        }
    }
}

void Sequencer::DrawToolbar()
{
    const float icon_size = ImGui::GetFontSize();

    // play pause toggle, uses the shared icon atlas
    const IconType transport_icon = m_playing ? IconType::Pause : IconType::Play;
    ImGui::BeginDisabled(m_render_status.active);
    if (ImGuiSp::image_button(transport_icon, math::Vector2(icon_size, icon_size), false, ImVec4(0.9f, 0.9f, 0.9f, 1.0f)))
    {
        SetPlayback(m_playing ? Playback::Pause : Playback::Play);
    }
    ImGuiSp::tooltip(m_playing ? "pause" : "play, drive events stage their cars when playback starts in play mode");

    ImGui::SameLine();
    if (ImGuiSp::button("stop"))
    {
        SetPlayback(Playback::Stop);
    }
    ImGuiSp::tooltip("stop, rewinds and hands staged cars back");
    ImGui::EndDisabled();

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
    ImGui::BeginDisabled(first_drivable_car() == nullptr || first_road_spline() == nullptr);
    if (ImGuiSp::button("+ drive"))
    {
        AddDriveAtPlayhead();
    }
    ImGui::EndDisabled();
    ImGuiSp::tooltip("add a physically driven car along a road spline at the playhead");

    ImGui::SameLine();
    if (m_render_status.active)
    {
        if (ImGuiSp::button("stop render"))
        {
            StopRender();
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("rendering %u / %u", m_render_status.frames_written, m_render_status.frames_total);
    }
    else
    {
        if (ImGuiSp::button("render"))
        {
            RenderRequest request;
            request.fps = m_render_fps_ui;
            string error;
            if (!StartRender(request, error))
            {
                m_render_status.last_error = error;
            }
        }
        ImGuiSp::tooltip("render the sequence to a png frame sequence at a fixed timestep, drives need play mode");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60.0f);
        ImGui::DragFloat("##render_fps", &m_render_fps_ui, 1.0f, 1.0f, 120.0f, "%.0f fps");
        if (!m_render_status.last_error.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", m_render_status.last_error.c_str());
        }
    }

    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s / %s", format_time(m_time).c_str(), format_time(m_duration).c_str());
    DrawTrackToolbar();

    // duration, right aligned
    const float input_width = 80.0f;
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + max(ImGui::GetContentRegionAvail().x - input_width, 0.0f));
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
    if (ImGui::IsItemActive() && !m_render_status.active)
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
    const float drive_track_y  = spline_track_y + track_height + 2.0f;
    const float label_offset_y = (track_height - ImGui::GetFontSize()) * 0.5f;
    draw->AddText(ImVec2(origin.x, camera_track_y + label_offset_y), col_text, "camera");
    draw->AddText(ImVec2(origin.x, spline_track_y + label_offset_y), col_text, "spline");
    draw->AddText(ImVec2(origin.x, drive_track_y + label_offset_y), col_text, "drive");
    DrawCameraTrack(timeline_x, camera_track_y, timeline_width, pixels_per_sec);
    DrawSplineTrack(timeline_x, spline_track_y, timeline_width, pixels_per_sec);
    DrawDriveTrack(timeline_x, drive_track_y, timeline_width, pixels_per_sec);
    DrawTrackLanes(timeline_x, origin.x, drive_track_y + track_height + 2.0f, timeline_width, pixels_per_sec);

    // beat grid and markers over every track, then the playhead on top
    const float playhead_x    = timeline_x + max(m_time, 0.0f) * pixels_per_sec;
    const float tracks_bottom = drive_track_y + track_height + 2.0f + GetTrackLanesHeight();
    DrawBeatGrid(timeline_x, origin.y + ruler_height, tracks_bottom, pixels_per_sec);
    DrawMarkers(timeline_x, origin.y, tracks_bottom, pixels_per_sec);
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
        DeselectAll();
        m_selected        = hovered_event;
        m_spline_selected = -1;
        m_drive_selected  = -1;
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
            m_drive_selected  = -1;
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
        if (m_events[i].move != ShotMove::Rig)
        {
            label = GetMoveName(m_events[i].move);
            if (m_events[i].blend_in > 0.0f)
            {
                label = "~ " + label;
            }
        }
        else if (m_events[i].rig)
        {
            label = string(space_names[static_cast<int>(m_events[i].space)]) + " shot";
            if (m_events[i].anchor_entity_id != 0 && m_events[i].space != ShotSpace::World)
            {
                label += " on " + get_entity_name(m_events[i].anchor_entity_id);
            }
        }
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
        DeselectAll();
        m_spline_selected = hovered_event;
        m_selected        = -1;
        m_drive_selected  = -1;
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
            m_selected       = -1;
            m_drive_selected = -1;
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

void Sequencer::DrawDriveTrack(float origin_x, float track_y, float width, float pixels_per_sec)
{
    ImDrawList* draw         = ImGui::GetWindowDrawList();
    const ImU32 col_bg       = ImGui::ColorConvertFloat4ToU32(ImGui::Style::bg_color_1);
    const ImU32 col_tick     = ImGui::ColorConvertFloat4ToU32(ImGui::Style::h_color_2);
    const ImU32 col_text     = ImGui::ColorConvertFloat4ToU32(ImGui::Style::color_info);
    const ImU32 col_playhead = ImGui::ColorConvertFloat4ToU32(ImGui::Style::color_accent_1);

    const ImVec2 track_min = ImVec2(origin_x, track_y);
    const ImVec2 track_max = ImVec2(origin_x + width, track_y + track_height);
    ImGui::SetCursorScreenPos(track_min);
    ImGui::InvisibleButton("##sequencer_drive_track", ImVec2(width, track_height));
    const bool track_hovered = ImGui::IsItemHovered();
    const float mouse_time   = clamp((ImGui::GetIO().MousePos.x - track_min.x) / pixels_per_sec, 0.0f, m_duration);

    int hovered_event     = -1;
    int hovered_edge      = 0;
    const float edge_time = edge_grab_px / pixels_per_sec;
    if (track_hovered)
    {
        for (int i = 0; i < static_cast<int>(m_drive_events.size()); i++)
        {
            if (mouse_time >= m_drive_events[i].start_time && mouse_time <= m_drive_events[i].end_time)
            {
                hovered_event = i;
                if (mouse_time - m_drive_events[i].start_time <= edge_time)
                {
                    hovered_edge = -1;
                }
                else if (m_drive_events[i].end_time - mouse_time <= edge_time)
                {
                    hovered_edge = 1;
                }
                break;
            }
        }
    }

    draw->AddRectFilled(track_min, track_max, col_bg, 3.0f);

    if (track_hovered && hovered_event == -1 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        const float saved = m_time;
        m_time            = mouse_time;
        AddDriveAtPlayhead();
        m_time            = saved;
    }
    else if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        DeselectAll();
        m_drive_selected  = hovered_event;
        m_selected        = -1;
        m_spline_selected = -1;
        if (m_drive_selected != -1)
        {
            m_drive_dragging    = m_drive_selected;
            m_drive_drag_edge   = hovered_edge;
            m_drive_drag_offset = mouse_time - (hovered_edge == 1 ? m_drive_events[m_drive_selected].end_time : m_drive_events[m_drive_selected].start_time);
            m_drive_drag_moved  = false;
            m_drag_undo_state   = CaptureState();
        }
    }
    else if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && hovered_event != -1)
    {
        m_drive_selected  = hovered_event;
        m_selected        = -1;
        m_spline_selected = -1;
        ImGui::OpenPopup("##sequencer_drive_event");
    }

    if (m_drive_dragging != -1)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            DriveEvent& event = m_drive_events[m_drive_dragging];
            if (m_drive_drag_edge == -1)
            {
                const float new_start = clamp(mouse_time - m_drive_drag_offset, 0.0f, event.end_time - min_event_gap);
                m_drive_drag_moved   |= new_start != event.start_time;
                event.start_time      = new_start;
            }
            else if (m_drive_drag_edge == 1)
            {
                const float new_end = clamp(mouse_time - m_drive_drag_offset, event.start_time + min_event_gap, m_duration);
                m_drive_drag_moved |= new_end != event.end_time;
                event.end_time      = new_end;
            }
            else
            {
                // keys move with the window so the speed profile keeps its shape
                const float span      = event.end_time - event.start_time;
                const float new_start = clamp(mouse_time - m_drive_drag_offset, 0.0f, m_duration - span);
                const float shift     = new_start - event.start_time;
                if (shift != 0.0f)
                {
                    for (SpeedKey& key : event.speed_keys)
                    {
                        key.time += shift;
                    }
                    event.start_time   = new_start;
                    event.end_time     = new_start + span;
                    m_drive_drag_moved = true;
                }
            }
        }
        else
        {
            if (m_drive_drag_moved)
            {
                CommitState(m_drag_undo_state);
            }
            m_drive_dragging = -1;
        }
    }

    for (int i = 0; i < static_cast<int>(m_drive_events.size()); i++)
    {
        const DriveEvent& event = m_drive_events[i];
        const ImVec2 seg_min    = ImVec2(track_min.x + event.start_time * pixels_per_sec + 1.0f, track_min.y + 2.0f);
        const ImVec2 seg_max    = ImVec2(track_min.x + event.end_time * pixels_per_sec - 1.0f, track_max.y - 2.0f);

        ImVec4 fill = ImGui::Style::color_warning;
        fill.w      = 0.55f;
        draw->AddRectFilled(seg_min, seg_max, ImGui::ColorConvertFloat4ToU32(fill), 3.0f);
        if (i == m_drive_selected || i == hovered_event)
        {
            draw->AddRect(seg_min, seg_max, col_playhead, 3.0f, i == m_drive_selected ? 2.0f : 1.0f);
        }

        // the speed profile as a polyline over the window, scaled to the fastest key
        float top_speed = 1.0f;
        for (const SpeedKey& key : event.speed_keys)
        {
            top_speed = max(top_speed, key.speed_kmh);
        }
        draw->PushClipRect(seg_min, seg_max, true);
        ImVec2 previous;
        for (size_t k = 0; k < event.speed_keys.size(); k++)
        {
            const SpeedKey& key = event.speed_keys[k];
            const ImVec2 point  = ImVec2(track_min.x + key.time * pixels_per_sec, seg_max.y - 3.0f - (seg_max.y - seg_min.y - 6.0f) * key.speed_kmh / top_speed);
            if (k > 0)
            {
                draw->AddLine(previous, point, col_text, 1.5f);
            }
            draw->AddCircleFilled(point, 2.5f, col_text);
            previous = point;
        }
        const string label = get_entity_name(event.car_entity_id) + " on " + get_entity_name(event.spline_entity_id);
        draw->AddText(ImVec2(seg_min.x + 6.0f, seg_min.y + 2.0f), col_text, label.c_str());
        draw->PopClipRect();
    }

    if (m_drive_events.empty())
    {
        const char* hint  = "double click to drive a car along a road, physics does the rest";
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
            SortCameraEvents();
            m_selected        = GetEventIndexAtTime(m_popup_time);
            m_spline_selected = -1;
            m_drive_selected  = -1;
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
            m_drive_selected  = -1;
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

    if (ImGui::BeginPopup("##sequencer_drive_event"))
    {
        if (m_drive_selected != -1 && m_drive_selected < static_cast<int>(m_drive_events.size()))
        {
            if (ImGui::MenuItem("delete"))
            {
                DeleteSelectedDrive();
            }
        }
        ImGui::EndPopup();
    }
}

void Sequencer::DrawShotInspector(float width)
{
    CameraEvent& event = m_events[m_selected];

    // a drag edits live and commits one undo step when released
    auto track_edit = [this]()
    {
        if (ImGui::IsItemActivated())
        {
            m_drag_undo_state = CaptureState();
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            CommitState(m_drag_undo_state);
        }
    };

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    inspector_section("motion");

    {
        bool rig = event.rig;
        inspector_label("animated", width);
        if (ImGui::Checkbox("##seq_i_rig", &rig))
        {
            const State before = CaptureState();
            event.rig          = rig;
            if (rig && event.position_start == Vector3::Zero && event.position_end == Vector3::Zero)
            {
                if (Entity* camera = World::GetEntityById(event.camera_entity_id))
                {
                    event.position_start = event.position_end = camera->GetPosition();
                    event.look_start     = event.look_end     = camera->GetPosition() + camera->GetForward() * 10.0f;
                }
            }
            CommitState(before);
        }
    }
    if (!event.rig)
    {
        return;
    }

    {
        const char* move_labels[] = { "rig", "orbit", "dolly zoom", "path" };
        int move = static_cast<int>(event.move);
        inspector_label("move", width);
        if (ImGui::Combo("##seq_i_move", &move, move_labels, IM_ARRAYSIZE(move_labels)))
        {
            const State before = CaptureState();
            event.move         = static_cast<ShotMove>(move);
            CommitState(before);
        }
        ImGuiSp::tooltip("rig moves between from and to, orbit circles the look at target (bullet time with a frozen time scale),\ndolly zoom solves the fov so the subject keeps its size, path rides a spline");
    }
    inspector_label("blend in", width);
    ImGui::DragFloat("##seq_i_blend", &event.blend_in, 0.01f, 0.0f, 10.0f, event.blend_in > 0.0f ? "%.2f s" : "cut");
    track_edit();
    ImGuiSp::tooltip("ease in from the previous shot instead of cutting, a short blend is a whip pan");

    if (event.move == ShotMove::Orbit)
    {
        inspector_label("radius", width);
        ImGui::DragFloat("##seq_i_or", &event.orbit_radius, 0.05f, 0.1f, 500.0f, "%.2f m");
        track_edit();
        inspector_label("height", width);
        ImGui::DragFloat("##seq_i_oh", &event.orbit_height, 0.05f, -50.0f, 200.0f, "%.2f m");
        track_edit();
        inspector_label("angle from", width);
        ImGui::DragFloat("##seq_i_oa0", &event.orbit_angle_start, 0.5f, -1080.0f, 1080.0f, "%.1f deg");
        track_edit();
        inspector_label("angle to", width);
        ImGui::DragFloat("##seq_i_oa1", &event.orbit_angle_end, 0.5f, -1080.0f, 1080.0f, "%.1f deg");
        track_edit();
        inspector_label("rise", width);
        ImGui::DragFloat("##seq_i_orise", &event.orbit_rise, 0.05f, -100.0f, 100.0f, "%.2f m");
        track_edit();
        inspector_label("center from", width);
        ImGui::DragFloat3("##seq_i_ocs", &event.look_start.x, 0.05f);
        track_edit();
        inspector_label("center to", width);
        ImGui::DragFloat3("##seq_i_oce", &event.look_end.x, 0.05f);
        track_edit();
        ImGuiSp::tooltip("offset from the target in its heading frame, or a world point without a target");
    }
    else if (event.move == ShotMove::Path)
    {
        uint64_t path_id = event.path_entity_id;
        inspector_label("spline", width);
        if (spline_combo("##seq_i_path", path_id))
        {
            const State before   = CaptureState();
            event.path_entity_id = path_id;
            CommitState(before);
        }
        inspector_label("along from", width);
        ImGui::DragFloat("##seq_i_p0", &event.path_start, 0.002f, 0.0f, 1.0f, "%.3f");
        track_edit();
        inspector_label("along to", width);
        ImGui::DragFloat("##seq_i_p1", &event.path_end, 0.002f, 0.0f, 1.0f, "%.3f");
        track_edit();
        inspector_label("offset from", width);
        ImGui::DragFloat3("##seq_i_pos0", &event.position_start.x, 0.05f);
        track_edit();
        inspector_label("offset to", width);
        ImGui::DragFloat3("##seq_i_pos1", &event.position_end.x, 0.05f);
        track_edit();
        ImGuiSp::tooltip("x right, y up, z forward along the spline");
    }
    if (event.move == ShotMove::Orbit || event.move == ShotMove::Path)
    {
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        inspector_section("lens");
        inspector_label("fov from", width);
        ImGui::DragFloat("##seq_i_fs", &event.fov_start, 0.2f, 0.0f, 150.0f, event.fov_start > 0.0f ? "%.1f deg" : "camera");
        track_edit();
        inspector_label("fov to", width);
        ImGui::DragFloat("##seq_i_fe", &event.fov_end, 0.2f, 0.0f, 150.0f, event.fov_end > 0.0f ? "%.1f deg" : "same");
        track_edit();
        inspector_label("roll", width);
        ImGui::DragFloat("##seq_i_roll", &event.roll, 0.1f, -45.0f, 45.0f, "%.1f deg");
        track_edit();
        inspector_label("shake", width);
        ImGui::DragFloat("##seq_i_shake", &event.shake, 0.01f, 0.0f, 3.0f, "%.2f");
        track_edit();
        int ease = static_cast<int>(event.ease);
        inspector_label("ease", width);
        if (ImGui::Combo("##seq_i_ease", &ease, ease_names, IM_ARRAYSIZE(ease_names)))
        {
            const State before = CaptureState();
            event.ease         = static_cast<ShotEase>(ease);
            CommitState(before);
        }
        return;
    }

    {
        int space = static_cast<int>(event.space);
        inspector_label("space", width);
        if (ImGui::Combo("##seq_i_space", &space, space_names, IM_ARRAYSIZE(space_names)))
        {
            const State before = CaptureState();
            event.space        = static_cast<ShotSpace>(space);
            CommitState(before);
        }
    }
    if (event.space != ShotSpace::World)
    {
        uint64_t anchor_id = event.anchor_entity_id;
        inspector_label("anchor", width);
        if (target_combo("##seq_i_anchor", anchor_id))
        {
            const State before     = CaptureState();
            event.anchor_entity_id = anchor_id;
            CommitState(before);
        }
    }

    inspector_label("from", width);
    ImGui::DragFloat3("##seq_i_ps", &event.position_start.x, 0.05f);
    track_edit();
    inspector_label("to", width);
    ImGui::DragFloat3("##seq_i_pe", &event.position_end.x, 0.05f);
    track_edit();
    inspector_label("look from", width);
    ImGui::DragFloat3("##seq_i_ls", &event.look_start.x, 0.05f);
    track_edit();
    inspector_label("look to", width);
    ImGui::DragFloat3("##seq_i_le", &event.look_end.x, 0.05f);
    track_edit();

    // grabbing the editor view into the shot is the fast way to author one, it only works while the editor camera is live
    const bool previewing = m_playing || m_scrubbing || m_preview || m_render_status.active;
    ImGui::BeginDisabled(previewing);
    const float button_w = (width - 6.0f) * 0.5f;
    for (int which = 0; which < 2; which++)
    {
        if (which == 1)
        {
            ImGui::SameLine();
        }
        if (ImGuiSp::button(which == 0 ? "from = view" : "to = view", ImVec2(button_w, 0.0f)))
        {
            Camera* view = World::GetCamera();
            if (view)
            {
                Vector3 origin   = Vector3::Zero;
                Quaternion frame = Quaternion::Identity;
                Entity* anchor   = GetMovingEntity(World::GetEntityById(event.anchor_entity_id));
                if (anchor && event.space != ShotSpace::World)
                {
                    origin = anchor->GetPosition();
                    frame  = event.space == ShotSpace::Anchor ? anchor->GetRotation() : Quaternion::FromAxisAngle(Vector3::Up, heading_yaw(anchor->GetForward()));
                }
                const Quaternion inverse = frame.Inverse();
                const Vector3 position   = view->GetEntity()->GetPosition();
                const Vector3 look       = position + view->GetEntity()->GetForward() * 10.0f;
                const State before       = CaptureState();
                (which == 0 ? event.position_start : event.position_end) = inverse * (position - origin);
                if (event.target_entity_id == 0)
                {
                    (which == 0 ? event.look_start : event.look_end) = inverse * (look - origin);
                }
                if (which == 0 && event.fov_start <= 0.0f)
                {
                    event.fov_start = view->GetFovHorizontalDeg();
                }
                CommitState(before);
            }
        }
    }
    ImGui::EndDisabled();
    ImGuiSp::tooltip("store the editor camera pose as the shot start or end, in the shot space");

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    inspector_section("lens");
    inspector_label("fov from", width);
    ImGui::DragFloat("##seq_i_fs", &event.fov_start, 0.2f, 0.0f, 150.0f, event.fov_start > 0.0f ? "%.1f deg" : "camera");
    track_edit();
    inspector_label("fov to", width);
    ImGui::DragFloat("##seq_i_fe", &event.fov_end, 0.2f, 0.0f, 150.0f, event.fov_end > 0.0f ? "%.1f deg" : "same");
    track_edit();
    inspector_label("aperture", width);
    ImGui::DragFloat("##seq_i_ap", &event.aperture, 0.05f, 0.0f, 32.0f, event.aperture > 0.0f ? "f/%.1f" : "camera");
    track_edit();
    inspector_label("roll", width);
    ImGui::DragFloat("##seq_i_roll", &event.roll, 0.1f, -45.0f, 45.0f, "%.1f deg");
    track_edit();
    inspector_label("shake", width);
    ImGui::DragFloat("##seq_i_shake", &event.shake, 0.01f, 0.0f, 3.0f, "%.2f");
    track_edit();
    if (event.space == ShotSpace::AnchorHeading)
    {
        inspector_label("heading lag", width);
        ImGui::DragFloat("##seq_i_lag", &event.lag, 0.01f, 0.0f, 2.0f, "%.2f s");
        track_edit();
    }
    {
        int ease = static_cast<int>(event.ease);
        inspector_label("ease", width);
        if (ImGui::Combo("##seq_i_ease", &ease, ease_names, IM_ARRAYSIZE(ease_names)))
        {
            const State before = CaptureState();
            event.ease         = static_cast<ShotEase>(ease);
            CommitState(before);
        }
    }
}

void Sequencer::DrawDriveInspector(float width)
{
    DriveEvent& event = m_drive_events[m_drive_selected];

    auto track_edit = [this]()
    {
        if (ImGui::IsItemActivated())
        {
            m_drag_undo_state = CaptureState();
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            CommitState(m_drag_undo_state);
        }
    };

    inspector_section("drive");
    {
        uint64_t car_id = event.car_entity_id;
        inspector_label("car", width);
        if (car_combo("##seq_d_car", car_id))
        {
            const State before  = CaptureState();
            event.car_entity_id = car_id;
            CommitState(before);
        }
    }
    {
        uint64_t spline_id = event.spline_entity_id;
        inspector_label("road", width);
        if (spline_combo("##seq_d_spline", spline_id))
        {
            const State before     = CaptureState();
            event.spline_entity_id = spline_id;
            CommitState(before);
        }
    }
    inspector_label("start", width);
    ImGui::DragFloat("##seq_d_start", &event.start_time, 0.05f, 0.0f, event.end_time - min_event_gap, "%.2f s");
    track_edit();
    inspector_label("end", width);
    ImGui::DragFloat("##seq_d_end", &event.end_time, 0.05f, event.start_time + min_event_gap, m_duration, "%.2f s");
    track_edit();
    inspector_label("staged at", width);
    ImGui::DragFloat("##seq_d_dist", &event.start_distance, 0.5f, 0.0f, 100000.0f, "%.1f m");
    track_edit();
    inspector_label("lane offset", width);
    ImGui::DragFloat("##seq_d_lane", &event.lane_offset, 0.05f, -20.0f, 20.0f, "%.2f m");
    track_edit();
    inspector_label("corner grip", width);
    ImGui::DragFloat("##seq_d_grip", &event.max_lateral_g, 0.01f, 0.1f, 2.0f, "%.2f g");
    track_edit();
    {
        bool reversed = event.reverse;
        inspector_label("reverse", width);
        if (ImGui::Checkbox("##seq_d_rev", &reversed))
        {
            const State before = CaptureState();
            event.reverse      = reversed;
            CommitState(before);
        }
    }

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    inspector_section("speed keys");
    int remove_index = -1;
    for (int k = 0; k < static_cast<int>(event.speed_keys.size()); k++)
    {
        ImGui::PushID(k);
        const float field_w = (width - 30.0f) * 0.5f;
        ImGui::SetNextItemWidth(field_w);
        ImGui::DragFloat("##t", &event.speed_keys[k].time, 0.05f, 0.0f, m_duration, "%.2f s");
        track_edit();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(field_w);
        ImGui::DragFloat("##v", &event.speed_keys[k].speed_kmh, 0.5f, 0.0f, 400.0f, "%.0f km/h");
        track_edit();
        ImGui::SameLine();
        if (ImGuiSp::button("x"))
        {
            remove_index = k;
        }
        ImGui::PopID();
    }
    if (remove_index != -1)
    {
        const State before = CaptureState();
        event.speed_keys.erase(event.speed_keys.begin() + remove_index);
        CommitState(before);
    }
    if (ImGuiSp::button("+ key at playhead", ImVec2(width, 0.0f)))
    {
        const State before = CaptureState();
        SpeedKey key;
        key.time      = clamp(m_time, event.start_time, event.end_time);
        key.speed_kmh = event.speed_keys.empty() ? 60.0f : event.speed_keys.back().speed_kmh;
        event.speed_keys.push_back(key);
        sort(event.speed_keys.begin(), event.speed_keys.end(), [](const SpeedKey& a, const SpeedKey& b) { return a.time < b.time; });
        CommitState(before);
    }

    // live autopilot readout while a take runs
    if (m_drive_selected < static_cast<int>(m_drive_runtime.size()) && m_drive_runtime[m_drive_selected].staged)
    {
        const DriveTelemetry& telemetry = m_drive_runtime[m_drive_selected].telemetry;
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        inspector_section("autopilot");
        ImGui::Text("speed %.0f / %.0f km/h", telemetry.speed_kmh, telemetry.target_kmh);
        ImGui::Text("distance %.0f / %.0f m", telemetry.distance, telemetry.path_length);
        ImGui::Text("lateral error %.2f m", telemetry.lateral_error);
        ImGui::Text("throttle %.2f  brake %.2f  steer %.2f", telemetry.throttle, telemetry.brake, telemetry.steering);
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.18f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.60f, 0.22f, 0.22f, 1.0f));
    if (ImGuiSp::button("delete", ImVec2(width, 0.0f)))
    {
        DeleteSelectedDrive();
    }
    ImGui::PopStyleColor(2);
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

        DrawShotInspector(width);
        if (m_selected == -1)
        {
            return;
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

    // a drive is selected
    if (m_drive_selected != -1 && m_drive_selected < static_cast<int>(m_drive_events.size()))
    {
        DrawDriveInspector(width);
        return;
    }

    // a key on one of the curve lanes is selected
    if (m_curve_channel >= 0 && m_curve_channel < static_cast<int>(Channel::Count) && m_curve_selected >= 0 &&
        m_curve_selected < static_cast<int>(m_cinematic.curves[m_curve_channel].size()))
    {
        DrawCurveKeyInspector(width);
        return;
    }

    // a title card is selected
    if (m_title_selected >= 0 && m_title_selected < static_cast<int>(m_cinematic.titles.size()))
    {
        DrawTitleInspector(width);
        return;
    }

    // nothing selected, the sequence wide settings
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("select a shot on the timeline to edit it, or use + camera, + motion and + drive to add one, double click a lane to key it");
    ImGui::PopTextWrapPos();
    DrawCinematicInspector(width);
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
    for (DriveEvent& event : m_drive_events)
    {
        event.start_time = min(event.start_time, m_duration);
        event.end_time   = min(event.end_time, m_duration);
    }
    for (vector<CurveKey>& curve : m_cinematic.curves)
    {
        for (CurveKey& key : curve)
        {
            key.time = min(key.time, m_duration);
        }
    }
    for (TitleEvent& title : m_cinematic.titles)
    {
        title.start_time = min(title.start_time, m_duration);
        title.end_time   = min(title.end_time, m_duration);
    }
    for (Marker& marker : m_cinematic.markers)
    {
        marker.time = min(marker.time, m_duration);
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
    event.time             = max(m_time, 0.0f);
    event.camera_entity_id = camera->GetObjectId();
    m_events.push_back(event);
    SortCameraEvents();
    m_selected        = GetEventIndexAtTime(event.time);
    m_spline_selected = -1;
    m_drive_selected  = -1;
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
    event.start_time         = max(m_time, 0.0f);
    event.end_time           = min(event.start_time + 5.0f, m_duration);
    if (event.end_time < event.start_time + min_event_gap)
    {
        event.end_time = min(event.start_time + min_event_gap, m_duration);
    }
    event.follower_entity_id = follower->GetObjectId();
    m_spline_events.push_back(event);
    m_spline_selected = static_cast<int>(m_spline_events.size()) - 1;
    m_selected        = -1;
    m_drive_selected  = -1;
    CommitState(before);
}

void Sequencer::AddDriveAtPlayhead()
{
    Car* car       = first_drivable_car();
    Entity* spline = first_road_spline();
    if (!car || !spline)
    {
        return;
    }
    const State before = CaptureState();
    DriveEvent event;
    event.start_time       = max(m_time, 0.0f);
    event.end_time         = min(event.start_time + 8.0f, m_duration);
    event.car_entity_id    = GetPersistentEntity(car->GetRootEntity())->GetObjectId();
    event.spline_entity_id = spline->GetObjectId();
    event.speed_keys.push_back({ event.start_time, 0.0f });
    event.speed_keys.push_back({ min(event.start_time + 4.0f, event.end_time), 100.0f });
    m_drive_events.push_back(event);
    m_drive_selected  = static_cast<int>(m_drive_events.size()) - 1;
    m_selected        = -1;
    m_spline_selected = -1;
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

void Sequencer::DeleteSelectedDrive()
{
    if (m_drive_selected < 0 || m_drive_selected >= static_cast<int>(m_drive_events.size()))
    {
        return;
    }
    RemoveDriveEvent(m_drive_selected);
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
    SortCameraEvents();
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
    // one timeline per world, entity ids only mean something inside the world they came from
    return string(ResourceCache::GetProjectDirectory()) + "sequencer_" + FileSystem::GetFileNameWithoutExtensionFromFilePath(world_path) + ".xml";
}

Sequencer::State Sequencer::CaptureState() const
{
    State state;
    state.duration      = m_duration;
    state.loop          = m_loop;
    state.events        = m_events;
    state.spline_events = m_spline_events;
    state.drive_events  = m_drive_events;
    state.cinematic     = m_cinematic;
    return state;
}

void Sequencer::ApplyState(const State& state)
{
    m_duration        = state.duration;
    m_loop            = state.loop;
    m_events          = state.events;
    m_spline_events   = state.spline_events;
    m_drive_events    = state.drive_events;
    m_cinematic       = state.cinematic;
    m_selected        = -1;
    m_spline_selected = -1;
    m_drive_selected  = -1;
    m_dragging        = -1;
    m_spline_dragging = -1;
    m_drive_dragging  = -1;
    DeselectAll();
    m_time            = min(m_time, m_duration);
    Save();
}

void Sequencer::CommitState(const State& before)
{
    // skip when nothing actually changed so undo steps stay meaningful
    if (before.duration == m_duration && before.loop == m_loop && before.events == m_events && before.spline_events == m_spline_events &&
        before.drive_events == m_drive_events && before.cinematic == m_cinematic)
    {
        return;
    }
    CommandStack::Push(make_shared<SequencerCommand>(this, before, CaptureState()));
    Save();
}

namespace
{
    void write_vector(pugi::xml_node& node, const char* name, const Vector3& value)
    {
        char buffer[96];
        snprintf(buffer, sizeof(buffer), "%.4f,%.4f,%.4f", value.x, value.y, value.z);
        node.append_attribute(name) = buffer;
    }

    Vector3 read_vector(const pugi::xml_node& node, const char* name)
    {
        Vector3 value = Vector3::Zero;
        if (const char* text = node.attribute(name).as_string(nullptr))
        {
            char* end = nullptr;
            value.x   = strtof(text, &end);
            value.y   = end && *end == ',' ? strtof(end + 1, &end) : 0.0f;
            value.z   = end && *end == ',' ? strtof(end + 1, &end) : 0.0f;
        }
        return value;
    }
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
        if (IsAnimated(event))
        {
            node.append_attribute("rig")              = true;
            if (event.move != ShotMove::Rig)
            {
                node.append_attribute("move") = GetMoveName(event.move);
            }
            if (event.move == ShotMove::Orbit)
            {
                node.append_attribute("orbit_radius")      = event.orbit_radius;
                node.append_attribute("orbit_height")      = event.orbit_height;
                node.append_attribute("orbit_angle_start") = event.orbit_angle_start;
                node.append_attribute("orbit_angle_end")   = event.orbit_angle_end;
                node.append_attribute("orbit_rise")        = event.orbit_rise;
            }
            if (event.move == ShotMove::Path)
            {
                node.append_attribute("path_entity_id") = event.path_entity_id;
                node.append_attribute("path_start")     = event.path_start;
                node.append_attribute("path_end")       = event.path_end;
            }
            if (event.blend_in > 0.0f)
            {
                node.append_attribute("blend_in") = event.blend_in;
            }
            node.append_attribute("anchor_entity_id") = event.anchor_entity_id;
            node.append_attribute("space")            = static_cast<int>(event.space);
            write_vector(node, "position_start", event.position_start);
            write_vector(node, "position_end", event.position_end);
            write_vector(node, "look_start", event.look_start);
            write_vector(node, "look_end", event.look_end);
            node.append_attribute("fov_start") = event.fov_start;
            node.append_attribute("fov_end")   = event.fov_end;
            node.append_attribute("aperture")  = event.aperture;
            node.append_attribute("roll")      = event.roll;
            node.append_attribute("shake")     = event.shake;
            node.append_attribute("lag")       = event.lag;
            node.append_attribute("ease")      = static_cast<int>(event.ease);
        }
    }
    for (const SplineEvent& event : m_spline_events)
    {
        pugi::xml_node node = root.append_child("spline_event");
        node.append_attribute("start")              = event.start_time;
        node.append_attribute("end")                = event.end_time;
        node.append_attribute("follower_entity_id") = event.follower_entity_id;
    }
    for (const DriveEvent& event : m_drive_events)
    {
        pugi::xml_node node = root.append_child("drive_event");
        node.append_attribute("start")            = event.start_time;
        node.append_attribute("end")              = event.end_time;
        node.append_attribute("car_entity_id")    = event.car_entity_id;
        node.append_attribute("spline_entity_id") = event.spline_entity_id;
        node.append_attribute("start_distance")   = event.start_distance;
        node.append_attribute("lane_offset")      = event.lane_offset;
        node.append_attribute("max_lateral_g")    = event.max_lateral_g;
        node.append_attribute("reverse")          = event.reverse;
        for (const SpeedKey& key : event.speed_keys)
        {
            pugi::xml_node key_node = node.append_child("speed_key");
            key_node.append_attribute("time")      = key.time;
            key_node.append_attribute("speed_kmh") = key.speed_kmh;
        }
    }
    SaveTracks(root);
    doc.save_file(file_path.c_str());
}

void Sequencer::Load()
{
    if (m_render_status.active)
    {
        FinishRender();
    }
    m_events.clear();
    m_spline_events.clear();
    m_drive_events.clear();
    m_drive_runtime.clear();
    m_time            = 0.0f;
    m_playing         = false;
    m_scrubbing       = false;
    m_preview         = false;
    m_was_previewing  = false;
    m_last_event      = -1;
    m_selected        = -1;
    m_spline_selected = -1;
    m_drive_selected  = -1;
    World::SetActiveCamera(nullptr);

    // the environment snapshot belongs to the world that was just unloaded
    m_world_snapshot_valid = false;
    m_music_entity_id      = 0;
    RestoreTracks();
    m_cinematic = Cinematic();
    DeselectAll();

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
        event.rig              = node.attribute("rig").as_bool(false);
        if (event.rig)
        {
            event.move              = FindMove(node.attribute("move").as_string("rig")).value_or(ShotMove::Rig);
            event.orbit_radius      = node.attribute("orbit_radius").as_float(6.0f);
            event.orbit_height      = node.attribute("orbit_height").as_float(1.5f);
            event.orbit_angle_start = node.attribute("orbit_angle_start").as_float(0.0f);
            event.orbit_angle_end   = node.attribute("orbit_angle_end").as_float(90.0f);
            event.orbit_rise        = node.attribute("orbit_rise").as_float(0.0f);
            event.path_entity_id    = node.attribute("path_entity_id").as_ullong(0);
            event.path_start        = node.attribute("path_start").as_float(0.0f);
            event.path_end          = node.attribute("path_end").as_float(1.0f);
            event.blend_in          = node.attribute("blend_in").as_float(0.0f);
            event.anchor_entity_id = node.attribute("anchor_entity_id").as_ullong(0);
            event.space            = static_cast<ShotSpace>(clamp(node.attribute("space").as_int(0), 0, 2));
            event.position_start   = read_vector(node, "position_start");
            event.position_end     = read_vector(node, "position_end");
            event.look_start       = read_vector(node, "look_start");
            event.look_end         = read_vector(node, "look_end");
            event.fov_start        = node.attribute("fov_start").as_float(0.0f);
            event.fov_end          = node.attribute("fov_end").as_float(0.0f);
            event.aperture         = node.attribute("aperture").as_float(0.0f);
            event.roll             = node.attribute("roll").as_float(0.0f);
            event.shake            = node.attribute("shake").as_float(0.0f);
            event.lag              = node.attribute("lag").as_float(0.0f);
            event.ease             = static_cast<ShotEase>(clamp(node.attribute("ease").as_int(1), 0, 3));
        }
        m_events.push_back(event);
    }
    SortCameraEvents();

    for (pugi::xml_node node : root.children("spline_event"))
    {
        SplineEvent event;
        event.start_time         = node.attribute("start").as_float(0.0f);
        event.end_time           = node.attribute("end").as_float(0.0f);
        event.follower_entity_id = node.attribute("follower_entity_id").as_ullong(0);
        m_spline_events.push_back(event);
    }

    for (pugi::xml_node node : root.children("drive_event"))
    {
        DriveEvent event;
        event.start_time       = node.attribute("start").as_float(0.0f);
        event.end_time         = node.attribute("end").as_float(0.0f);
        event.car_entity_id    = node.attribute("car_entity_id").as_ullong(0);
        event.spline_entity_id = node.attribute("spline_entity_id").as_ullong(0);
        event.start_distance   = node.attribute("start_distance").as_float(0.0f);
        event.lane_offset      = node.attribute("lane_offset").as_float(0.0f);
        event.max_lateral_g    = node.attribute("max_lateral_g").as_float(0.8f);
        event.reverse          = node.attribute("reverse").as_bool(false);
        for (pugi::xml_node key_node : node.children("speed_key"))
        {
            SpeedKey key;
            key.time      = key_node.attribute("time").as_float(0.0f);
            key.speed_kmh = key_node.attribute("speed_kmh").as_float(0.0f);
            event.speed_keys.push_back(key);
        }
        m_drive_events.push_back(event);
    }

    LoadTracks(root);
}
