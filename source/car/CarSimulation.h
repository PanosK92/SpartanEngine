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

//= INCLUDES ==========
#include "CarState.h"
#include "CarPacejka.h"
#include "CarThermal.h"
#include "CarKinematics.h"
#include "CarInertia.h"
#include "CarCalibration.h"
#include "CarMultibody.h"
//=====================

namespace car
{
    // everything setup() needs to build the physx actor and the aero model
    struct setup_params
    {
        PxPhysics*          physics      = nullptr;
        PxScene*            scene        = nullptr;
        PxConvexMesh*       chassis_mesh = nullptr; // optional seed hull, replaced by compound later
        std::vector<PxVec3> vertices;               // original mesh verts for aero calculation
        config              car_config;
        bool                create_mechanisms = true; // false skips suspension multibody for cheap sim
    };

    // the whole vehicle model, a physx chassis body plus per-wheel tire, suspension, drivetrain and aero
    class Simulation
    {
    private:
        car_preset spec = car_preset();
        bool log_pacejka = false;
        bool log_telemetry = false;
        bool log_to_file = false;
        std::string telemetry_path = "car_telemetry.csv";
        PxRigidDynamic* body = nullptr;
        PxMaterial*     material         = nullptr;
        config          cfg;
        car_preset      base_spec;
        active_upgrades upgrades;
        wheel           wheels[wheel_count];
        input_state     input;
        input_state     input_target;
        assist_command  assisted_actuators;
        PxVec3          wheel_offsets[wheel_count];
        float           wheel_moi[wheel_count];
        float           spring_stiffness[wheel_count];
        float           spring_damping[wheel_count];
        float           spring_force[wheel_count] = {};
        float           sweep_distance[wheel_count] = {};
        float           abs_phase               = 0.0f;
        bool            abs_active[wheel_count] = {};
        float           tc_reduction            = 0.0f;
        bool            tc_active               = false;
        bool            burnout_active          = false;
        float           reverse_request_timer   = 0.0f;
        float           engine_rpm              = 800.0f;
        float           engine_rotation         = 0.0f;
        int             current_gear            = 1;
        float           shift_timer             = 0.0f;
        bool            is_shifting             = false;
        float           clutch                  = 1.0f;
        float           shift_cooldown          = 0.0f;
        int             last_shift_direction    = 0;
        float           previous_automatic_throttle = 0.0f;
        float           boost_pressure          = 0.0f;
        float           motor_torque            = 0.0f;
        hybrid_state    battery;
        bool            engine_running = true;
        bool            starter_requested = false;
        float           regen_axle_torque = 0;
        double          clutch_heat_j = 0;
        double          gearbox_loss_j = 0;
        double          distance_m = 0;
        PxVec3          previous_position = PxVec3(0);
        bool            position_valid = false;
        PxVec3          contact_impulse = PxVec3(0);
        unsigned        event_flags = 0;
        unsigned        reset_count = 0;
        bool            rev_limiter_active      = false;
        float           downshift_blip_timer    = 0.0f;
        float           driveshaft_twist        = 0.0f;
        float           driveshaft_torque       = 0.0f;
        float           gearbox_input_angular_velocity = 0.0f;
        bool            drs_active              = false;
        float           longitudinal_accel      = 0.0f;
        float           lateral_accel           = 0.0f;
        PxVec3          prev_velocity           = PxVec3(0);
        float           vehicle_sleep_timer     = 0.0f;
        bool            vehicle_sleeping        = false;
        float           engine_brake_torque     = 0.0f;
        float           engine_output_torque    = 0.0f;
        float           axle_drive_torque       = 0.0f;
        aero_debug_data aero_debug;
        debug_sweep_data debug_sweep[wheel_count];
        PxVec3           debug_suspension_top[wheel_count];
        PxVec3           debug_suspension_bottom[wheel_count];
        SelfFilterCallback self_filter;
        multibody_state multibody;
        bool simulation_enabled = true;
        shape_2d shape_data;

    public:

