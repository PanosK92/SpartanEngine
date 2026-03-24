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

//= INCLUDES ==================
#include "Core/SpartanObject.h"
#include <vector>
#include <string>
#include "Math/Vector3.h"
#include "Math/Quaternion.h"
//=============================

namespace pugi { class xml_node; }

namespace spartan
{
    enum class SequenceTrackType : uint8_t
    {
        CameraCut,
        Transform,
        Event,
        Max
    };

    enum class SequenceEventAction : uint8_t
    {
        CarEnter,
        CarExit,
        SetSplineFollowerSpeed,
        PlayAudio,
        StopAudio,
        Max
    };

    struct SequenceKeyframe
    {
        float            time     = 0.0f;
        math::Vector3    position = math::Vector3::Zero;
        math::Quaternion rotation = math::Quaternion::Identity;
        float            value    = 0.0f;
    };

    struct SequenceCameraCutClip
    {
        float    start_time        = 0.0f;
        float    end_time          = 1.0f;
        uint64_t camera_entity_id  = 0;
        float    transition_in     = 0.0f; // blend duration from previous clip
    };

    struct SequenceEventClip
    {
        float                time             = 0.0f;
        SequenceEventAction  action           = SequenceEventAction::Max;
        uint64_t             target_entity_id = 0;
        float                parameter        = 0.0f;
    };

    struct SequenceTrack
    {
        SequenceTrackType                  type             = SequenceTrackType::Max;
        uint64_t                           target_entity_id = 0;
        std::string                        name;
        std::vector<SequenceKeyframe>      keyframes;
        std::vector<SequenceCameraCutClip> camera_clips;
        std::vector<SequenceEventClip>     event_clips;
    };

    class Sequence : public SpartanObject
    {
    public:
        Sequence();
        ~Sequence() = default;

        void Tick();

        // serialization
        void Save(pugi::xml_node& node);
        void Load(pugi::xml_node& node);

        // name
        const std::string& GetName() const     { return m_object_name; }
        void SetName(const std::string& name)  { m_object_name = name; }
        uint64_t GetId() const                 { return m_object_id; }

        // playback control
        void Play();
        void Pause();
        void StopPlayback();
        bool IsPlaying() const { return m_playing; }
        bool IsPaused() const  { return m_paused; }

        // timeline
        float GetDuration() const          { return m_duration; }
        void SetDuration(float duration)   { m_duration = duration; }
        float GetPlaybackTime() const      { return m_playback_time; }
        void SetPlaybackTime(float time);  
        float GetPlaybackSpeed() const     { return m_playback_speed; }
        void SetPlaybackSpeed(float speed) { m_playback_speed = speed; }
        bool IsLooping() const             { return m_looping; }
        void SetLooping(bool looping)      { m_looping = looping; }

        // tracks
        const std::vector<SequenceTrack>& GetTracks() const { return m_tracks; }
        std::vector<SequenceTrack>& GetTracks()             { return m_tracks; }
        uint32_t AddTrack(SequenceTrackType type, uint64_t target_entity_id = 0, const std::string& name = "");
        void RemoveTrack(uint32_t index);

        // camera override (set during playback by camera cut tracks)
        uint64_t GetActiveCameraEntityId() const { return m_active_camera_entity_id; }

    private:
        void Evaluate(float time);
        void EvaluateCameraCutTrack(const SequenceTrack& track, float time);
        void EvaluateTransformTrack(const SequenceTrack& track, float time);
        void EvaluateEventTrack(const SequenceTrack& track, float time);
        void ResetEventTracking();

        float m_duration       = 10.0f;
        float m_playback_time  = 0.0f;
        float m_playback_speed = 1.0f;
        bool  m_playing        = false;
        bool  m_paused         = false;
        bool  m_looping        = false;

        std::vector<SequenceTrack> m_tracks;

        // camera cut evaluation result
        uint64_t m_active_camera_entity_id = 0;

        // per-event-track index of the last fired event (to avoid re-firing)
        std::vector<int32_t> m_event_fire_indices;
    };
}
