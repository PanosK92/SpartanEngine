/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===========================
#include "pch.h"
#include "Sequencer.h"
#include "SequencerCommon.h"
#include <algorithm>
#include "world/World.h"
#include "world/Entity.h"
#include "world/Environment.h"
#include "world/components/AudioSource.h"
#include "audio/AudioMixer.h"
#include "rendering/Renderer.h"
#include "rhi/RHI_Viewport.h"
#include "file_system/FileSystem.h"
#include "../imgui/ImGui_EditorUi.h"
#include "../imgui/ImGui_Extension.h"
#include "../imgui/ImGui_Style.h"
SP_WARNINGS_OFF
#include "io/pugixml.hpp"
SP_WARNINGS_ON
//======================================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
using namespace spartan::math;
using namespace sequencer_common;
//======================

namespace
{
    struct channel_info
    {
        const char* name;
        Sequencer::TrackGroup group;
        float default_value;
        float min_value;
        float max_value;
        const char* format;
        ImVec4 color;
    };

    using Group = Sequencer::TrackGroup;
    const channel_info channel_infos[] =
    {
        { "time_scale",    Group::Time,    1.0f,  0.0f, 16.0f, "x%.2f",     ImVec4(1.00f, 0.78f, 0.25f, 1.0f) },
        { "gain_vehicle",  Group::Audio,   1.0f,  0.0f, 4.0f,  "%.2f",      ImVec4(1.00f, 0.45f, 0.30f, 1.0f) },
        { "gain_ambience", Group::Audio,   1.0f,  0.0f, 4.0f,  "%.2f",      ImVec4(0.45f, 0.85f, 0.45f, 1.0f) },
        { "gain_sfx",      Group::Audio,   1.0f,  0.0f, 4.0f,  "%.2f",      ImVec4(0.55f, 0.70f, 1.00f, 1.0f) },
        { "gain_music",    Group::Audio,   1.0f,  0.0f, 4.0f,  "%.2f",      ImVec4(0.85f, 0.50f, 1.00f, 1.0f) },
        { "muffle",        Group::Audio,   0.0f,  0.0f, 1.0f,  "%.2f",      ImVec4(0.60f, 0.60f, 0.60f, 1.0f) },
        { "time_of_day",   Group::World,   12.0f, 0.0f, 48.0f, "%.2f h",    ImVec4(1.00f, 0.85f, 0.40f, 1.0f) },
        { "rain",          Group::World,   0.0f,  0.0f, 1.0f,  "%.2f",      ImVec4(0.40f, 0.65f, 1.00f, 1.0f) },
        { "clouds",        Group::World,   0.0f,  0.0f, 1.0f,  "%.2f",      ImVec4(0.80f, 0.80f, 0.85f, 1.0f) },
        { "wind",          Group::World,   0.0f,  0.0f, 60.0f, "%.1f m/s",  ImVec4(0.55f, 0.90f, 0.85f, 1.0f) },
        { "puddles",       Group::World,   0.0f,  0.0f, 1.0f,  "%.2f",      ImVec4(0.30f, 0.50f, 0.80f, 1.0f) },
        { "fade",          Group::Overlay, 0.0f,  0.0f, 1.0f,  "%.2f",      ImVec4(0.90f, 0.90f, 0.90f, 1.0f) },
        { "letterbox",     Group::Overlay, 0.0f,  0.0f, 4.0f,  "%.2f : 1",  ImVec4(0.95f, 0.60f, 0.35f, 1.0f) },
    };
    static_assert(size(channel_infos) == static_cast<size_t>(Sequencer::Channel::Count), "one entry per channel");

    const char* ease_names[]  = { "linear", "in_out", "in", "out", "hold" };
    const char* move_names[]  = { "rig", "orbit", "dolly_zoom", "path" };
    const char* group_names[] = { "time", "audio", "world", "overlay" };
    const char* music_entity_name = "sequencer_music";

    // world audio keeps some tone in a frozen moment, half pitch and most of the top end gone
    const float slowmo_pitch_floor  = 0.5f;
    const float slowmo_muffle_max   = 0.85f;
    // a music clip further than this from the playhead jumps instead of drifting back
    const float music_resync_seconds = 0.25f;

    const channel_info& info(Sequencer::Channel channel)
    {
        return channel_infos[static_cast<size_t>(channel)];
    }

    size_t index_of(Sequencer::Channel channel)
    {
        return static_cast<size_t>(channel);
    }

    string normalize_name(string name)
    {
        for (char& c : name)
        {
            c = c == '-' || c == ' ' ? '_' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
        }
        return name;
    }

    float clamp_channel(Sequencer::Channel channel, float value)
    {
        if (!isfinite(value))
        {
            return info(channel).default_value;
        }
        value = clamp(value, info(channel).min_value, info(channel).max_value);
        // a letterbox narrower than square has no bars, so anything that small means off
        if (channel == Sequencer::Channel::Letterbox && value < 1.0f)
        {
            value = 0.0f;
        }
        return value;
    }

    float screen_aspect()
    {
        const RHI_Viewport& viewport = Renderer::GetViewport();
        return viewport.height > 0.0f ? viewport.width / viewport.height : 16.0f / 9.0f;
    }

    void write_color(pugi::xml_node& node, const char* name, const Vector3& value)
    {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%.3f,%.3f,%.3f", value.x, value.y, value.z);
        node.append_attribute(name) = buffer;
    }

    Vector3 read_color(const pugi::xml_node& node, const char* name, const Vector3& fallback)
    {
        Vector3 value = fallback;
        if (const char* text = node.attribute(name).as_string(nullptr))
        {
            char* end = nullptr;
            value.x   = strtof(text, &end);
            value.y   = end && *end == ',' ? strtof(end + 1, &end) : value.x;
            value.z   = end && *end == ',' ? strtof(end + 1, &end) : value.x;
        }
        return value;
    }

    bool parse_float(const string& text, float& value)
    {
        if (text.empty())
        {
            return false;
        }
        char* end = nullptr;
        value     = strtof(text.c_str(), &end);
        return end && *end == '\0' && isfinite(value);
    }

    string trim(const string& text)
    {
        const size_t first = text.find_first_not_of(" \t");
        if (first == string::npos)
        {
            return "";
        }
        const size_t last = text.find_last_not_of(" \t");
        return text.substr(first, last - first + 1);
    }
}

ImVec4 sequencer_common::channel_color(Sequencer::Channel channel)
{
    return info(channel).color;
}

//= NAMES =======================================================================================================================

bool Sequencer::Cinematic::operator==(const Cinematic& other) const
{
    for (size_t i = 0; i < static_cast<size_t>(Channel::Count); i++)
    {
        if (curves[i] != other.curves[i])
        {
            return false;
        }
    }
    return titles == other.titles && markers == other.markers && music == other.music && bpm == other.bpm && beat_offset == other.beat_offset &&
        beats_per_bar == other.beats_per_bar && snap_to_beats == other.snap_to_beats && slowmo_audio == other.slowmo_audio && fade_color == other.fade_color;
}

const char* Sequencer::GetChannelName(Channel channel)
{
    return channel < Channel::Count ? info(channel).name : "unknown";
}

const char* Sequencer::GetTitleStyleName(TitleStyle style)
{
    switch (style)
    {
        case TitleStyle::Trailer: return "trailer";
        case TitleStyle::Legend:  return "legend";
        default:                  return "plain";
    }
}

optional<Sequencer::TitleStyle> Sequencer::FindTitleStyle(const string& name_in)
{
    const string name = normalize_name(name_in);
    if (name == "plain")
    {
        return TitleStyle::Plain;
    }
    if (name == "trailer" || name == "hero")
    {
        return TitleStyle::Trailer;
    }
    if (name == "legend" || name == "caption" || name == "callout")
    {
        return TitleStyle::Legend;
    }
    return nullopt;
}

optional<Sequencer::Channel> Sequencer::FindChannel(const string& name_in)
{
    const string name = normalize_name(name_in);
    for (size_t i = 0; i < static_cast<size_t>(Channel::Count); i++)
    {
        if (name == channel_infos[i].name)
        {
            return static_cast<Channel>(i);
        }
    }

    // the words a director would use
    struct alias { const char* name; Channel channel; };
    static const alias aliases[] =
    {
        { "time",         Channel::TimeScale    }, { "speed",    Channel::TimeScale    }, { "timescale", Channel::TimeScale    }, { "slowmo", Channel::TimeScale },
        { "vehicle",      Channel::GainVehicle  }, { "engine",   Channel::GainVehicle  }, { "car",       Channel::GainVehicle  }, { "cars",   Channel::GainVehicle },
        { "ambience",     Channel::GainAmbience }, { "ambient",  Channel::GainAmbience }, { "world",     Channel::GainAmbience },
        { "sfx",          Channel::GainSfx      }, { "effects",  Channel::GainSfx      },
        { "music",        Channel::GainMusic    },
        { "lowpass",      Channel::Muffle       }, { "low_pass", Channel::Muffle       },
        { "tod",          Channel::TimeOfDay    }, { "hour",     Channel::TimeOfDay    }, { "sun",       Channel::TimeOfDay    },
        { "cloud",        Channel::Clouds       }, { "cloud_coverage", Channel::Clouds },
        { "wind_speed",   Channel::Wind         },
        { "puddle",       Channel::Puddles      }, { "puddliness", Channel::Puddles    },
        { "fade_out",     Channel::Fade         }, { "fade_in",  Channel::Fade         },
        { "bars",         Channel::Letterbox    }, { "aspect",   Channel::Letterbox    },
    };
    for (const alias& entry : aliases)
    {
        if (name == entry.name)
        {
            return entry.channel;
        }
    }
    return nullopt;
}

