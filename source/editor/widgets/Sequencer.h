/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ==============
#include "Widget.h"
#include <vector>
#include <string>
#include <optional>
#include <cstdint>
#include "math/Vector3.h"
#include "math/Quaternion.h"
//=========================

namespace spartan
{
    class Car;
    class Entity;
}

class Sequencer : public Widget
{
public:
    Sequencer(Editor* editor);
    ~Sequencer() override;

    void OnTick() override;
    void OnTickVisible() override;

    // the frame of reference a shot's positions are authored in
    enum class ShotSpace : uint8_t
    {
        World,        // fixed world positions
        Anchor,       // rigidly mounted on the anchor, rolls and pitches with it
        AnchorHeading // follows the anchor's position and heading but stays level, like a camera car
    };

    enum class ShotEase : uint8_t
    {
        Linear,
        InOut,
        In,
        Out
    };

    // a cut to a camera, active from its time until the next event
    struct CameraEvent
    {
        float time                = 0.0f;
        uint64_t camera_entity_id = 0;
        uint64_t target_entity_id = 0; // optional, the camera pans to look at this entity

        // motion, when off the camera keeps its own pose and only the look at lock applies
        bool rig                  = false;
        uint64_t anchor_entity_id = 0;
        ShotSpace space           = ShotSpace::World;
        spartan::math::Vector3 position_start = spartan::math::Vector3::Zero;
        spartan::math::Vector3 position_end   = spartan::math::Vector3::Zero;
        spartan::math::Vector3 look_start     = spartan::math::Vector3::Zero; // in the shot space, or an offset from the target when one is set
        spartan::math::Vector3 look_end       = spartan::math::Vector3::Zero;
        float fov_start   = 0.0f; // horizontal degrees, 0 keeps the camera's own
        float fov_end     = 0.0f;
        float aperture    = 0.0f; // f-stop, 0 keeps the camera's own
        float roll        = 0.0f; // degrees
        float shake       = 0.0f; // handheld sway, 0 to 1
        float lag         = 0.0f; // seconds the heading space trails the anchor
        ShotEase ease     = ShotEase::InOut;

        bool operator==(const CameraEvent& other) const;
    };

    // a window during which an entity follows its spline, driven by the timeline
    struct SplineEvent
    {
        float start_time            = 0.0f;
        float end_time              = 0.0f;
        uint64_t follower_entity_id = 0; // entity with a spline follower component
        bool operator==(const SplineEvent& other) const { return start_time == other.start_time && end_time == other.end_time && follower_entity_id == other.follower_entity_id; }
    };

    // target speed at an absolute timeline time
    struct SpeedKey
    {
        float time      = 0.0f;
        float speed_kmh = 0.0f;
        bool operator==(const SpeedKey& other) const { return time == other.time && speed_kmh == other.speed_kmh; }
    };

    // a physically simulated car driven along a road spline by an autopilot, pedals and steering go
    // through the real vehicle simulation so the body, tires and rain react to what it does
    struct DriveEvent
    {
        float start_time          = 0.0f;
        float end_time            = 0.0f;
        uint64_t car_entity_id    = 0;
        uint64_t spline_entity_id = 0;
        float start_distance      = 0.0f; // meters along the spline where the car is staged
        float lane_offset         = 0.0f; // meters right of the centerline
        float max_lateral_g       = 0.8f; // corner speed limit
        bool reverse              = false; // drive from the spline end towards its start
        std::vector<SpeedKey> speed_keys;
        bool operator==(const DriveEvent& other) const;
    };

    // live autopilot readout, for tuning a take
    struct DriveTelemetry
    {
        bool staged           = false;
        bool active           = false;
        float distance        = 0.0f;
        float speed_kmh       = 0.0f;
        float target_kmh      = 0.0f;
        float lateral_error   = 0.0f;
        float throttle        = 0.0f;
        float brake           = 0.0f;
        float steering        = 0.0f;
        float path_length     = 0.0f;
    };

    // a full snapshot of the editable state, used by the undo/redo command
    struct State
    {
        float duration = 10.0f;
        bool loop      = false;
        std::vector<CameraEvent> events;
        std::vector<SplineEvent> spline_events;
        std::vector<DriveEvent> drive_events;
    };

    // restores a snapshot into the widget and persists it, used by the undo/redo command
    void ApplyState(const State& state);

    // offline frame sequence capture
    struct RenderRequest
    {
        float fps     = 30.0f;
        float start   = 0.0f;
        float end     = -1.0f; // below zero means the sequence end
        float preroll = 1.0f;  // seconds simulated before time zero so staged cars settle
        uint32_t stride = 1;   // keep every nth frame, for quick previews
        std::string directory;
    };

    struct RenderStatus
    {
        bool active             = false;
        uint32_t frames_written = 0;
        uint32_t frames_total   = 0;
        float fps               = 0.0f;
        std::string directory;
        std::string last_error;
    };

    // plain view of the timeline for drivers outside the panel
    struct Snapshot
    {
        std::vector<CameraEvent> events;
        std::vector<SplineEvent> spline_events;
        std::vector<DriveEvent> drive_events;
        std::vector<DriveTelemetry> drive_telemetry;
        RenderStatus render;
        float duration = 0.0f;
        float time     = 0.0f;
        bool playing   = false;
        bool loop      = false;
        bool preview   = false;
    };

    enum class Playback
    {
        Play,
        Pause,
        Stop
    };

    // an unset field keeps whatever the panel already had
    struct TimelineRequest
    {
        std::optional<float> duration;
        std::optional<float> time;
        std::optional<bool> loop;
        std::optional<bool> visible;
        std::optional<bool> preview;
    };

