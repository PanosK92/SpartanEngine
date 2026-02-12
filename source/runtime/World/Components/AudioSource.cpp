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

//= INCLUDES ===============
#include "pch.h"
#include "AudioSource.h"
#include "Camera.h"
#include "Volume.h"
#include "../Entity.h"
#include "../World.h"
SP_WARNINGS_OFF
#include <SDL3/SDL_audio.h>
#include "../IO/pugixml.hpp"
SP_WARNINGS_ON
//==========================

//=== NAMESPACES =============
using namespace std;
using namespace spartan::math;
//============================

#define CHECK_SDL_ERROR(call)           \
if (!(call)) {                          \
    SP_LOG_ERROR("%s", SDL_GetError()); \
    return;                             \
}

namespace audio_clip_cache
{
    struct AudioClip
    {
        uint8_t* buffer     = nullptr;
        uint32_t length     = 0;
        SDL_AudioSpec* spec = nullptr;

        ~AudioClip()
        {
            if (buffer)
            {
                SDL_free(buffer);
                buffer = nullptr;
            }
            delete spec;
            spec = nullptr;
        }
    };
    unordered_map<string, weak_ptr<AudioClip>> cache;

    shared_ptr<AudioClip> Get(const string& file_path)
    {
        auto it = cache.find(file_path);
        shared_ptr<AudioClip> clip;
        if (it != cache.end())
        {
            clip = it->second.lock();
            if (clip)
            {
                return clip;
            }
        }

        SDL_AudioSpec wav_spec = {};
        uint8_t* wav_buffer    = nullptr;
        uint32_t wav_length    = 0;
        if (!SDL_LoadWAV(file_path.c_str(), &wav_spec, &wav_buffer, &wav_length))
        {
            SP_LOG_ERROR("%s", SDL_GetError());
            return nullptr;
        }

        SDL_AudioSpec target_spec = {};
        target_spec.freq          = wav_spec.freq;
        target_spec.format        = SDL_AUDIO_F32;
        target_spec.channels      = 1;
        uint8_t* target_buffer    = nullptr;
        int target_length         = 0;
        if (!SDL_ConvertAudioSamples(&wav_spec, wav_buffer, static_cast<int>(wav_length), &target_spec, &target_buffer, &target_length))
        {
            SP_LOG_ERROR("%s", SDL_GetError());
            SDL_free(wav_buffer);
            return nullptr;
        }
        SDL_free(wav_buffer);

        clip         = make_shared<AudioClip>();
        clip->buffer = target_buffer;
        clip->length = static_cast<uint32_t>(target_length);
        clip->spec   = new SDL_AudioSpec(target_spec);

        cache[file_path] = clip;
        return clip;
    }

    void ReleaseAll()
    {
        cache.clear();
    }
}

namespace audio_device
{
    mutex device_mutex;
    SDL_AudioSpec spec  = {};
    uint32_t id         = 0;
    uint32_t references = 0;

    // acquire the shared audio device, open it if it's not already open
    void acquire()
    {
        lock_guard<mutex> lock(device_mutex);
        if (references == 0)
        {
            id = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
            if (id == 0)
            {
                SP_LOG_ERROR("%s", SDL_GetError());
                return;
            }
            if (!SDL_GetAudioDeviceFormat(id, &spec, nullptr))
            {
                SP_LOG_ERROR("%s", SDL_GetError());
            }
        }
        ++references;
    }

    // release the shared audio device, close it when no one is using it
    void release()
    {
        lock_guard<mutex> lock(device_mutex);
        --references;
        if (references == 0 && id != 0)
        {
            SDL_CloseAudioDevice(id);
            id = 0;
        }
    }
}

namespace spartan
{
    AudioSource::AudioSource(Entity* entity) : Component(entity)
    {
        audio_device::acquire();
    }

    AudioSource::~AudioSource()
    {
        StopClip();

        if (m_stream)
        {
            SDL_DestroyAudioStream(m_stream);
            m_stream = nullptr;
        }

        audio_device::release();
    }

