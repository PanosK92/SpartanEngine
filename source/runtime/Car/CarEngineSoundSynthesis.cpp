/*
Copyright(c) 2015-2026 Panos Karabelas
*/

#include "pch.h"
#include "CarEngineSoundSynthesis.h"
#include "CarEngineSimFerrari412.h"

#include "../Audio/Engine/Core/engine.h"
#include "../Audio/Engine/Core/ignition_module.h"
#include "../Audio/Engine/Core/piston_engine_simulator.h"
#include "../Audio/Engine/Core/synthesizer.h"
#include "../Audio/Engine/Core/units.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace engine_sound
{
    namespace
    {
        constexpr int upstream_sample_rate = 44100;
        constexpr int upstream_render_chunk = 1800;
        constexpr int output_chunk_frames = 1024;
        constexpr int ready_capacity_frames = 8192;

        bool write_wav(
            const char* path,
            const float* interleaved,
            int frames,
            int sample_rate
        )
        {
            FILE* file = nullptr;
            fopen_s(
                &file,
                path,
                "wb"
            );
            if (!file)
            {
                return false;
            }

            const std::uint32_t data_bytes =
                static_cast<std::uint32_t>(
                    frames *
                    2 *
                    sizeof(std::int16_t)
                );
            const std::uint32_t riff_size =
                36 +
                data_bytes;
            const std::uint32_t byte_rate =
                static_cast<std::uint32_t>(
                    sample_rate *
                    2 *
                    sizeof(std::int16_t)
                );
            const std::uint32_t format_size = 16;
            const std::uint16_t pcm_format = 1;
            const std::uint16_t channels = 2;
            const std::uint16_t bits = 16;
            const std::uint16_t block_align =
                channels *
                sizeof(std::int16_t);

            fwrite("RIFF", 1, 4, file);
            fwrite(&riff_size, 4, 1, file);
            fwrite("WAVE", 1, 4, file);
            fwrite("fmt ", 1, 4, file);
            fwrite(&format_size, 4, 1, file);
            fwrite(&pcm_format, 2, 1, file);
            fwrite(&channels, 2, 1, file);
            fwrite(&sample_rate, 4, 1, file);
            fwrite(&byte_rate, 4, 1, file);
            fwrite(&block_align, 2, 1, file);
            fwrite(&bits, 2, 1, file);
            fwrite("data", 1, 4, file);
            fwrite(&data_bytes, 4, 1, file);

            for (int i = 0; i < frames * 2; i++)
            {
                const float sample = std::clamp(
                    interleaved[i],
                    -1.0f,
                    1.0f
                );
                const std::int16_t value =
                    static_cast<std::int16_t>(
                        std::lround(
                            sample *
                            32767.0f
                        )
                    );
                fwrite(
                    &value,
                    sizeof(value),
                    1,
                    file
                );
            }

            fclose(file);
            return true;
        }

        std::string resolve_impulse_response_path(
            const char* requested_path
        )
        {
            std::vector<std::filesystem::path> candidates;
            if (
                requested_path &&
                requested_path[0] != '\0'
            )
            {
                candidates.emplace_back(requested_path);
            }
            candidates.emplace_back(
                "project/music/"
                "engine_minimal_muffling_01.wav"
            );

            for (
                const std::filesystem::path& candidate :
                candidates
            )
            {
                if (std::filesystem::exists(candidate))
                {
                    return candidate.string();
                }
            }

            return
                "project/music/"
                "engine_minimal_muffling_01.wav";
        }
    }

    engine_config::engine_config()
    {
        for (
            int i = 0;
            i < tuning::max_cylinders;
            i++
        )
        {
            firing_order[i] = i;
            cylinder_bank[i] = i & 1;
        }
    }

    class synthesizer::implementation
    {
    public:
        ~implementation()
        {
            stop_worker();
        }

        void initialize(
            int sample_rate,
            const char* impulse_response_path
        )
        {
            stop_worker();
            m_sample_rate = std::max(
                sample_rate,
                1
            );
            m_impulse_response_path =
                resolve_impulse_response_path(
                    impulse_response_path
                );
            create_upstream();
            m_ready_samples.resize(
                ready_capacity_frames
            );
            m_generate_buffer.resize(
                ready_capacity_frames
            );
            m_worker_output.resize(
                output_chunk_frames
            );
            m_upstream_buffer.resize(
                upstream_render_chunk
            );
            clear_ready_audio();
            m_initialized.store(
                true,
                std::memory_order_release
            );
            m_debug.initialized = true;
        }

        void configure(const engine_config& config)
        {
            m_config = config;
        }

        void set_parameters(
            float rpm,
            float throttle,
            float load,
            float boost_pressure,
            float crank_angle,
            float torque_normalized,
            bool fuel_cut
        )
        {
            const float maximum_rpm = std::max(
                m_config.max_rpm,
                1.0f
            );
            m_target_rpm.store(
                std::clamp(
                    rpm,
                    0.0f,
                    maximum_rpm
                ),
                std::memory_order_relaxed
            );
            m_target_throttle.store(
                std::clamp(
                    throttle,
                    0.0f,
                    1.0f
                ),
                std::memory_order_relaxed
            );
            m_target_load.store(
                std::clamp(
                    std::max(
                        load,
                        torque_normalized
                    ),
                    0.0f,
                    1.0f
                ),
                std::memory_order_relaxed
            );
            m_target_boost.store(
                std::max(
                    boost_pressure,
                    0.0f
                ),
                std::memory_order_relaxed
            );
            m_target_crank_angle.store(
                crank_angle,
                std::memory_order_relaxed
            );
            m_fuel_cut.store(
                fuel_cut,
                std::memory_order_relaxed
            );
        }

        void generate(
            float* output_buffer,
            int num_samples,
            bool stereo,
            const runtime_params& params
        )
        {
            if (
                !output_buffer ||
                num_samples <= 0
            )
            {
                return;
            }

            publish_runtime_params(params);

            m_debug.generate_calls++;
            m_debug.samples_generated +=
                static_cast<std::uint64_t>(num_samples);

            if (
                !m_initialized.load(
                    std::memory_order_acquire
                )
            )
            {
                const int total_samples =
                    stereo
                        ? num_samples * 2
                        : num_samples;
                std::fill(
                    output_buffer,
                    output_buffer + total_samples,
                    0.0f
                );
                return;
            }

            start_worker();
            if (
                m_generate_buffer.size() <
                static_cast<std::size_t>(num_samples)
            )
            {
                m_generate_buffer.resize(
                    static_cast<std::size_t>(num_samples)
                );
            }

            float sum = 0.0f;
            float peak = 0.0f;
            int ready_count = 0;
            {
                std::lock_guard<std::mutex> lock(
                    m_worker_mutex
                );
                ready_count = std::min(
                    num_samples,
                    m_ready_count
                );
                for (
                    int i = 0;
                    i < ready_count;
                    i++
                )
                {
                    m_generate_buffer[
                        static_cast<std::size_t>(i)
                    ] = pop_ready_sample();
                }
                m_output_demand = std::min(
                    ready_capacity_frames,
                    m_output_demand +
                    num_samples
                );
            }
            m_worker_condition.notify_one();

            for (int i = 0; i < num_samples; i++)
            {
                const float sample =
                    i < ready_count
                        ? m_generate_buffer[
                            static_cast<std::size_t>(i)
                        ]
                        : 0.0f;

                const float absolute_sample =
                    std::abs(sample);
                sum += sample * sample;
                peak = std::max(
                    peak,
                    absolute_sample
                );

                m_debug.waveform[
                    m_debug.waveform_write_pos
                ] = sample;
                m_debug.waveform_write_pos =
                    (
                        m_debug.waveform_write_pos +
                        1
                    ) %
                    debug_data::waveform_size;

                if (stereo)
                {
                    output_buffer[i * 2] = sample;
                    output_buffer[i * 2 + 1] = sample;
                }
                else
                {
                    output_buffer[i] = sample;
                }

                capture_sample(sample);
            }

            const float inverse_count =
                1.0f /
                static_cast<float>(num_samples);
            m_debug.exhaust_level =
                std::sqrt(sum * inverse_count);
            m_debug.output_level =
                m_debug.exhaust_level;
            m_debug.output_peak = peak;
            m_debug.leveler_gain =
                m_leveler_gain.load(
                    std::memory_order_relaxed
                );
            m_debug.rpm = m_target_rpm.load(
                std::memory_order_relaxed
            );
            m_debug.throttle =
                m_target_throttle.load(
                    std::memory_order_relaxed
                );
            m_debug.load = m_target_load.load(
                std::memory_order_relaxed
            );
            m_debug.boost = m_target_boost.load(
                std::memory_order_relaxed
            );
            m_debug.firing_freq =
                m_debug.rpm /
                60.0f *
                6.0f;
        }

        void reset()
        {
            stop_worker();
            if (m_impulse_response_path.empty())
            {
                return;
            }

            create_upstream();
            m_debug = {};
            m_debug.initialized = true;
            m_dump_buffer.clear();
            m_dump_active = false;
            m_dump_total = 0;
            m_dump_progress = 0;
            clear_ready_audio();
        }

        bool is_initialized() const
        {
            return m_initialized.load(
                std::memory_order_acquire
            );
        }

        const debug_data& get_debug() const
        {
            return m_debug;
        }

        bool begin_dump(float seconds)
        {
            if (
                m_dump_active ||
                seconds <= 0.0f
            )
            {
                return false;
            }

            m_dump_total =
                static_cast<int>(
                    seconds *
                    m_sample_rate
                );
            m_dump_progress = 0;
            m_dump_active = true;
            m_dump_buffer.assign(
                static_cast<std::size_t>(
                    m_dump_total
                ) *
                2,
                0.0f
            );
            m_debug.dump_total = m_dump_total;
            m_debug.dump_progress = 0;
            m_debug.dump_ready = false;
            return true;
        }

        bool dump_ready() const
        {
            return m_debug.dump_ready;
        }

        bool save_dump(const char* path)
        {
            if (
                !m_debug.dump_ready ||
                m_dump_buffer.empty()
            )
            {
                return false;
            }

            const bool result = write_wav(
                path,
                m_dump_buffer.data(),
                m_dump_total,
                m_sample_rate
            );
            m_dump_buffer.clear();
            m_dump_total = 0;
            m_dump_progress = 0;
            m_debug.dump_total = 0;
            m_debug.dump_progress = 0;
            m_debug.dump_ready = false;
            return result;
        }

    private:
        void publish_runtime_params(
            const runtime_params& params
        )
        {
            std::lock_guard<std::mutex> lock(
                m_worker_mutex
            );
            m_runtime_params = params;
        }

        void start_worker()
        {
            std::lock_guard<std::mutex> lock(
                m_worker_mutex
            );
            if (m_worker.joinable())
            {
                return;
            }

            m_stop_worker = false;
            m_worker = std::thread(
                &implementation::worker_loop,
                this
            );
        }

        void stop_worker()
        {
            {
                std::lock_guard<std::mutex> lock(
                    m_worker_mutex
                );
                m_stop_worker = true;
            }
            m_worker_condition.notify_one();

            if (m_worker.joinable())
            {
                m_worker.join();
            }

            {
                std::lock_guard<std::mutex> lock(
                    m_worker_mutex
                );
                m_stop_worker = false;
                m_output_demand = 0;
            }
        }

        void worker_loop()
        {
            while (true)
            {
                runtime_params params;
                {
                    std::unique_lock<std::mutex> lock(
                        m_worker_mutex
                    );
                    m_worker_condition.wait(
                        lock,
                        [this]()
                        {
                            return
                                m_stop_worker ||
                                (
                                    m_output_demand > 0 &&
                                    m_ready_count <=
                                        ready_capacity_frames -
                                        output_chunk_frames
                                );
                        }
                    );
                    if (m_stop_worker)
                    {
                        return;
                    }
                    params = m_runtime_params;
                }

                produce_output_chunk(
                    params
                );

                {
                    std::lock_guard<std::mutex> lock(
                        m_worker_mutex
                    );
                    if (m_stop_worker)
                    {
                        return;
                    }

                    for (
                        int i = 0;
                        i < output_chunk_frames;
                        i++
                    )
                    {
                        push_ready_sample(
                            m_worker_output[
                                static_cast<std::size_t>(i)
                            ]
                        );
                    }
                    m_output_demand -=
                        output_chunk_frames;
                }
            }
        }

        void produce_output_chunk(
            const runtime_params& params
        )
        {
            apply_controls(params);
            prepare_resampler_input(
                output_chunk_frames
            );

            if (
                m_worker_output.size() <
                output_chunk_frames
            )
            {
                m_worker_output.resize(
                    output_chunk_frames
                );
            }

            for (
                int i = 0;
                i < output_chunk_frames;
                i++
            )
            {
                const std::size_t index =
                    static_cast<std::size_t>(
                        m_source_position
                    );
                const double fraction =
                    m_source_position -
                    static_cast<double>(index);
                m_worker_output[
                    static_cast<std::size_t>(i)
                ] =
                    m_source_samples[index] *
                    static_cast<float>(
                        1.0 -
                        fraction
                    ) +
                    m_source_samples[index + 1] *
                    static_cast<float>(fraction);

                m_source_position +=
                    static_cast<double>(
                        upstream_sample_rate
                    ) /
                    static_cast<double>(m_sample_rate);
                discard_consumed_source();
            }

            m_leveler_gain.store(
                static_cast<float>(
                    m_simulator
                        ->synthesizer()
                        .getLevelerGain()
                ),
                std::memory_order_relaxed
            );
        }

        void clear_ready_audio()
        {
            std::lock_guard<std::mutex> lock(
                m_worker_mutex
            );
            m_ready_read = 0;
            m_ready_write = 0;
            m_ready_count = 0;
            m_output_demand = 0;
        }

        float pop_ready_sample()
        {
            const float sample =
                m_ready_samples[
                    static_cast<std::size_t>(
                        m_ready_read
                    )
                ];
            m_ready_read =
                (
                    m_ready_read +
                    1
                ) %
                ready_capacity_frames;
            m_ready_count--;
            return sample;
        }

        void push_ready_sample(float sample)
        {
            m_ready_samples[
                static_cast<std::size_t>(
                    m_ready_write
                )
            ] = sample;
            m_ready_write =
                (
                    m_ready_write +
                    1
                ) %
                ready_capacity_frames;
            m_ready_count++;
        }

        void create_upstream()
        {
            m_factory =
                spartan::CarEngineSimFerrari412::create(
                    m_impulse_response_path
                );
            m_engine = m_factory->get_engine();
            m_simulator = m_factory->get_simulator();

            m_simulator->m_dyno.m_enabled = true;
            m_simulator->m_dyno.m_hold = true;

            const float initial_rpm = std::max(
                m_target_rpm.load(
                    std::memory_order_relaxed
                ),
                std::max(
                    m_config.idle_rpm,
                    1000.0f
                )
            );
            m_engine
                ->getOutputCrankshaft()
                ->m_body
                .v_theta =
                    -units::rpm(initial_rpm);
            m_simulator->m_dyno.m_rotationSpeed =
                units::rpm(initial_rpm);
            m_engine
                ->getIgnitionModule()
                ->m_enabled = true;
            m_engine->setSpeedControl(0.0);

            std::vector<std::int16_t> initial_silence(
                upstream_sample_rate
            );
            m_simulator->readAudioOutput(
                upstream_sample_rate,
                initial_silence.data()
            );

            m_source_samples.clear();
            m_source_position = 0.0;
        }

        void apply_controls(
            const runtime_params& params
        )
        {
            const bool fuel_cut = m_fuel_cut.load(
                std::memory_order_relaxed
            );
            const float throttle =
                fuel_cut
                    ? 0.0f
                    : m_target_throttle.load(
                        std::memory_order_relaxed
                    );
            m_engine->setSpeedControl(throttle);
            m_engine
                ->getIgnitionModule()
                ->m_enabled = !fuel_cut;

            Synthesizer::AudioParameters audio_parameters =
                m_simulator
                    ->synthesizer()
                    .getAudioParameters();
            audio_parameters.convolution = std::clamp(
                params.convolution_wet,
                0.0f,
                1.0f
            );
            audio_parameters.dF_F_mix = std::clamp(
                params.df_f_mix,
                0.0f,
                1.0f
            );
            audio_parameters.airNoise = std::clamp(
                params.air_noise,
                0.0f,
                1.0f
            );
            audio_parameters.levelerTarget =
                std::max(
                    params.leveler_target,
                    0.0f
                ) *
                32767.0f;
            m_simulator
                ->synthesizer()
                .setAudioParameters(
                    audio_parameters
                );
        }

        void prepare_resampler_input(
            int output_samples
        )
        {
            const std::size_t required =
                static_cast<std::size_t>(
                    m_source_position +
                    (
                        output_samples *
                        static_cast<double>(
                            upstream_sample_rate
                        ) /
                        static_cast<double>(
                            m_sample_rate
                        )
                    )
                ) +
                2;
            drain_upstream_audio(
                static_cast<int>(
                    required -
                    std::min(
                        required,
                        m_source_samples.size()
                    )
                )
            );
            while (
                m_source_samples.size() <
                required
            )
            {
                const int requested_samples =
                    std::min(
                        upstream_render_chunk,
                        static_cast<int>(
                            required -
                            m_source_samples.size()
                        )
                    );
                render_upstream(requested_samples);
            }
        }

        void render_upstream(int requested_samples)
        {
            requested_samples = std::max(
                requested_samples,
                32
            );
            const float target_rpm = std::max(
                m_target_rpm.load(
                    std::memory_order_relaxed
                ),
                1.0f
            );
            m_simulator->m_dyno.m_rotationSpeed =
                units::rpm(target_rpm);
            m_simulator->m_dyno.m_enabled = true;
            m_simulator->m_dyno.m_hold = true;

            const double duration =
                static_cast<double>(
                    requested_samples
                ) /
                upstream_sample_rate;
            m_simulator->startFrame(duration);
            if (m_simulator->simulationSteps() <= 0)
            {
                m_source_samples.insert(
                    m_source_samples.end(),
                    static_cast<std::size_t>(
                        requested_samples
                    ),
                    0.0f
                );
                return;
            }
            while (m_simulator->simulateStep())
            {
            }
            m_simulator->endFrame();
            m_simulator
                ->synthesizer()
                .renderAudio();

            drain_upstream_audio(requested_samples);
        }

        int drain_upstream_audio(int requested_samples)
        {
            if (requested_samples <= 0)
            {
                return 0;
            }

            if (
                m_upstream_buffer.size() <
                static_cast<std::size_t>(
                    requested_samples
                )
            )
            {
                m_upstream_buffer.resize(
                    static_cast<std::size_t>(
                        requested_samples
                    )
                );
            }
            const int produced =
                m_simulator->readAudioOutput(
                    requested_samples,
                    m_upstream_buffer.data()
                );
            for (
                int i = 0;
                i < produced;
                i++
            )
            {
                m_source_samples.push_back(
                    static_cast<float>(
                        m_upstream_buffer[
                            static_cast<std::size_t>(i)
                        ]
                    ) /
                    32768.0f
                );
            }
            return produced;
        }

        void discard_consumed_source()
        {
            const std::size_t consumed =
                std::min(
                    static_cast<std::size_t>(
                        m_source_position
                    ),
                    m_source_samples.size()
                );
            for (
                std::size_t i = 0;
                i < consumed;
                i++
            )
            {
                m_source_samples.pop_front();
            }
            m_source_position -=
                static_cast<double>(consumed);
        }

        void capture_sample(float sample)
        {
            if (
                !m_dump_active ||
                m_dump_progress >= m_dump_total
            )
            {
                return;
            }

            const std::size_t index =
                static_cast<std::size_t>(
                    m_dump_progress
                ) *
                2;
            m_dump_buffer[index] = sample;
            m_dump_buffer[index + 1] = sample;
            m_dump_progress++;
            m_debug.dump_progress = m_dump_progress;
            if (m_dump_progress >= m_dump_total)
            {
                m_dump_active = false;
                m_debug.dump_ready = true;
            }
        }

        std::unique_ptr<spartan::CarEngineSimFerrari412>
            m_factory;
        ::Engine* m_engine = nullptr;
        PistonEngineSimulator* m_simulator = nullptr;
        std::string m_impulse_response_path;
        int m_sample_rate = tuning::sample_rate;
        std::atomic<bool> m_initialized = false;

        engine_config m_config;
        std::atomic<float> m_target_rpm = 1000.0f;
        std::atomic<float> m_target_throttle = 0.0f;
        std::atomic<float> m_target_load = 0.0f;
        std::atomic<float> m_target_boost = 0.0f;
        std::atomic<float> m_target_crank_angle = 0.0f;
        std::atomic<bool> m_fuel_cut = false;

        std::deque<float> m_source_samples;
        double m_source_position = 0.0;
        std::vector<std::int16_t> m_upstream_buffer;

        std::mutex m_worker_mutex;
        std::condition_variable m_worker_condition;
        std::thread m_worker;
        runtime_params m_runtime_params;
        bool m_stop_worker = false;
        int m_output_demand = 0;
        std::vector<float> m_ready_samples;
        int m_ready_read = 0;
        int m_ready_write = 0;
        int m_ready_count = 0;
        std::vector<float> m_worker_output;
        std::vector<float> m_generate_buffer;
        std::atomic<float> m_leveler_gain = 1.0f;

        std::vector<float> m_dump_buffer;
        int m_dump_total = 0;
        int m_dump_progress = 0;
        bool m_dump_active = false;
        debug_data m_debug;
    };

    synthesizer::synthesizer()
        : m_implementation(
            std::make_unique<implementation>()
        )
    {
    }

    synthesizer::~synthesizer() = default;

    void synthesizer::initialize(
        int sample_rate,
        const char* impulse_response_path
    )
    {
        m_implementation->initialize(
            sample_rate,
            impulse_response_path
        );
    }

    void synthesizer::configure(
        const engine_config& config
    )
    {
        m_implementation->configure(config);
    }

    void synthesizer::set_parameters(
        float rpm,
        float throttle,
        float load,
        float boost_pressure,
        float crank_angle,
        float torque_normalized,
        bool fuel_cut
    )
    {
        m_implementation->set_parameters(
            rpm,
            throttle,
            load,
            boost_pressure,
            crank_angle,
            torque_normalized,
            fuel_cut
        );
    }

    void synthesizer::generate(
        float* output_buffer,
        int num_samples,
        bool stereo
    )
    {
        m_implementation->generate(
            output_buffer,
            num_samples,
            stereo,
            params
        );
    }

    void synthesizer::reset()
    {
        m_implementation->reset();
    }

    bool synthesizer::is_initialized() const
    {
        return m_implementation->is_initialized();
    }

    const debug_data& synthesizer::get_debug() const
    {
        return m_implementation->get_debug();
    }

    bool synthesizer::begin_dump(float seconds)
    {
        return m_implementation->begin_dump(seconds);
    }

    bool synthesizer::dump_ready() const
    {
        return m_implementation->dump_ready();
    }

    bool synthesizer::save_dump(const char* path)
    {
        return m_implementation->save_dump(path);
    }

    synthesizer& get_synthesizer()
    {
        static synthesizer instance;
        return instance;
    }

    void initialize(int sample_rate)
    {
        get_synthesizer().initialize(sample_rate);
    }

    void configure(const engine_config& config)
    {
        get_synthesizer().configure(config);
    }

    void set_parameters(
        float rpm,
        float throttle,
        float load,
        float boost,
        float crank_angle,
        float torque_normalized,
        bool fuel_cut
    )
    {
        get_synthesizer().set_parameters(
            rpm,
            throttle,
            load,
            boost,
            crank_angle,
            torque_normalized,
            fuel_cut
        );
    }

    void generate(
        float* buffer,
        int num_samples,
        bool stereo
    )
    {
        get_synthesizer().generate(
            buffer,
            num_samples,
            stereo
        );
    }

    void reset()
    {
        get_synthesizer().reset();
    }

    const debug_data& get_debug()
    {
        return get_synthesizer().get_debug();
    }
}