Sequencer::TrackGroup Sequencer::GetChannelGroup(Channel channel)
{
    return info(channel).group;
}

float Sequencer::GetChannelDefault(Channel channel)
{
    return info(channel).default_value;
}

const char* Sequencer::GetEaseName(ShotEase ease)
{
    const size_t index = static_cast<size_t>(ease);
    return index < size(ease_names) ? ease_names[index] : "in_out";
}

optional<Sequencer::ShotEase> Sequencer::FindEase(const string& name_in)
{
    const string name = normalize_name(name_in);
    for (size_t i = 0; i < size(ease_names); i++)
    {
        if (name == ease_names[i])
        {
            return static_cast<ShotEase>(i);
        }
    }
    if (name == "smooth" || name == "ease_in_out" || name == "inout")
    {
        return ShotEase::InOut;
    }
    if (name == "ease_in")
    {
        return ShotEase::In;
    }
    if (name == "ease_out")
    {
        return ShotEase::Out;
    }
    if (name == "step" || name == "cut" || name == "constant")
    {
        return ShotEase::Hold;
    }
    return nullopt;
}

const char* Sequencer::GetMoveName(ShotMove move)
{
    const size_t index = static_cast<size_t>(move);
    return index < size(move_names) ? move_names[index] : "rig";
}

optional<Sequencer::ShotMove> Sequencer::FindMove(const string& name_in)
{
    const string name = normalize_name(name_in);
    for (size_t i = 0; i < size(move_names); i++)
    {
        if (name == move_names[i])
        {
            return static_cast<ShotMove>(i);
        }
    }
    if (name == "bullet_time" || name == "matrix" || name == "orbit_shot" || name == "panorama")
    {
        return ShotMove::Orbit;
    }
    if (name == "vertigo" || name == "dolly" || name == "dollyzoom" || name == "zolly")
    {
        return ShotMove::DollyZoom;
    }
    if (name == "spline" || name == "rail")
    {
        return ShotMove::Path;
    }
    return nullopt;
}

//= EVALUATION ==================================================================================================================

float Sequencer::EvaluateCurve(Channel channel, float time, bool* keyed) const
{
    const vector<CurveKey>& keys = m_cinematic.curves[index_of(channel)];
    if (keyed)
    {
        *keyed = !keys.empty();
    }
    if (keys.empty())
    {
        return info(channel).default_value;
    }
    if (time <= keys.front().time)
    {
        return keys.front().value;
    }
    if (time >= keys.back().time)
    {
        return keys.back().value;
    }

    size_t next = 1;
    while (next < keys.size() - 1 && keys[next].time <= time)
    {
        next++;
    }
    const CurveKey& a = keys[next - 1];
    const CurveKey& b = keys[next];
    if (b.ease == ShotEase::Hold || b.time <= a.time)
    {
        return a.value;
    }

    float from = a.value;
    float to   = b.value;
    // bars growing from nothing start at the screen's own shape, so they slide in for the whole segment
    if (channel == Channel::Letterbox && (from <= 0.0f) != (to <= 0.0f))
    {
        (from <= 0.0f ? from : to) = screen_aspect();
    }
    const float u = apply_ease(b.ease, (time - a.time) / (b.time - a.time));
    const float v = from + (to - from) * u;
    if (channel == Channel::Letterbox && v <= screen_aspect() + 0.001f && (a.value <= 0.0f || b.value <= 0.0f))
    {
        return 0.0f;
    }
    return v;
}

bool Sequencer::HasKeys(TrackGroup group) const
{
    for (size_t i = 0; i < static_cast<size_t>(Channel::Count); i++)
    {
        if (channel_infos[i].group == group && !m_cinematic.curves[i].empty())
        {
            return true;
        }
    }
    return group == TrackGroup::Overlay && !m_cinematic.titles.empty();
}

void Sequencer::EvaluateTracks(bool previewing)
{
    if (!previewing)
    {
        if (m_tracks_applied)
        {
            RestoreTracks();
        }
        ApplyMusic(false);
        return;
    }

    m_tracks_applied = true;
    ApplyTimeTrack(true);
    ApplyAudioTrack(true);
    ApplyWorldTrack(true);
    ApplyOverlayTrack(true);
    ApplyMusic(true);
}

void Sequencer::ApplyTimeTrack(bool previewing)
{
    bool keyed    = false;
    float scale   = EvaluateCurve(Channel::TimeScale, max(m_time, 0.0f), &keyed);
    // the preroll is there for staged cars to settle, it always runs at real time
    if (!previewing || m_time < 0.0f)
    {
        keyed = false;
    }
    scale = keyed ? clamp(scale, 0.0f, 16.0f) : 1.0f;
    if (keyed || m_applied_time_scale != 1.0f)
    {
        Timer::SetTimeScale(scale);
        m_applied_time_scale = scale;
    }
}

void Sequencer::ApplyAudioTrack(bool previewing)
{
    const bool slowmo = m_cinematic.slowmo_audio && m_applied_time_scale < 1.0f;
    if (!previewing || (!HasKeys(TrackGroup::Audio) && !slowmo))
    {
        AudioMixer::Reset();
        return;
    }

    const float t = max(m_time, 0.0f);
    AudioMixer::SetGain(AudioGroup::Vehicle, EvaluateCurve(Channel::GainVehicle, t));
    AudioMixer::SetGain(AudioGroup::Ambience, EvaluateCurve(Channel::GainAmbience, t));
    AudioMixer::SetGain(AudioGroup::Sfx, EvaluateCurve(Channel::GainSfx, t));
    AudioMixer::SetGain(AudioGroup::Music, EvaluateCurve(Channel::GainMusic, t));

    // the world muffles and drops in pitch with the time scale, the music stays untouched on top of it
    float muffle = EvaluateCurve(Channel::Muffle, t);
    float pitch  = 1.0f;
    if (slowmo)
    {
        muffle = max(muffle, (1.0f - m_applied_time_scale) * slowmo_muffle_max);
        pitch  = slowmo_pitch_floor + (1.0f - slowmo_pitch_floor) * m_applied_time_scale;
    }
    for (AudioGroup group : { AudioGroup::Vehicle, AudioGroup::Ambience, AudioGroup::Sfx })
    {
        AudioMixer::SetMuffle(group, muffle);
        AudioMixer::SetPitch(group, pitch);
    }
    AudioMixer::SetMuffle(AudioGroup::Music, 0.0f);
    AudioMixer::SetPitch(AudioGroup::Music, 1.0f);
}

void Sequencer::ApplyWorldTrack(bool previewing)
{
    if (!previewing || !HasKeys(TrackGroup::World))
    {
        // keys removed mid preview hand their channels back right away
        // only what the track overrode goes back, anything else may have been changed by someone else meanwhile
        if (m_world_snapshot_valid && m_world_applied != 0)
        {
            EnvironmentSettings settings = Environment::GetSettings();
            if (m_world_applied & (1u << 0))
            {
                settings.time_scale = m_world_time_scale;
                settings.utc_days   = m_world_days;
            }
            if (m_world_applied & (1u << 1))
            {
                settings.rain = m_world_rain;
            }
            if (m_world_applied & (1u << 2))
            {
                settings.cloud_coverage = m_world_clouds;
            }
            if (m_world_applied & (1u << 3))
            {
                settings.wind = m_world_wind;
            }
            if (m_world_applied & (1u << 4))
            {
                settings.puddliness = m_world_puddles;
            }
            Environment::SetSettings(settings);
            m_world_applied = 0;
        }
        m_world_snapshot_valid = false;
        return;
    }

    if (!m_world_snapshot_valid)
    {
        const EnvironmentSettings settings = Environment::GetSettings();
        m_world_time_scale     = settings.time_scale;
        m_world_days           = settings.utc_days;
        m_world_rain           = settings.rain;
        m_world_clouds         = settings.cloud_coverage;
        m_world_wind           = settings.wind;
        m_world_puddles        = settings.puddliness;
        m_world_applied        = 0;
        m_world_snapshot_valid = true;
    }

    const float t                = max(m_time, 0.0f);
    EnvironmentSettings settings = Environment::GetSettings();
    bool keyed                   = false;

    // local solar hours, 12 is the sun at its highest wherever the world is, keys past 24 run into the next day
    const float hours = EvaluateCurve(Channel::TimeOfDay, t, &keyed);
    if (keyed)
    {
        const double utc_hours = static_cast<double>(hours) - settings.longitude / 15.0;
        const double day_start = floor(m_world_days + 0.5) - 0.5;
        settings.utc_days      = day_start + utc_hours / 24.0;
        settings.time_scale    = 0.0;
        m_world_applied       |= 1u << 0;
    }
    const float rain = EvaluateCurve(Channel::Rain, t, &keyed);
    if (keyed)
    {
        settings.rain    = rain;
        m_world_applied |= 1u << 1;
    }
    const float clouds = EvaluateCurve(Channel::Clouds, t, &keyed);
    if (keyed)
    {
        settings.cloud_coverage = clouds;
        m_world_applied        |= 1u << 2;
    }
    const float wind = EvaluateCurve(Channel::Wind, t, &keyed);
    if (keyed)
    {
        // the speed is keyed, the direction stays what the world had
        Vector3 direction = m_world_wind;
        direction.y       = 0.0f;
        direction         = direction.LengthSquared() > 1e-6f ? direction.Normalized() : Vector3(1.0f, 0.0f, 0.0f);
        settings.wind     = direction * wind;
        m_world_applied  |= 1u << 3;
    }
    const float puddles = EvaluateCurve(Channel::Puddles, t, &keyed);
    if (keyed)
    {
        settings.puddliness = puddles;
        m_world_applied    |= 1u << 4;
    }
    Environment::SetSettings(settings);
}