    void AudioSource::RegisterForScripting(sol::state_view State)
    {
        State.new_usertype<AudioSource>("AudioSource",
            sol::base_classes,              sol::bases<Component>(),
            "SetAudioClip",                 &AudioSource::SetAudioClip,
            "IsPlaying",                    &AudioSource::IsPlaying,
            "PlayClip",                     &AudioSource::PlayClip,
            "StopClip",                     &AudioSource::StopClip,
            "GetAudioClipName",             &AudioSource::GetAudioClipName,


            "GetMute",                      &AudioSource::GetMute,
            "SetMute",                      &AudioSource::SetMute,

            "IsSynthesisMode",              &AudioSource::IsSynthesisMode,
            "SetSynthesisMode",             &AudioSource::SetSynthesisMode,
            "StartSynthesis",               &AudioSource::StartSynthesis,
            "StopSynthesis",                &AudioSource::StopSynthesis,

            "GetPitch",                     &AudioSource::GetPitch,
            "SetPitch",                     &AudioSource::SetPitch

            );
    }

    void AudioSource::Initialize()
    {
        Component::Initialize();
    }

    void AudioSource::Start()
    {
        if (m_play_on_start)
        {
            PlayClip();
        }
    }

    void AudioSource::Stop()
    {
        StopClip();
    }

    void AudioSource::Remove()
    {
        StopClip();
    }

    void AudioSource::Tick()
    {
        if (!m_is_playing)
            return;

        if (m_is_3d)
        {
            if (Camera* camera = World::GetCamera())
            {
                // get current positions
                static Vector3 camera_position_previous = Vector3::Zero;
                Vector3 camera_position                 = camera->GetEntity()->GetPosition();
                Vector3 sound_position                  = GetEntity()->GetPosition();

                // panning
                {
                    Vector3 camera_to_sound = (sound_position - camera_position).Normalized();
                    Vector3 camera_right    = camera->GetEntity()->GetRight();
                    m_pan                   = Vector3::Dot(camera_to_sound, camera_right);
                }

                // attenuation
                {
                    float distance_squared     = Vector3::DistanceSquared(camera_position, sound_position);
                    const float rolloff_factor = 15.0f;
                    m_attenuation              = 1.0f / (1.0f + (distance_squared / (rolloff_factor * rolloff_factor)));
                    m_attenuation              = clamp(m_attenuation, 0.0f, 1.0f);
                }
                // doppler effect
                {
                    const float dt             = static_cast<float>(Timer::GetDeltaTimeSec());
                    const float speed_of_sound = 343.0f;

                    Vector3 rel_velocity = (camera_position - camera_position_previous) / dt - (sound_position - position_previous) / dt;
                    Vector3 to_sound     = (sound_position - camera_position).Normalized();
                    float radial_v       = Vector3::Dot(to_sound, rel_velocity);
                    float target_ratio   = 1.0f + radial_v / speed_of_sound;

                    // clamping and smooething
                    target_ratio    = clamp(target_ratio, 0.5f, 2.0f);
                    const float s   = 0.2f; // smoothing factor
                    m_doppler_ratio = lerp(m_doppler_ratio, target_ratio, s);
                    SetPitch(m_pitch);
                }

                // update previous positions
                camera_position_previous = camera_position;
                position_previous        = sound_position;
            }
        }

        // check if inside any volume that has reverb enabled
        {
            Vector3 source_position      = GetEntity()->GetPosition();
            bool found_reverb_volume     = false;

            for (Entity* entity : World::GetEntities())
            {
                Volume* volume = entity->GetComponent<Volume>();
                if (!volume || !volume->GetReverbEnabled())
                    continue;

                // transform the volume's local bounding box into world space
                BoundingBox transformed_box = volume->GetBoundingBox() * entity->GetMatrix();
                if (transformed_box.Contains(source_position))
                {
                    // allocate reverb buffers if they haven't been yet
                    if (m_reverb_buffer_l.empty())
                    {
                        m_reverb_buffer_l.assign(reverb_buffer_size, 0.0f);
                        m_reverb_buffer_r.assign(reverb_buffer_size, 0.0f);
                        m_reverb_write_pos = 0;
                    }

                    // derive reverb parameters from the volume's physical size
                    // larger volumes produce longer, more resonant reverb
                    Vector3 size       = transformed_box.GetSize();
                    float longest_axis = max({ size.x, size.y, size.z });
                    float size_factor  = clamp(longest_axis / 50.0f, 0.0f, 1.0f); // 50m+ = full scale

                    m_reverb_enabled   = true;
                    m_reverb_room_size = 0.6f + size_factor * 0.4f;               // [0.6, 1.0]
                    m_reverb_decay     = 0.7f + size_factor * 0.28f;              // [0.7, 0.98]
                    m_reverb_wet       = 0.6f + size_factor * 0.35f;              // [0.6, 0.95]
                    found_reverb_volume = true;
                    break;
                }
            }

            // leaving a reverb volume, disable the override
            if (!found_reverb_volume && m_volume_reverb_active)
            {
                m_reverb_enabled = false;
            }

            m_volume_reverb_active = found_reverb_volume;
        }

        // feed audio based on mode
        if (m_synthesis_mode)
            FeedSynthesizedChunk();
        else
            FeedAudioChunk();
    }

