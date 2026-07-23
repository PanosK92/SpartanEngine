/*
Copyright(c) 2015-2026 Panos Karabelas
*/
#include "pch.h"
#include "pch.h"
#include "CarEngineSimFerrari412.h"

#include "../Audio/Engine/Core/camshaft.h"
#include "../Audio/Engine/Core/combustion_chamber.h"
#include "../Audio/Engine/Core/constants.h"
#include "../Audio/Engine/Core/direct_throttle_linkage.h"
#include "../Audio/Engine/Core/engine.h"
#include "../Audio/Engine/Core/function.h"
#include "../Audio/Engine/Core/gas_system.h"
#include "../Audio/Engine/Core/impulse_response.h"
#include "../Audio/Engine/Core/piston_engine_simulator.h"
#include "../Audio/Engine/Core/standard_valvetrain.h"
#include "../Audio/Engine/Core/transmission.h"
#include "../Audio/Engine/Core/units.h"
#include "../Audio/Engine/Core/vehicle.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace spartan
{
    namespace
    {
        constexpr int cylinder_count = 12;
        constexpr int cylinders_per_bank = 6;
        constexpr int bank_count = 2;
        constexpr int exhaust_count = 2;

        std::string resolve_impulse_response_path(
            const std::string& requested_path
        )
        {
            const std::array<std::filesystem::path, 2> candidates =
            {
                requested_path,
                "project/music/"
                    "engine_minimal_muffling_01.wav"
            };

            for (const std::filesystem::path& candidate : candidates)
            {
                if (
                    !candidate.empty() &&
                    std::filesystem::exists(candidate)
                )
                {
                    return candidate.string();
                }
            }

            return candidates[1].string();
        }

        std::uint16_t read_u16(
            std::istream& stream
        )
        {
            std::array<unsigned char, 2> bytes{};
            stream.read(
                reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size())
            );
            if (!stream)
            {
                throw std::runtime_error(
                    "invalid ferrari 412 impulse response"
                );
            }
            return
                static_cast<std::uint16_t>(bytes[0]) |
                (
                    static_cast<std::uint16_t>(bytes[1]) <<
                    8
                );
        }

        std::uint32_t read_u32(
            std::istream& stream
        )
        {
            std::array<unsigned char, 4> bytes{};
            stream.read(
                reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size())
            );
            if (!stream)
            {
                throw std::runtime_error(
                    "invalid ferrari 412 impulse response"
                );
            }
            return
                static_cast<std::uint32_t>(bytes[0]) |
                (
                    static_cast<std::uint32_t>(bytes[1]) <<
                    8
                ) |
                (
                    static_cast<std::uint32_t>(bytes[2]) <<
                    16
                ) |
                (
                    static_cast<std::uint32_t>(bytes[3]) <<
                    24
                );
        }

        std::vector<std::int16_t> load_pcm16_wav(
            const std::string& filename
        )
        {
            std::ifstream stream(
                filename,
                std::ios::binary
            );
            if (!stream)
            {
                throw std::runtime_error(
                    "could not open ferrari 412 impulse response"
                );
            }

            std::array<char, 4> chunk_id{};
            stream.read(
                chunk_id.data(),
                static_cast<std::streamsize>(chunk_id.size())
            );
            static_cast<void>(read_u32(stream));
            std::array<char, 4> wave_id{};
            stream.read(
                wave_id.data(),
                static_cast<std::streamsize>(wave_id.size())
            );
            if (
                !stream ||
                chunk_id != std::array<char, 4>{ 'R', 'I', 'F', 'F' } ||
                wave_id != std::array<char, 4>{ 'W', 'A', 'V', 'E' }
            )
            {
                throw std::runtime_error(
                    "invalid ferrari 412 impulse response"
                );
            }

            bool format_found = false;
            std::uint16_t format = 0;
            std::uint16_t channels = 0;
            std::uint16_t bits_per_sample = 0;
            std::vector<unsigned char> sample_bytes;

            while (
                stream &&
                sample_bytes.empty()
            )
            {
                stream.read(
                    chunk_id.data(),
                    static_cast<std::streamsize>(chunk_id.size())
                );
                if (!stream)
                {
                    break;
                }
                const std::uint32_t chunk_size =
                    read_u32(stream);

                if (
                    chunk_id ==
                    std::array<char, 4>{ 'f', 'm', 't', ' ' }
                )
                {
                    if (chunk_size < 16)
                    {
                        throw std::runtime_error(
                            "invalid ferrari 412 impulse response"
                        );
                    }
                    format = read_u16(stream);
                    channels = read_u16(stream);
                    static_cast<void>(read_u32(stream));
                    static_cast<void>(read_u32(stream));
                    static_cast<void>(read_u16(stream));
                    bits_per_sample = read_u16(stream);
                    stream.seekg(
                        static_cast<std::streamoff>(
                            chunk_size -
                            16
                        ),
                        std::ios::cur
                    );
                    format_found = true;
                }
                else if (
                    chunk_id ==
                    std::array<char, 4>{ 'd', 'a', 't', 'a' }
                )
                {
                    sample_bytes.resize(chunk_size);
                    stream.read(
                        reinterpret_cast<char*>(
                            sample_bytes.data()
                        ),
                        static_cast<std::streamsize>(
                            sample_bytes.size()
                        )
                    );
                }
                else
                {
                    stream.seekg(
                        static_cast<std::streamoff>(chunk_size),
                        std::ios::cur
                    );
                }

                if ((chunk_size & 1U) != 0U)
                {
                    stream.seekg(
                        1,
                        std::ios::cur
                    );
                }
            }

            if (
                !stream ||
                !format_found ||
                format != 1 ||
                channels != 1 ||
                bits_per_sample != 16 ||
                sample_bytes.empty() ||
                sample_bytes.size() % 2 != 0
            )
            {
                throw std::runtime_error(
                    "ferrari 412 impulse response must be mono pcm16"
                );
            }

            std::vector<std::int16_t> samples(
                sample_bytes.size() /
                2
            );
            for (std::size_t i = 0; i < samples.size(); i++)
            {
                const std::uint16_t sample =
                    static_cast<std::uint16_t>(
                        sample_bytes[i * 2]
                    ) |
                    (
                        static_cast<std::uint16_t>(
                            sample_bytes[i * 2 + 1]
                        ) <<
                        8
                    );
                samples[i] =
                    static_cast<std::int16_t>(sample);
            }

            return samples;
        }

        std::unique_ptr<Function> create_function(
            int capacity,
            double filter_radius
        )
        {
            std::unique_ptr<Function> function =
                std::make_unique<Function>();
            function->initialize(
                capacity,
                filter_radius
            );
            return function;
        }

        std::unique_ptr<Function> create_harmonic_cam_lobe(
            double duration_at_50_thou,
            double gamma,
            double lift,
            int steps
        )
        {
            const double angle =
                duration_at_50_thou /
                4.0;
            const double s =
                std::pow(
                    2.0 *
                    units::distance(
                        50.0,
                        units::thou
                    ) /
                    lift,
                    1.0 /
                    gamma
                ) -
                1.0;
            const double k =
                std::acos(s) /
                angle;
            const double extents =
                constants::pi /
                k;
            const double step =
                extents /
                (steps - 5.0);

            std::unique_ptr<Function> function =
                create_function(
                    1 + 2 * (steps - 1),
                    step
                );

            for (int i = 0; i < steps; i++)
            {
                if (i == 0)
                {
                    function->addSample(
                        0.0,
                        lift
                    );
                }
                else
                {
                    const double x =
                        i *
                        step;
                    const double sample_lift =
                        x >= extents
                            ? 0.0
                            : lift *
                                std::pow(
                                    0.5 +
                                    0.5 *
                                    std::cos(k * x),
                                    gamma
                                );
                    function->addSample(
                        x,
                        sample_lift
                    );
                    function->addSample(
                        -x,
                        sample_lift
                    );
                }
            }

            return function;
        }

        std::unique_ptr<Function> create_port_flow(
            const std::array<double, 10>& flow
        )
        {
            std::unique_ptr<Function> function =
                create_function(
                    static_cast<int>(flow.size()),
                    50.0 *
                    units::thou
                );

            for (int i = 0;
                i < static_cast<int>(flow.size());
                i++)
            {
                function->addSample(
                    i *
                    50.0 *
                    units::thou,
                    GasSystem::k_28inH2O(flow[i])
                );
            }

            return function;
        }

        void destroy_function(
            std::unique_ptr<Function>& function
        )
        {
            if (function)
            {
                function->destroy();
                function.reset();
            }
        }
    }

    class CarEngineSimFerrari412::implementation
    {
    public:
        void initialize(
            const std::string& impulse_response_path
        )
        {
            destroy();

            m_engine = std::make_unique<::Engine>();
            m_vehicle = std::make_unique<Vehicle>();
            m_transmission =
                std::make_unique<Transmission>();

            DirectThrottleLinkage* throttle =
                new DirectThrottleLinkage();
            DirectThrottleLinkage::Parameters throttle_parameters{};
            throttle_parameters.gamma = 2.0;
            throttle->initialize(
                throttle_parameters
            );

            ::Engine::Parameters engine_parameters{};
            engine_parameters.cylinderBanks = bank_count;
            engine_parameters.cylinderCount = cylinder_count;
            engine_parameters.crankshaftCount = 1;
            engine_parameters.exhaustSystemCount = exhaust_count;
            engine_parameters.intakeCount = 1;
            engine_parameters.name =
                "Ferrari 412 T2 [V12]";
            engine_parameters.starterTorque =
                70.0 *
                units::ft_lb;
            engine_parameters.starterSpeed =
                units::rpm(500.0);
            engine_parameters.redline =
                units::rpm(18000.0);
            engine_parameters.dynoMinSpeed =
                units::rpm(1000.0);
            engine_parameters.dynoMaxSpeed =
                units::rpm(18000.0);
            engine_parameters.dynoHoldStep =
                units::rpm(100.0);
            engine_parameters.throttle = throttle;
            engine_parameters.initialSimulationFrequency =
                5000.0;
            engine_parameters.initialHighFrequencyGain =
                0.01;
            engine_parameters.initialNoise = 1.0;
            engine_parameters.initialJitter = 0.1;
            m_engine->initialize(
                engine_parameters
            );
            m_engine_initialized = true;

            initialize_impulse_responses(
                impulse_response_path
            );
            initialize_intake();
            initialize_exhausts();
            initialize_crankshaft();
            initialize_banks();
            initialize_pistons_and_rods();
            initialize_camshafts();
            initialize_heads();
            initialize_fuel();
            initialize_ignition();
            initialize_combustion_chambers();
            initialize_vehicle();
            initialize_transmission();
            initialize_simulator();
        }

        void destroy()
        {
            if (m_simulator)
            {
                m_simulator->endAudioRenderingThread();
                m_simulator->PistonEngineSimulator::destroy();
                m_simulator->Simulator::destroy();
                m_simulator.reset();
            }

            if (
                m_engine &&
                m_engine_initialized
            )
            {
                for (int i = 0; i < bank_count; i++)
                {
                    m_engine->getHead(i)->destroy();
                }
                m_engine->destroy();
                m_engine_initialized = false;
            }

            for (std::unique_ptr<Camshaft>& camshaft : m_camshafts)
            {
                if (camshaft)
                {
                    camshaft->destroy();
                    camshaft.reset();
                }
            }

            destroy_function(m_turbulence_to_flame_speed);
            destroy_function(m_mean_piston_speed_to_turbulence);
            destroy_function(m_intake_lobe);
            destroy_function(m_exhaust_lobe);
            destroy_function(m_timing_curve);

            for (std::unique_ptr<Function>& function : m_intake_flow)
            {
                destroy_function(function);
            }

            for (std::unique_ptr<Function>& function : m_exhaust_flow)
            {
                destroy_function(function);
            }

            for (
                std::unique_ptr<StandardValvetrain>& valvetrain :
                m_valvetrains
            )
            {
                valvetrain.reset();
            }

            for (
                std::unique_ptr<ImpulseResponse>& impulse_response :
                m_impulse_responses
            )
            {
                impulse_response.reset();
            }

            m_transmission.reset();
            m_vehicle.reset();
            m_engine.reset();
        }

        ::Engine* get_engine() const
        {
            return m_engine.get();
        }

        Vehicle* get_vehicle() const
        {
            return m_vehicle.get();
        }

        Transmission* get_transmission() const
        {
            return m_transmission.get();
        }

        PistonEngineSimulator* get_simulator() const
        {
            return m_simulator.get();
        }

    private:
        void initialize_impulse_responses(
            const std::string& impulse_response_path
        )
        {
            const std::string filename =
                resolve_impulse_response_path(
                    impulse_response_path
                );

            for (
                std::unique_ptr<ImpulseResponse>& impulse_response :
                m_impulse_responses
            )
            {
                impulse_response =
                    std::make_unique<ImpulseResponse>();
                impulse_response->initialize(
                    filename,
                    0.01
                );
            }
        }

        void initialize_intake()
        {
            Intake::Parameters parameters{};
            parameters.volume =
                1.325 *
                units::L;
            parameters.CrossSectionArea =
                20.0 *
                units::cm2;
            parameters.InputFlowK =
                GasSystem::k_carb(1400.0);
            parameters.IdleFlowK =
                GasSystem::k_carb(0.0);
            parameters.RunnerFlowRate =
                GasSystem::k_carb(200.0);
            parameters.MolecularAfr =
                25.0 /
                2.0;
            parameters.IdleThrottlePlatePosition =
                0.992;
            parameters.RunnerLength =
                4.0 *
                units::inch;
            parameters.VelocityDecay =
                0.5;
            m_engine->getIntake(0)->initialize(
                parameters
            );
        }

        void initialize_exhausts()
        {
            for (int i = 0; i < exhaust_count; i++)
            {
                ExhaustSystem::Parameters parameters{};
                parameters.length =
                    (
                        i == 0
                            ? 20.0
                            : 56.0
                    ) *
                    units::inch;
                parameters.collectorCrossSectionArea =
                    constants::pi *
                    2.0 *
                    units::inch *
                    2.0 *
                    units::inch;
                parameters.outletFlowRate =
                    GasSystem::k_carb(2000.0);
                parameters.primaryTubeLength =
                    20.0 *
                    units::inch;
                parameters.primaryFlowRate =
                    GasSystem::k_carb(200.0);
                parameters.velocityDecay =
                    0.5;
                parameters.audioVolume =
                    1.0 *
                    0.004;
                parameters.impulseResponse =
                    m_impulse_responses[i].get();
                m_engine->getExhaustSystem(i)->initialize(
                    parameters
                );
            }
        }

        void initialize_crankshaft()
        {
            const double stroke =
                43.0 *
                units::mm;
            const double crank_mass =
                20.0 *
                units::lb;
            const double flywheel_mass =
                10.0 *
                units::lb;
            const double flywheel_radius =
                5.0 *
                units::inch;
            const double crank_moment =
                0.5 *
                crank_mass *
                stroke *
                stroke;
            const double flywheel_moment =
                0.5 *
                flywheel_mass *
                flywheel_radius *
                flywheel_radius;
            const double other_moment =
                0.5 *
                1.0 *
                units::kg *
                1.0 *
                units::cm *
                1.0 *
                units::cm;

            Crankshaft::Parameters parameters{};
            parameters.mass = crank_mass;
            parameters.flywheelMass =
                flywheel_mass;
            parameters.momentOfInertia =
                crank_moment +
                flywheel_moment +
                other_moment;
            parameters.crankThrow =
                stroke /
                2.0;
            parameters.pos_x = 0.0;
            parameters.pos_y = 0.0;
            parameters.tdc =
                (
                    90.0 +
                    75.0 /
                    2.0
                ) *
                units::deg;
            parameters.frictionTorque =
                1.0 *
                units::ft_lb;
            parameters.rodJournals =
                cylinders_per_bank;

            Crankshaft* crankshaft =
                m_engine->getCrankshaft(0);
            crankshaft->initialize(
                parameters
            );

            constexpr std::array<double, cylinders_per_bank>
                journal_angles =
                {
                    0.0,
                    120.0,
                    240.0,
                    240.0,
                    120.0,
                    0.0
                };
            for (int i = 0; i < cylinders_per_bank; i++)
            {
                crankshaft->setRodJournalAngle(
                    i,
                    journal_angles[i] *
                    units::deg
                );
            }
        }

        void initialize_banks()
        {
            const double stroke =
                43.0 *
                units::mm;
            const double rod_length =
                120.0 *
                units::mm;
            const double compression_height =
                1.0 *
                units::inch;

            for (int i = 0; i < bank_count; i++)
            {
                CylinderBank::Parameters parameters{};
                parameters.crankshaft =
                    m_engine->getCrankshaft(0);
                parameters.positionX = 0.0;
                parameters.positionY = 0.0;
                parameters.angle =
                    (
                        i == 0
                            ? 75.0 / 2.0
                            : -75.0 / 2.0
                    ) *
                    units::deg;
                parameters.bore =
                    86.0 *
                    units::mm;
                parameters.deckHeight =
                    stroke /
                    2.0 +
                    rod_length +
                    compression_height;
                parameters.displayDepth = 0.5;
                parameters.cylinderCount =
                    cylinders_per_bank;
                parameters.index = i;
                m_engine->getCylinderBank(i)->initialize(
                    parameters
                );
            }
        }

        void initialize_pistons_and_rods()
        {
            const double rod_mass =
                50.0 *
                units::g;
            const double rod_length =
                120.0 *
                units::mm;

            for (int bank = 0; bank < bank_count; bank++)
            {
                for (
                    int cylinder = 0;
                    cylinder < cylinders_per_bank;
                    cylinder++
                )
                {
                    const int index =
                        bank *
                        cylinders_per_bank +
                        cylinder;
                    Piston* piston =
                        m_engine->getPiston(index);
                    ConnectingRod* rod =
                        m_engine->getConnectingRod(index);

                    Piston::Parameters piston_parameters{};
                    piston_parameters.Rod = rod;
                    piston_parameters.Bank =
                        m_engine->getCylinderBank(bank);
                    piston_parameters.CylinderIndex =
                        cylinder;
                    piston_parameters.BlowbyFlowCoefficient =
                        GasSystem::k_28inH2O(0.0);
                    piston_parameters.CompressionHeight =
                        1.0 *
                        units::inch;
                    piston_parameters.WristPinPosition =
                        0.0;
                    piston_parameters.Displacement =
                        0.0;
                    piston_parameters.mass =
                        50.0 *
                        units::g;
                    piston->initialize(
                        piston_parameters
                    );

                    ConnectingRod::Parameters rod_parameters{};
                    rod_parameters.mass =
                        rod_mass;
                    rod_parameters.momentOfInertia =
                        (
                            1.0 /
                            12.0
                        ) *
                        rod_mass *
                        rod_length *
                        rod_length;
                    rod_parameters.centerOfMass =
                        0.0;
                    rod_parameters.length =
                        rod_length;
                    rod_parameters.rodJournals =
                        0;
                    rod_parameters.slaveThrow =
                        0.0;
                    rod_parameters.piston =
                        piston;
                    rod_parameters.crankshaft =
                        m_engine->getCrankshaft(0);
                    rod_parameters.master =
                        nullptr;
                    rod_parameters.journal =
                        cylinder;
                    rod->initialize(
                        rod_parameters
                    );
                }
            }
        }

        void initialize_camshafts()
        {
            m_intake_lobe =
                create_harmonic_cam_lobe(
                    242.0 *
                    units::deg,
                    0.8,
                    15.95 *
                    units::mm,
                    512
                );
            m_exhaust_lobe =
                create_harmonic_cam_lobe(
                    246.0 *
                    units::deg,
                    0.8,
                    15.95 *
                    units::mm,
                    512
                );

            constexpr std::array<double, cylinders_per_bank>
                firing_angles =
                {
                    0.0,
                    480.0,
                    240.0,
                    600.0,
                    120.0,
                    360.0
                };

            for (int bank = 0; bank < bank_count; bank++)
            {
                const double bank_offset =
                    bank == 0
                        ? 0.0
                        : 75.0;

                initialize_camshaft(
                    bank * 2,
                    m_intake_lobe.get(),
                    360.0 +
                    90.0,
                    bank_offset,
                    firing_angles
                );
                initialize_camshaft(
                    bank * 2 + 1,
                    m_exhaust_lobe.get(),
                    360.0 -
                    112.0,
                    bank_offset,
                    firing_angles
                );
            }
        }

        void initialize_camshaft(
            int index,
            Function* lobe_profile,
            double center,
            double bank_offset,
            const std::array<double, cylinders_per_bank>&
                firing_angles
        )
        {
            m_camshafts[index] =
                std::make_unique<Camshaft>();

            Camshaft::Parameters parameters{};
            parameters.lobes =
                cylinders_per_bank;
            parameters.advance =
                0.0 *
                units::deg;
            parameters.crankshaft =
                m_engine->getCrankshaft(0);
            parameters.lobeProfile =
                lobe_profile;
            parameters.baseRadius =
                1.0 *
                units::inch;
            m_camshafts[index]->initialize(
                parameters
            );

            for (int i = 0; i < cylinders_per_bank; i++)
            {
                m_camshafts[index]->setLobeCenterline(
                    i,
                    (
                        center +
                        firing_angles[i] +
                        bank_offset
                    ) *
                    units::deg
                );
            }
        }

        void initialize_heads()
        {
            constexpr std::array<double, 10> intake_flow =
                {
                    0.0,
                    58.0,
                    103.0,
                    156.0,
                    214.0,
                    249.0,
                    268.0,
                    280.0,
                    280.0,
                    281.0
                };
            constexpr std::array<double, 10> exhaust_flow =
                {
                    0.0,
                    37.0,
                    72.0,
                    113.0,
                    160.0,
                    196.0,
                    222.0,
                    235.0,
                    245.0,
                    246.0
                };

            for (int bank = 0; bank < bank_count; bank++)
            {
                m_intake_flow[bank] =
                    create_port_flow(intake_flow);
                m_exhaust_flow[bank] =
                    create_port_flow(exhaust_flow);
                m_valvetrains[bank] =
                    std::make_unique<StandardValvetrain>();

                StandardValvetrain::Parameters
                    valvetrain_parameters{};
                valvetrain_parameters.intakeCamshaft =
                    m_camshafts[bank * 2].get();
                valvetrain_parameters.exhaustCamshaft =
                    m_camshafts[bank * 2 + 1].get();
                m_valvetrains[bank]->initialize(
                    valvetrain_parameters
                );

                CylinderHead::Parameters head_parameters{};
                head_parameters.Bank =
                    m_engine->getCylinderBank(bank);
                head_parameters.ExhaustPortFlow =
                    m_exhaust_flow[bank].get();
                head_parameters.IntakePortFlow =
                    m_intake_flow[bank].get();
                head_parameters.Valvetrain =
                    m_valvetrains[bank].get();
                head_parameters.CombustionChamberVolume =
                    1.5 *
                    25.0 *
                    units::cc;
                head_parameters.IntakeRunnerVolume =
                    149.6 *
                    units::cc;
                head_parameters.IntakeRunnerCrossSectionArea =
                    1.75 *
                    units::inch *
                    1.75 *
                    units::inch;
                head_parameters.ExhaustRunnerVolume =
                    50.0 *
                    units::cc;
                head_parameters.ExhaustRunnerCrossSectionArea =
                    1.75 *
                    units::inch *
                    1.75 *
                    units::inch;
                head_parameters.FlipDisplay =
                    bank == 0;
                m_engine->getHead(bank)->initialize(
                    head_parameters
                );
            }

            constexpr std::array<double, cylinders_per_bank>
                bank_0_sound =
                {
                    0.5,
                    1.0,
                    0.75,
                    0.9,
                    0.7,
                    1.0
                };
            constexpr std::array<double, cylinders_per_bank>
                bank_1_sound =
                {
                    0.5,
                    0.3,
                    1.0,
                    1.2,
                    0.7,
                    1.2
                };
            constexpr std::array<double, cylinders_per_bank>
                bank_0_primary =
                {
                    0.5,
                    0.0,
                    0.2,
                    1.5,
                    2.5,
                    0.5
                };
            constexpr std::array<double, cylinders_per_bank>
                bank_1_primary =
                {
                    0.5,
                    0.25,
                    3.5,
                    1.5,
                    0.5,
                    1.5
                };

            for (int bank = 0; bank < bank_count; bank++)
            {
                CylinderHead* head =
                    m_engine->getHead(bank);
                for (
                    int cylinder = 0;
                    cylinder < cylinders_per_bank;
                    cylinder++
                )
                {
                    head->setIntake(
                        cylinder,
                        m_engine->getIntake(0)
                    );
                    head->setExhaustSystem(
                        cylinder,
                        m_engine->getExhaustSystem(
                            bank == 0
                                ? 1
                                : 0
                        )
                    );
                    head->setSoundAttenuation(
                        cylinder,
                        bank == 0
                            ? bank_0_sound[cylinder]
                            : bank_1_sound[cylinder]
                    );
                    head->setHeaderPrimaryLength(
                        cylinder,
                        0.1 *
                        (
                            bank == 0
                                ? bank_0_primary[cylinder]
                                : bank_1_primary[cylinder]
                        ) *
                        units::cm
                    );
                }
            }
        }

        void initialize_fuel()
        {
            m_turbulence_to_flame_speed =
                create_function(
                    10,
                    5.0
                );
            constexpr std::array<double, 10> output =
                {
                    2.0 * 3.0,
                    2.0 * 1.5 * 5.0,
                    2.5 * 1.5 * 10.0,
                    3.0 * 1.5 * 15.0,
                    3.0 * 1.5 * 20.0,
                    3.0 * 1.5 * 25.0,
                    3.0 * 1.5 * 30.0,
                    3.0 * 1.5 * 35.0,
                    3.0 * 1.5 * 40.0,
                    3.0 * 1.5 * 45.0
                };
            for (int i = 0; i < 10; i++)
            {
                m_turbulence_to_flame_speed->addSample(
                    i *
                    5.0,
                    output[i]
                );
            }

            Fuel::Parameters parameters{};
            parameters.name =
                "Gasoline [Default]";
            parameters.molecularMass =
                100.0 *
                units::g;
            parameters.energyDensity =
                48.1 *
                units::kJ /
                units::g;
            parameters.density =
                0.755 *
                units::kg /
                units::L;
            parameters.molecularAfr =
                25.0 /
                2.0;
            parameters.burningEfficiencyRandomness =
                1.0;
            parameters.lowEfficiencyAttenuation =
                0.6;
            parameters.maxBurningEfficiency =
                1.0;
            parameters.maxTurbulenceEffect =
                10.0;
            parameters.maxDilutionEffect =
                5.0;
            parameters.turbulenceToFlameSpeedRatio =
                m_turbulence_to_flame_speed.get();
            m_engine->getFuel()->initialize(
                parameters
            );
        }

        void initialize_ignition()
        {
            m_timing_curve =
                create_function(
                    6,
                    units::rpm(4000.0)
                );
            m_timing_curve->addSample(
                units::rpm(0.0),
                12.0 *
                units::deg
            );
            constexpr std::array<double, 5> speeds =
                {
                    4000.0,
                    8000.0,
                    12000.0,
                    14000.0,
                    18000.0
                };
            for (double speed : speeds)
            {
                m_timing_curve->addSample(
                    units::rpm(speed),
                    40.0 *
                    units::deg
                );
            }

            IgnitionModule::Parameters parameters{};
            parameters.cylinderCount =
                cylinder_count;
            parameters.crankshaft =
                m_engine->getCrankshaft(0);
            parameters.timingCurve =
                m_timing_curve.get();
            parameters.revLimit =
                units::rpm(18500.0);
            parameters.limiterDuration =
                0.1;
            IgnitionModule* ignition =
                m_engine->getIgnitionModule();
            ignition->initialize(
                parameters
            );

            ignition->setFiringOrder(0, 0.0 * units::deg);
            ignition->setFiringOrder(6, 75.0 * units::deg);
            ignition->setFiringOrder(4, 120.0 * units::deg);
            ignition->setFiringOrder(10, 195.0 * units::deg);
            ignition->setFiringOrder(2, 240.0 * units::deg);
            ignition->setFiringOrder(8, 315.0 * units::deg);
            ignition->setFiringOrder(5, 360.0 * units::deg);
            ignition->setFiringOrder(11, 435.0 * units::deg);
            ignition->setFiringOrder(1, 480.0 * units::deg);
            ignition->setFiringOrder(7, 555.0 * units::deg);
            ignition->setFiringOrder(3, 600.0 * units::deg);
            ignition->setFiringOrder(9, 675.0 * units::deg);
        }

        void initialize_combustion_chambers()
        {
            m_mean_piston_speed_to_turbulence =
                create_function(
                    30,
                    1.0
                );
            for (int i = 0; i < 30; i++)
            {
                const double speed =
                    static_cast<double>(i);
                m_mean_piston_speed_to_turbulence->addSample(
                    speed,
                    speed *
                    0.5
                );
            }

            CombustionChamber::Parameters parameters{};
            parameters.Fuel =
                m_engine->getFuel();
            parameters.MeanPistonSpeedToTurbulence =
                m_mean_piston_speed_to_turbulence.get();
            parameters.StartingPressure =
                units::pressure(
                    1.0,
                    units::atm
                );
            parameters.StartingTemperature =
                units::celcius(25.0);
            parameters.CrankcasePressure =
                units::pressure(
                    1.0,
                    units::atm
                );

            for (int i = 0; i < cylinder_count; i++)
            {
                parameters.Piston =
                    m_engine->getPiston(i);
                parameters.Head =
                    m_engine->getHead(
                        parameters.Piston
                            ->getCylinderBank()
                            ->getIndex()
                    );
                m_engine->getChamber(i)->initialize(
                    parameters
                );
            }
        }

        void initialize_vehicle()
        {
            Vehicle::Parameters parameters{};
            parameters.mass =
                798.0 *
                units::kg;
            parameters.dragCoefficient =
                0.9;
            parameters.crossSectionArea =
                72.0 *
                units::inch *
                36.0 *
                units::inch;
            parameters.diffRatio =
                4.10;
            parameters.tireRadius =
                9.0 *
                units::inch;
            parameters.rollingResistance =
                200.0 *
                units::N;
            m_vehicle->initialize(
                parameters
            );
        }

        void initialize_transmission()
        {
            constexpr std::array<double, 6> gears =
                {
                    2.8,
                    2.29,
                    1.93,
                    1.583,
                    1.375,
                    1.19
                };

            Transmission::Parameters parameters{};
            parameters.GearCount =
                static_cast<int>(gears.size());
            parameters.GearRatios =
                gears.data();
            parameters.MaxClutchTorque =
                1000.0 *
                units::ft_lb;
            m_transmission->initialize(
                parameters
            );
        }

        void initialize_simulator()
        {
            m_simulator =
                std::make_unique<PistonEngineSimulator>();

            Simulator::Parameters parameters{};
            parameters.systemType =
                Simulator::SystemType::NsvOptimized;
            m_simulator->initialize(
                parameters
            );
            m_simulator->setSimulationFrequency(
                static_cast<int>(
                    m_engine->getSimulationFrequency()
                )
            );
            m_simulator->loadSimulation(
                m_engine.get(),
                m_vehicle.get(),
                m_transmission.get()
            );
            m_simulator->setFluidSimulationSteps(8);

            Synthesizer::AudioParameters audio_parameters =
                m_simulator
                    ->synthesizer()
                    .getAudioParameters();
            audio_parameters.dF_F_mix =
                static_cast<float>(
                    m_engine->getInitialHighFrequencyGain()
                );
            audio_parameters.airNoise =
                static_cast<float>(
                    m_engine->getInitialNoise()
                );
            audio_parameters.inputSampleNoise =
                static_cast<float>(
                    m_engine->getInitialJitter()
                );
            m_simulator
                ->synthesizer()
                .setAudioParameters(
                    audio_parameters
                );

            for (int i = 0; i < exhaust_count; i++)
            {
                const std::vector<std::int16_t> samples =
                    load_pcm16_wav(
                        m_impulse_responses[i]->getFilename()
                    );
                m_simulator
                    ->synthesizer()
                    .initializeImpulseResponse(
                        samples.data(),
                        static_cast<unsigned int>(
                            samples.size()
                        ),
                        static_cast<float>(
                            m_impulse_responses[i]->getVolume()
                        ),
                        i
                    );
            }
        }

        std::unique_ptr<::Engine> m_engine;
        std::unique_ptr<Vehicle> m_vehicle;
        std::unique_ptr<Transmission> m_transmission;
        std::unique_ptr<PistonEngineSimulator> m_simulator;

        std::array<
            std::unique_ptr<ImpulseResponse>,
            exhaust_count
        > m_impulse_responses;
        std::array<
            std::unique_ptr<Function>,
            bank_count
        > m_intake_flow;
        std::array<
            std::unique_ptr<Function>,
            bank_count
        > m_exhaust_flow;
        std::array<
            std::unique_ptr<Camshaft>,
            4
        > m_camshafts;
        std::array<
            std::unique_ptr<StandardValvetrain>,
            bank_count
        > m_valvetrains;

        std::unique_ptr<Function>
            m_turbulence_to_flame_speed;
        std::unique_ptr<Function>
            m_mean_piston_speed_to_turbulence;
        std::unique_ptr<Function> m_intake_lobe;
        std::unique_ptr<Function> m_exhaust_lobe;
        std::unique_ptr<Function> m_timing_curve;
        bool m_engine_initialized = false;
    };

    CarEngineSimFerrari412::CarEngineSimFerrari412()
        : m_implementation(
            std::make_unique<implementation>()
        )
    {
    }

    CarEngineSimFerrari412::~CarEngineSimFerrari412()
    {
        destroy();
    }

    std::unique_ptr<CarEngineSimFerrari412>
        CarEngineSimFerrari412::create(
            const std::string& impulse_response_path
        )
    {
        std::unique_ptr<CarEngineSimFerrari412> owner =
            std::make_unique<CarEngineSimFerrari412>();
        owner->initialize(
            impulse_response_path
        );
        return owner;
    }

    void CarEngineSimFerrari412::initialize(
        const std::string& impulse_response_path
    )
    {
        m_implementation->initialize(
            impulse_response_path
        );
    }

    void CarEngineSimFerrari412::destroy()
    {
        if (m_implementation)
        {
            m_implementation->destroy();
        }
    }

    ::Engine* CarEngineSimFerrari412::get_engine() const
    {
        return m_implementation->get_engine();
    }

    Vehicle* CarEngineSimFerrari412::get_vehicle() const
    {
        return m_implementation->get_vehicle();
    }

    Transmission*
        CarEngineSimFerrari412::get_transmission() const
    {
        return m_implementation->get_transmission();
    }

    PistonEngineSimulator*
        CarEngineSimFerrari412::get_simulator() const
    {
        return m_implementation->get_simulator();
    }
}