        surface_type (*surface_resolver)(const PxRigidActor*) = nullptr;
        Simulation() = default;
        ~Simulation();
        Simulation(const Simulation&) = delete;
        Simulation& operator=(const Simulation&) = delete;
        PxRigidDynamic* get_body() const;
        const config& get_config() const;
        config& get_config();
        const car_preset& get_spec() const;
        car_preset& get_spec();
        const car_preset& get_base_spec() const;
        const active_upgrades& get_upgrades() const;
        active_upgrades& get_upgrades();
        const wheel& get_wheel_state(int i) const;
        wheel& get_wheel_state(int i);
        const multibody_state& get_multibody_state() const;
        bool get_log_to_file() const;
        void set_log_to_file(bool enabled);
        void set_telemetry_path(const std::string& path);
        std::string get_telemetry_path() const;
        bool get_rev_limiter_active() const;
        float get_clutch() const;
        float get_engine_rotation() const;
        void set_simulation_enabled(bool enabled);
        // toggles suspension multibody actors only, chassis stays as set by set_simulation_enabled
        void set_mechanism_simulation_enabled(bool enabled);
        bool has_multibody() const;
        bool ensure_multibody(PxPhysics* physics, PxScene* scene);
        void set_force_retention(bool enabled);
        void clear_force_accumulators();

    private:
        FILE* file          = nullptr;
        int   frame_counter = 0;
        float elapsed_time  = 0.0f;
        void close_telemetry();
        void flush_telemetry();

        // reopen for append without truncating an existing csv
        bool reopen_telemetry_append();

    public:
        // flush, temporarily close, read the last max_rows lines, reopen for append
        bool snapshot_telemetry_tail(int max_rows, std::string& out_text, std::string& out_path, int& out_total_lines);
        bool open_telemetry_if_needed();
        void write_telemetry_wheel_state(int i);
        void tick_telemetry(float dt, float speed_kmh);

        // rejects invalid vectors before physx api calls
        bool is_finite_vec(const PxVec3& v);
        bool can_apply_force(PxRigidDynamic* body);
        void safe_add_force(PxRigidDynamic* body, const PxVec3& force, PxForceMode::Enum mode = PxForceMode::eFORCE);
        void safe_add_torque(PxRigidDynamic* body, const PxVec3& torque, PxForceMode::Enum mode = PxForceMode::eFORCE);
        void safe_add_force_at_pos(PxRigidDynamic* body, const PxVec3& force, const PxVec3& pos, PxForceMode::Enum mode = PxForceMode::eFORCE);

        // function local storage avoids duplicate definitions
        shape_2d& shape_data_ref();
        bool is_in_reverse();
        bool is_in_neutral();
        bool is_in_forward_gear();

        // sanitization prevents one invalid wheel value from poisoning physx
        bool sanitize_float(float& v, float fallback = 0.0f);
        bool sanitize_vec(PxVec3& v, const PxVec3& fallback = PxVec3(0.0f));
        bool sanitize_wheel_state(int i);
        bool is_front(int i) const;
        bool is_rear(int i);
        bool is_driven(int i);
        float lerp(float a, float b, float t);
        float exp_decay(float rate, float dt);
        bool is_valid_wheel(int i);
        const char* get_wheel_name(int i);
        void clear_abs_state();
        float get_assisted_steering_target(float raw_input);
        void update_assist_controller(bool traction_requested, bool braking_requested, float dt);
        void update_burnout(float forward_speed_ms);

        // lateral grip peaks at a slightly negative camber and falls off quadratically
        float get_camber_grip_factor(float camber);

        // derived from com z-offset and wheelbase, no need to store separately
        float get_weight_distribution_front();
        float load_sensitive_grip(float load);
        float get_tire_temp_grip_factor(float temperature);
        tire_condition_modifiers get_tire_condition_modifiers(float surface_temperature, float core_temperature, float wear, float load);
        float get_surface_friction(surface_type surface);
        float get_brake_efficiency(float temp);
        void compute_aero_from_shape(const std::vector<PxVec3>& vertices);