    void AudioSource::SetSynthesisMode(bool enabled, SynthesisCallback callback)
    {
        // stop any current playback when changing modes
        if (m_is_playing && enabled != m_synthesis_mode)
        {
            if (m_synthesis_mode)
                StopSynthesis();
            else
                StopClip();
        }

        m_synthesis_mode     = enabled;
        m_synthesis_callback = callback;
    }

    void AudioSource::StartSynthesis()
    {
        if (!m_synthesis_mode || !m_synthesis_callback)
        {
            SP_LOG_ERROR("synthesis mode not enabled or no callback set");
            return;
        }

        // create stream for synthesis: stereo float32 at 48khz
        SDL_AudioSpec src_spec = {};
        src_spec.freq          = 48000;
        src_spec.format        = SDL_AUDIO_F32;
        src_spec.channels      = 2;
        m_stream = SDL_CreateAudioStream(&src_spec, &audio_device::spec);
        if (!m_stream)
        {
            SP_LOG_ERROR("%s", SDL_GetError());
            return;
        }

        CHECK_SDL_ERROR(SDL_BindAudioStream(audio_device::id, m_stream));

        // initialize reverb buffers
        m_reverb_buffer_l.assign(reverb_buffer_size, 0.0f);
        m_reverb_buffer_r.assign(reverb_buffer_size, 0.0f);
        m_reverb_write_pos = 0;

        // start playing
        CHECK_SDL_ERROR(SDL_ResumeAudioStreamDevice(m_stream));
        m_is_playing = true;
    }

    void AudioSource::StopSynthesis()
    {
        if (!m_is_playing)
            return;

        if (m_stream)
        {
            SDL_ClearAudioStream(m_stream);
            SDL_DestroyAudioStream(m_stream);
            m_stream = nullptr;
        }
        m_is_playing = false;
    }

