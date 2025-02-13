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

//= includes ===================
#include "pch.h"
#include "AudioSource.h"
#include "../../IO/FileStream.h"
#include <SDL3/SDL_audio.h>
//==============================

using namespace std;
using namespace spartan::math;

#define CHECK_SDL_ERROR(call)           \
if (!(call)) {                          \
    SP_LOG_ERROR("%s", SDL_GetError()); \
    return;                             \
}

namespace
{
    mutex device_mutex;
    uint32_t shared_device_id   = 0;
    uint32_t shared_device_refs = 0;
    SDL_AudioSpec spec;

    // acquire the shared audio device; open it if it's not already open
    void acquire_shared_device()
    {
        lock_guard<mutex> lock(device_mutex);
        if (shared_device_refs == 0)
        {
            shared_device_id = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
            if (shared_device_id == 0)
            {
                SP_LOG_ERROR("%s", SDL_GetError());
            }
        }

        ++shared_device_refs;
    }

    // release the shared audio device; close it when no one is using it
    void release_shared_device()
    {
        lock_guard<mutex> lock(device_mutex);
        --shared_device_refs;
        if (shared_device_refs == 0 && shared_device_id != 0)
        {
            SDL_CloseAudioDevice(shared_device_id);
            shared_device_id = 0;
        }
    }
}

namespace spartan
{
    AudioSource::AudioSource(Entity* entity) : Component(entity)
    {
        acquire_shared_device();
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

        release_shared_device();
    }

    void AudioSource::OnInitialize()
    {
        Component::OnInitialize();
    }

    void AudioSource::OnStart()
    {
        if (!m_play_on_start)
            return;

        Play();
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
        if (m_loop)
        {
            if (SDL_GetAudioStreamAvailable(m_stream) < static_cast<int>(m_length))
            {
                SDL_PutAudioStreamData(m_stream, m_buffer, m_length);
            }
        }
    }

    void AudioSource::Serialize(FileStream* stream)
    {
        stream->Write(m_mute);
        stream->Write(m_loop);
        stream->Write(m_play_on_start);
        stream->Write(m_volume);
        stream->Write(m_pitch);
        stream->Write(m_pan);
    }

    void AudioSource::Deserialize(FileStream* stream)
    {
        stream->Read(&m_mute);
        stream->Read(&m_loop);
        stream->Read(&m_play_on_start);
        stream->Read(&m_volume);
        stream->Read(&m_pitch);
        stream->Read(&m_pan);
    }

    void AudioSource::SetAudioClip(const string& file_path)
    {
        // store the filename from the provided path
        m_name = FileSystem::GetFileNameFromFilePath(file_path);

        // allocate an audio spec and load the wav file into our buffer
        CHECK_SDL_ERROR(SDL_LoadWAV(file_path.c_str(), &spec, &m_buffer, &m_length));

        // create an audio stream for conversion (assuming source and device specs are the same)
        m_stream = SDL_CreateAudioStream(&spec, &spec);
        if (!m_stream)
        {
            SP_LOG_ERROR("%s", SDL_GetError());
            SDL_CloseAudioDevice(shared_device_id);
            SDL_free(m_buffer);
            return;
        }

        // bind stream and pause (as it starts playing automatically)
        CHECK_SDL_ERROR(SDL_BindAudioStream(shared_device_id, m_stream));
        CHECK_SDL_ERROR(SDL_PauseAudioStreamDevice(m_stream));
    }

    void AudioSource::Play()
    {
        if (m_is_playing)
            return;

        CHECK_SDL_ERROR(SDL_ResumeAudioStreamDevice(m_stream));
        CHECK_SDL_ERROR(SDL_PutAudioStreamData(m_stream, m_buffer, m_length));

        m_is_playing = true;
    }

    void AudioSource::Stop()
    {
        if (!m_is_playing)
            return;

        // re-create the stream so that playback can start from the beginning again
        SDL_DestroyAudioStream(m_stream);
        CHECK_SDL_ERROR(SDL_CreateAudioStream(&spec, &spec));

        // bind the new stream to the shared device and pause it, ready for playing
        CHECK_SDL_ERROR(SDL_BindAudioStream(shared_device_id, m_stream));
        CHECK_SDL_ERROR(SDL_PauseAudioStreamDevice(m_stream));

        m_is_playing = false;
    }

    float AudioSource::GetProgress() const
    {
        return 0.0f;
    }

    void AudioSource::SetMute(bool mute)
    {
        if (m_mute == mute)
            return;

        m_mute = mute;
    }

    void AudioSource::SetLoop(const bool loop)
    {
        m_loop = loop;
    }

    void AudioSource::SetVolume(float volume)
    {
        m_volume = clamp(volume, 0.0f, 1.0f);

        if (!SDL_SetAudioDeviceGain(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, m_volume))
        {
            SP_LOG_ERROR("%s", SDL_GetError());
        }
    }

    void AudioSource::SetPitch(float pitch)
    {
        m_pitch = clamp(pitch, 0.0f, 3.0f);
    }

    void AudioSource::SetPan(float pan)
    {
        m_pan = clamp(pan, -1.0f, 1.0f);
    }
}
