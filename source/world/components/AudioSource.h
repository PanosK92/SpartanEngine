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

//= includes =========
#include "Component.h"
#include <atomic>
#include <string>
#include <vector>
#include <functional>
#include <sol/forward.hpp>
//====================

struct SDL_AudioStream;
struct SDL_AudioSpec;
namespace audio_clip_cache
{
    struct AudioClip;
}

namespace spartan
{
    // callback type for audio synthesis: generates stereo samples into buffer
    // parameters: output buffer (stereo interleaved), number of sample frames
    // runs on the sdl audio thread, so it must not block or allocate
    using SynthesisCallback     = std::function<void(float*, int)>;

    class AudioSource : public Component
    {
    public:
        AudioSource(Entity* entity);
        ~AudioSource();


        static void RegisterForScripting(sol::state_view State);


        // component interface
        void Initialize() override;
        void Start() override;
        void Stop() override;
        void Remove() override;
        void Tick() override;
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;


        sol::reference AsLua(sol::state_view state) override;

        void SetAudioClip(const std::string& file_path);
        const std::string& GetAudioClipName() const { return m_name; };

        // synthesis mode - generates audio procedurally instead of playing a clip
        void SetSynthesisMode(bool enabled, SynthesisCallback callback = nullptr);
        bool IsSynthesisMode() const { return m_synthesis_mode; }
        void StartSynthesis();  // start synthesis playback
        void StopSynthesis();   // fade out, the stream is released once it is silent
        // native rate of the shared output device, synthesizers render at it so sdl never resamples
        static int GetDeviceSampleRate();

        bool IsPlaying() { return m_is_playing; }
        void PlayClip();
        void StopClip();
        float GetProgress() const;

        bool GetMute() const { return m_mute; }
        void SetMute(bool mute);

        bool GetPlayOnStart() const                   { return m_play_on_start; }
        void SetPlayOnStart(const bool play_on_start) { m_play_on_start = play_on_start; }

        bool GetLoop() const          { return m_loop; }
        void SetLoop(const bool loop) { m_loop = loop; }

        bool GetIs3d() const           { return m_is_3d; }
        void SetIs3d(const bool is_3d) { m_is_3d = is_3d; }

        // Stereo ambience follows the listener's weight in the Volume on this entity.
        bool GetAmbient() const { return m_ambient; }
        void SetAmbient(bool value);
        float GetAmbientGain() const { return m_ambient_gain; }
        // 0 region only, 1 cicadas, 2 vegetation birds, 3 exposed wind.
        uint32_t GetAmbientProfile() const { return m_ambient_profile; }
        void SetAmbientProfile(uint32_t value) { m_ambient_profile = value <= 3 ? value : 0; }
        float GetHabitatGain() const { return m_habitat_gain; }

        float GetVolume() const { return m_volume; }
        void SetVolume(float volume);

        float GetPitch() const { return m_pitch; }
        void SetPitch(const float pitch);

        // reverb
        bool GetReverbEnabled() const                     { return m_reverb_enabled; }
        void SetReverbEnabled(const bool enabled)         { m_reverb_enabled = enabled; }
        float GetReverbRoomSize() const                   { return m_reverb_room_size; }
        void SetReverbRoomSize(const float room_size);
        float GetReverbDecay() const                      { return m_reverb_decay; }
        void SetReverbDecay(const float decay);
        float GetReverbWet() const                        { return m_reverb_wet; }
        void SetReverbWet(const float wet);

    private:
        void FeedAudioChunk();
        void PublishSynthesisMix();
        void RenderSynthesis(SDL_AudioStream* stream, int bytes_needed);
        void DestroyStream();
        static void SynthesisStreamCallback(void* userdata, SDL_AudioStream* stream, int additional_amount, int total_amount);
        void TickAmbient(bool in_play_mode);

        std::vector<float> m_stereo_chunk; // reused to avoid per-call allocation
        std::string m_name                             = "N/A";
        bool m_is_3d                                   = false;
        bool m_mute                                    = false;
        bool m_loop                                    = true;
        bool m_play_on_start                           = true;
        float m_volume                                 = 1.0f;
        float m_pitch                                  = 1.0f;
        float m_attenuation                            = 1.0f;
        float m_pan                                    = 0.0f; // -1.0 (left) to 1.0 (right)
        bool m_is_playing                              = false;
        bool m_auto_play_consumed                      = false; // gates play_on_start so it fires once per play session
        uint32_t m_position                            = 0; // in bytes
        SDL_AudioStream* m_stream                      = nullptr;
        float m_doppler_ratio                          = 1.0f;
        math::Vector3 position_previous                = math::Vector3::Zero;
        std::shared_ptr<audio_clip_cache::AudioClip> m_clip = nullptr;
        std::string m_file_path;
        bool m_ambient = false;
        uint32_t m_ambient_profile = 0;
        float m_habitat_gain = 1.0f;
        float m_ambient_gain = 0.0f;
        float m_ambient_target = 0.0f;
        float m_ambient_update_timer = 0.0f;

        // synthesis mode, the audio thread pulls; the main thread only publishes the mix below
        enum SynthesisState : int { synthesis_running = 0, synthesis_stopping, synthesis_silent };
        bool m_synthesis_mode                           = false;
        SynthesisCallback m_synthesis_callback          = nullptr;
        std::vector<float> m_synthesis_chunk;
        static constexpr uint32_t synthesis_chunk_frames = 512;
        int m_synthesis_rate                            = 48000;
        float m_synthesis_gain_l                        = 0.0f; // audio thread
        float m_synthesis_gain_r                        = 0.0f; // audio thread
        std::atomic<float> m_synthesis_target_l         { 0.0f };
        std::atomic<float> m_synthesis_target_r         { 0.0f };
        std::atomic<bool> m_synthesis_reverb            { false };
        std::atomic<float> m_synthesis_room_size        { 0.5f };
        std::atomic<float> m_synthesis_decay            { 0.5f };
        std::atomic<float> m_synthesis_wet              { 0.3f };
        std::atomic<int> m_synthesis_state              { synthesis_running };

        // reverb state
        bool m_reverb_enabled         = false;
        float m_reverb_room_size      = 0.5f;  // 0.0 to 1.0, affects delay times
        float m_reverb_decay          = 0.5f;  // 0.0 to 1.0, feedback factor
        float m_reverb_wet            = 0.3f;  // 0.0 to 1.0, wet/dry mix
        std::vector<float> m_reverb_buffer_l;  // circular buffer for left channel
        std::vector<float> m_reverb_buffer_r;  // circular buffer for right channel
        uint32_t m_reverb_write_pos   = 0;     // write position in reverb buffers
        static constexpr uint32_t reverb_buffer_size = 48000; // ~1 second at 48khz

        // volume-driven reverb override
        bool m_volume_reverb_active = false;
    };
}
