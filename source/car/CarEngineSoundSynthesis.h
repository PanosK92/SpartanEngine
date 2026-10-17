/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include <cstdint>
#include <memory>

// procedural engine sound, every cylinder fires a blowdown pulse into its own exhaust runner,
// the runners meet in a per bank collector, pass a muffler and a tailpipe, on top sit the
// induction roar, the turbo, the valvetrain and the overrun pops, all driven by the car spec

namespace engine_sound
{
    namespace tuning
    {
        constexpr int max_cylinders = 16;
        constexpr int sample_rate = 48000;
    }

    // where the listener sits, decides how much body and cabin sit between them and the exhaust
    enum class listener_view
    {
        chase,
        hood,
        cabin
    };

    // the hardware around the cylinders, each gives an engine part of its voice
    enum class forced_induction : int
    {
        turbo,       // exhaust driven, lags, whistles and vents its charge on lift
        roots,       // belt driven lobes, whines locked to the crank
        twin_screw,  // belt driven screws, a higher and smoother whine
        centrifugal  // belt driven impeller, a whistle locked to the crank
    };

    enum class cam_drive : int
    {
        chain,
        belt,
        gear
    };

    enum class fuel_injection : int
    {
        port,
        direct,      // high pressure injectors and their cam driven pump tick
        port_direct, // port at light load, direct as load rises
        mechanical   // continuous or pump driven, no solenoids to click
    };

    enum class exhaust_crossover : int
    {
        none,   // true dual, each bank keeps its own pipe
        h_pipe, // a balance tube, the banks share a little
        x_pipe, // the banks merge and split again, much of the off beat is evened out
        merged  // one pipe for both banks
    };

    enum class gearbox_type : int
    {
        manual,      // helical, synchronised
        dual_clutch,
        automatic,   // torque converter planetary, the quietest gears
        straight_cut // spur gears, a dog box or a sequential, they sing
    };

    struct engine_config
    {
        int cylinder_count = 4;
        int bank_count = 1;
        float bank_angle_deg = 0.0f;
        float idle_rpm = 800.0f;
        float redline_rpm = 7000.0f;
        float max_rpm = 7500.0f;
        float displacement_l = 2.0f;
        float bore_mm = 86.0f;
        float stroke_mm = 86.0f;
        float compression_ratio = 10.0f;
        float primary_length_m = 0.45f;
        float collector_length_m = 2.0f;
        float tailpipe_length_m = 0.6f;
        // one is a quiet stock muffler, zero is a straight pipe
        float muffler_level = 1.0f;
        // Zero runner length derives an estimate from stroke. Angles are crank degrees.
        float intake_runner_length_m = 0.0f;
        float intake_valve_duration_deg = 220.0f;
        float combustion_variation = 0.025f;
        float crank_inertia = 0.2f;
        // Intervals in firing-order sequence, totaling 720; all zero means even firing.
        // Bank angle alone cannot tell us whether a crank uses split pins.
        float firing_intervals_deg[tuning::max_cylinders] = {};
        int firing_order[tuning::max_cylinders] = {};
        int cylinder_bank[tuning::max_cylinders] = {};
        bool turbo_enabled = false;
        bool turbo_bypass_valve = true; // a working bypass vents charge instead of compressor surge
        float boost_max_pressure = 0.0f;
        float boost_wastegate_rpm = 0.0f;
        forced_induction induction = forced_induction::turbo;
        int turbo_count = 1; // the flow split over more wheels makes each smaller and faster
        float supercharger_drive_ratio = 2.0f; // rotor or impeller turns per crank turn
        int supercharger_lobes = 3; // per rotor
        // valvetrain
        int valves_per_cylinder = 4;
        int camshafts_per_bank = 2; // zero is a pushrod engine's single cam in the block
        bool solid_lifters = false; // mechanical lash taps where hydraulic lifters stay silent
        cam_drive cam_drive_type = cam_drive::chain;
        fuel_injection injection = fuel_injection::port;
        bool individual_throttle_bodies = false;
        // an air cooled engine has no water jacket to hold its mechanical noise in, and a fan of its own
        bool air_cooled = false;
        float cooling_fan_ratio = 1.6f;
        int cooling_fan_blades = 11;
        // exhaust aftertreatment and routing
        bool catalytic_converter = true;
        bool particulate_filter = false;
        exhaust_crossover crossover = exhaust_crossover::none;
        bool exhaust_valve = false; // an active flap that bypasses the muffler at high load
        gearbox_type gearbox = gearbox_type::manual;
        bool electric_motor = false;
        int motor_pole_pairs = 4;
        // upgrade stages as a fraction of their maximum stage
        float engine_stage = 0.0f;
        float exhaust_stage = 0.0f;
        float intake_stage = 0.0f;
        float turbo_stage = 0.0f;