void Sequencer::ApplyOverlayTrack(bool previewing)
{
    if (!previewing)
    {
        Renderer::SetCinematicOverlay(0.0f, Vector3::Zero, 0.0f);
        return;
    }

    const float t = max(m_time, 0.0f);
    Renderer::SetCinematicOverlay(EvaluateCurve(Channel::Fade, t), m_cinematic.fade_color, EvaluateCurve(Channel::Letterbox, t));

    for (const TitleEvent& title : m_cinematic.titles)
    {
        if (t < title.start_time || t > title.end_time || title.text.empty())
        {
            continue;
        }
        CinematicTitle card;
        card.text      = title.text.c_str();
        card.kicker    = title.kicker.c_str();
        card.position  = Vector2(title.x, title.y);
        card.color     = title.color;
        card.accent    = title.accent;
        card.style     = static_cast<CinematicTitleStyle>(title.style);
        card.large     = title.large;
        card.age       = t - title.start_time;
        card.remaining = title.end_time - t;
        card.fade      = title.fade;
        Renderer::DrawCinematicTitle(card);
    }
}

void Sequencer::ApplyMusic(bool previewing)
{
    const MusicClip& clip = m_cinematic.music;
    Entity* entity        = m_music_entity_id != 0 ? World::GetEntityById(m_music_entity_id) : nullptr;
    AudioSource* source   = entity ? entity->GetComponent<AudioSource>() : nullptr;

    // music follows real playback only, scrubbing, stills and offline renders are silent
    const bool audible = previewing && m_playing && !m_render_status.active && !clip.path.empty() && m_time >= clip.start;
    if (!audible)
    {
        if (source && source->IsPlaying())
        {
            source->StopClip();
        }
        return;
    }

    if (!entity)
    {
        entity = World::CreateEntity();
        entity->SetObjectName(music_entity_name);
        entity->SetTransient(true);
        m_music_entity_id = entity->GetObjectId();
        m_music_loaded_path.clear();
    }
    if (!source)
    {
        source = entity->AddComponent<AudioSource>();
        source->SetPlayOnStart(false);
        source->SetIs3d(false);
        source->SetLoop(false);
        source->SetGroup(AudioGroup::Music);
        m_music_loaded_path.clear();
    }
    if (m_music_loaded_path != clip.path)
    {
        source->SetAudioClip(clip.path);
        m_music_loaded_path = clip.path;
    }
    if (source->GetVolume() != clip.volume)
    {
        source->SetVolume(clip.volume);
    }

    const float desired = m_time - clip.start + clip.offset;
    const float length  = source->GetDuration();
    if (length > 0.0f && desired >= length)
    {
        if (source->IsPlaying())
        {
            source->StopClip();
        }
        return;
    }
    if (!source->IsPlaying())
    {
        source->PlayClip();
        source->SetPlaybackPosition(desired);
    }
    else if (fabsf(source->GetPlaybackPosition() - desired) > music_resync_seconds)
    {
        source->SetPlaybackPosition(desired);
    }
}

void Sequencer::RestoreTracks()
{
    if (m_tracks_applied || m_applied_time_scale != 1.0f)
    {
        Timer::SetTimeScale(1.0);
        m_applied_time_scale = 1.0f;
        AudioMixer::Reset();
    }
    ApplyWorldTrack(false);
    Renderer::SetCinematicOverlay(0.0f, Vector3::Zero, 0.0f);
    ApplyMusic(false);
    m_tracks_applied = false;
}

Sequencer::TrackSample Sequencer::SampleTracks(float time) const
{
    TrackSample sample;
    sample.time = time;
    sample.shot = GetEventIndexAtTime(max(time, 0.0f));
    for (size_t i = 0; i < static_cast<size_t>(Channel::Count); i++)
    {
        bool keyed        = false;
        sample.values[i]  = EvaluateCurve(static_cast<Channel>(i), time, &keyed);
        sample.keyed[i]   = keyed;
    }
    for (int i = 0; i < static_cast<int>(m_cinematic.titles.size()); i++)
    {
        const TitleEvent& title = m_cinematic.titles[i];
        if (time >= title.start_time && time <= title.end_time)
        {
            sample.titles.push_back(i);
        }
    }
    const float beat_length = GetBeatLength();
    sample.beat             = beat_length > 0.0f ? (time - m_cinematic.beat_offset) / beat_length + 1.0f : 0.0f;
    return sample;
}

//= TIME ========================================================================================================================

float Sequencer::GetBeatLength() const
{
    return m_cinematic.bpm > 0.0f ? 60.0f / m_cinematic.bpm : 0.0f;
}

float Sequencer::SnapTime(float time) const
{
    const float beat_length = GetBeatLength();
    if (!m_cinematic.snap_to_beats || beat_length <= 0.0f || ImGui::GetIO().KeyAlt)
    {
        return time;
    }
    const float beats = roundf((time - m_cinematic.beat_offset) / beat_length);
    return clamp(m_cinematic.beat_offset + beats * beat_length, 0.0f, m_duration);
}

optional<float> Sequencer::ResolveTime(const string& text_in, string& error) const
{
    string text = trim(text_in);
    float value = 0.0f;
    if (parse_float(text, value))
    {
        return value;
    }

    // a trailing +seconds or -seconds nudges the reference
    float offset = 0.0f;
    const size_t colon = text.find(':');
    const size_t sign  = text.find_last_of("+-");
    if (sign != string::npos && sign > 0 && (colon == string::npos || sign > colon + 1))
    {
        float parsed = 0.0f;
        if (parse_float(trim(text.substr(sign)), parsed))
        {
            offset = parsed;
            text   = trim(text.substr(0, sign));
        }
    }

    if (colon == string::npos)
    {
        error = "'" + text_in + "' is not a time, use seconds, beat:N, bar:N or marker:NAME";
        return nullopt;
    }
    const string kind      = normalize_name(trim(text.substr(0, colon)));
    const string reference = trim(text.substr(colon + 1));
    if (kind == "beat" || kind == "bar")
    {
        const float beat_length = GetBeatLength();
        if (beat_length <= 0.0f)
        {
            error = "'" + text_in + "' needs a beat grid, set bpm with sequencer_markers first";
            return nullopt;
        }
        float number = 0.0f;
        if (!parse_float(reference, number))
        {
            error = "'" + text_in + "' needs a number after " + kind + ":";
            return nullopt;
        }
        const float beats = kind == "bar" ? (number - 1.0f) * static_cast<float>(max(m_cinematic.beats_per_bar, 1u)) : number - 1.0f;
        return m_cinematic.beat_offset + beats * beat_length + offset;
    }
    if (kind == "marker")
    {
        const string wanted = normalize_name(reference);
        for (const Marker& marker : m_cinematic.markers)
        {
            if (normalize_name(marker.name) == wanted)
            {
                return marker.time + offset;
            }
        }
        string names;
        for (const Marker& marker : m_cinematic.markers)
        {
            names += (names.empty() ? "" : ", ") + marker.name;
        }
        error = "no marker named '" + reference + "'" + (names.empty() ? ", there are no markers" : ", markers: " + names);
        return nullopt;
    }
    error = "'" + text_in + "' has an unknown reference, use beat:, bar: or marker:";
    return nullopt;
}

//= EDITING =====================================================================================================================

void Sequencer::SetCurveKey(Channel channel, const CurveKey& key_in)
{
    if (channel >= Channel::Count)
    {
        return;
    }
    CurveKey key = key_in;
    key.time     = clamp(isfinite(key.time) ? key.time : 0.0f, 0.0f, m_duration);
    key.value    = clamp_channel(channel, key.value);

    const State before     = CaptureState();
    vector<CurveKey>& keys = m_cinematic.curves[index_of(channel)];
    auto existing          = find_if(keys.begin(), keys.end(), [&](const CurveKey& k) { return fabsf(k.time - key.time) < 0.001f; });
    if (existing != keys.end())
    {
        *existing = key;
    }
    else
    {
        keys.push_back(key);
        sort(keys.begin(), keys.end(), [](const CurveKey& a, const CurveKey& b) { return a.time < b.time; });
    }
    CommitState(before);
}

bool Sequencer::UpdateCurveKey(Channel channel, int index, const CurveKey& key_in)
{
    if (channel >= Channel::Count)
    {
        return false;
    }
    vector<CurveKey>& keys = m_cinematic.curves[index_of(channel)];
    if (index < 0 || index >= static_cast<int>(keys.size()))
    {
        return false;
    }
    const State before = CaptureState();
    keys[index]        = key_in;
    keys[index].time   = clamp(isfinite(key_in.time) ? key_in.time : 0.0f, 0.0f, m_duration);
    keys[index].value  = clamp_channel(channel, key_in.value);
    sort(keys.begin(), keys.end(), [](const CurveKey& a, const CurveKey& b) { return a.time < b.time; });
    DeselectAll();
    CommitState(before);
    return true;
}

bool Sequencer::RemoveCurveKey(Channel channel, int index)
{
    if (channel >= Channel::Count)
    {
        return false;
    }
    vector<CurveKey>& keys = m_cinematic.curves[index_of(channel)];
    if (index < 0 || index >= static_cast<int>(keys.size()))
    {
        return false;
    }
    const State before = CaptureState();
    keys.erase(keys.begin() + index);
    DeselectAll();
    CommitState(before);
    return true;
}

void Sequencer::ClearCurve(Channel channel)
{
    if (channel >= Channel::Count)
    {
        return;
    }
    const State before = CaptureState();
    m_cinematic.curves[index_of(channel)].clear();
    DeselectAll();
    CommitState(before);
}