        // convex hull supports side and front chassis silhouettes
        std::vector<std::pair<float, float>> graham_scan_hull_2d(std::vector<std::pair<float, float>> points);
        void compute_shape_visualization(const std::vector<PxVec3>& vertices, const PxVec3& min_pt, const PxVec3& max_pt);
        void apply_aero_and_resistance();
        PxU32 multibody_collision_group();
        actor_motion_state capture_actor_motion(PxRigidDynamic* actor);
        void restore_actor_motion(PxRigidDynamic* actor, const actor_motion_state& state);
        multibody_motion_state capture_multibody_motion();
        void restore_multibody_motion(const multibody_motion_state& state);
        PxTransform local_anchor(PxRigidActor* actor, const PxVec3& world_point);
        void register_multibody_actor(PxRigidDynamic* actor);
        void register_multibody_joint(PxJoint* joint);
        void configure_mechanism_shape(PxShape* shape);
        PxRigidDynamic* create_mechanism_actor(const PxTransform& pose, const PxGeometry& geometry, float mass);
        PxRigidDynamic* create_segment_actor(
            const PxVec3& world_start,
            const PxVec3& world_end,
            float radius,
            float mass);
        PxSphericalJoint* create_spherical_joint(PxRigidActor* actor_a, PxRigidActor* actor_b, const PxVec3& world_anchor);

        // an inboard pickup is rubber, not a pin joint, stiff along the member because that is the load path
        // and softer across it, which is where compliance steer and fore aft absorption live
        PxJoint* create_bushing_joint(PxRigidActor* actor_a, PxRigidActor* actor_b, const PxVec3& world_anchor, const PxVec3& load_direction);
        PxRigidDynamic* create_link(const PxVec3& world_start, const PxVec3& world_end, float mass, PxRigidDynamic* start_actor = nullptr, PxJoint** out_anchor_joint = nullptr, bool* out_anchor_is_bushing = nullptr);
        bool connect_link_to_upright(PxRigidDynamic* link, PxRigidDynamic* upright, const PxVec3& world_anchor);
        bool add_link_member(suspension_corner& corner, const PxVec3& world_start, const PxVec3& world_end, float mass, PxRigidDynamic* start_actor = nullptr);
        bool add_wishbone(suspension_corner& corner, const PxVec3& inner_front, const PxVec3& inner_rear, const PxVec3& outer, float mass);
        bool add_macpherson_strut(suspension_corner& corner, const PxVec3& top, const PxVec3& bottom, float mass);
        bool add_steering_stop(suspension_corner& corner, PxRigidDynamic* upright, const PxTransform& chassis_pose, const PxVec3& wheel_world, float angle_limit);
        PxVec3 hardpoint_world(const PxTransform& chassis_pose, const PxVec3& local_point) const;
        float mechanism_actor_mass();
        float unsprung_mass();
        float chassis_mass();
        float sprung_mass();
        bool has_authored_inertia() const;
        void apply_chassis_mass_properties(float mass, const PxVec3& com_local);
        void update_assembled_center_of_mass();
        PxMat33 get_assembled_inertia() const;
        bool export_chassis_hulls(const std::string& path) const;
        bool create_suspension_corner(int wheel_index, const suspension_geometry& geometry);
        bool create_locked_differential(int left, int right);
        bool create_steering_rack();
        bool create_anti_roll_bar(anti_roll_bar& arb, int left, int right, float stiffness);
        bool create_anti_roll_bars();
        bool create_coilover(int wheel_index);
        bool create_driveline_shafts();
        bool create_driveline();
        bool has_physical_coilover(int wheel_index) const;
        bool has_physical_driveline() const;
        float read_driveline_twist() const;
        float read_driveline_gearbox_speed() const;
        void sync_driveline_axle_speed(float axle_speed);
        void set_driveline_torsion_enabled(bool enabled);
        void destroy_multibody();
        bool create_multibody(PxPhysics* physics, PxScene* scene, bool destroy_existing = true);
        bool rebuild_multibody(bool preserve_motion = true);
        PxVec3 actor_point_velocity(const PxRigidBody* actor, const PxVec3& world_point);
        PxVec3 ground_point_velocity(const wheel& wheel_state);
        void refresh_wheel_actor_state();
        void wake_vehicle_assembly();
        bool vehicle_assembly_is_settled();
        void sleep_vehicle_assembly();
        float compute_damper_force(float velocity, float base_damping);
        void update_multibody(float delta_time);