    void AudioSource::FeedSynthesizedChunk()
    {
        if (!m_stream || !m_is_playing || !m_synthesis_callback)
            return;

        int queued               = SDL_GetAudioStreamQueued(m_stream);
        const int low_water_mark = 16384;
        if (queued >= low_water_mark)
            return;

        const uint32_t num_samples = 2048;
        m_stereo_chunk.resize(num_samples * 2);

        // call the synthesis callback to generate samples
        m_synthesis_callback(m_stereo_chunk.data(), num_samples);

        // apply volume and panning
        float gain         = m_volume * m_attenuation * (m_mute ? 0.0f : 1.0f);
        float left_factor  = sqrt(0.5f * (1.0f - m_pan));
        float right_factor = sqrt(0.5f * (1.0f + m_pan));
        float left_gain    = gain * left_factor;
        float right_gain   = gain * right_factor;

        for (uint32_t i = 0; i < num_samples; ++i)
        {
            m_stereo_chunk[2 * i]     *= left_gain;
            m_stereo_chunk[2 * i + 1] *= right_gain;
        }

        // apply reverb effect if enabled
        if (m_reverb_enabled && !m_reverb_buffer_l.empty())
        {
            const uint32_t base_delays[4] = { 1087, 1283, 1511, 1777 };
            const float room_scale        = 0.3f + m_reverb_room_size * 0.7f;
            uint32_t delays[4];
            for (int d = 0; d < 4; ++d)
                delays[d] = static_cast<uint32_t>(base_delays[d] * room_scale);

            const float feedback = m_reverb_decay * 0.7f;
            const float wet      = m_reverb_wet;
            const float dry      = 1.0f - wet * 0.5f;

            for (uint32_t i = 0; i < num_samples; ++i)
            {
                float dry_l = m_stereo_chunk[2 * i];
                float dry_r = m_stereo_chunk[2 * i + 1];

                float reverb_l = 0.0f;
                float reverb_r = 0.0f;
                for (int d = 0; d < 4; ++d)
                {
                    uint32_t read_pos_l = (m_reverb_write_pos + reverb_buffer_size - delays[d]) % reverb_buffer_size;
                    uint32_t read_pos_r = (m_reverb_write_pos + reverb_buffer_size - delays[d] - 23) % reverb_buffer_size;
                    reverb_l += m_reverb_buffer_l[read_pos_l] * 0.25f;
                    reverb_r += m_reverb_buffer_r[read_pos_r] * 0.25f;
                }

                m_reverb_buffer_l[m_reverb_write_pos] = dry_l + reverb_l * feedback;
                m_reverb_buffer_r[m_reverb_write_pos] = dry_r + reverb_r * feedback;

                m_stereo_chunk[2 * i]     = dry_l * dry + reverb_l * wet;
                m_stereo_chunk[2 * i + 1] = dry_r * dry + reverb_r * wet;

                m_reverb_write_pos = (m_reverb_write_pos + 1) % reverb_buffer_size;
            }
        }

        if (!SDL_PutAudioStreamData(m_stream, m_stereo_chunk.data(), static_cast<int>(m_stereo_chunk.size() * sizeof(float))))
        {
            SP_LOG_ERROR("%s", SDL_GetError());
        }
    }

    void AudioSource::Save(pugi::xml_node& node)
    {
        node.append_attribute("path")              = m_file_path.c_str();
        node.append_attribute("is_3d")             = m_is_3d;
        node.append_attribute("mute")              = m_mute;
        node.append_attribute("loop")              = m_loop;
        node.append_attribute("play_on_start")     = m_play_on_start;
        node.append_attribute("volume")            = m_volume;
        node.append_attribute("pitch")             = m_pitch;
        node.append_attribute("reverb_enabled")    = m_reverb_enabled;
        node.append_attribute("reverb_room_size")  = m_reverb_room_size;
        node.append_attribute("reverb_decay")      = m_reverb_decay;
        node.append_attribute("reverb_wet")        = m_reverb_wet;
    }

    void AudioSource::Load(pugi::xml_node& node)
    {
        m_file_path        = node.attribute("path").as_string("N/A");
        m_is_3d            = node.attribute("is_3d").as_bool(false);
        m_mute             = node.attribute("mute").as_bool(false);
        m_loop             = node.attribute("loop").as_bool(true);
        m_play_on_start    = node.attribute("play_on_start").as_bool(true);
        m_volume           = node.attribute("volume").as_float(1.0f);
        m_pitch            = node.attribute("pitch").as_float(1.0f);
        m_reverb_enabled   = node.attribute("reverb_enabled").as_bool(false);
        m_reverb_room_size = node.attribute("reverb_room_size").as_float(0.5f);
        m_reverb_decay     = node.attribute("reverb_decay").as_float(0.5f);
        m_reverb_wet       = node.attribute("reverb_wet").as_float(0.3f);

        SetAudioClip(m_file_path);
    }

