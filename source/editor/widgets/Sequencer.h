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

namespace pugi
{
    class xml_node;
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
        Out,
        Hold // steps to the key's value when it is reached, for curves only
    };

    // how an animated shot places the camera
    enum class ShotMove : uint8_t
    {
        Rig,       // from position_start to position_end in the shot space
        Orbit,     // circles the target (or a world center), the bullet time move
        DollyZoom, // rig motion with the fov solved so the subject keeps its size, the vertigo effect
        Path       // rides a spline entity
    };

    // a cut to a camera, active from its time until the next event
    struct CameraEvent
    {
        float time                = 0.0f;
        uint64_t camera_entity_id = 0;
        uint64_t target_entity_id = 0; // optional, the camera pans to look at this entity

        ShotMove move             = ShotMove::Rig;
        // orbit: angles in degrees, 0 looks at the target's front (or along +z without a target), positive turns counter clockwise seen from above
        float orbit_radius        = 6.0f;
        float orbit_height        = 1.5f;
        float orbit_angle_start   = 0.0f;
        float orbit_angle_end     = 90.0f;
        float orbit_rise          = 0.0f; // meters added to the height over the shot
        // path: a spline entity and the normalized range the camera travels, position_start offsets it in the path frame
        uint64_t path_entity_id   = 0;
        float path_start          = 0.0f;
        float path_end            = 1.0f;
        // seconds the shot eases in from the previous shot's pose instead of cutting, short blends make whip pans
        float blend_in            = 0.0f;

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

    // throttle while the car sits in neutral on the handbrake, revving the engine on the spot
    struct RevKey
    {
        float time     = 0.0f;
        float throttle = 0.0f; // 0 to 1
        bool operator==(const RevKey& other) const { return time == other.time && throttle == other.throttle; }
    };

    // a line lock burnout while the speed keys hold the car at rest: first gear, the fronts braked, the rears spinning
    struct BurnoutSpan
    {
        float start    = 0.0f;
        float end      = 0.0f;
        float throttle = 1.0f; // 0 to 1
        bool operator==(const BurnoutSpan& other) const { return start == other.start && end == other.end && throttle == other.throttle; }
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
        std::vector<RevKey> rev_keys; // between the first and last key, wherever the speed keys hold the car still, it revs in neutral
        std::vector<BurnoutSpan> burnouts; // take precedence over revs, the launch follows when one ends
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

    // every keyed value the timeline can drive, grouped into the time, audio, world and overlay tracks
    enum class Channel : uint8_t
    {
        TimeScale,     // simulation speed, 1 real time, 0 frozen
        GainVehicle,   // group gains, 1 is the source's own level
        GainAmbience,
        GainSfx,
        GainMusic,
        Muffle,        // low pass on vehicle, ambience and sfx, 0 to 1
        TimeOfDay,     // utc hours, 0 to 24
        Rain,          // 0 to 1
        Clouds,        // 0 to 1
        Wind,          // m/s, keeps the direction
        Puddles,       // 0 to 1
        Fade,          // 0 clear, 1 solid fade color
        Letterbox,     // target aspect ratio, 0 off
        Count
    };

    enum class TrackGroup : uint8_t
    {
        Time,
        Audio,
        World,
        Overlay,
        Count
    };

    // a keyed value, the ease shapes the segment that arrives at this key
    struct CurveKey
    {
        float time      = 0.0f;
        float value     = 0.0f;
        ShotEase ease   = ShotEase::InOut;
        bool operator==(const CurveKey& other) const = default;
    };

    // plain: centered lines. trailer: tracked capitals with a kicker, a rule and a tagline. legend: a left aligned caption card
    enum class TitleStyle : uint8_t
    {
        Plain,
        Trailer,
        Legend
    };

    // text over the frame, fading in and out at its edges
    struct TitleEvent
    {
        float start_time = 0.0f;
        float end_time   = 3.0f;
        std::string text;
        std::string kicker; // small tracked line above the text, trailer and legend styles
        float x          = 0.5f; // screen fractions, the center for plain and trailer, the top left for legend
        float y          = 0.82f;
        bool large       = true;
        float fade       = 0.5f; // seconds
        TitleStyle style = TitleStyle::Plain;
        spartan::math::Vector3 color  = spartan::math::Vector3::One;
        spartan::math::Vector3 accent = spartan::math::Vector3(0.95f, 0.62f, 0.25f);
        bool operator==(const TitleEvent& other) const = default;
    };

    static const char* GetTitleStyleName(TitleStyle style);
    static std::optional<TitleStyle> FindTitleStyle(const std::string& name);

    struct Marker
    {
        float time = 0.0f;
        std::string name;
        bool operator==(const Marker& other) const = default;
    };

    // a music file played in sync with the playhead
    struct MusicClip
    {
        std::string path;    // empty is no music
        float start  = 0.0f; // timeline seconds the clip starts at
        float offset = 0.0f; // seconds into the file at that moment
        float volume = 1.0f;
        bool operator==(const MusicClip& other) const = default;
    };

    // everything beyond cameras, splines and drives, compared and stored as one block
    struct Cinematic
    {
        std::vector<CurveKey> curves[static_cast<size_t>(Channel::Count)];
        std::vector<TitleEvent> titles;
        std::vector<Marker> markers;
        MusicClip music;
        float bpm            = 0.0f; // 0 has no beat grid
        float beat_offset    = 0.0f; // timeline seconds of beat 1
        uint32_t beats_per_bar = 4;
        bool snap_to_beats   = true;
        bool slowmo_audio    = true; // world audio muffles and pitches down with the time scale
        spartan::math::Vector3 fade_color = spartan::math::Vector3::Zero;
        bool operator==(const Cinematic& other) const;
    };

    // the value of every track at one moment, for previews and the agent's timing checks
    struct TrackSample
    {
        float time       = 0.0f;
        int shot         = -1;
        float values[static_cast<size_t>(Channel::Count)] = {};
        bool keyed[static_cast<size_t>(Channel::Count)]   = {};
        std::vector<int> titles; // indices of visible titles
        float beat       = 0.0f; // 1 based, 0 without a grid
    };

    static const char* GetChannelName(Channel channel);
    static std::optional<Channel> FindChannel(const std::string& name);
    static TrackGroup GetChannelGroup(Channel channel);
    static float GetChannelDefault(Channel channel);
    static const char* GetEaseName(ShotEase ease);
    static std::optional<ShotEase> FindEase(const std::string& name);
    static const char* GetMoveName(ShotMove move);
    static std::optional<ShotMove> FindMove(const std::string& name);
    static bool IsAnimated(const CameraEvent& event) { return event.rig || event.move != ShotMove::Rig; }

    // a full snapshot of the editable state, used by the undo/redo command
    struct State
    {
        float duration = 10.0f;
        bool loop      = false;
        std::vector<CameraEvent> events;
        std::vector<SplineEvent> spline_events;
        std::vector<DriveEvent> drive_events;
        Cinematic cinematic;
    };

    // restores a snapshot into the widget and persists it, used by the undo/redo command
    void ApplyState(const State& state);

    // the sequence being edited, a world opens its own sequencer_<world>.xml and any other can be loaded over it
    std::string GetFilePath() const;
    void LoadSequence(const std::string& file_path);
    std::vector<std::string> GetSequenceFiles() const;

    // offline frame sequence capture
    struct RenderRequest
    {
        float fps     = 30.0f;
        float start   = 0.0f;
        float end     = -1.0f; // below zero means the sequence end
        float preroll = 1.0f;  // seconds simulated before time zero so staged cars settle
        uint32_t stride = 1;   // keep every nth frame, for quick previews
        std::string directory;
        std::vector<float> capture_times; // when set only these moments are written, as capture_NN.png
    };

    struct RenderStatus
    {
        bool active             = false;
        bool capture            = false; // a still capture rather than a frame sequence
        bool scrubbed           = false; // the capture jumped the playhead instead of simulating
        uint32_t frames_written = 0;
        uint32_t frames_total   = 0;
        float fps               = 0.0f;
        std::string directory;
        std::string last_error;
        std::vector<std::string> files;  // capture paths, in the order of the requested times
        std::vector<float> times;
    };

    // stills at chosen moments, simulate replays the take from zero so physics drives are where they will be,
    // scrub jumps the playhead which is instant but only exact for shots without simulated motion
    struct CaptureRequest
    {
        std::vector<float> times;
        std::string directory;
        std::string mode = "auto"; // auto, simulate or scrub
        float fps        = 30.0f;  // simulation step of a simulated capture
    };

    // plain view of the timeline for drivers outside the panel
    struct Snapshot
    {
        std::vector<CameraEvent> events;
        std::vector<SplineEvent> spline_events;
        std::vector<DriveEvent> drive_events;
        std::vector<DriveTelemetry> drive_telemetry;
        RenderStatus render;
        Cinematic cinematic;
        float duration = 0.0f;
        float time     = 0.0f;
        bool playing   = false;
        bool loop      = false;
        bool preview   = false;
        float time_scale = 1.0f; // what the time track applies right now
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
    bool StartCapture(const CaptureRequest& request, std::string& error);
    Snapshot GetSnapshot() const;

    // keyed tracks, a key at the same time and channel is replaced rather than duplicated
    void SetCurveKey(Channel channel, const CurveKey& key);
    bool UpdateCurveKey(Channel channel, int index, const CurveKey& key);
    bool RemoveCurveKey(Channel channel, int index);
    void ClearCurve(Channel channel);
    void AddTitle(const TitleEvent& title);
    bool UpdateTitle(int index, const TitleEvent& title);
    bool RemoveTitle(int index);
    void ClearTitles();
    void SetMarker(const Marker& marker);
    bool RemoveMarker(int index);
    void ClearMarkers();
    // music, beat grid, fade color and the slow motion audio switch, everything in the block except keys, titles and markers
    void SetCinematicSettings(const Cinematic& settings);

    // the evaluated value of every track at a time, without touching the world
    TrackSample SampleTracks(float time) const;
    // numbers, "beat:N", "bar:N" or "marker:NAME", each optionally followed by +seconds or -seconds
    std::optional<float> ResolveTime(const std::string& text, std::string& error) const;
    float GetBeatLength() const;

    // the car and the entity that actually moves for any entity inside a car's hierarchy, or the entity itself
    static spartan::Car* FindCar(spartan::Entity* entity);
    static spartan::Entity* GetMovingEntity(spartan::Entity* entity);
    // what a timeline should store for an entity: a car's vehicle child gets a new id on every load, its prefab owner does not
    static spartan::Entity* GetPersistentEntity(spartan::Entity* entity);
    // the shared camera animated shots drive, found by name or created on demand
    static spartan::Entity* GetOrCreateShotCamera();

private:
    // the camera pose a shot produces at the current time, before the blend from the previous shot
    struct ShotPose
    {
        spartan::math::Vector3 position    = spartan::math::Vector3::Zero;
        spartan::math::Quaternion rotation = spartan::math::Quaternion::Identity;
        float fov                          = 0.0f; // 0 keeps the camera's own
        spartan::math::Vector3 look        = spartan::math::Vector3::Zero;
        bool has_look                      = false;
    };

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
        bool revving         = false; // the gearbox is held in manual neutral, handed back to automatic after
        bool burning         = false; // the gearbox is held in manual first for a burnout, handed back to automatic after
        DriveTelemetry telemetry;
    };

    void OnWorldTicked();
    void Evaluate(float delta_time);
    void EvaluateCamera(int index, bool cut);
    // stateful shots update the heading lag, a previous shot sampled for a blend does not
    ShotPose EvaluateShotPose(const CameraEvent& event, int index, float time, bool cut, bool stateful, spartan::Entity* camera);
    void EvaluateDrives(float delta_time);
    void StageDrives();
    bool WaitForDrives(float delta_time);
    void ReleaseDrives();
    bool BuildDrivePath(const DriveEvent& event, DriveRuntime& runtime);
    void TickRender();
    void FinishRender();
    void TickScrubCapture();
    void FinishScrubCapture();

    // tracks, SequencerTracks.cpp
    float EvaluateCurve(Channel channel, float time, bool* keyed = nullptr) const;
    void EvaluateTracks(bool previewing);
    void ApplyTimeTrack(bool previewing);
    void ApplyAudioTrack(bool previewing);
    void ApplyWorldTrack(bool previewing);
    void ApplyOverlayTrack(bool previewing);
    void ApplyMusic(bool previewing);
    void RestoreTracks();
    bool HasKeys(TrackGroup group) const;
    float SnapTime(float time) const;
    void SaveTracks(pugi::xml_node& root) const;
    void LoadTracks(const pugi::xml_node& root);
    void DrawTrackLanes(float origin_x, float track_y, float pixels_per_sec);
    void DrawCurveLane(TrackGroup group, float origin_x, float track_y, float pixels_per_sec);
    void DrawTitleLane(float origin_x, float track_y, float pixels_per_sec);
    void DrawBeatGrid(float origin_x, float top, float bottom, float pixels_per_sec);
    void DrawCurveKeyInspector(float width);
    void DrawTitleInspector(float width);
    void DrawCinematicInspector(float width);
    void DrawTrackToolbar();
    void DeselectAll();
    float GetTrackLanesHeight() const;
    bool HasSimulatedMotion() const;

    // what a drag is moving, so snapping never pulls an item onto its own edges
    enum class SnapSource : uint8_t
    {
        Camera,
        Spline,
        Drive,
        Title,
        Curve,
        Marker
    };

    void DrawToolbar();
    void DrawAddMenu();
    void DrawTimeline();
    void DrawRuler(float origin_x, float top, float pixels_per_sec);
    void DrawMarkerStrip(float origin_x, float top, float pixels_per_sec);
    void DrawTrackHeaders(float x, float top);
    void DrawZoomBar(float x, float y, float width);
    void HandleViewInput(float lane_bottom);
    void HandleShortcuts();
    void FitView();
    void JumpToCut(int direction);
    float SnapDrag(float time, SnapSource source, int index, bool* snapped = nullptr);
    float SnapWindow(float start, float span, SnapSource source, int index);
    float TimeToX(float time) const { return m_lane_x + (time - m_view_start) * m_pixels_per_sec; }
    float XToTime(float x) const { return m_view_start + (x - m_lane_x) / m_pixels_per_sec; }
    void DrawCameraTrack(float origin_x, float track_y, float pixels_per_sec);
    void DrawSplineTrack(float origin_x, float track_y, float pixels_per_sec);
    void DrawDriveTrack(float origin_x, float track_y, float pixels_per_sec);
    void DrawPopups();
    void DrawInspector();
    void DrawShotInspector(float width);
    void DrawDriveInspector(float width);
    void ClampToDuration();
    void AddCameraAtPlayhead();
    void AddMotionAtPlayhead();
    void AddDriveAtPlayhead();
    void AddMarkerAtPlayhead();
    void AddTitleAtPlayhead();
    void DeleteSelectedCamera();
    void DeleteSelectedSpline();
    void DeleteSelectedDrive();
    void DuplicateSelectedCamera();
    void DuplicateSelectedSpline();
    int GetEventIndexAtTime(float time) const;
    void Save() const;
    void Load();
    std::string GetDefaultFilePath() const;
    void OnWorldLoaded();
    void DrawSequencePicker(float width);
    void DrawSequenceDropTarget();
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
    uint32_t m_render_finish_wait = 0; // frames left before finishing, the last screenshot is taken on the next rendered frame
    uint32_t m_render_save_failures_start = 0;
    float m_render_fps_ui         = 30.0f;

    State m_drag_undo_state; // captured when a drag begins, committed when it ends

    // keyed tracks
    Cinematic m_cinematic;
    bool m_tracks_applied        = false; // the tracks drove the world last frame and must hand it back
    bool m_world_snapshot_valid  = false;
    double m_world_time_scale    = 0.0;   // the environment's own day speed, pinned while time of day is keyed
    float m_world_rain           = 0.0f;
    float m_world_clouds         = 0.0f;
    float m_world_puddles        = 0.0f;
    spartan::math::Vector3 m_world_wind = spartan::math::Vector3::Zero;
    double m_world_days          = 0.0;
    uint32_t m_world_applied     = 0;     // bit per world channel the track has overridden since the snapshot
    float m_applied_time_scale   = 1.0f;
    uint64_t m_music_entity_id   = 0;
    std::string m_music_loaded_path;

    // curve and title selection, a curve key is addressed by its channel and index
    int m_curve_channel          = -1;
    int m_curve_selected         = -1;
    int m_curve_dragging         = -1;
    float m_curve_drag_offset    = 0.0f;
    bool m_curve_drag_moved      = false;
    int m_title_selected         = -1;
    int m_title_dragging         = -1;
    int m_title_drag_edge        = 0;
    float m_title_drag_offset    = 0.0f;
    bool m_title_drag_moved      = false;
    int m_marker_dragging        = -1;
    int m_marker_selected        = -1;
    float m_marker_drag_offset   = 0.0f;
    bool m_marker_drag_moved     = false;
    bool m_show_cinematic        = false; // the inspector shows the music, beat grid and overlay settings
    float m_lane_popup_time      = 0.0f;
    int m_lane_popup_group       = 0;

    // shot blending, the pose the previous shot left the camera in when the cut happened
    ShotPose m_last_pose;
    ShotPose m_blend_from;
    bool m_last_pose_valid       = false;
    float m_blend_start_time     = 0.0f;

    // scrubbed capture, jumps the playhead to each time and waits for the image to settle
    std::vector<float> m_scrub_times;
    size_t m_scrub_index         = 0;
    int m_scrub_wait             = 0;
    bool m_scrub_was_preview     = false;
    float m_scrub_was_time       = 0.0f;

    // timeline view, the visible window eases towards its target so zooming glides
    float m_view_start           = 0.0f;
    float m_view_span            = 0.0f; // 0 fits the whole sequence
    float m_view_start_target    = 0.0f;
    float m_view_span_target     = 0.0f;
    bool m_view_panning          = false;
    float m_lane_x               = 0.0f; // screen rect of the visible lanes, refreshed every frame
    float m_lane_w               = 1.0f;
    float m_lane_top             = 0.0f;
    float m_lane_bottom          = 0.0f;
    float m_pixels_per_sec       = 1.0f;
    float m_snap_guide           = -1.0f; // the time a drag snapped to this frame, drawn as a guide
    float m_drag_readout         = -1.0f; // the time a drag is at, shown next to the cursor
    bool m_zoom_dragging         = false;
    float m_zoom_drag_offset     = 0.0f;
    int m_lane_popup_request     = -1;    // a header asked a curve lane to open its add menu
    char m_marker_name[64]       = {};
    std::string m_file_path;              // empty means the world's own sequence
    char m_sequence_name[64]     = {};
    bool m_sequence_copy         = false; // the name popup saves a copy instead of starting an empty sequence

    uint64_t m_world_loaded_handle = 0;
    uint64_t m_world_ticked_handle = 0;
};
