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

#define CHECK_SDL_ERROR(call)               \
    if (!(call)) {                          \
        SP_LOG_ERROR("%s", SDL_GetError()); \
        return;                             \
    }

namespace spartan
{
    AudioSource::AudioSource(Entity* entity) : Component(entity)
    {

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
        m_spec = make_shared<SDL_AudioSpec>();
        CHECK_SDL_ERROR(SDL_LoadWAV(file_path.c_str(), m_spec.get(), &m_buffer, &m_length));

        // open an audio stream with the wav file's spec, conversion to the hardware format is automatic
        m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, m_spec.get(), nullptr, nullptr);
        if (!m_stream)
        {
            SP_LOG_ERROR("%s", SDL_GetError());
            SDL_free(m_buffer);
        }
    }

    void AudioSource::Play()
    {
        if (m_is_playing)
            return;

        CHECK_SDL_ERROR(SDL_ResumeAudioStreamDevice(m_stream));
        // feed the entire wav data into the stream; for looping playback, you'll want to call this
        // again when the stream's available data falls below a certain threshold (in an update loop)
        CHECK_SDL_ERROR(SDL_PutAudioStreamData(m_stream, m_buffer, m_length));

        m_is_playing = true;
    }

    void AudioSource::Stop()
    {
        if (!m_is_playing)
            return;

        // pause the audio stream to halt playback
        CHECK_SDL_ERROR(!SDL_PauseAudioStreamDevice(m_stream));
        // flush any queued audio data so that playback starts from the beginning next time
        CHECK_SDL_ERROR(SDL_FlushAudioStream(m_stream));

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