        // one tread row, its columns straddle the contact arc and every hit is projected back to the
        // perpendicular height, so the row rides an averaged plane and spans a pothole instead of dropping in
        tire_probe_row probe_tread_row(PxScene* scene, const PxVec3& row_center, const PxVec3& plane_down, const PxVec3& wheel_axis, const PxVec3& local_up, float row_radius, float ray_length, float max_penetration, int column_count, float arc, const PxQueryFilterData& filter);
        void update_suspension(PxScene* scene, float dt, bool apply_forces = true);
        float angular_velocity_to_rpm(float angular_velocity);
        float get_driven_wheel_radius();
        float get_average_driven_angular_velocity(bool absolute, int* count = nullptr);
        void update_boost(float throttle, float rpm, float dt);
        float get_engine_torque(float rpm);
        float get_electric_motor_torque(float rpm, float throttle);
        float wheel_rpm_to_engine_rpm(float wheel_rpm, int gear);
        float get_upshift_speed(int from_gear, float throttle);
        float get_downshift_speed(int gear);
        void set_active_gear(int gear);
        void update_automatic_gearbox(float dt, float throttle, float forward_speed);
        const char* get_gear_string();

        // apply differential torque to a single axle (left/right wheel pair)
        void apply_axle_diff(int left, int right, float axle_torque, float dt);

        // route torque to driven axle(s) based on drivetrain layout
        void apply_drive_torque(float total_torque, float dt);
        void integrate_powertrain(float dt);
        float brake_torque_sign(int wheel_index);

        // service brakes add wheel torque heat and abs modulation
        void apply_service_brakes(float forward_speed_ms, float dt);
        void apply_drivetrain(float forward_speed_kmh, float dt);
        float get_effective_wheel_radius(int wheel_index, float tire_load);
        void update_handbrake();

        // tire condition only tracks temperature and wear, both of which move far slower than a step, so
        // it is evaluated once per step while slip itself is relaxed inside the substep loop
        void update_tire_condition();

        // slip is a state, the tread has to roll a relaxation length before it carries the force the
        // geometry asks for, and it builds faster than it releases
        void relax_tire_slip(wheel& w, float raw_slip_ratio, float raw_slip_angle, float ground_speed, float surface_speed, float dt);
        void apply_tire_forces(float dt);
        void apply_self_aligning_torque();

        // used by car bench to seed scenario start speed
        void set_validation_speed(float speed);

        // refresh after any direct wheel offset change
        void refresh_geometry_cache();
        void apply_positive_override(float& target, float value);

        // zero preset dimensions preserve safe engine defaults
        void apply_preset_geometry(const car_preset& spec);
        void compute_constants();
        void destroy();
        bool setup(const setup_params& params);
        // replaces chassis collision with overlapping convex proxies in body space
        bool set_chassis(
            const std::vector<PxConvexMesh*>& meshes,
            const std::vector<PxVec3>& vertices,
            PxPhysics* physics
        );
        void update_mass_properties();
        void apply_car_spec(const car_preset& preset, bool set_as_base);
        bool rebuild_vehicle_geometry();
        void reset_drivetrain_transients();
        void reset_wheel_thermals();

        // runtime preset swaps rebuild all geometry and reset transient state
        void load_car(const car_preset& new_spec);
        car_preset make_upgraded_spec(const car_preset& base, const active_upgrades& ups);
        void clamp_upgrade_stage(int& stage, int max_stage);
        void reapply_upgrades();
        void reset_upgrades();
        void set_center_of_mass(float x, float y, float z);
        void set_center_of_mass_x(float x);
        void set_center_of_mass_y(float y);
        void set_center_of_mass_z(float z);
        float get_center_of_mass_x();
        float get_center_of_mass_y();
        float get_center_of_mass_z();
        float get_frontal_area();
        float get_side_area();
        float get_drag_coeff();
        float get_lift_coeff_front();
        float get_lift_coeff_rear();
        void set_frontal_area(float area);
        void set_side_area(float area);
        void set_drag_coeff(float cd);
        void set_lift_coeff_front(float cl);
        void set_lift_coeff_rear(float cl);
        void set_ground_effect_enabled(bool enabled);
        bool get_ground_effect_enabled();
        void set_ground_effect_multiplier(float mult);
        float get_ground_effect_multiplier();
        void set_throttle(float v);
        void set_brake(float v);
        void set_steering(float v);
        void set_handbrake(float v);
        void update_input(float dt);
        void tick(float dt);
        float get_speed_kmh();
        float get_throttle();
        float get_brake();
        float get_steering();

        // where the lateral force acts behind the wheel centre. the brush model already knows this from its
        // patch length, only the curve fit needs the separate trail model
        float get_wheel_pneumatic_trail(int i);
        float get_wheel_self_aligning_torque(int i);

