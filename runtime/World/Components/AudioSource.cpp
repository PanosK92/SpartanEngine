/*
copyright(c) 2016-2025 panos karabelas

permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "software"), to deal
in the software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the software, and to permit persons to whom the software is furnished
to do so, subject to the following conditions :

the above copyright notice and this permission notice shall be included in
all copies or substantial portions of the software.

the software is provided "as is", without warranty of any kind, express or
implied, including but not limited to the warranties of merchantability, fitness
for a particular purpose and noninfringement. in no event shall the authors or
copyright holders be liable for any claim, damages or other liability, whether
in an action of contract, tort or otherwise, arising from, out of or in
connection with the software or the use or other dealings in the software.
*/

//= includes ========================
#include "pch.h"
#include "AudioSource.h"
SP_WARNINGS_OFF
#include <SDL3/SDL_audio.h>
SP_WARNINGS_ON
#include "../../IO/FileStream.h"
#include "../../Rendering/Renderer.h"
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
                if (SDL_GetAudioStreamAvailable(m_stream) == 0) // buffer empty, restart
                {
                    Stop(); // destroy stream
                    Play(); // create stream
                }
            }

            if (m_is_3d)
            {
                if (Camera* camera = Renderer::GetCamera().get())
                {
                    Vector3 camera_position = camera->GetEntity()->GetPosition();
                    Vector3 sound_position  = GetEntity()->GetPosition();

                    // panning
                    {
                        Vector3 camera_to_sound = (sound_position - camera_position).Normalized();
                        float camera_dot_sound  = abs(Vector3::Dot(camera->GetEntity()->GetForward(), camera_to_sound));

                        // todo
                        // Using something SDL_SetAudioStreamPutCallback or similar to have a callback
                        // in which we can modualte the bytes of each channel to do panning
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

        // allocate an audio spec and load the wav file into our buffer
        CHECK_SDL_ERROR(SDL_LoadWAV(file_path.c_str(), &audio_device::spec, &m_buffer, &m_length));
    }

    void AudioSource::Play()
    {
        if (m_is_playing)
            return;

        m_stream = SDL_CreateAudioStream(&audio_device::spec, &audio_device::spec);
        CHECK_SDL_ERROR(SDL_BindAudioStream(audio_device::id, m_stream));
        CHECK_SDL_ERROR(SDL_ResumeAudioStreamDevice(m_stream));
        CHECK_SDL_ERROR(SDL_PutAudioStreamData(m_stream, m_buffer, m_length));

        m_is_playing = true;

        SetVolume(m_volume);
    }

    void AudioSource::Stop()
    {
        if (!m_is_playing)
            return;

        // re-create the stream so that playback can start from the beginning again
        SDL_DestroyAudioStream(m_stream);
        m_stream = SDL_CreateAudioStream(&audio_device::spec, &audio_device::spec);

        m_is_playing = false;
    }

    float AudioSource::GetProgress() const
    {
        if (!m_is_playing)
            return 0.0f;

        return 1.0f; // todo: track bytes
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
            CHECK_SDL_ERROR(SDL_SetAudioDeviceGain(audio_device::id, m_volume * m_attenuation * mute));
        }
    }
}
