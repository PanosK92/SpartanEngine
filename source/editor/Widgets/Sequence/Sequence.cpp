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

//= INCLUDES ==========================
#include "pch.h"
#include "Sequence.h"
#include "World/Entity.h"
#include "World/World.h"
#include "World/Components/Camera.h"
#include "World/Components/SplineFollower.h"
#include "World/Components/AudioSource.h"
#include "Car/Car.h"
SP_WARNINGS_OFF
#include "IO/pugixml.hpp"
SP_WARNINGS_ON
//=====================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        const char* track_type_to_string(SequenceTrackType type)
        {
            switch (type)
            {
                case SequenceTrackType::CameraCut: return "camera_cut";
                case SequenceTrackType::Transform: return "transform";
                case SequenceTrackType::Event:     return "event";
                default:                           return "unknown";
            }
        }

        SequenceTrackType string_to_track_type(const string& str)
        {
            if (str == "camera_cut") return SequenceTrackType::CameraCut;
            if (str == "transform")  return SequenceTrackType::Transform;
            if (str == "event")      return SequenceTrackType::Event;
            return SequenceTrackType::Max;
        }

        const char* event_action_to_string(SequenceEventAction action)
        {
            switch (action)
            {
                case SequenceEventAction::CarEnter:               return "car_enter";
                case SequenceEventAction::CarExit:                return "car_exit";
                case SequenceEventAction::SetSplineFollowerSpeed: return "set_spline_follower_speed";
                case SequenceEventAction::PlayAudio:              return "play_audio";
                case SequenceEventAction::StopAudio:              return "stop_audio";
                default:                                          return "unknown";
            }
        }

        SequenceEventAction string_to_event_action(const string& str)
        {
            if (str == "car_enter")                 return SequenceEventAction::CarEnter;
            if (str == "car_exit")                  return SequenceEventAction::CarExit;
            if (str == "set_spline_follower_speed") return SequenceEventAction::SetSplineFollowerSpeed;
            if (str == "play_audio")                return SequenceEventAction::PlayAudio;
            if (str == "stop_audio")                return SequenceEventAction::StopAudio;
            return SequenceEventAction::Max;
        }

        // catmull-rom interpolation for positions
        Vector3 catmull_rom(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t)
        {
            float t2 = t * t;
            float t3 = t2 * t;

            return 0.5f * (
                (2.0f * p1) +
                (-p0 + p2) * t +
                (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3
            );
        }
    }

    Sequence::Sequence()
    {
        m_object_name = "new sequence";
    }

    void Sequence::Tick()
    {
        if (!m_playing || m_paused)
            return;

        float delta = static_cast<float>(Timer::GetDeltaTimeSec()) * m_playback_speed;
        m_playback_time += delta;

        if (m_playback_time >= m_duration)
        {
            if (m_looping)
            {
                m_playback_time = fmodf(m_playback_time, m_duration);
                ResetEventTracking();
            }
            else
            {
                m_playback_time = m_duration;
                StopPlayback();
            }
        }

        Evaluate(m_playback_time);
    }

    void Sequence::Play()
    {
        if (m_paused)
        {
            m_paused = false;
            return;
        }

        m_playing       = true;
        m_paused        = false;
        m_playback_time = 0.0f;
        ResetEventTracking();
    }

    void Sequence::Pause()
    {
        if (m_playing)
        {
            m_paused = true;
        }
    }

    void Sequence::StopPlayback()
    {
        m_playing       = false;
        m_paused        = false;
        m_playback_time = 0.0f;
        m_active_camera_entity_id = 0;
        ResetEventTracking();
    }

    void Sequence::SetPlaybackTime(float time)
    {
        m_playback_time = max(0.0f, min(time, m_duration));

        // when scrubbing, re-evaluate but skip events
        Evaluate(m_playback_time);
    }

    uint32_t Sequence::AddTrack(SequenceTrackType type, uint64_t target_entity_id, const string& name)
    {
        SequenceTrack track;
        track.type             = type;
        track.target_entity_id = target_entity_id;
        track.name             = name.empty() ? track_type_to_string(type) : name;
        m_tracks.push_back(track);
        return static_cast<uint32_t>(m_tracks.size() - 1);
    }

    void Sequence::RemoveTrack(uint32_t index)
    {
        if (index < static_cast<uint32_t>(m_tracks.size()))
        {
            m_tracks.erase(m_tracks.begin() + index);
        }
    }

    void Sequence::Evaluate(float time)
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_tracks.size()); i++)
        {
            const SequenceTrack& track = m_tracks[i];

            switch (track.type)
            {
                case SequenceTrackType::CameraCut: EvaluateCameraCutTrack(track, time); break;
                case SequenceTrackType::Transform: EvaluateTransformTrack(track, time); break;
                case SequenceTrackType::Event:     EvaluateEventTrack(track, time);     break;
                default: break;
            }
        }
    }

    void Sequence::EvaluateCameraCutTrack(const SequenceTrack& track, float time)
    {
        if (track.camera_clips.empty())
            return;

        // find the active clip
        const SequenceCameraCutClip* active_clip = nullptr;
        for (const auto& clip : track.camera_clips)
        {
            if (time >= clip.start_time && time < clip.end_time)
            {
                active_clip = &clip;
                break;
            }
        }

        if (!active_clip)
        {
            m_active_camera_entity_id = 0;
            return;
        }

        Entity* camera_entity = World::GetEntityById(active_clip->camera_entity_id);
        if (!camera_entity)
        {
            m_active_camera_entity_id = 0;
            return;
        }

        Camera* camera_comp = camera_entity->GetComponent<Camera>();
        if (!camera_comp)
        {
            m_active_camera_entity_id = 0;
            return;
        }

        m_active_camera_entity_id = active_clip->camera_entity_id;
    }

    void Sequence::EvaluateTransformTrack(const SequenceTrack& track, float time)
    {
        if (track.keyframes.size() < 2)
            return;

        Entity* entity = World::GetEntityById(track.target_entity_id);
        if (!entity)
            return;

        const auto& kfs = track.keyframes;

        // clamp to range
        if (time <= kfs.front().time)
        {
            entity->SetPosition(kfs.front().position);
            entity->SetRotation(kfs.front().rotation);
            return;
        }
        if (time >= kfs.back().time)
        {
            entity->SetPosition(kfs.back().position);
            entity->SetRotation(kfs.back().rotation);
            return;
        }

        // find the segment
        uint32_t seg = 0;
        for (uint32_t i = 0; i < static_cast<uint32_t>(kfs.size()) - 1; i++)
        {
            if (time >= kfs[i].time && time < kfs[i + 1].time)
            {
                seg = i;
                break;
            }
        }

        float segment_duration = kfs[seg + 1].time - kfs[seg].time;
        float t = (segment_duration > 0.0f) ? (time - kfs[seg].time) / segment_duration : 0.0f;

        // catmull-rom for position (clamp boundary control points)
        uint32_t i0 = (seg > 0) ? seg - 1 : 0;
        uint32_t i1 = seg;
        uint32_t i2 = seg + 1;
        uint32_t i3 = (seg + 2 < static_cast<uint32_t>(kfs.size())) ? seg + 2 : static_cast<uint32_t>(kfs.size()) - 1;

        Vector3 position = catmull_rom(kfs[i0].position, kfs[i1].position, kfs[i2].position, kfs[i3].position, t);
        entity->SetPosition(position);

        // normalized lerp for rotation
        Quaternion rotation = Quaternion::Lerp(kfs[i1].rotation, kfs[i2].rotation, t);
        entity->SetRotation(rotation);
    }

    void Sequence::EvaluateEventTrack(const SequenceTrack& track, float time)
    {
        if (track.event_clips.empty())
            return;

        // find the track's index in m_tracks so we can use the matching fire index
        uint32_t track_index = 0;
        uint32_t event_track_counter = 0;
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_tracks.size()); i++)
        {
            if (&m_tracks[i] == &track)
            {
                track_index = event_track_counter;
                break;
            }
            if (m_tracks[i].type == SequenceTrackType::Event)
            {
                event_track_counter++;
            }
        }

        // ensure fire index array is large enough
        while (track_index >= static_cast<uint32_t>(m_event_fire_indices.size()))
        {
            m_event_fire_indices.push_back(-1);
        }

        int32_t& last_fired = m_event_fire_indices[track_index];

        for (uint32_t i = static_cast<uint32_t>(max(0, last_fired + 1)); i < static_cast<uint32_t>(track.event_clips.size()); i++)
        {
            const SequenceEventClip& evt = track.event_clips[i];

            if (evt.time > time)
                break;

            // fire this event
            Entity* target = World::GetEntityById(evt.target_entity_id);

            switch (evt.action)
            {
                case SequenceEventAction::CarEnter:
                {
                    if (target)
                    {
                        for (Car* car : Car::GetAll())
                        {
                            if (car->GetRootEntity() == target)
                            {
                                car->Enter();
                                break;
                            }
                        }
                    }
                } break;

                case SequenceEventAction::CarExit:
                {
                    if (target)
                    {
                        for (Car* car : Car::GetAll())
                        {
                            if (car->GetRootEntity() == target)
                            {
                                car->Exit();
                                break;
                            }
                        }
                    }
                } break;

                case SequenceEventAction::SetSplineFollowerSpeed:
                {
                    if (target)
                    {
                        if (SplineFollower* follower = target->GetComponent<SplineFollower>())
                        {
                            follower->SetSpeed(evt.parameter);
                        }
                    }
                } break;

                case SequenceEventAction::PlayAudio:
                {
                    if (target)
                    {
                        if (AudioSource* audio = target->GetComponent<AudioSource>())
                        {
                            audio->PlayClip();
                        }
                    }
                } break;

                case SequenceEventAction::StopAudio:
                {
                    if (target)
                    {
                        if (AudioSource* audio = target->GetComponent<AudioSource>())
                        {
                            audio->StopClip();
                        }
                    }
                } break;

                default:
                    break;
            }

            last_fired = static_cast<int32_t>(i);
        }
    }

    void Sequence::ResetEventTracking()
    {
        m_event_fire_indices.clear();
    }

    void Sequence::Save(pugi::xml_node& node)
    {
        node.append_attribute("name")           = m_object_name.c_str();
        node.append_attribute("id")             = m_object_id;
        node.append_attribute("duration")       = m_duration;
        node.append_attribute("playback_speed") = m_playback_speed;
        node.append_attribute("looping")        = m_looping;

        for (const SequenceTrack& track : m_tracks)
        {
            pugi::xml_node track_node = node.append_child("track");
            track_node.append_attribute("type")             = track_type_to_string(track.type);
            track_node.append_attribute("target_entity_id") = track.target_entity_id;
            track_node.append_attribute("name")             = track.name.c_str();

            switch (track.type)
            {
                case SequenceTrackType::CameraCut:
                {
                    for (const auto& clip : track.camera_clips)
                    {
                        pugi::xml_node clip_node = track_node.append_child("clip");
                        clip_node.append_attribute("start")            = clip.start_time;
                        clip_node.append_attribute("end")              = clip.end_time;
                        clip_node.append_attribute("camera_entity_id") = clip.camera_entity_id;
                        clip_node.append_attribute("transition_in")    = clip.transition_in;
                    }
                } break;

                case SequenceTrackType::Transform:
                {
                    for (const auto& kf : track.keyframes)
                    {
                        pugi::xml_node kf_node = track_node.append_child("keyframe");
                        kf_node.append_attribute("time") = kf.time;

                        stringstream ss_pos;
                        ss_pos << kf.position.x << " " << kf.position.y << " " << kf.position.z;
                        kf_node.append_attribute("position") = ss_pos.str().c_str();

                        stringstream ss_rot;
                        ss_rot << kf.rotation.x << " " << kf.rotation.y << " " << kf.rotation.z << " " << kf.rotation.w;
                        kf_node.append_attribute("rotation") = ss_rot.str().c_str();
                    }
                } break;

                case SequenceTrackType::Event:
                {
                    for (const auto& evt : track.event_clips)
                    {
                        pugi::xml_node evt_node = track_node.append_child("event");
                        evt_node.append_attribute("time")             = evt.time;
                        evt_node.append_attribute("action")           = event_action_to_string(evt.action);
                        evt_node.append_attribute("target_entity_id") = evt.target_entity_id;
                        evt_node.append_attribute("parameter")        = evt.parameter;
                    }
                } break;

                default:
                    break;
            }
        }
    }

    void Sequence::Load(pugi::xml_node& node)
    {
        m_object_name    = node.attribute("name").as_string("new sequence");
        m_object_id      = node.attribute("id").as_ullong(m_object_id);
        m_duration       = node.attribute("duration").as_float(10.0f);
        m_playback_speed = node.attribute("playback_speed").as_float(1.0f);
        m_looping        = node.attribute("looping").as_bool(false);

        m_tracks.clear();

        for (pugi::xml_node track_node = node.child("track"); track_node; track_node = track_node.next_sibling("track"))
        {
            SequenceTrack track;
            track.type             = string_to_track_type(track_node.attribute("type").as_string());
            track.target_entity_id = track_node.attribute("target_entity_id").as_ullong(0);
            track.name             = track_node.attribute("name").as_string();

            switch (track.type)
            {
                case SequenceTrackType::CameraCut:
                {
                    for (pugi::xml_node clip_node = track_node.child("clip"); clip_node; clip_node = clip_node.next_sibling("clip"))
                    {
                        SequenceCameraCutClip clip;
                        clip.start_time       = clip_node.attribute("start").as_float(0.0f);
                        clip.end_time         = clip_node.attribute("end").as_float(1.0f);
                        clip.camera_entity_id = clip_node.attribute("camera_entity_id").as_ullong(0);
                        clip.transition_in    = clip_node.attribute("transition_in").as_float(0.0f);
                        track.camera_clips.push_back(clip);
                    }
                } break;

                case SequenceTrackType::Transform:
                {
                    for (pugi::xml_node kf_node = track_node.child("keyframe"); kf_node; kf_node = kf_node.next_sibling("keyframe"))
                    {
                        SequenceKeyframe kf;
                        kf.time = kf_node.attribute("time").as_float(0.0f);

                        {
                            string pos_str = kf_node.attribute("position").as_string();
                            stringstream ss(pos_str);
                            ss >> kf.position.x >> kf.position.y >> kf.position.z;
                        }

                        {
                            string rot_str = kf_node.attribute("rotation").as_string();
                            stringstream ss(rot_str);
                            ss >> kf.rotation.x >> kf.rotation.y >> kf.rotation.z >> kf.rotation.w;
                        }

                        track.keyframes.push_back(kf);
                    }
                } break;

                case SequenceTrackType::Event:
                {
                    for (pugi::xml_node evt_node = track_node.child("event"); evt_node; evt_node = evt_node.next_sibling("event"))
                    {
                        SequenceEventClip evt;
                        evt.time             = evt_node.attribute("time").as_float(0.0f);
                        evt.action           = string_to_event_action(evt_node.attribute("action").as_string());
                        evt.target_entity_id = evt_node.attribute("target_entity_id").as_ullong(0);
                        evt.parameter        = evt_node.attribute("parameter").as_float(0.0f);
                        track.event_clips.push_back(evt);
                    }
                } break;

                default:
                    break;
            }

            m_tracks.push_back(track);
        }
    }
}