    sol::reference AudioSource::AsLua(sol::state_view state)
    {
        return sol::make_reference(state, this);
    }

    void AudioSource::SetAudioClip(const string& file_path)
    {
        // store the filename from the provided path
        m_file_path = file_path;
        m_name      = FileSystem::GetFileNameFromFilePath(file_path);
        m_clip      = audio_clip_cache::Get(file_path);
        if (!m_clip)
        {
            SP_LOG_ERROR("Failed to load audio clip: %s", file_path.c_str());
        }
    }

    void AudioSource::PlayClip()
    {
        if (!m_clip || m_clip->length == 0)
        {
            SP_LOG_ERROR("No valid audio clip set");
            return;
        }

        // create stream: source is stereo float32, destination is device spec
        SDL_AudioSpec src_spec = {};
        src_spec.freq          = m_clip->spec->freq;
        src_spec.format        = SDL_AUDIO_F32;
        src_spec.channels      = 2;
        m_stream = SDL_CreateAudioStream(&src_spec, &audio_device::spec);
        if (!m_stream)
        {
            SP_LOG_ERROR("%s", SDL_GetError());
            return;
        }

        CHECK_SDL_ERROR(SDL_BindAudioStream(audio_device::id, m_stream));

        // initialize reverb buffers
        m_reverb_buffer_l.assign(reverb_buffer_size, 0.0f);
        m_reverb_buffer_r.assign(reverb_buffer_size, 0.0f);
        m_reverb_write_pos = 0;

        // start playing
        CHECK_SDL_ERROR(SDL_ResumeAudioStreamDevice(m_stream));
        m_position   = 0;
        m_is_playing = true;
        SetPitch(m_pitch);
    }

    void AudioSource::StopClip()
    {
        if (!m_is_playing)
            return;

        if (m_stream)
        {
            SDL_ClearAudioStream(m_stream);
            SDL_DestroyAudioStream(m_stream);
            m_stream = nullptr;
        }
        m_is_playing = false;
        m_position = 0;
    }

    float AudioSource::GetProgress() const
    {
        if (!m_clip || m_clip->length == 0)
            return 0.0f;

        return static_cast<float>(m_position) / static_cast<float>(m_clip->length);
    }

    void AudioSource::SetMute(bool mute)
    {
        if (m_mute == mute)
            return;

        m_mute = mute;
    }

    void AudioSource::SetVolume(float volume)
    {
        m_volume = clamp(volume, 0.0f, 1.0f);
    }

    void AudioSource::SetPitch(const float pitch)
    {
        m_pitch = clamp(pitch, 0.01f, 5.0f);

        if (m_is_playing && m_stream)
        {
            const float effective_pitch = m_pitch * m_doppler_ratio;
            CHECK_SDL_ERROR(SDL_SetAudioStreamFrequencyRatio(m_stream, effective_pitch));
        }
    }

    void AudioSource::SetReverbRoomSize(const float room_size)
    {
        m_reverb_room_size = clamp(room_size, 0.0f, 1.0f);
    }

    void AudioSource::SetReverbDecay(const float decay)
    {
        m_reverb_decay = clamp(decay, 0.0f, 0.99f); // cap at 0.99 to prevent infinite buildup
    }

    void AudioSource::SetReverbWet(const float wet)
    {
        m_reverb_wet = clamp(wet, 0.0f, 1.0f);
    }