        engine_config();
        bool operator==(const engine_config& other) const;
        bool operator!=(const engine_config& other) const;
    };

    struct debug_data
    {
        float rpm = 0.0f;
        float throttle = 0.0f;
        float load = 0.0f;
        float boost = 0.0f;
        float firing_freq = 0.0f;
        float exhaust_level = 0.0f;
        float intake_level = 0.0f;
        float turbo_level = 0.0f;
        float mechanical_level = 0.0f;
        float pop_level = 0.0f;
        float output_level = 0.0f;
        float output_peak = 0.0f;
        float limiter_gain = 1.0f;
        int pops_fired = 0;
        bool odd_fire = false;

        static constexpr int waveform_size = 512;
        float waveform[waveform_size] = {};
        int waveform_write_pos = 0;

        int dump_total = 0;
        int dump_progress = 0;
        bool dump_ready = false;
        std::uint64_t generate_calls = 0;
        std::uint64_t samples_generated = 0;
        bool initialized = false;
    };

    class synthesizer
    {
    public:
        synthesizer();
        ~synthesizer();

        synthesizer(const synthesizer&) = delete;
        synthesizer& operator=(const synthesizer&) = delete;

        // initialize runs before any stream pulls; everything else below is safe while generate()
        // runs on the audio thread, start, prime and reset are queued and applied at its next block
        void initialize(int sample_rate = tuning::sample_rate);
        void configure(const engine_config& config);
        // on ignition: crank, catch, flare, then follow live telemetry
        void start();
        // joining an engine that is already running: settle on the current controls, no starter
        void prime();
        void set_parameters(
            float rpm,
            float throttle,
            float load,
            float boost_pressure,
            bool fuel_cut,
            int gear,
            bool shifting,
            listener_view view,
            float gearbox_rpm,
            bool overrun,
            float bank_pan
        );
        // a hybrid's traction motor, its speed and its torque as a fraction of peak
        void set_electric(float motor_rpm, float motor_load);
        void generate(
            float* output_buffer,
            int num_samples,
            bool stereo = true
        );
        void reset();

        bool is_initialized() const;
        int get_output_sample_rate() const;
        const engine_config& get_config() const;
        debug_data get_debug() const;
        bool begin_dump(float seconds);
        bool dump_ready() const;
        bool save_dump(const char* path);

    private:
        class implementation;
        std::unique_ptr<implementation> m_implementation;
    };

    synthesizer& get_synthesizer();
    void initialize(int sample_rate = tuning::sample_rate);
    void configure(const engine_config& config);
    void start();
    void prime();
    void set_parameters(
        float rpm,
        float throttle,
        float load,
        float boost,
        bool fuel_cut,
        int gear,
        bool shifting,
        listener_view view,
        float gearbox_rpm,
        bool overrun,
        float bank_pan
    );
    void set_electric(float motor_rpm, float motor_load);
    void generate(
        float* buffer,
        int num_samples,
        bool stereo = true
    );
    void reset();
    debug_data get_debug();
}
