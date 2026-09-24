/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===============
#include "pch.h"
#include "AudioSource.h"
#include "Camera.h"
#include "Volume.h"
#include "Terrain.h"
#include "Render.h"
#include "../TerrainAcoustics.h"
#include "../Entity.h"
#include "../World.h"
#include "../../core/Engine.h"
#include "../../core/ThreadPool.h"
#include "../../commands/console/ConsoleCommands.h"
#include "../../file_system/FileSystem.h"
SP_WARNINGS_OFF
#include <sol/sol.hpp>
#include <SDL3/SDL_audio.h>
#define DR_MP3_IMPLEMENTATION
#include <SDL3/dr_mp3.h>
#include "../io/pugixml.hpp"
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
    mutex cache_mutex;

    shared_ptr<AudioClip> Get(const string& file_path, bool stereo = false)
    {
        // parallel entity loads can hit this together, the map is not thread safe without a lock
        lock_guard<mutex> lock(cache_mutex);

        const string key = file_path + (stereo ? "#stereo" : "#mono");
        auto it = cache.find(key);
        if (it != cache.end())
        {
            if (shared_ptr<AudioClip> existing = it->second.lock())
            {
                return existing;
            }
        }

        // decode into an interleaved f32 source buffer, mp3 via dr_mp3, everything else via sdl wav
        SDL_AudioSpec source_spec = {};
        uint8_t* source_buffer    = nullptr;
        uint32_t source_length    = 0;
        const bool is_mp3         = spartan::FileSystem::ConvertToUppercase(spartan::FileSystem::GetExtensionFromFilePath(file_path)) == ".MP3";
        if (is_mp3)
        {
            drmp3_config config      = {};
            drmp3_uint64 frame_count = 0;
            float* pcm               = drmp3_open_file_and_read_pcm_frames_f32(file_path.c_str(), &config, &frame_count, nullptr);
            if (!pcm)
            {
                SP_LOG_ERROR("Failed to decode mp3 '%s'.", file_path.c_str());
                return nullptr;
            }
            source_spec.freq     = static_cast<int>(config.sampleRate);
            source_spec.format   = SDL_AUDIO_F32;
            source_spec.channels = static_cast<int>(config.channels);
            source_buffer        = reinterpret_cast<uint8_t*>(pcm);
            source_length        = static_cast<uint32_t>(frame_count * config.channels * sizeof(float));
        }
        else
        {
            if (!SDL_LoadWAV(file_path.c_str(), &source_spec, &source_buffer, &source_length))
            {
                SP_LOG_ERROR("%s", SDL_GetError());
                return nullptr;
            }
        }

        SDL_AudioSpec target_spec = {};
        target_spec.freq          = source_spec.freq;
        target_spec.format        = SDL_AUDIO_F32;
        target_spec.channels      = stereo ? 2 : 1;
        uint8_t* target_buffer    = nullptr;
        int target_length         = 0;
        const bool converted      = SDL_ConvertAudioSamples(&source_spec, source_buffer, static_cast<int>(source_length), &target_spec, &target_buffer, &target_length);
        if (is_mp3)
        {
            drmp3_free(source_buffer, nullptr);
        }
        else
        {
            SDL_free(source_buffer);
        }
        if (!converted)
        {
            SP_LOG_ERROR("%s", SDL_GetError());
            return nullptr;
        }

        shared_ptr<AudioClip> clip = make_shared<AudioClip>();
        clip->buffer = target_buffer;
        clip->length = static_cast<uint32_t>(target_length);
        clip->spec   = new SDL_AudioSpec(target_spec);

        cache[key] = clip;
        return clip;
    }

    void ReleaseAll()
    {
        lock_guard<mutex> lock(cache_mutex);
        cache.clear();
    }
}

namespace audio_device
{
    mutex device_mutex;
    SDL_AudioSpec spec  = {};
    uint32_t id         = 0;
    uint32_t references = 0;
    float limiter_gain  = 1.0f; // audio thread