    void AudioSource::FeedAudioChunk()
    {
        if (!m_stream || !m_is_playing)
            return;

        int queued               = SDL_GetAudioStreamQueued(m_stream);
        const int low_water_mark = 16384;
        if (queued >= low_water_mark)
            return;

        const uint32_t target_mono_samples = 2048;
        uint32_t bytes_to_add = target_mono_samples * sizeof(float);
        if (m_position + bytes_to_add > m_clip->length)
        {
            bytes_to_add = m_clip->length - m_position;
        }

        if (bytes_to_add == 0)
        {
            if (m_loop)
            {
                m_position = 0;
                bytes_to_add = min<uint32_t>(target_mono_samples * sizeof(float), m_clip->length);
            }
            else
            {
                StopClip();
                return;
            }
        }

        uint32_t num_samples = bytes_to_add / sizeof(float);
        float* mono_samples  = reinterpret_cast<float*>(m_clip->buffer + m_position);
        m_stereo_chunk.resize(num_samples * 2); // reuses capacity, no allocation if size fits
        float gain           = m_volume * m_attenuation * (m_mute ? 0.0f : 1.0f);

        // constant power panning
        float left_factor    = sqrt(0.5f * (1.0f - m_pan));
        float right_factor   = sqrt(0.5f * (1.0f + m_pan));
        float left_gain      = gain * left_factor;
        float right_gain     = gain * right_factor;
        for (uint32_t i = 0; i < num_samples; ++i)
        {
            float sample = mono_samples[i];
            m_stereo_chunk[2 * i] = sample * left_gain;
            m_stereo_chunk[2 * i + 1]= sample * right_gain;
        }

        // apply reverb effect using a feedback delay network
        if (m_reverb_enabled && !m_reverb_buffer_l.empty())
        {
            // delay tap offsets scaled by room size (in samples at 48khz)
            // these prime-number-based delays create a more natural reverb
            const uint32_t base_delays[4] = { 1087, 1283, 1511, 1777 };
            const float room_scale        = 0.3f + m_reverb_room_size * 0.7f;
            uint32_t delays[4];
            for (int d = 0; d < 4; ++d)
            {
                delays[d] = static_cast<uint32_t>(base_delays[d] * room_scale);
            }

            const float feedback = m_reverb_decay * 0.7f; // scale feedback for stability
            const float wet      = m_reverb_wet;
            const float dry      = 1.0f - wet * 0.5f; // keep dry signal prominent

            for (uint32_t i = 0; i < num_samples; ++i)
            {
                float dry_l = m_stereo_chunk[2 * i];
                float dry_r = m_stereo_chunk[2 * i + 1];

                // read from multiple delay taps and sum for diffuse reverb
                float reverb_l = 0.0f;
                float reverb_r = 0.0f;
                for (int d = 0; d < 4; ++d)
                {
                    uint32_t read_pos_l = (m_reverb_write_pos + reverb_buffer_size - delays[d]) % reverb_buffer_size;
                    uint32_t read_pos_r = (m_reverb_write_pos + reverb_buffer_size - delays[d] - 23) % reverb_buffer_size; // slight offset for stereo width
                    reverb_l += m_reverb_buffer_l[read_pos_l] * 0.25f;
                    reverb_r += m_reverb_buffer_r[read_pos_r] * 0.25f;
                }

                // write new samples with feedback to the delay buffer
                m_reverb_buffer_l[m_reverb_write_pos] = dry_l + reverb_l * feedback;
                m_reverb_buffer_r[m_reverb_write_pos] = dry_r + reverb_r * feedback;

                // mix dry and wet signals
                m_stereo_chunk[2 * i]     = dry_l * dry + reverb_l * wet;
                m_stereo_chunk[2 * i + 1] = dry_r * dry + reverb_r * wet;

                // advance write position
                m_reverb_write_pos = (m_reverb_write_pos + 1) % reverb_buffer_size;
            }
        }

        if (!SDL_PutAudioStreamData(m_stream, m_stereo_chunk.data(), static_cast<int>(m_stereo_chunk.size() * sizeof(float))))
        {
            SP_LOG_ERROR("%s", SDL_GetError());
        }
        m_position += bytes_to_add;
    }
}
