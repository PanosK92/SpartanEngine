/*
Copyright(c) 2015-2025 Panos Karabelas

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

//= includes ========================
#include "pch.h"
#include "AudioSource.h"
SP_WARNINGS_OFF
#include <SDL3/SDL_audio.h>
SP_WARNINGS_ON
#include "../../IO/FileStream.h"
#include "Camera.h"
#include "../Entity.h"
//===================================

using namespace std;
using namespace spartan::math;

#define CHECK_SDL_ERROR(call)           \
if (!(call)) {                          \
    SP_LOG_ERROR("%s", SDL_GetError()); \
    return;                             \
}

namespace audio_device
{
    mutex device_mutex;
    SDL_AudioSpec spec;
    uint32_t id         = 0;
    uint32_t references = 0;

    // acquire the shared audio device, open it if it's not already open
    void acquire()
    {
        lock_guard<mutex> lock(device_mutex);
        if (references == 0)
        {
            id = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
            if (id == 0)
            {
                SP_LOG_ERROR("%s", SDL_GetError());
                return;
            }
            // Query actual obtained spec
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
        Stop();

        if (m_stream)
        {
            SDL_DestroyAudioStream(m_stream);
            m_stream = nullptr;
        }

        if (m_buffer)
        {
            SDL_free(m_buffer);
            m_buffer = nullptr;
        }

        audio_device::release();

        delete m_spec;
        m_spec = nullptr;
    }

    void AudioSource::OnInitialize()
    {
        Component::OnInitialize();
    }

    void AudioSource::OnStart()
    {
        if (m_play_on_start)
        {
            Play();
        }
    }

    void AudioSource::OnStop()
    {
        Stop();
    }

    void AudioSource::OnRemove()
    {
        Stop();
    }

    void AudioSource::OnTick()
    {
        if (m_is_playing)
        {
            if (m_loop)
            {
                int queued = SDL_GetAudioStreamQueued(m_stream);
                int available = SDL_GetAudioStreamAvailable(m_stream);
                if (queued <= 0 && available <= 0) // fully done (handles buffering/resampling)
                {
                    Stop(); // destroy stream
                    Play(); // restart
                }
            }
            if (m_is_3d)
            {
                if (Camera* camera = World::GetCamera())
                {
                    Vector3 camera_position = camera->GetEntity()->GetPosition();
                    Vector3 sound_position  = GetEntity()->GetPosition();
                    // panning
                    {
                        Vector3 camera_to_sound = (sound_position - camera_position).Normalized();
                        float camera_dot_sound  = abs(Vector3::Dot(camera->GetEntity()->GetForward(), camera_to_sound));
                        // todo
                    }
                    // attenuation
                    {
                        // inverse square law with a rolloff factor
                        float distance_squared     = Vector3::DistanceSquared(camera_position, sound_position);
                        const float rolloff_factor = 15.0f;
                        m_attenuation              = 1.0f / (1.0f + (distance_squared / (rolloff_factor * rolloff_factor)));
                        m_attenuation              = max(0.0f, min(m_attenuation, 1.0f));
                        SetVolume(m_volume);
                    }
                }
            }
        }
    }

    void AudioSource::Serialize(FileStream* stream)
    {
        stream->Write(m_mute);
        stream->Write(m_loop);
        stream->Write(m_play_on_start);
        stream->Write(m_volume);
    }

    void AudioSource::Deserialize(FileStream* stream)
    {
        stream->Read(&m_mute);
        stream->Read(&m_loop);
        stream->Read(&m_play_on_start);
        stream->Read(&m_volume);
    }

    void AudioSource::SetAudioClip(const string& file_path)
    {
        // store the filename from the provided path
        m_name = FileSystem::GetFileNameFromFilePath(file_path);
        
        // use local spec to avoid overwriting global device spec
        SDL_AudioSpec wav_spec;
        CHECK_SDL_ERROR(SDL_LoadWAV(file_path.c_str(), &wav_spec, &m_buffer, &m_length));
        
        // store the WAV's actual spec
        if (m_spec)
        {
            delete m_spec;
        }
        m_spec = new SDL_AudioSpec(wav_spec);
    }

    void AudioSource::Play()
    {
        if (m_is_playing)
            return;
        
        // create stream with WAV spec as src, device spec as dst (handles conversion)
        m_stream = SDL_CreateAudioStream(m_spec, &audio_device::spec);
        CHECK_SDL_ERROR(SDL_BindAudioStream(audio_device::id, m_stream));
        
        // start playing
        CHECK_SDL_ERROR(SDL_ResumeAudioStreamDevice(m_stream));
        CHECK_SDL_ERROR(SDL_PutAudioStreamData(m_stream, m_buffer, m_length));
        m_is_playing = true;
        
        // set user volume and pitch
        SetVolume(m_volume);
        SetPitch(m_pitch);
    }

    void AudioSource::Stop()
    {
        if (!m_is_playing)
            return;

        SDL_DestroyAudioStream(m_stream);
        m_stream    = nullptr;
        m_is_playing = false;
    }

    float AudioSource::GetProgress() const
    {
        if (!m_is_playing || !m_stream || m_length == 0)
            return 0.0f;
        
        int queued = SDL_GetAudioStreamQueued(m_stream);
        if (queued < 0)
        {
            SP_LOG_ERROR("%s", SDL_GetError());
            return 0.0f;
        }
        

        float remaining = static_cast<float>(queued) / static_cast<float>(m_length);
        remaining       = min(remaining, 1.0f);
        
        return 1.0f - remaining;
    }

    void AudioSource::SetMute(bool mute)
    {
        if (m_mute == mute)
            return;

        m_mute = mute;
        SetVolume(m_volume);
    }

    void AudioSource::SetVolume(float volume)
    {
        m_volume = clamp(volume, 0.0f, 1.0f);
        if (m_is_playing)
        {
            float mute = m_mute ? 0.0f : 1.0f;
            CHECK_SDL_ERROR(SDL_SetAudioStreamGain(m_stream, m_volume * m_attenuation * mute));
        }
    }

    void AudioSource::SetPitch(const float pitch)
    {
        m_pitch = clamp(pitch, 0.01f, 100.0f);
        if (m_is_playing)
        {
            CHECK_SDL_ERROR(SDL_SetAudioStreamFrequencyRatio(m_stream, m_pitch));
        }
    }
}