void Sequencer::AddTitle(const TitleEvent& title_in)
{
    TitleEvent title = title_in;
    title.start_time = clamp(title.start_time, 0.0f, m_duration);
    title.end_time   = clamp(title.end_time, title.start_time + min_event_gap, max(m_duration, title.start_time + min_event_gap));
    title.fade       = clamp(title.fade, 0.0f, 10.0f);
    const State before = CaptureState();
    m_cinematic.titles.push_back(title);
    DeselectAll();
    CommitState(before);
}

bool Sequencer::UpdateTitle(int index, const TitleEvent& title_in)
{
    if (index < 0 || index >= static_cast<int>(m_cinematic.titles.size()))
    {
        return false;
    }
    TitleEvent title = title_in;
    title.start_time = clamp(title.start_time, 0.0f, m_duration);
    title.end_time   = clamp(title.end_time, title.start_time + min_event_gap, max(m_duration, title.start_time + min_event_gap));
    title.fade       = clamp(title.fade, 0.0f, 10.0f);
    const State before         = CaptureState();
    m_cinematic.titles[index]  = title;
    CommitState(before);
    return true;
}

bool Sequencer::RemoveTitle(int index)
{
    if (index < 0 || index >= static_cast<int>(m_cinematic.titles.size()))
    {
        return false;
    }
    const State before = CaptureState();
    m_cinematic.titles.erase(m_cinematic.titles.begin() + index);
    DeselectAll();
    CommitState(before);
    return true;
}

void Sequencer::ClearTitles()
{
    const State before = CaptureState();
    m_cinematic.titles.clear();
    DeselectAll();
    CommitState(before);
}

void Sequencer::SetMarker(const Marker& marker_in)
{
    Marker marker = marker_in;
    marker.time   = clamp(marker.time, 0.0f, m_duration);
    const State before = CaptureState();
    vector<Marker>& markers = m_cinematic.markers;
    auto existing = find_if(markers.begin(), markers.end(), [&](const Marker& m) { return normalize_name(m.name) == normalize_name(marker.name); });
    if (existing != markers.end())
    {
        *existing = marker;
    }
    else
    {
        markers.push_back(marker);
    }
    sort(markers.begin(), markers.end(), [](const Marker& a, const Marker& b) { return a.time < b.time; });
    CommitState(before);
}

bool Sequencer::RemoveMarker(int index)
{
    if (index < 0 || index >= static_cast<int>(m_cinematic.markers.size()))
    {
        return false;
    }
    const State before = CaptureState();
    m_cinematic.markers.erase(m_cinematic.markers.begin() + index);
    CommitState(before);
    return true;
}

void Sequencer::ClearMarkers()
{
    const State before = CaptureState();
    m_cinematic.markers.clear();
    CommitState(before);
}

void Sequencer::SetCinematicSettings(const Cinematic& settings)
{
    const State before          = CaptureState();
    m_cinematic.music           = settings.music;
    m_cinematic.music.start     = max(settings.music.start, 0.0f);
    m_cinematic.music.offset    = max(settings.music.offset, 0.0f);
    m_cinematic.music.volume    = clamp(settings.music.volume, 0.0f, 4.0f);
    m_cinematic.bpm             = clamp(settings.bpm, 0.0f, 400.0f);
    m_cinematic.beat_offset     = clamp(settings.beat_offset, 0.0f, m_duration);
    m_cinematic.beats_per_bar   = clamp(settings.beats_per_bar, 1u, 16u);
    m_cinematic.snap_to_beats   = settings.snap_to_beats;
    m_cinematic.slowmo_audio    = settings.slowmo_audio;
    m_cinematic.fade_color      = Vector3(clamp(settings.fade_color.x, 0.0f, 1.0f), clamp(settings.fade_color.y, 0.0f, 1.0f), clamp(settings.fade_color.z, 0.0f, 1.0f));
    CommitState(before);
}

//= PERSISTENCE =================================================================================================================

void Sequencer::SaveTracks(pugi::xml_node& root) const
{
    pugi::xml_node node = root.append_child("cinematic");
    node.append_attribute("bpm")           = m_cinematic.bpm;
    node.append_attribute("beat_offset")   = m_cinematic.beat_offset;
    node.append_attribute("beats_per_bar") = m_cinematic.beats_per_bar;
    node.append_attribute("snap_to_beats") = m_cinematic.snap_to_beats;
    node.append_attribute("slowmo_audio")  = m_cinematic.slowmo_audio;
    write_color(node, "fade_color", m_cinematic.fade_color);

    if (!m_cinematic.music.path.empty())
    {
        pugi::xml_node music = node.append_child("music");
        music.append_attribute("path")   = m_cinematic.music.path.c_str();
        music.append_attribute("start")  = m_cinematic.music.start;
        music.append_attribute("offset") = m_cinematic.music.offset;
        music.append_attribute("volume") = m_cinematic.music.volume;
    }

    for (size_t i = 0; i < static_cast<size_t>(Channel::Count); i++)
    {
        if (m_cinematic.curves[i].empty())
        {
            continue;
        }
        pugi::xml_node curve = node.append_child("curve");
        curve.append_attribute("channel") = channel_infos[i].name;
        for (const CurveKey& key : m_cinematic.curves[i])
        {
            pugi::xml_node key_node = curve.append_child("key");
            key_node.append_attribute("time")  = key.time;
            key_node.append_attribute("value") = key.value;
            key_node.append_attribute("ease")  = GetEaseName(key.ease);
        }
    }

    for (const TitleEvent& title : m_cinematic.titles)
    {
        pugi::xml_node title_node = node.append_child("title");
        title_node.append_attribute("start") = title.start_time;
        title_node.append_attribute("end")   = title.end_time;
        title_node.append_attribute("text")  = title.text.c_str();
        title_node.append_attribute("x")     = title.x;
        title_node.append_attribute("y")     = title.y;
        title_node.append_attribute("large") = title.large;
        title_node.append_attribute("fade")  = title.fade;
        write_color(title_node, "color", title.color);
        if (title.style != TitleStyle::Plain)
        {
            title_node.append_attribute("style")  = GetTitleStyleName(title.style);
            title_node.append_attribute("kicker") = title.kicker.c_str();
            write_color(title_node, "accent", title.accent);
        }
    }

    for (const Marker& marker : m_cinematic.markers)
    {
        pugi::xml_node marker_node = node.append_child("marker");
        marker_node.append_attribute("time") = marker.time;
        marker_node.append_attribute("name") = marker.name.c_str();
    }
}

void Sequencer::LoadTracks(const pugi::xml_node& root)
{
    m_cinematic = Cinematic();
    pugi::xml_node node = root.child("cinematic");
    if (!node)
    {
        return;
    }
    m_cinematic.bpm           = node.attribute("bpm").as_float(0.0f);
    m_cinematic.beat_offset   = node.attribute("beat_offset").as_float(0.0f);
    m_cinematic.beats_per_bar = clamp(node.attribute("beats_per_bar").as_uint(4), 1u, 16u);
    m_cinematic.snap_to_beats = node.attribute("snap_to_beats").as_bool(true);
    m_cinematic.slowmo_audio  = node.attribute("slowmo_audio").as_bool(true);
    m_cinematic.fade_color    = read_color(node, "fade_color", Vector3::Zero);

    if (pugi::xml_node music = node.child("music"))
    {
        m_cinematic.music.path   = music.attribute("path").as_string("");
        m_cinematic.music.start  = music.attribute("start").as_float(0.0f);
        m_cinematic.music.offset = music.attribute("offset").as_float(0.0f);
        m_cinematic.music.volume = music.attribute("volume").as_float(1.0f);
    }

    for (pugi::xml_node curve : node.children("curve"))
    {
        const optional<Channel> channel = FindChannel(curve.attribute("channel").as_string(""));
        if (!channel)
        {
            continue;
        }
        vector<CurveKey>& keys = m_cinematic.curves[index_of(*channel)];
        for (pugi::xml_node key_node : curve.children("key"))
        {
            CurveKey key;
            key.time  = key_node.attribute("time").as_float(0.0f);
            key.value = clamp_channel(*channel, key_node.attribute("value").as_float(info(*channel).default_value));
            key.ease  = FindEase(key_node.attribute("ease").as_string("in_out")).value_or(ShotEase::InOut);
            keys.push_back(key);
        }
        sort(keys.begin(), keys.end(), [](const CurveKey& a, const CurveKey& b) { return a.time < b.time; });
    }

    for (pugi::xml_node title_node : node.children("title"))
    {
        TitleEvent title;
        title.start_time = title_node.attribute("start").as_float(0.0f);
        title.end_time   = title_node.attribute("end").as_float(3.0f);
        title.text       = title_node.attribute("text").as_string("");
        title.x          = title_node.attribute("x").as_float(0.5f);
        title.y          = title_node.attribute("y").as_float(0.82f);
        title.large      = title_node.attribute("large").as_bool(true);
        title.fade       = title_node.attribute("fade").as_float(0.5f);
        title.color      = read_color(title_node, "color", Vector3::One);
        title.style      = FindTitleStyle(title_node.attribute("style").as_string("plain")).value_or(TitleStyle::Plain);
        title.kicker     = title_node.attribute("kicker").as_string("");
        title.accent     = read_color(title_node, "accent", TitleEvent().accent);
        m_cinematic.titles.push_back(title);
    }

    for (pugi::xml_node marker_node : node.children("marker"))
    {
        Marker marker;
        marker.time = marker_node.attribute("time").as_float(0.0f);
        marker.name = marker_node.attribute("name").as_string("");
        m_cinematic.markers.push_back(marker);
    }
}

//= PANEL =======================================================================================================================

void Sequencer::DeselectAll()
{
    m_curve_channel  = -1;
    m_curve_selected = -1;
    m_curve_dragging = -1;
    m_title_selected = -1;
    m_title_dragging = -1;
}