        // visual setters also repair the underlying simulation state
        void set_wheel_rotation(int i, float v);
        void set_wheel_angular_velocity(int i, float v);
        float get_handbrake();
        float get_suspension_travel();
        float get_wheel_compression(int i);
        float get_wheel_slip_angle(int i);
        float get_wheel_slip_ratio(int i);
        float get_wheel_tire_load(int i);
        float get_wheel_lateral_force(int i);
        float get_wheel_longitudinal_force(int i);
        float get_wheel_angular_velocity(int i);
        float get_wheel_rotation(int i);
        float get_wheel_effective_radius(int i);
        float get_wheel_dynamic_camber(int i);
        float get_wheel_dynamic_toe(int i);
        float get_wheel_bump_steer(int i);
        float get_wheel_motion_ratio(int i);
        float get_wheel_temperature(int i);
        bool is_wheel_grounded(int i);
        float get_wheel_suspension_force(int i);
        float get_axle_roll_stiffness(bool front);
        float get_wheel_temp_grip_factor(int i);
        float get_wheel_surface_temp(int i, int zone);
        float get_wheel_core_temp(int i);
        float get_tire_pressure();
        float get_tire_pressure_optimal();
        float get_chassis_visual_offset_y();
        void set_abs_enabled(bool enabled);
        bool get_abs_enabled();
        bool is_abs_active(int i);
        bool is_abs_active_any();
        float get_abs_phase();
        void set_tc_enabled(bool enabled);
        bool get_tc_enabled();
        bool is_tc_active();
        float get_tc_reduction();
        bool is_burnout_active();
        void set_manual_transmission(bool enabled);
        bool get_manual_transmission();
        void begin_shift(int direction);
        void shift_up();
        void shift_down();
        void shift_to_neutral();
        int get_current_gear();
        float get_current_engine_rpm();
        bool get_is_shifting();
        float get_clutch();
        float get_engine_torque_current();
        float get_engine_output_torque();
        float get_motor_torque();
        float get_driveshaft_twist();
        float get_driveshaft_torque();
        float get_axle_drive_torque();
        float get_longitudinal_accel();
        float get_lateral_accel();
        bool get_vehicle_sleeping();
        float get_gearbox_input_angular_velocity();
        float get_motor_power_kw();
        const hybrid_state& get_hybrid_state() const { return battery; }
        bool get_engine_running() const { return engine_running; }
        void set_starter(bool active) { starter_requested = active; wake_vehicle_assembly(); }
        void record_contact_impulse(const PxVec3& impulse) { if (impulse.isFinite()) { contact_impulse += impulse; event_flags |= 4; } }
        void record_reset() { event_flags |= 1; ++reset_count; position_valid = false; }
        float get_redline_rpm();
        float get_max_rpm();
        float get_idle_rpm();
        void set_turbo_enabled(bool enabled);
        bool get_turbo_enabled();
        float get_boost_pressure();
        float get_boost_max_pressure();
        void set_drs_enabled(bool enabled);
        bool get_drs_enabled();
        void set_drs_active(bool active);
        bool get_drs_active();
        void set_diff_type(int type);
        int get_diff_type();
        const char* get_diff_type_name();
        float get_wheel_wear(int i);
        void reset_tire_wear();
        float get_wheel_wear_grip_factor(int i);
        float get_wheel_brake_temp(int i);
        float get_wheel_brake_efficiency(int i);
        void set_wheel_surface(int i, surface_type surface);
        surface_type get_wheel_surface(int i);
        const char* get_surface_name(surface_type surface);
        float get_front_camber();
        float get_rear_camber();
        float get_front_toe();
        float get_rear_toe();
        void set_wheel_offset(int wheel, float x, float z);
        PxVec3 get_wheel_offset(int wheel);
        const aero_debug_data& get_aero_debug();
        const shape_2d& get_shape_data();
        void get_debug_sweep(int wheel, PxVec3& origin, PxVec3& hit_point, bool& hit);

        // the individual tread rows the solver was given, so the drawn patch is the real one
        int get_debug_contact_rows(int wheel) const;
        void get_debug_contact_row(int wheel, int row, PxVec3& point, PxVec3& normal, float& load) const;
        void get_debug_suspension(int wheel, PxVec3& top, PxVec3& bottom);
        float get_wheel_radius();
        float get_wheel_width();
        PxTransform get_body_pose();
    };
}