    // every source sums into one bus, keep the sum under full scale instead of letting it hard clip:
    // instant attack to exactly the ceiling, slow release so the gain riding is inaudible
    void SDLCALL master_limiter(void* userdata, const SDL_AudioSpec* mix_spec, float* buffer, int buffer_bytes)
    {
        const int channels = max(mix_spec->channels, 1);
        const int frames   = buffer_bytes / static_cast<int>(sizeof(float) * channels);
        const float ceiling = 0.97f;
        const float release = 1.0f - expf(-1.0f / (0.15f * static_cast<float>(max(mix_spec->freq, 8000))));
        for (int frame = 0; frame < frames; frame++)
        {
            float* samples = buffer + frame * channels;
            float peak = 0.0f;
            for (int c = 0; c < channels; c++)
            {
                peak = max(peak, fabsf(samples[c]));
            }
            const float target = peak > ceiling ? ceiling / peak : 1.0f;
            limiter_gain = target < limiter_gain ? target : limiter_gain + (target - limiter_gain) * release;
            if (limiter_gain < 1.0f)
            {
                for (int c = 0; c < channels; c++)
                {
                    samples[c] *= limiter_gain;
                }
            }
        }
    }

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
            limiter_gain = 1.0f;
            if (!SDL_SetAudioPostmixCallback(id, master_limiter, nullptr))
            {
                SP_LOG_ERROR("%s", SDL_GetError());
            }
        }
        ++references;
    }

    int sample_rate()
    {
        lock_guard<mutex> lock(device_mutex);
        return spec.freq > 0 ? spec.freq : 48000;
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
    namespace
    {
        TConsoleVar<float> ambience_volume("audio.ambience_volume", 1.0f, "soundscape master volume, 0 to 1; leaves vehicles and other audio unchanged");
    }
    AudioSource::AudioSource(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_GET_SET(GetAudioClipName, SetAudioClip, std::string);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_is_3d, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_mute, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_loop, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_play_on_start, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_volume, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_pitch, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_reverb_enabled, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_reverb_room_size, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_reverb_decay, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_reverb_wet, float);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetAmbient, SetAmbient, bool);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetAmbientProfile, SetAmbientProfile, uint32_t);

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
            "SetPitch",                     &AudioSource::SetPitch,

            "GetLoop",                      &AudioSource::GetLoop,
            "SetLoop",                      &AudioSource::SetLoop,
            "GetVolume",                    &AudioSource::GetVolume,
            "SetVolume",                    &AudioSource::SetVolume,
            "GetPlayOnStart",               &AudioSource::GetPlayOnStart,
            "SetPlayOnStart",               &AudioSource::SetPlayOnStart,
            "GetIs3d",                      &AudioSource::GetIs3d,
            "SetIs3d",                      &AudioSource::SetIs3d,
            "GetAmbient",                   &AudioSource::GetAmbient,
            "SetAmbient",                   &AudioSource::SetAmbient,
            "GetAmbientGain",               &AudioSource::GetAmbientGain,
            "GetAmbientProfile",            &AudioSource::GetAmbientProfile,
            "SetAmbientProfile",            &AudioSource::SetAmbientProfile,
            "GetHabitatGain",               &AudioSource::GetHabitatGain,
            "GetReverbEnabled",             &AudioSource::GetReverbEnabled,
            "SetReverbEnabled",             &AudioSource::SetReverbEnabled,
            "GetReverbRoomSize",            &AudioSource::GetReverbRoomSize,
            "SetReverbRoomSize",            &AudioSource::SetReverbRoomSize,
            "GetReverbDecay",               &AudioSource::GetReverbDecay,
            "SetReverbDecay",               &AudioSource::SetReverbDecay,
            "GetReverbWet",                 &AudioSource::GetReverbWet,
            "SetReverbWet",                 &AudioSource::SetReverbWet
            );
    }

    void AudioSource::Initialize()
    {
        Component::Initialize();
    }

    void AudioSource::Start()
    {
        if (m_play_on_start && !m_ambient)
        {
            PlayClip();
            m_auto_play_consumed = true;
        }
    }

    void AudioSource::Stop()
    {
        StopClip();
        m_auto_play_consumed = false;
        m_ambient_gain = m_ambient_target = m_ambient_update_timer = 0.0f;
    }

    void AudioSource::Remove()
    {
        StopClip();
    }

    void AudioSource::Tick()
    {
        const bool in_play_mode = Engine::IsFlagSet(EngineMode::Playing) && !Engine::IsFlagSet(EngineMode::Paused);
        if (m_ambient)
        {
            TickAmbient(in_play_mode);
            return;
        }

        // auto start playback when entering play mode, covers cases where Start was missed
        // due to async world load timing or entities arriving after the play transition
        if (in_play_mode && m_play_on_start && !m_is_playing && !m_auto_play_consumed && !m_synthesis_mode && m_clip)
        {
            PlayClip();
            m_auto_play_consumed = true;
        }

        // a stopped synthesis stream keeps playing its fade, release it once the audio thread reports silence
        if (!m_is_playing && m_synthesis_mode && m_stream && m_synthesis_state.load(memory_order_acquire) == synthesis_silent)
        {
            DestroyStream();
        }

        if (!m_is_playing)
        {
            return;
        }

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

            for (Entity* entity : World::GetEntitiesWithVolume())
            {
                Volume* volume = entity->GetComponent<Volume>();
                if (!volume || !volume->GetReverbEnabled())
                {
                    continue;
                }

                // transform the volume's local bounding box into world space
                BoundingBox transformed_box = volume->GetBoundingBox() * entity->GetMatrix();
                if (transformed_box.Contains(source_position))
                {
                    // allocate reverb buffers if they haven't been yet, a synthesis stream owns its own
                    if (m_reverb_buffer_l.empty() && !m_synthesis_mode)
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

        // clips are pushed from here, synthesis is pulled by the audio thread and only needs the mix
        if (m_synthesis_mode)
        {
            PublishSynthesisMix();
        }
        else
        {
            FeedAudioChunk();
        }
    }

    void AudioSource::SetSynthesisMode(bool enabled, SynthesisCallback callback)
    {
        // the audio thread calls the callback, so it can only be swapped once no stream pulls it
        if (m_stream)
        {
            DestroyStream();
        }
        m_is_playing = false;

        m_synthesis_mode     = enabled;
        m_synthesis_callback = callback;
    }

    int AudioSource::GetDeviceSampleRate()
    {
        return audio_device::sample_rate();
    }

    void AudioSource::StartSynthesis()
    {
        if (m_is_playing) return;
        if (!m_synthesis_mode || !m_synthesis_callback)
        {
            SP_LOG_ERROR("synthesis mode not enabled or no callback set");
            return;
        }

        // restarted inside its own fade: take the stream back unless it already went silent
        if (m_stream)
        {
            int expected = synthesis_stopping;
            if (m_synthesis_state.compare_exchange_strong(expected, synthesis_running, memory_order_acq_rel))
            {
                m_is_playing = true;
                PublishSynthesisMix();
                return;
            }
            DestroyStream();
        }

        // the synthesizers render at the device rate, so sdl only converts the format
        m_synthesis_rate = audio_device::sample_rate();
        SDL_AudioSpec src_spec = {};
        src_spec.freq          = m_synthesis_rate;
        src_spec.format        = SDL_AUDIO_F32;
        src_spec.channels      = 2;
        m_stream = SDL_CreateAudioStream(&src_spec, &audio_device::spec);
        if (!m_stream)
        {
            SP_LOG_ERROR("%s", SDL_GetError());
            return;
        }

        // everything the audio thread touches is allocated here, before it can run
        m_reverb_buffer_l.assign(reverb_buffer_size, 0.0f);
        m_reverb_buffer_r.assign(reverb_buffer_size, 0.0f);
        m_reverb_write_pos = 0;
        m_synthesis_chunk.assign(synthesis_chunk_frames * 2, 0.0f);
        m_synthesis_gain_l = m_synthesis_gain_r = 0.0f;
        m_synthesis_state.store(synthesis_running, memory_order_release);
        m_is_playing = true;
        PublishSynthesisMix();

        CHECK_SDL_ERROR(SDL_SetAudioStreamGetCallback(m_stream, &AudioSource::SynthesisStreamCallback, this));
        CHECK_SDL_ERROR(SDL_BindAudioStream(audio_device::id, m_stream));
        SetPitch(m_pitch);
        CHECK_SDL_ERROR(SDL_ResumeAudioStreamDevice(m_stream));
    }

    void AudioSource::StopSynthesis()
    {
        if (!m_is_playing)
        {
            return;
        }

        // cutting mid waveform clicks, the audio thread fades to silence and Tick releases the stream
        m_is_playing = false;
        if (m_stream)
        {
            m_synthesis_state.store(synthesis_stopping, memory_order_release);
        }
    }

    void AudioSource::DestroyStream()
    {
        if (!m_stream)
        {
            return;
        }

        // unbinding takes the device lock, so no callback is running once this returns
        SDL_ClearAudioStream(m_stream);
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
        m_synthesis_state.store(synthesis_running, memory_order_release);
    }

    void AudioSource::PublishSynthesisMix()
    {
        const float gain = m_volume * m_attenuation * (m_mute ? 0.0f : 1.0f);
        m_synthesis_target_l.store(gain * sqrt(0.5f * (1.0f - m_pan)), memory_order_relaxed);
        m_synthesis_target_r.store(gain * sqrt(0.5f * (1.0f + m_pan)), memory_order_relaxed);
        m_synthesis_room_size.store(m_reverb_room_size, memory_order_relaxed);
        m_synthesis_decay.store(m_reverb_decay, memory_order_relaxed);
        m_synthesis_wet.store(m_reverb_wet, memory_order_relaxed);
        m_synthesis_reverb.store(m_reverb_enabled, memory_order_relaxed);
    }

    void AudioSource::SynthesisStreamCallback(void* userdata, SDL_AudioStream* stream, int additional_amount, int total_amount)
    {
        if (additional_amount > 0)
        {
            static_cast<AudioSource*>(userdata)->RenderSynthesis(stream, additional_amount);
        }
    }

    void AudioSource::RenderSynthesis(SDL_AudioStream* stream, int bytes_needed)
    {
        // audio thread: no allocation, no locks, only the atomics published by the main thread
        const int state       = m_synthesis_state.load(memory_order_acquire);
        const bool stopping   = state != synthesis_running;
        const float target_l  = stopping ? 0.0f : m_synthesis_target_l.load(memory_order_relaxed);
        const float target_r  = stopping ? 0.0f : m_synthesis_target_r.load(memory_order_relaxed);
        const bool reverb     = m_synthesis_reverb.load(memory_order_relaxed) && !m_reverb_buffer_l.empty();
        const float rate      = static_cast<float>(m_synthesis_rate);
        // 5 ms slew hides frame-rate volume and pan steps, the stop fade reaches -80 db in about 40 ms
        const float slew      = 1.0f - expf(-1.0f / (rate * (stopping ? 0.0045f : 0.005f)));

        const uint32_t base_delays[6] = { 4799, 6907, 8893, 10007, 11903, 13313 };
        const float room_scale        = 0.3f + m_synthesis_room_size.load(memory_order_relaxed) * 0.7f;
        uint32_t delays[6];
        for (int d = 0; d < 6; ++d)
        {
            delays[d] = static_cast<uint32_t>(base_delays[d] * room_scale);
        }
        const float tap_gain = 1.0f / 6.0f;
        const float feedback = m_synthesis_decay.load(memory_order_relaxed) * 0.85f;
        const float wet      = m_synthesis_wet.load(memory_order_relaxed);
        const float dry      = 1.0f - wet * 0.4f;

        int frames_needed = (bytes_needed + static_cast<int>(2 * sizeof(float)) - 1) / static_cast<int>(2 * sizeof(float));
        float peak = 0.0f;
        while (frames_needed > 0)
        {
            const int frames = min(frames_needed, static_cast<int>(synthesis_chunk_frames));
            frames_needed -= frames;
            float* chunk = m_synthesis_chunk.data();

            if (state == synthesis_silent)
            {
                fill(chunk, chunk + frames * 2, 0.0f);
            }
            else
            {
                // once faded, stop pulling the shared synthesizer so the next car's stream owns it alone
                if (stopping && max(m_synthesis_gain_l, m_synthesis_gain_r) < 1e-4f)
                {
                    fill(chunk, chunk + frames * 2, 0.0f);
                }
                else
                {
                    m_synthesis_callback(chunk, frames);
                    for (int i = 0; i < frames; ++i)
                    {
                        m_synthesis_gain_l += (target_l - m_synthesis_gain_l) * slew;
                        m_synthesis_gain_r += (target_r - m_synthesis_gain_r) * slew;
                        chunk[2 * i]     *= m_synthesis_gain_l;
                        chunk[2 * i + 1] *= m_synthesis_gain_r;
                    }
                }

                // feedback delay network, 6 long taps for large-space character (tunnels, halls)
                if (reverb)
                {
                    for (int i = 0; i < frames; ++i)
                    {
                        float dry_l = chunk[2 * i];
                        float dry_r = chunk[2 * i + 1];

                        float reverb_l = 0.0f;
                        float reverb_r = 0.0f;
                        for (int d = 0; d < 6; ++d)
                        {
                            uint32_t read_pos_l = (m_reverb_write_pos + reverb_buffer_size - delays[d]) % reverb_buffer_size;
                            uint32_t read_pos_r = (m_reverb_write_pos + reverb_buffer_size - delays[d] - 181) % reverb_buffer_size;
                            reverb_l += m_reverb_buffer_l[read_pos_l] * tap_gain;
                            reverb_r += m_reverb_buffer_r[read_pos_r] * tap_gain;
                        }

                        m_reverb_buffer_l[m_reverb_write_pos] = dry_l + reverb_l * feedback;
                        m_reverb_buffer_r[m_reverb_write_pos] = dry_r + reverb_r * feedback;

                        chunk[2 * i]     = dry_l * dry + reverb_l * wet;
                        chunk[2 * i + 1] = dry_r * dry + reverb_r * wet;

                        m_reverb_write_pos = (m_reverb_write_pos + 1) % reverb_buffer_size;
                    }
                }

                if (stopping)
                {
                    for (int i = 0; i < frames * 2; ++i)
                    {
                        peak = max(peak, fabsf(chunk[i]));
                    }
                }
            }

            SDL_PutAudioStreamData(stream, chunk, frames * 2 * static_cast<int>(sizeof(float)));
        }

        // report silence once the fade and any reverb tail are done; a restart that raced us wins
        if (state == synthesis_stopping && max(m_synthesis_gain_l, m_synthesis_gain_r) < 1e-4f && peak < 1e-4f)
        {
            int expected = synthesis_stopping;
            m_synthesis_state.compare_exchange_strong(expected, synthesis_silent, memory_order_acq_rel);
        }
    }

    void AudioSource::SetAmbient(bool value)
    {
        if (m_ambient == value) return;
        StopClip();
        m_ambient = value;
        m_ambient_gain = m_ambient_target = m_ambient_update_timer = 0.0f;
        if (!m_file_path.empty()) SetAudioClip(m_file_path);
    }

    void AudioSource::TickAmbient(bool in_play_mode)
    {
        // Never pause the shared SDL device: the car and other sounds use it too.
        if (!in_play_mode || !m_play_on_start || !m_clip || !GetEntity()->GetActive())
        {
            StopClip();
            m_ambient_gain = m_ambient_target = m_ambient_update_timer = 0.0f;
            return;
        }

        m_ambient_update_timer -= static_cast<float>(Timer::GetDeltaTimeSec());
        if (m_ambient_update_timer <= 0.0f)
        {
            m_ambient_update_timer = 0.1f;
            Volume* region = GetEntity()->GetComponent<Volume>();
            Camera* camera = World::GetCamera();
            m_ambient_target = region && camera && !m_mute ? region->GetAudioWeight(camera->GetEntity()->GetPosition()) : 0.0f;
            if (m_ambient_target > 0.0f)
            {
                const Vector3 listener = camera->GetEntity()->GetPosition();
                // All ambient sources share one 10 Hz environmental query. The
                // existing instance spatial groups track placement, carving and
                // transforms; neither frustum visibility nor LOD affects sound.
                static double sampled_at = -1.0;
                static Vector3 sampled_position = Vector3::Infinity;
                static unordered_map<string, float> group_weights;
                static bool indoors = false;
                static float canopy = 0.0f, scrub = 0.0f, exposure = 1.0f;
                const double now = Timer::GetTimeSec();
                if (now - sampled_at >= 0.1 || Vector3::DistanceSquared(sampled_position, listener) > 25.0f)
                {
                    sampled_at = now;
                    sampled_position = listener;
                    group_weights.clear();
                    indoors = false;
                    canopy = scrub = 0.0f;
                    exposure = 1.0f;
                    // The island has many more visual props than acoustic
                    // contributors. Filter independently, then evaluate the
                    // compact list in source order to preserve gain summation.
                    static array<vector<Entity*>, 8> candidates;
                    for (auto& batch : candidates) batch.clear();
                    const auto& entities = World::GetEntities();
                    const uint32_t jobs = entities.size() >= 256 ? static_cast<uint32_t>(candidates.size()) : 1u;
                    auto collect = [&](uint32_t first, uint32_t last)
                    {
                        auto& batch = candidates[first];
                        for (size_t i = entities.size() * first / jobs; i < entities.size() * last / jobs; ++i)
                        {
                            Entity* entity = entities[i];
                            if (entity->GetActive() && (entity->GetComponent<Volume>() || entity->GetComponent<Terrain>() ||
                                entity->HasTag("terrain_canopy") || entity->HasTag("terrain_scrub")))
                                batch.push_back(entity);
                        }
                    };
                    if (jobs == 1) collect(0, 1);
                    else ThreadPool::ParallelLoop(collect, jobs);
                    for (const auto& batch : candidates)
                    for (Entity* entity : batch)
                    {
                        if (!entity->GetActive()) continue;
                        if (Volume* other = entity->GetComponent<Volume>())
                        {
                            if (other->GetReverbEnabled() && (other->GetBoundingBox() * entity->GetMatrix()).Contains(listener)) indoors = true;
                            AudioSource* source = entity->GetComponent<AudioSource>();
                            if (source && source->m_ambient && source->m_play_on_start && !source->m_mute && source->m_clip)
                                group_weights[other->GetAudioGroup()] += other->GetAudioWeight(listener);
                        }
                        if (Terrain* terrain = entity->GetComponent<Terrain>())
                        {
                            TerrainSurfaceSample surface;
                            if (terrain->SampleSurface(listener.x, listener.z, surface))
                                exposure = clamp(surface.occlusion * 0.7f + surface.insolation * 0.3f, 0.0f, 1.0f);
                        }
                        const bool tree = entity->HasTag("terrain_canopy");
                        const bool bush = entity->HasTag("terrain_scrub");
                        if (!tree && !bush) continue;
                        Render* render = entity->GetComponent<Render>();
                        // An emptied scatter renderer is not a plant at its tile origin.
                        if (!render || !render->HasInstancing()) continue;
                        render->UpdateAabb();
                        const float radius = tree ? 70.0f : 35.0f;
                        if (Vector3::DistanceSquared(listener, render->GetBoundingBox().GetClosestPoint(listener)) >= radius * radius) continue;
                        for (const Render::InstanceBoundsGroup& group : render->GetInstanceBoundsGroups())
                        {
                            if (Vector3::DistanceSquared(listener, group.bounds.GetClosestPoint(listener)) >= radius * radius) continue;
                            for (uint32_t j = 0; j < group.count; ++j)
                            {
                                const BoundingBox& bounds = render->GetInstanceBounds(render->GetGroupedInstanceIndex(group.offset + j));
                                const Vector3 size = bounds.GetSize();
                                const float distance = (listener - bounds.GetClosestPoint(listener)).Length();
                                const float area = terrain_acoustics::contribution(distance, size.x, size.z, radius);
                                (tree ? canopy : scrub) += area;
                            }
                        }
                    }
                    canopy = clamp(canopy / 600.0f, 0.0f, 1.0f);
                    scrub = clamp(scrub / 160.0f, 0.0f, 1.0f);
                }
                // Overlap does not raise the overall level. Isolated outer edges still fade to silence.
                const float total = region->GetAudioGroup().empty() ? 0.0f : group_weights[region->GetAudioGroup()];
                m_ambient_target /= std::max(1.0f, total);
                m_habitat_gain = terrain_acoustics::gain(m_ambient_profile, canopy, scrub, exposure);
                m_ambient_target *= m_habitat_gain;
                if (indoors) m_ambient_target *= 0.2f;
                const float master = ambience_volume.GetValue();
                m_ambient_target *= std::isfinite(master) ? std::clamp(master, 0.0f, 1.0f) : 0.0f;
            }
        }

        if (m_ambient_target <= 0.0001f && m_ambient_gain <= 0.0001f)
        {
            StopClip(); // distant regions have decoded shared clips, but no active streams or mixing work
            m_ambient_gain = 0.0f;
            return;
        }
        if (!m_is_playing)
        {
            PlayClip();
            if (m_is_playing)
            {
                const uint32_t frames = m_clip->length / (m_clip->spec->channels * sizeof(float));
                const auto elapsed = static_cast<uint64_t>(Timer::GetTimeSec() * m_clip->spec->freq);
                m_position = static_cast<uint32_t>((elapsed + GetEntity()->GetObjectId()) % frames) * m_clip->spec->channels * sizeof(float);
            }
        }
        // Refill enough frames even at low rendering frame rates, with a bounded queue.
        for (int i = 0; i < 4 && m_is_playing; ++i) FeedAudioChunk();
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
        node.append_attribute("ambient")           = m_ambient;
        node.append_attribute("ambient_profile")   = m_ambient_profile;
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
        m_ambient          = node.attribute("ambient").as_bool(false);
        SetAmbientProfile(node.attribute("ambient_profile").as_uint(0));

        SetAudioClip(m_file_path);
    }

    sol::reference AudioSource::AsLua(sol::state_view state)
    {
        return sol::make_reference(state, this);
    }

    void AudioSource::SetAudioClip(const string& file_path)
    {
        const bool was_playing = m_is_playing;
        StopClip();
        // store the filename from the provided path
        m_file_path = file_path;
        m_name      = FileSystem::GetFileNameFromFilePath(file_path);
        m_clip      = audio_clip_cache::Get(file_path, m_ambient);
        if (!m_clip)
        {
            SP_LOG_ERROR("Failed to load audio clip: %s", file_path.c_str());
        }
        else if (was_playing)
        {
            if (m_ambient) m_ambient_gain = 0.0f;
            PlayClip();
        }
    }

    void AudioSource::PlayClip()
    {
        if (m_is_playing) return;
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
        // Stagger regional recordings so overlapping copies do not reinforce one another.
        const uint32_t frame_bytes = static_cast<uint32_t>(m_clip->spec->channels) * sizeof(float);
        const uint32_t frames = m_clip->length / frame_bytes;
        m_position   = m_ambient && frames ? static_cast<uint32_t>(GetEntity()->GetObjectId() % frames) * frame_bytes : 0;
        m_is_playing = true;
        SetPitch(m_pitch);
    }

    void AudioSource::StopClip()
    {
        // a synthesis stream can still be fading after its source stopped playing, release it too
        if (!m_is_playing && !m_stream)
        {
            return;
        }

        DestroyStream();
        m_is_playing = false;
        m_position = 0;
    }

    float AudioSource::GetProgress() const
    {
        if (!m_clip || m_clip->length == 0)
        {
            return 0.0f;
        }

        return static_cast<float>(m_position) / static_cast<float>(m_clip->length);
    }

    void AudioSource::SetMute(bool mute)
    {
        if (m_mute == mute)
        {
            return;
        }

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
        {
            return;
        }

        int queued               = SDL_GetAudioStreamQueued(m_stream);
        const int low_water_mark = 16384;
        if (queued >= low_water_mark)
        {
            return;
        }

        const uint32_t target_mono_samples = 2048;
        const uint32_t channels = static_cast<uint32_t>(m_clip->spec->channels);
        const uint32_t frame_bytes = channels * sizeof(float);
        uint32_t bytes_to_add = target_mono_samples * frame_bytes;
        if (m_position + bytes_to_add > m_clip->length)
        {
            bytes_to_add = m_clip->length - m_position;
        }

        if (bytes_to_add == 0)
        {
            if (m_loop)
            {
                m_position = 0;
                bytes_to_add = min<uint32_t>(target_mono_samples * frame_bytes, m_clip->length);
            }
            else
            {
                StopClip();
                return;
            }
        }

        uint32_t num_samples = bytes_to_add / frame_bytes;
        float* mono_samples  = reinterpret_cast<float*>(m_clip->buffer + m_position);
        m_stereo_chunk.resize(num_samples * 2); // reuses capacity, no allocation if size fits
        float gain           = m_volume * m_attenuation * (m_mute ? 0.0f : 1.0f);

        // constant power panning
        float left_factor    = sqrt(0.5f * (1.0f - m_pan));
        float right_factor   = sqrt(0.5f * (1.0f + m_pan));
        float left_gain      = gain * left_factor;
        float right_gain     = gain * right_factor;
        const float ambient_slew = m_ambient
            ? audio_region::slew(0.0f, 1.0f, static_cast<float>(m_clip->spec->freq), 0.5f)
            : 0.0f;
        for (uint32_t i = 0; i < num_samples; ++i)
        {
            if (m_ambient)
            {
                m_ambient_gain += (m_ambient_target - m_ambient_gain) * ambient_slew;
                const float ambient_gain = m_volume * m_ambient_gain;
                m_stereo_chunk[2 * i] = mono_samples[i * channels] * ambient_gain;
                m_stereo_chunk[2 * i + 1] = mono_samples[i * channels + channels - 1] * ambient_gain;
            }
            else
            {
                float sample = mono_samples[i * channels];
                m_stereo_chunk[2 * i] = sample * left_gain;
                m_stereo_chunk[2 * i + 1] = sample * right_gain;
            }
        }

        // apply reverb effect using a feedback delay network
        // 6 taps with long delays for large-space character (tunnels, halls)
        if (!m_ambient && m_reverb_enabled && !m_reverb_buffer_l.empty())
        {
            const uint32_t base_delays[6] = { 4799, 6907, 8893, 10007, 11903, 13313 };
            const float room_scale        = 0.3f + m_reverb_room_size * 0.7f;
            uint32_t delays[6];
            for (int d = 0; d < 6; ++d)
            {
                delays[d] = static_cast<uint32_t>(base_delays[d] * room_scale);
            }

            const float tap_gain = 1.0f / 6.0f;
            const float feedback = m_reverb_decay * 0.85f;
            const float wet      = m_reverb_wet;
            const float dry      = 1.0f - wet * 0.4f;

            for (uint32_t i = 0; i < num_samples; ++i)
            {
                float dry_l = m_stereo_chunk[2 * i];
                float dry_r = m_stereo_chunk[2 * i + 1];

                float reverb_l = 0.0f;
                float reverb_r = 0.0f;
                for (int d = 0; d < 6; ++d)
                {
                    uint32_t read_pos_l = (m_reverb_write_pos + reverb_buffer_size - delays[d]) % reverb_buffer_size;
                    uint32_t read_pos_r = (m_reverb_write_pos + reverb_buffer_size - delays[d] - 181) % reverb_buffer_size;
                    reverb_l += m_reverb_buffer_l[read_pos_l] * tap_gain;
                    reverb_r += m_reverb_buffer_r[read_pos_r] * tap_gain;
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
        m_position += bytes_to_add;
    }
}