float Sequencer::GetTrackLanesHeight() const
{
    return (static_cast<float>(TrackGroup::Count) + 1.0f) * (lane_height + track_gap);
}

void Sequencer::DrawTrackLanes(float origin_x, float track_y, float pixels_per_sec)
{
    float y = track_y;
    for (int group = 0; group < static_cast<int>(TrackGroup::Count); group++)
    {
        DrawCurveLane(static_cast<TrackGroup>(group), origin_x, y, pixels_per_sec);
        y += lane_height + track_gap;
    }
    DrawTitleLane(origin_x, y, pixels_per_sec);
}

void Sequencer::DrawCurveLane(TrackGroup group, float origin_x, float track_y, float pixels_per_sec)
{
    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImGui::PushID(static_cast<int>(group));
    const ImVec2 lane_min = ImVec2(m_lane_x, track_y);
    const ImVec2 lane_max = ImVec2(m_lane_x + m_lane_w, track_y + lane_height);
    ImGui::SetCursorScreenPos(lane_min);
    ImGui::InvisibleButton("##lane", ImVec2(m_lane_w, lane_height));
    const bool hovered     = ImGui::IsItemHovered();
    const float mouse_x    = ImGui::GetIO().MousePos.x;
    const float mouse_time = clamp((mouse_x - origin_x) / pixels_per_sec, 0.0f, m_duration);

    // each channel draws its curve normalized to its own key range, so small moves still read
    int hovered_channel      = -1;
    int hovered_key          = -1;
    float hovered_dist       = edge_grab_px + 1.0f;
    const float inner_top    = lane_min.y + 6.0f;
    const float inner_bottom = lane_max.y - 6.0f;
    bool any_keys            = false;
    for (size_t c = 0; c < static_cast<size_t>(Channel::Count); c++)
    {
        const vector<CurveKey>& keys = m_cinematic.curves[c];
        if (channel_infos[c].group != group || keys.empty())
        {
            continue;
        }
        any_keys = true;
        float lo = keys.front().value;
        float hi = keys.front().value;
        for (const CurveKey& key : keys)
        {
            lo = min(lo, key.value);
            hi = max(hi, key.value);
        }
        if (hi - lo < 1e-4f)
        {
            lo -= 0.5f;
            hi += 0.5f;
        }
        auto to_y = [&](float value)
        {
            return inner_bottom - (value - lo) / (hi - lo) * (inner_bottom - inner_top);
        };

        // the curve over the visible window with a soft fill under it
        const Channel channel = static_cast<Channel>(c);
        const ImVec4& tint    = channel_infos[c].color;
        const ImU32 fill      = to_color(tint, 0.07f);
        const ImU32 line      = to_color(tint, 0.85f);
        ImVec2 previous;
        for (float x = 0.0f; x <= m_lane_w + 3.0f; x += 3.0f)
        {
            const float t  = clamp((m_lane_x + x - origin_x) / pixels_per_sec, 0.0f, m_duration);
            const ImVec2 p = ImVec2(m_lane_x + x, to_y(EvaluateCurve(channel, t)));
            if (x > 0.0f)
            {
                draw->AddQuadFilled(previous, p, ImVec2(p.x, lane_max.y), ImVec2(previous.x, lane_max.y), fill);
                draw->AddLine(previous, p, line, 1.5f);
            }
            previous = p;
        }

        for (int k = 0; k < static_cast<int>(keys.size()); k++)
        {
            const float kx    = IM_ROUND(origin_x + keys[k].time * pixels_per_sec);
            const float ky    = IM_ROUND(to_y(keys[k].value));
            const float dist  = fabsf(mouse_x - kx);
            if (hovered && dist < hovered_dist)
            {
                hovered_dist    = dist;
                hovered_channel = static_cast<int>(c);
                hovered_key     = k;
            }
            const bool select = m_curve_channel == static_cast<int>(c) && m_curve_selected == k;
            const float r     = select ? 6.0f : 5.0f;
            if (select)
            {
                ImGui::EditorUi::draw_glow(draw, ImVec2(kx - r, ky - r), ImVec2(kx + r, ky + r), tint, r, 6.0f, 1.0f);
            }
            draw->AddQuadFilled(ImVec2(kx, ky - r - 1.5f), ImVec2(kx + r + 1.5f, ky), ImVec2(kx, ky + r + 1.5f), ImVec2(kx - r - 1.5f, ky), to_color(ImGui::Style::color_canvas_deep));
            draw->AddQuadFilled(ImVec2(kx, ky - r), ImVec2(kx + r, ky), ImVec2(kx, ky + r), ImVec2(kx - r, ky), to_color(select ? ImGui::Style::lerp(tint, ImVec4(1, 1, 1, 1), 0.4f) : tint));
            if (select)
            {
                draw->AddQuad(ImVec2(kx, ky - r - 2.5f), ImVec2(kx + r + 2.5f, ky), ImVec2(kx, ky + r + 2.5f), ImVec2(kx - r - 2.5f, ky), to_color(ImGui::Style::color_accent_hi), 1.5f);
            }
        }
    }
    if (!any_keys)
    {
        const char* hint = group == TrackGroup::Time ? "double click to key the time scale" : group == TrackGroup::Audio ? "double click to key the mix" :
                           group == TrackGroup::World ? "double click to key time of day or weather" : "double click to key fades or letterbox";
        draw_empty_hint(draw, lane_min, lane_max, hint);
    }
    if (hovered_key != -1 && m_curve_dragging == -1 && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
    {
        const channel_info& ci = channel_infos[hovered_channel];
        const CurveKey& key    = m_cinematic.curves[hovered_channel][hovered_key];
        ImGui::BeginTooltip();
        ImGui::TextColored(ci.color, "%s", ci.name);
        ImGui::SameLine();
        ImGui::Text(ci.format, key.value);
        ImGui::TextDisabled("at %s, eases %s", format_time(key.time).c_str(), ease_names[static_cast<int>(key.ease)]);
        ImGui::EndTooltip();
    }

    // interactions
    if (m_lane_popup_request == static_cast<int>(group))
    {
        m_lane_popup_request = -1;
        ImGui::OpenPopup("##seq_lane_add");
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hovered_key == -1)
    {
        m_lane_popup_time  = SnapTime(mouse_time);
        m_lane_popup_group = static_cast<int>(group);
        ImGui::OpenPopup("##seq_lane_add");
    }
    else if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        m_selected        = -1;
        m_spline_selected = -1;
        m_drive_selected  = -1;
        DeselectAll();
        if (hovered_key != -1)
        {
            m_curve_channel     = hovered_channel;
            m_curve_selected    = hovered_key;
            m_curve_dragging    = hovered_key;
            m_curve_drag_offset = mouse_time - m_cinematic.curves[hovered_channel][hovered_key].time;
            m_curve_drag_moved  = false;
            m_drag_undo_state   = CaptureState();
        }
    }
    else if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        if (hovered_key != -1)
        {
            m_selected        = -1;
            m_spline_selected = -1;
            m_drive_selected  = -1;
            DeselectAll();
            m_curve_channel  = hovered_channel;
            m_curve_selected = hovered_key;
            ImGui::OpenPopup("##seq_key");
        }
        else
        {
            m_lane_popup_time  = SnapTime(mouse_time);
            m_lane_popup_group = static_cast<int>(group);
            ImGui::OpenPopup("##seq_lane_add");
        }
    }

    // a key slides between its neighbors so the order, and every index an agent holds, stays put
    if (m_curve_dragging != -1 && m_curve_channel >= 0 && channel_infos[m_curve_channel].group == group)
    {
        vector<CurveKey>& keys = m_cinematic.curves[m_curve_channel];
        if (m_curve_dragging < static_cast<int>(keys.size()) && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const float time_min = m_curve_dragging > 0 ? keys[m_curve_dragging - 1].time + 0.01f : 0.0f;
            const float time_max = m_curve_dragging + 1 < static_cast<int>(keys.size()) ? keys[m_curve_dragging + 1].time - 0.01f : m_duration;
            const float new_time = clamp(SnapDrag(mouse_time - m_curve_drag_offset, SnapSource::Curve, m_curve_dragging), time_min, max(time_min, time_max));
            if (new_time != keys[m_curve_dragging].time)
            {
                keys[m_curve_dragging].time = new_time;
                m_curve_drag_moved          = true;
            }
            if (m_curve_drag_moved)
            {
                m_drag_readout = new_time;
            }
        }
        else
        {
            if (m_curve_drag_moved)
            {
                CommitState(m_drag_undo_state);
            }
            m_curve_dragging = -1;
        }
    }

    if (ImGui::BeginPopup("##seq_lane_add"))
    {
        ImGui::TextDisabled("key at %s", format_time(m_lane_popup_time).c_str());
        ImGui::Separator();
        for (size_t c = 0; c < static_cast<size_t>(Channel::Count); c++)
        {
            if (channel_infos[c].group != static_cast<TrackGroup>(m_lane_popup_group))
            {
                continue;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::lerp(channel_infos[c].color, ImVec4(1, 1, 1, 1), 0.35f));
            const bool clicked = ImGui::MenuItem(channel_infos[c].name, m_cinematic.curves[c].empty() ? nullptr : "keyed");
            ImGui::PopStyleColor();
            if (clicked)
            {
                const Channel channel = static_cast<Channel>(c);
                CurveKey key;
                key.time  = m_lane_popup_time;
                key.value = EvaluateCurve(channel, m_lane_popup_time);
                // the world channels have no neutral value, a first key starts from what the world itself has
                if (m_cinematic.curves[c].empty() && channel_infos[c].group == TrackGroup::World)
                {
                    const EnvironmentSettings settings = Environment::GetSettings();
                    const bool snapshot                = m_world_snapshot_valid;
                    if (channel == Channel::TimeOfDay)
                    {
                        const double days     = snapshot ? m_world_days : settings.utc_days;
                        const double utc_hour = (days + 0.5 - floor(days + 0.5)) * 24.0;
                        key.value             = static_cast<float>(fmod(utc_hour + settings.longitude / 15.0 + 48.0, 24.0));
                    }
                    else if (channel == Channel::Rain)
                    {
                        key.value = snapshot ? m_world_rain : settings.rain;
                    }
                    else if (channel == Channel::Clouds)
                    {
                        key.value = snapshot ? m_world_clouds : settings.cloud_coverage;
                    }
                    else if (channel == Channel::Wind)
                    {
                        key.value = (snapshot ? m_world_wind : settings.wind).Length();
                    }
                    else if (channel == Channel::Puddles)
                    {
                        key.value = snapshot ? m_world_puddles : settings.puddliness;
                    }
                }
                SetCurveKey(channel, key);
                const vector<CurveKey>& keys = m_cinematic.curves[c];
                for (int k = 0; k < static_cast<int>(keys.size()); k++)
                {
                    if (fabsf(keys[k].time - key.time) < 0.001f)
                    {
                        m_selected        = -1;
                        m_spline_selected = -1;
                        m_drive_selected  = -1;
                        DeselectAll();
                        m_curve_channel  = static_cast<int>(c);
                        m_curve_selected = k;
                    }
                }
            }
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("##seq_key"))
    {
        if (m_curve_channel >= 0 && m_curve_selected >= 0 && m_curve_selected < static_cast<int>(m_cinematic.curves[m_curve_channel].size()))
        {
            const Channel channel = static_cast<Channel>(m_curve_channel);
            CurveKey key          = m_cinematic.curves[m_curve_channel][m_curve_selected];
            ImGui::TextDisabled("%s at %s", GetChannelName(channel), format_time(key.time).c_str());
            ImGui::Separator();
            if (ImGui::BeginMenu("ease in"))
            {
                for (int e = 0; e < static_cast<int>(size(ease_names)); e++)
                {
                    if (ImGui::MenuItem(ease_names[e], nullptr, static_cast<int>(key.ease) == e))
                    {
                        const int selected = m_curve_selected;
                        key.ease           = static_cast<ShotEase>(e);
                        UpdateCurveKey(channel, selected, key);
                        m_curve_channel  = static_cast<int>(channel);
                        m_curve_selected = selected;
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("delete", "del"))
            {
                RemoveCurveKey(channel, m_curve_selected);
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void Sequencer::DrawTitleLane(float origin_x, float track_y, float pixels_per_sec)
{
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const ImVec2 lane_min = ImVec2(m_lane_x, track_y);
    const ImVec2 lane_max = ImVec2(m_lane_x + m_lane_w, track_y + lane_height);
    ImGui::SetCursorScreenPos(lane_min);
    ImGui::InvisibleButton("##seq_title_lane", ImVec2(m_lane_w, lane_height));
    const bool hovered     = ImGui::IsItemHovered();
    const float mouse_x    = ImGui::GetIO().MousePos.x;
    const float mouse_time = clamp((mouse_x - origin_x) / pixels_per_sec, 0.0f, m_duration);

    // the topmost title under the cursor and which part of it, an edge resizes and the body moves
    int hovered_title = -1;
    int hovered_edge  = 0;
    for (int i = static_cast<int>(m_cinematic.titles.size()) - 1; i >= 0 && hovered; i--)
    {
        const TitleEvent& title = m_cinematic.titles[i];
        const float x0 = origin_x + title.start_time * pixels_per_sec;
        const float x1 = origin_x + title.end_time * pixels_per_sec;
        if (mouse_x >= x0 - edge_grab_px && mouse_x <= x1 + edge_grab_px)
        {
            hovered_title = i;
            hovered_edge  = fabsf(mouse_x - x0) <= edge_grab_px ? -1 : fabsf(mouse_x - x1) <= edge_grab_px ? 1 : 0;
            break;
        }
    }
    if ((hovered_title != -1 && hovered_edge != 0) || (m_title_dragging != -1 && m_title_drag_edge != 0))
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hovered_title == -1)
    {
        const float saved = m_time;
        m_time            = mouse_time;
        AddTitleAtPlayhead();
        m_time            = saved;
    }
    else if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        m_selected        = -1;
        m_spline_selected = -1;
        m_drive_selected  = -1;
        DeselectAll();
        if (hovered_title != -1)
        {
            m_title_selected    = hovered_title;
            m_title_dragging    = hovered_title;
            m_title_drag_edge   = hovered_edge;
            m_title_drag_offset = mouse_time - m_cinematic.titles[hovered_title].start_time;
            m_title_drag_moved  = false;
            m_drag_undo_state   = CaptureState();
        }
    }
    else if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && hovered_title != -1)
    {
        m_selected        = -1;
        m_spline_selected = -1;
        m_drive_selected  = -1;
        DeselectAll();
        m_title_selected = hovered_title;
        ImGui::OpenPopup("##seq_title");
    }

    if (m_title_dragging != -1 && m_title_dragging < static_cast<int>(m_cinematic.titles.size()))
    {
        TitleEvent& title = m_cinematic.titles[m_title_dragging];
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const TitleEvent before = title;
            const float span        = title.end_time - title.start_time;
            if (m_title_drag_edge < 0)
            {
                title.start_time = clamp(SnapDrag(mouse_time, SnapSource::Title, m_title_dragging), 0.0f, title.end_time - min_event_gap);
                m_drag_readout   = title.start_time;
            }
            else if (m_title_drag_edge > 0)
            {
                title.end_time = clamp(SnapDrag(mouse_time, SnapSource::Title, m_title_dragging), title.start_time + min_event_gap, m_duration);
                m_drag_readout = title.end_time;
            }
            else
            {
                title.start_time = clamp(SnapWindow(mouse_time - m_title_drag_offset, span, SnapSource::Title, m_title_dragging), 0.0f, max(m_duration - span, 0.0f));
                title.end_time   = title.start_time + span;
                m_drag_readout   = title.start_time;
            }
            m_title_drag_moved |= !(before == title);
            if (!m_title_drag_moved)
            {
                m_drag_readout = -1.0f;
            }
        }
        else
        {
            if (m_title_drag_moved)
            {
                CommitState(m_drag_undo_state);
            }
            m_title_dragging = -1;
        }
    }

    const ImVec4 hue = track_hue(7);
    for (int i = 0; i < static_cast<int>(m_cinematic.titles.size()); i++)
    {
        const TitleEvent& title = m_cinematic.titles[i];
        const ImVec2 bar_min    = ImVec2(origin_x + title.start_time * pixels_per_sec + 1.0f, lane_min.y + 4.0f);
        const ImVec2 bar_max    = ImVec2(origin_x + title.end_time * pixels_per_sec - 1.0f, lane_max.y - 4.0f);
        if (bar_max.x < m_lane_x || bar_min.x > m_lane_x + m_lane_w)
        {
            continue;
        }
        draw_clip(draw, bar_min, bar_max, hue, i == m_title_selected, i == hovered_title);

        // the fades darken the ends of the card
        const float fade_px = min(title.fade * pixels_per_sec, (bar_max.x - bar_min.x) * 0.5f);
        if (fade_px > 1.0f)
        {
            const ImU32 dark  = to_color(ImGui::Style::color_canvas_deep, 0.75f);
            const ImU32 clear = to_color(ImGui::Style::color_canvas_deep, 0.0f);
            draw->AddRectFilledMultiColor(bar_min, ImVec2(bar_min.x + fade_px, bar_max.y), dark, clear, clear, dark);
            draw->AddRectFilledMultiColor(ImVec2(bar_max.x - fade_px, bar_min.y), bar_max, clear, dark, dark, clear);
        }

        string label = title.text;
        replace(label.begin(), label.end(), '\n', ' ');
        draw_clip_text(draw, bar_min, bar_max, label.c_str(), nullptr, nullptr, false, Glyph::Plus);
    }
    if (m_cinematic.titles.empty())
    {
        draw_empty_hint(draw, lane_min, lane_max, "double click to add a title card");
    }

    if (ImGui::BeginPopup("##seq_title"))
    {
        if (m_title_selected >= 0 && m_title_selected < static_cast<int>(m_cinematic.titles.size()))
        {
            if (ImGui::MenuItem("duplicate"))
            {
                TitleEvent copy  = m_cinematic.titles[m_title_selected];
                const float span = copy.end_time - copy.start_time;
                copy.start_time  = min(copy.end_time, m_duration);
                copy.end_time    = min(copy.start_time + span, m_duration);
                AddTitle(copy);
                m_title_selected = static_cast<int>(m_cinematic.titles.size()) - 1;
            }
            if (ImGui::MenuItem("delete", "del"))
            {
                RemoveTitle(m_title_selected);
            }
        }
        ImGui::EndPopup();
    }
}

void Sequencer::DrawBeatGrid(float origin_x, float top, float bottom, float pixels_per_sec)
{
    const float beat_length = GetBeatLength();
    if (beat_length <= 0.0f || beat_length * pixels_per_sec < 4.0f)
    {
        return;
    }
    ImDrawList* draw       = ImGui::GetWindowDrawList();
    const ImU32 col_beat   = IM_COL32(255, 255, 255, 10);
    const ImU32 col_bar    = IM_COL32(255, 255, 255, 30);
    const uint32_t per_bar = max(m_cinematic.beats_per_bar, 1u);
    const float view_end   = m_view_start + m_lane_w / pixels_per_sec;
    const int first_beat   = static_cast<int>(floorf((max(m_view_start, 0.0f) - m_cinematic.beat_offset) / beat_length));
    for (int beat = first_beat;; beat++)
    {
        const float t = m_cinematic.beat_offset + static_cast<float>(beat) * beat_length;
        if (t > min(view_end, m_duration) + 0.001f)
        {
            break;
        }
        if (t < 0.0f)
        {
            continue;
        }
        const bool bar = ((beat % static_cast<int>(per_bar)) + static_cast<int>(per_bar)) % static_cast<int>(per_bar) == 0;
        const float x  = IM_ROUND(origin_x + t * pixels_per_sec) + 0.5f;
        draw->AddLine(ImVec2(x, top), ImVec2(x, bottom), bar ? col_bar : col_beat);
    }
}

void Sequencer::DrawMarkerStrip(float origin_x, float top, float pixels_per_sec)
{
    ImDrawList* draw         = ImGui::GetWindowDrawList();
    const ImGuiIO& io        = ImGui::GetIO();
    const ImVec4 tint        = ImVec4(0.98f, 0.82f, 0.42f, 1.0f);
    const float label_size   = ImGui::GetFontSize() * 0.82f;
    const ImVec2 strip_min   = ImVec2(m_lane_x, top);
    const ImVec2 strip_max   = ImVec2(m_lane_x + m_lane_w, top + marker_height);
    ImGui::SetCursorScreenPos(strip_min);
    ImGui::InvisibleButton("##seq_markers", ImVec2(m_lane_w, marker_height));
    const bool hovered       = ImGui::IsItemHovered();
    const float mouse_time   = clamp((io.MousePos.x - origin_x) / pixels_per_sec, 0.0f, m_duration);
    vector<Marker>& markers  = m_cinematic.markers;

    // a flag is the line plus its label, either one picks the marker
    int hovered_marker = -1;
    for (int i = static_cast<int>(markers.size()) - 1; i >= 0 && hovered; i--)
    {
        const float x       = origin_x + markers[i].time * pixels_per_sec;
        const float label_w = ImGui::GetFont()->CalcTextSizeA(label_size, FLT_MAX, 0.0f, markers[i].name.c_str()).x;
        if (io.MousePos.x >= x - 5.0f && io.MousePos.x <= x + label_w + 14.0f)
        {
            hovered_marker = i;
            break;
        }
    }
    if (hovered_marker != -1 || m_marker_dragging != -1)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    if (hovered && hovered_marker == -1 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        const float saved = m_time;
        m_time            = SnapDrag(mouse_time, SnapSource::Marker, -1);
        AddMarkerAtPlayhead();
        m_time            = saved;
    }
    else if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && hovered_marker != -1)
    {
        m_marker_dragging    = hovered_marker;
        m_marker_drag_offset = mouse_time - markers[hovered_marker].time;
        m_marker_drag_moved  = false;
        m_drag_undo_state    = CaptureState();
    }
    else if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && hovered_marker != -1)
    {
        m_marker_selected = hovered_marker;
        snprintf(m_marker_name, sizeof(m_marker_name), "%s", markers[hovered_marker].name.c_str());
        ImGui::OpenPopup("##seq_marker");
    }

    if (m_marker_dragging != -1 && m_marker_dragging < static_cast<int>(markers.size()))
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const float new_time = clamp(SnapDrag(mouse_time - m_marker_drag_offset, SnapSource::Marker, m_marker_dragging), 0.0f, m_duration);
            m_marker_drag_moved |= new_time != markers[m_marker_dragging].time;
            markers[m_marker_dragging].time = new_time;
            if (m_marker_drag_moved)
            {
                m_drag_readout = new_time;
            }
        }
        else
        {
            if (m_marker_drag_moved)
            {
                sort(markers.begin(), markers.end(), [](const Marker& a, const Marker& b) { return a.time < b.time; });
                CommitState(m_drag_undo_state);
            }
            m_marker_dragging = -1;
        }
    }
    else
    {
        m_marker_dragging = -1;
    }

    draw->AddRectFilled(strip_min, strip_max, to_color(ImGui::Style::lerp(ImGui::Style::color_canvas_deep, ImGui::Style::color_canvas, 0.55f)));
    for (int i = 0; i < static_cast<int>(markers.size()); i++)
    {
        const Marker& marker = markers[i];
        const float x        = IM_ROUND(origin_x + marker.time * pixels_per_sec) + 0.5f;
        const bool lit       = i == hovered_marker || i == m_marker_dragging;
        const float label_w  = ImGui::GetFont()->CalcTextSizeA(label_size, FLT_MAX, 0.0f, marker.name.c_str()).x;
        const ImVec2 flag_min = ImVec2(x, top + 3.0f);
        const ImVec2 flag_max = ImVec2(x + label_w + 12.0f, top + marker_height - 3.0f);
        draw->AddLine(ImVec2(x, top), ImVec2(x, m_lane_bottom), to_color(tint, lit ? 0.6f : 0.28f), 1.0f);
        draw->AddRectFilled(flag_min, flag_max, to_color(tint, lit ? 0.32f : 0.18f), 3.0f, ImDrawFlags_RoundCornersRight);
        draw->AddRectFilled(flag_min, ImVec2(x + 2.0f, flag_max.y), to_color(tint));
        draw->AddText(nullptr, label_size, ImVec2(x + 7.0f, IM_ROUND(flag_min.y + (flag_max.y - flag_min.y - label_size) * 0.5f)), to_color(ImGui::Style::lerp(tint, ImVec4(1, 1, 1, 1), 0.3f)), marker.name.c_str());
    }
    if (markers.empty() && hovered)
    {
        draw->AddText(nullptr, label_size, ImVec2(strip_min.x + 8.0f, IM_ROUND(top + (marker_height - label_size) * 0.5f)), to_color(ImGui::Style::color_text_faint), "double click to drop a marker, m drops one at the playhead");
    }
    draw->AddLine(ImVec2(strip_min.x, strip_max.y - 0.5f), ImVec2(strip_max.x, strip_max.y - 0.5f), to_color(ImGui::Style::color_border));

    if (ImGui::BeginPopup("##seq_marker"))
    {
        if (m_marker_selected >= 0 && m_marker_selected < static_cast<int>(markers.size()))
        {
            ImGui::TextDisabled("marker at %s", format_time(markers[m_marker_selected].time).c_str());
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::IsWindowAppearing())
            {
                ImGui::SetKeyboardFocusHere();
            }
            if (ImGui::InputText("##seq_marker_name", m_marker_name, sizeof(m_marker_name), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
            {
                const string name = trim(m_marker_name);
                if (!name.empty() && name != markers[m_marker_selected].name)
                {
                    const State before = CaptureState();
                    markers[m_marker_selected].name = name;
                    CommitState(before);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGuiSp::tooltip("rename, agents address markers by name as marker:NAME");
            ImGui::Separator();
            if (ImGui::MenuItem("go to"))
            {
                m_playing = false;
                m_time    = markers[m_marker_selected].time;
            }
            if (ImGui::MenuItem("delete"))
            {
                RemoveMarker(m_marker_selected);
                m_marker_selected = -1;
            }
        }
        ImGui::EndPopup();
    }
}

void Sequencer::DrawTrackToolbar()
{
    // state chips laid out from the cursor: the live time scale, the bar and beat, the recording progress
    ImVec2 pen        = ImGui::GetCursorScreenPos();
    bool any          = false;
    auto chip         = [&](const char* text, const ImVec4& background, const ImVec4& foreground)
    {
        ImGui::SetCursorScreenPos(pen);
        ImGui::EditorUi::draw_chip(text, background, foreground);
        pen.x = ImGui::GetItemRectMax().x + 6.0f;
        any   = true;
    };

    const ImVec4 amber = ImVec4(1.0f, 0.78f, 0.25f, 1.0f);
    if (m_render_status.active)
    {
        char text[64];
        snprintf(text, sizeof(text), "rec  %u / %u", m_render_status.frames_written, m_render_status.frames_total);
        chip(text, ImGui::Style::with_alpha(ImGui::Style::color_error, 0.16f), ImGui::Style::color_error);
    }
    if (m_applied_time_scale != 1.0f)
    {
        char text[32];
        if (m_applied_time_scale == 0.0f)
        {
            snprintf(text, sizeof(text), "frozen");
        }
        else
        {
            snprintf(text, sizeof(text), "x%.2f", m_applied_time_scale);
        }
        chip(text, ImGui::Style::with_alpha(amber, 0.14f), amber);
    }
    const float beat_length = GetBeatLength();
    if (beat_length > 0.0f)
    {
        const float beat       = max((m_time - m_cinematic.beat_offset) / beat_length, 0.0f);
        const uint32_t per_bar = max(m_cinematic.beats_per_bar, 1u);
        const uint32_t whole   = static_cast<uint32_t>(beat);
        char text[32];
        snprintf(text, sizeof(text), "bar %u.%u", whole / per_bar + 1, whole % per_bar + 1);
        chip(text, ImGui::Style::with_alpha(ImGui::Style::color_text, 0.05f), ImGui::Style::color_text_muted);
    }
    if (m_preview && !m_playing)
    {
        chip("preview", ImGui::Style::color_accent_dim, ImGui::Style::color_accent_1);
    }
    if (!any)
    {
        ImGui::SetCursorScreenPos(pen);
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }
}

void Sequencer::DrawCurveKeyInspector(float width)
{
    const Channel channel  = static_cast<Channel>(m_curve_channel);
    vector<CurveKey>& keys = m_cinematic.curves[m_curve_channel];
    CurveKey& key          = keys[m_curve_selected];
    const channel_info& ci = info(channel);

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

    {
        char kicker[64];
        snprintf(kicker, sizeof(kicker), "%s key %d of %d", group_names[static_cast<int>(ci.group)], m_curve_selected + 1, static_cast<int>(keys.size()));
        char value[64];
        snprintf(value, sizeof(value), ci.format, key.value);
        const string subtitle = string(value) + "  at " + format_time(key.time);
        inspector_header(kicker, ci.name, subtitle.c_str(), ci.color);
    }
    inspector_section("key");

    {
        const float t_min = m_curve_selected > 0 ? keys[m_curve_selected - 1].time + 0.01f : 0.0f;
        const float t_max = max(t_min, m_curve_selected + 1 < static_cast<int>(keys.size()) ? keys[m_curve_selected + 1].time - 0.01f : m_duration);
        inspector_label("time", width);
        if (ImGui::DragFloat("##seq_k_time", &key.time, 0.01f, t_min, t_max, "%.2f s"))
        {
            key.time = clamp(key.time, t_min, t_max);
        }
        track_edit();
    }
    {
        inspector_label("value", width);
        const float speed = (ci.max_value - ci.min_value) / 400.0f;
        if (ImGui::DragFloat("##seq_k_value", &key.value, speed, ci.min_value, ci.max_value, ci.format))
        {
            key.value = clamp_channel(channel, key.value);
        }
        track_edit();
    }
    {
        int ease = static_cast<int>(key.ease);
        inspector_label("ease in", width);
        if (ImGui::Combo("##seq_k_ease", &ease, ease_names, IM_ARRAYSIZE(ease_names)))
        {
            const State before = CaptureState();
            key.ease           = static_cast<ShotEase>(ease);
            CommitState(before);
        }
        ImGuiSp::tooltip("how the curve arrives at this key, hold jumps to it");
    }

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::PushTextWrapPos(0.0f);
    switch (channel)
    {
        case Channel::TimeScale:  ImGui::TextDisabled("1 is real time, 0.25 is slow motion, 0 freezes the world while the camera keeps moving"); break;
        case Channel::Muffle:     ImGui::TextDisabled("low pass on vehicles, ambience and effects, the music stays clear"); break;
        case Channel::TimeOfDay:  ImGui::TextDisabled("local solar hours, 12 is the sun at its highest, keys past 24 run into the next day"); break;
        case Channel::Wind:       ImGui::TextDisabled("wind speed, the direction stays what the world has"); break;
        case Channel::Fade:       ImGui::TextDisabled("0 clear, 1 the fade color, set the color with nothing selected"); break;
        case Channel::Letterbox:  ImGui::TextDisabled("target aspect, 2.39 is scope, 0 is off"); break;
        default:                  ImGui::TextDisabled("1 leaves the group at its own level"); break;
    }
    ImGui::PopTextWrapPos();

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    if (danger_button("delete key", ImVec2(width, 0.0f)))
    {
        RemoveCurveKey(channel, m_curve_selected);
    }
}

void Sequencer::DrawTitleInspector(float width)
{
    TitleEvent& title = m_cinematic.titles[m_title_selected];
    auto track_edit   = [this]()
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

    {
        string first_line = title.text.substr(0, title.text.find('\n'));
        if (first_line.empty())
        {
            first_line = "untitled";
        }
        const string subtitle = format_time(title.start_time) + "  to  " + format_time(title.end_time);
        inspector_header("title card", first_line.c_str(), subtitle.c_str(), track_hue(7));
    }
    inspector_section("text");
    {
        char buffer[512];
        snprintf(buffer, sizeof(buffer), "%s", title.text.c_str());
        ImGui::SetNextItemWidth(width);
        if (ImGui::InputTextMultiline("##seq_t_text", buffer, sizeof(buffer), ImVec2(width, ImGui::GetFontSize() * 3.5f)))
        {
            title.text = buffer;
        }
        track_edit();
    }
    inspector_label("start", width);
    if (ImGui::DragFloat("##seq_t_start", &title.start_time, 0.02f, 0.0f, title.end_time - min_event_gap, "%.2f s"))
    {
        title.start_time = clamp(title.start_time, 0.0f, title.end_time - min_event_gap);
    }
    track_edit();
    inspector_label("end", width);
    if (ImGui::DragFloat("##seq_t_end", &title.end_time, 0.02f, title.start_time + min_event_gap, m_duration, "%.2f s"))
    {
        title.end_time = clamp(title.end_time, title.start_time + min_event_gap, m_duration);
    }
    track_edit();
    inspector_label("fade", width);
    ImGui::DragFloat("##seq_t_fade", &title.fade, 0.01f, 0.0f, 10.0f, "%.2f s");
    track_edit();
    inspector_label("position", width);
    ImGui::DragFloat2("##seq_t_pos", &title.x, 0.002f, 0.0f, 1.0f, "%.3f");
    track_edit();
    {
        bool large = title.large;
        inspector_label("large", width);
        if (ImGui::Checkbox("##seq_t_large", &large))
        {
            const State before = CaptureState();
            title.large        = large;
            CommitState(before);
        }
    }
    inspector_label("color", width);
    ImGui::ColorEdit3("##seq_t_color", &title.color.x, ImGuiColorEditFlags_NoInputs);
    track_edit();

    inspector_section("style");
    {
        static const char* styles[] = { "plain", "trailer", "legend" };
        int style = static_cast<int>(title.style);
        inspector_label("style", width);
        if (ImGui::Combo("##seq_t_style", &style, styles, IM_COUNTOF(styles)))
        {
            const State before = CaptureState();
            title.style        = static_cast<TitleStyle>(style);
            CommitState(before);
        }
    }
    if (title.style != TitleStyle::Plain)
    {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "%s", title.kicker.c_str());
        inspector_label("kicker", width);
        if (ImGui::InputText("##seq_t_kicker", buffer, sizeof(buffer)))
        {
            title.kicker = buffer;
        }
        track_edit();
        inspector_label("accent", width);
        ImGui::ColorEdit3("##seq_t_accent", &title.accent.x, ImGuiColorEditFlags_NoInputs);
        track_edit();
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    if (danger_button("delete title", ImVec2(width, 0.0f)))
    {
        RemoveTitle(m_title_selected);
    }
}

void Sequencer::DrawCinematicInspector(float width)
{
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
    inspector_section("music");
    {
        char buffer[512];
        snprintf(buffer, sizeof(buffer), "%s", m_cinematic.music.path.c_str());
        inspector_label("file", width);
        if (ImGui::InputText("##seq_c_music", buffer, sizeof(buffer)))
        {
            m_cinematic.music.path = buffer;
        }
        track_edit();
        ImGuiSp::tooltip("an audio file played in sync with the playhead, relative to the binaries folder or absolute");
    }
    inspector_label("starts at", width);
    ImGui::DragFloat("##seq_c_mstart", &m_cinematic.music.start, 0.01f, 0.0f, m_duration, "%.2f s");
    track_edit();
    inspector_label("file offset", width);
    ImGui::DragFloat("##seq_c_moffset", &m_cinematic.music.offset, 0.01f, 0.0f, 3600.0f, "%.2f s");
    track_edit();
    inspector_label("volume", width);
    ImGui::DragFloat("##seq_c_mvolume", &m_cinematic.music.volume, 0.01f, 0.0f, 4.0f, "%.2f");
    track_edit();

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    inspector_section("beat grid");
    inspector_label("bpm", width);
    ImGui::DragFloat("##seq_c_bpm", &m_cinematic.bpm, 0.1f, 0.0f, 400.0f, m_cinematic.bpm > 0.0f ? "%.1f" : "off");
    track_edit();
    inspector_label("first beat", width);
    ImGui::DragFloat("##seq_c_offset", &m_cinematic.beat_offset, 0.005f, 0.0f, m_duration, "%.3f s");
    track_edit();
    {
        int per_bar = static_cast<int>(m_cinematic.beats_per_bar);
        inspector_label("beats per bar", width);
        if (ImGui::DragInt("##seq_c_bar", &per_bar, 0.1f, 1, 16))
        {
            m_cinematic.beats_per_bar = static_cast<uint32_t>(clamp(per_bar, 1, 16));
        }
        track_edit();
    }
    {
        bool snap = m_cinematic.snap_to_beats;
        inspector_label("snap to beats", width);
        if (ImGui::Checkbox("##seq_c_snap", &snap))
        {
            const State before         = CaptureState();
            m_cinematic.snap_to_beats  = snap;
            CommitState(before);
        }
        ImGuiSp::tooltip("keys and titles snap to beats while dragged, hold alt to place them freely");
    }

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    inspector_section("slow motion and overlay");
    {
        bool slowmo = m_cinematic.slowmo_audio;
        inspector_label("slowmo audio", width);
        if (ImGui::Checkbox("##seq_c_slowmo", &slowmo))
        {
            const State before        = CaptureState();
            m_cinematic.slowmo_audio  = slowmo;
            CommitState(before);
        }
        ImGuiSp::tooltip("world audio drops in pitch and muffles with the time scale, music is left alone");
    }
    inspector_label("fade color", width);
    ImGui::ColorEdit3("##seq_c_fade", &m_cinematic.fade_color.x, ImGuiColorEditFlags_NoInputs);
    track_edit();

    if (!m_cinematic.markers.empty())
    {
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        inspector_section("markers");
        for (int i = 0; i < static_cast<int>(m_cinematic.markers.size()); i++)
        {
            ImGui::PushID(i);
            const Marker& marker = m_cinematic.markers[i];
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", format_time(marker.time).c_str());
            ImGui::SameLine(70.0f);
            ImGui::TextUnformatted(marker.name.c_str());
            ImGui::SameLine(max(width - 24.0f, 0.0f));
            if (ImGui::SmallButton("x"))
            {
                RemoveMarker(i);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    }
    if (ImGuiSp::button("+ marker at playhead", ImVec2(width, 0.0f)))
    {
        AddMarkerAtPlayhead();
    }
}