    // control surface for drivers outside the panel, it takes resolved entity ids because naming and
    // parsing belong to whoever is driving it. an out of range index is reported rather than clamped
    // so a caller is told it addressed nothing
    void SetTimeline(const TimelineRequest& request);
    void SetPlayback(Playback action);
    void AddCameraEvent(const CameraEvent& event);
    bool UpdateCameraEvent(int index, const CameraEvent& event);
    bool RemoveCameraEvent(int index);
    void ClearCameraEvents();
    void AddSplineEvent(float start_time, float end_time, uint64_t follower_entity_id);
    bool UpdateSplineEvent(int index, std::optional<float> start_time, std::optional<float> end_time, std::optional<uint64_t> follower_entity_id);
    bool RemoveSplineEvent(int index);
    void ClearSplineEvents();
    void AddDriveEvent(const DriveEvent& event);
    bool UpdateDriveEvent(int index, const DriveEvent& event);
    bool RemoveDriveEvent(int index);
    void ClearDriveEvents();
    bool StartRender(const RenderRequest& request, std::string& error);
    void StopRender();
    Snapshot GetSnapshot() const;

    // the car and the entity that actually moves for any entity inside a car's hierarchy, or the entity itself
    static spartan::Car* FindCar(spartan::Entity* entity);
    static spartan::Entity* GetMovingEntity(spartan::Entity* entity);
    // what a timeline should store for an entity: a car's vehicle child gets a new id on every load, its prefab owner does not
    static spartan::Entity* GetPersistentEntity(spartan::Entity* entity);
    // the shared camera animated shots drive, found by name or created on demand
    static spartan::Entity* GetOrCreateShotCamera();

private:
    // runtime state of one drive, rebuilt whenever a take is staged
    struct DriveRuntime
    {
        std::vector<spartan::math::Vector3> points; // polyline at ~1 m spacing, lane offset applied
        std::vector<float> distances;
        std::vector<float> curvatures;
        size_t nearest       = 0;
        float integral       = 0.0f;
        float steering       = 0.0f;
        float throttle       = 0.0f;
        bool staged          = false;
        DriveTelemetry telemetry;
    };

    void OnWorldTicked();
    void Evaluate(float delta_time);
    void EvaluateCamera(int index, bool cut);
    void EvaluateDrives(float delta_time);
    void StageDrives();
    bool WaitForDrives(float delta_time);
    void ReleaseDrives();
    bool BuildDrivePath(const DriveEvent& event, DriveRuntime& runtime);
    void TickRender();
    void FinishRender();

    void DrawToolbar();
    void DrawTimeline();
    void DrawCameraTrack(float origin_x, float track_y, float width, float pixels_per_sec);
    void DrawSplineTrack(float origin_x, float track_y, float width, float pixels_per_sec);
    void DrawDriveTrack(float origin_x, float track_y, float width, float pixels_per_sec);
    void DrawPopups();
    void DrawInspector();
    void DrawShotInspector(float width);
    void DrawDriveInspector(float width);
    void ClampToDuration();
    void AddCameraAtPlayhead();
    void AddMotionAtPlayhead();
    void AddDriveAtPlayhead();
    void DeleteSelectedCamera();
    void DeleteSelectedSpline();
    void DeleteSelectedDrive();
    void DuplicateSelectedCamera();
    void DuplicateSelectedSpline();
    int GetEventIndexAtTime(float time) const;
    std::string GetFilePath() const;
    void Save() const;
    void Load();
    State CaptureState() const;
    void CommitState(const State& before); // pushes an undo step when the state changed, then saves
    void SortCameraEvents();

    std::vector<CameraEvent> m_events; // always sorted by time
    std::vector<SplineEvent> m_spline_events;
    std::vector<DriveEvent> m_drive_events;
    std::vector<DriveRuntime> m_drive_runtime;
    float m_duration      = 10.0f;
    float m_time          = 0.0f;
    bool m_playing        = false;
    bool m_loop           = false;
    bool m_scrubbing      = false;
    bool m_preview        = false;
    bool m_was_previewing = false;
    bool m_skip_advance   = false;
    float m_drive_wait    = 0.0f;  // seconds playback has been held waiting for drive cars to stage
    bool m_drive_wait_expired = false;
    int m_last_event      = -1;
    int m_selected        = -1;
    int m_dragging        = -1;
    float m_drag_offset   = 0.0f;
    bool m_drag_moved     = false;
    float m_popup_time    = 0.0f;

    // heading space smoothing, reset on every cut
    spartan::math::Vector3 m_lag_position = spartan::math::Vector3::Zero;
    float m_lag_yaw                       = 0.0f;

    // spline track interaction state
    int m_spline_selected      = -1;
    int m_spline_dragging      = -1;
    int m_spline_drag_edge     = 0; // -1 left edge, 0 body, +1 right edge
    float m_spline_drag_offset = 0.0f;
    bool m_spline_drag_moved   = false;
    float m_spline_popup_time  = 0.0f;

    // drive track interaction state
    int m_drive_selected      = -1;
    int m_drive_dragging      = -1;
    int m_drive_drag_edge     = 0;
    float m_drive_drag_offset = 0.0f;
    bool m_drive_drag_moved   = false;

    // recording
    RenderRequest m_render_request;
    RenderStatus m_render_status;
    uint32_t m_render_frame_index = 0;
    uint32_t m_render_save_failures_start = 0;
    float m_render_fps_ui         = 30.0f;

    State m_drag_undo_state; // captured when a drag begins, committed when it ends

    uint64_t m_world_loaded_handle = 0;
    uint64_t m_world_ticked_handle = 0;
};
