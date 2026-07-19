#pragma once

private:
car_preset spec = car_preset();
bool log_pacejka = false;
bool log_telemetry = false;
bool log_to_file = true;
std::string telemetry_path = "car_telemetry.csv";

PxRigidDynamic* body = nullptr;
PxMaterial*     material         = nullptr;
PxMaterial*     wheel_guard_material = nullptr;
PxConvexMesh*   wheel_sweep_mesh = nullptr;
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
bool multibody_enabled = true;
bool simulation_enabled = true;
validation_state validation;

    shape_2d shape_data;

public:

    Simulation() = default;
    ~Simulation() { close_telemetry(); destroy(); }
    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;

    PxRigidDynamic* get_body() const { return body; }
    const config& get_config() const { return cfg; }
    config& get_config() { return cfg; }
    const car_preset& get_spec() const { return spec; }
    car_preset& get_spec() { return spec; }
    const car_preset& get_base_spec() const { return base_spec; }
    const active_upgrades& get_upgrades() const { return upgrades; }
    active_upgrades& get_upgrades() { return upgrades; }
    const wheel& get_wheel_state(int i) const { return wheels[PxClamp(i, 0, wheel_count - 1)]; }
    wheel& get_wheel_state(int i) { return wheels[PxClamp(i, 0, wheel_count - 1)]; }
    const multibody_state& get_multibody_state() const { return multibody; }
    bool get_log_to_file() const { return log_to_file; }
    void set_log_to_file(bool enabled) { log_to_file = enabled; }
    void set_telemetry_path(const std::string& path) { close_telemetry(); telemetry_path = path.empty() ? "car_telemetry.csv" : path; }
    bool get_rev_limiter_active() const { return rev_limiter_active; }
    float get_clutch() const { return clutch; }
    float get_engine_rotation() const { return engine_rotation; }
    void set_simulation_enabled(bool enabled)
    {
        if (simulation_enabled == enabled)
        {
            return;
        }
        auto set_body_queries_enabled = [this](bool query_enabled)
        {
            if (!body)
            {
                return;
            }
            std::vector<PxShape*> shapes(body->getNbShapes());
            if (!shapes.empty())
            {
                body->getShapes(shapes.data(), static_cast<PxU32>(shapes.size()));
                for (PxShape* shape : shapes)
                {
                    shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, query_enabled);
                }
            }
        };
        if (!enabled)
        {
            clear_force_accumulators();
            set_body_queries_enabled(false);
        }
        simulation_enabled = enabled;
        if (body)
        {
            body->setActorFlag(PxActorFlag::eDISABLE_SIMULATION, !enabled);
        }
        for (int i = 0; i < multibody.actor_count; i++)
        {
            if (multibody.actors[i])
            {
                multibody.actors[i]->setActorFlag(PxActorFlag::eDISABLE_SIMULATION, !enabled);
            }
        }
        if (enabled)
        {
            set_body_queries_enabled(true);
        }
    }
    void set_force_retention(bool enabled)
    {
        if (body)
        {
            body->setRigidBodyFlag(PxRigidBodyFlag::eRETAIN_ACCELERATIONS, enabled);
        }
        for (int i = 0; i < multibody.actor_count; i++)
        {
            if (multibody.actors[i])
            {
                multibody.actors[i]->setRigidBodyFlag(PxRigidBodyFlag::eRETAIN_ACCELERATIONS, enabled);
            }
        }
    }
    void clear_force_accumulators()
    {
        if (body)
        {
            body->clearForce();
            body->clearTorque();
        }
        for (int i = 0; i < multibody.actor_count; i++)
        {
            if (multibody.actors[i])
            {
                multibody.actors[i]->clearForce();
                multibody.actors[i]->clearTorque();
            }
        }
    }

private:
        FILE* file          = nullptr;
        int   frame_counter = 0;
        float elapsed_time  = 0.0f;
void close_telemetry()
        {
            if (file)
            {
                fclose(file);
                file = nullptr;
            }
            frame_counter = 0;
            elapsed_time  = 0.0f;
        }

        void flush_telemetry()
        {
            if (file)
            {
                fflush(file);
            }
        }

        // absolute path of the csv when known, empty if not open yet
        std::string get_telemetry_path() const
        {
            char abs_path[1024] = {};
            if (_fullpath(abs_path, telemetry_path.c_str(), sizeof(abs_path)))
            {
                return abs_path;
            }
            return telemetry_path;
        }

        // reopen for append without truncating an existing csv
        bool reopen_telemetry_append()
        {
            if (file)
            {
                return true;
            }
            fopen_s(&file, telemetry_path.c_str(), "a");
            return file != nullptr;
        }

public:
        // flush, temporarily close, read the last max_rows lines, reopen for append
        bool snapshot_telemetry_tail(int max_rows, std::string& out_text, std::string& out_path, int& out_total_lines)
        {
            out_text.clear();
            out_path = get_telemetry_path();
            out_total_lines = 0;
            if (!log_to_file)
            {
                return false;
            }
            flush_telemetry();
            if (file)
            {
                fclose(file);
                file = nullptr;
            }

            FILE* read_file = nullptr;
            fopen_s(&read_file, telemetry_path.c_str(), "r");
            if (!read_file)
            {
                reopen_telemetry_append();
                return false;
            }

            std::vector<std::string> lines;
            char buffer[8192];
            while (fgets(buffer, sizeof(buffer), read_file))
            {
                lines.emplace_back(buffer);
            }
            fclose(read_file);
            out_total_lines = static_cast<int>(lines.size());

            if (lines.empty())
            {
                reopen_telemetry_append();
                return true;
            }

            // always keep header, then the last max_rows data rows
            out_text = lines[0];
            if (!out_text.empty() && out_text.back() != '\n')
            {
                out_text.push_back('\n');
            }
            const int data_count = std::max(0, out_total_lines - 1);
            const int start = 1 + std::max(0, data_count - std::max(max_rows, 0));
            for (int i = start; i < out_total_lines; i++)
            {
                out_text += lines[static_cast<size_t>(i)];
                if (!out_text.empty() && out_text.back() != '\n')
                {
                    out_text.push_back('\n');
                }
            }

            return reopen_telemetry_append();
        }

        bool open_telemetry_if_needed()
        {
            if (file)
            {
                return true;
            }
            fopen_s(&file, telemetry_path.c_str(), "w");
            if (!file)
            {
                return false;
            }

            // log absolute path so the user can find the file
            char abs_path[1024] = {};
            if (_fullpath(abs_path, "car_telemetry.csv", sizeof(abs_path)))
            {
                SP_LOG_INFO("car telemetry: writing to %s", abs_path);
            }

            // header and writer order form one external schema and must remain aligned
            fprintf(file,
                // time + body state
                "frame,time,dt,car_name,"
                "pos_x,pos_y,pos_z,"
                "speed_kmh,forward_speed_ms,lateral_speed_ms,"
                "yaw_rate,body_slip_deg,"
                "long_accel,lat_accel,"
                // body vertical velocity and angular velocity magnitude, top level signal for
                // whether the chassis is settling, oscillating or being pumped by the springs
                "vy,ang_vel_mag,"
                // inputs
                "throttle,brake,steering,handbrake,"
                // drivetrain
                "gear,engine_rpm,is_shifting,clutch,tc_active,tc_reduction,"
                // per-wheel grounded
                "fl_grounded,fr_grounded,rl_grounded,rr_grounded,"
                // per-wheel slip
                "fl_slip_angle,fr_slip_angle,rl_slip_angle,rr_slip_angle,"
                "fl_slip_ratio,fr_slip_ratio,rl_slip_ratio,rr_slip_ratio,"
                // per-wheel forces and loads
                "fl_lat_force,fr_lat_force,rl_lat_force,rr_lat_force,"
                "fl_long_force,fr_long_force,rl_long_force,rr_long_force,"
                "fl_tire_load,fr_tire_load,rl_tire_load,rr_tire_load,"
                // per-wheel angular velocity
                "fl_ang_vel,fr_ang_vel,rl_ang_vel,rr_ang_vel,"
                // per wheel net torque accumulated this tick_telemetry from engine, brakes, tire reaction and bearing
                "fl_net_torque,fr_net_torque,rl_net_torque,rr_net_torque,"
                // total engine braking torque applied to driven axle, key signal for liftoff oversteer
                "engine_brake_torque,"
                // suspension diagnostics from the physical assembly and contact sweep
                // contact_ny is the y component of the contact normal so we can spot tilted or
                // degenerate ground hits, key to debugging chassis on ground vs spring on ground
                "fl_comp,fr_comp,rl_comp,rr_comp,"
                "fl_sweep_dist,fr_sweep_dist,rl_sweep_dist,rr_sweep_dist,"
                "fl_spring_force,fr_spring_force,rl_spring_force,rr_spring_force,"
                "fl_contact_ny,fr_contact_ny,rl_contact_ny,rr_contact_ny,"
                // upgrades levels and key handling state that changes at runtime
                "eng_up,susp_up,tire_up,brake_up,aero_up,weight_up,"
                // per wheel brake and wear directly affect braking and grip
                "fl_brake_temp,fr_brake_temp,rl_brake_temp,rr_brake_temp,"
                "fl_wear,fr_wear,rl_wear,rr_wear,"
                // tire thermals surface avg core drive grip via temp factor
                "fl_surf_temp,fr_surf_temp,rl_surf_temp,rr_surf_temp,"
                "fl_core_temp,fr_core_temp,rl_core_temp,rr_core_temp,"
                // instantaneous efficiency and grip multipliers used this tick_telemetry
                "fl_brake_eff,fr_brake_eff,rl_brake_eff,rr_brake_eff,"
                "fl_grip_temp_f,fr_grip_temp_f,rl_grip_temp_f,rr_grip_temp_f,"
                "fl_grip_wear_f,fr_grip_wear_f,rl_grip_wear_f,rr_grip_wear_f,"
                // effective wheel geometry used by tire forces
                "fl_eff_r,fr_eff_r,rl_eff_r,rr_eff_r,"
                "fl_dyn_camb,fr_dyn_camb,rl_dyn_camb,rr_dyn_camb,"
                "fl_abs,fr_abs,rl_abs,rr_abs,"
                "mass,tire_friction,brake_force,engine_peak_tq,"
                "rot_x,rot_y,rot_z,rot_w,vel_x,vel_y,vel_z,ang_vel_x,ang_vel_y,ang_vel_z,"
                "target_throttle,target_brake,target_steering,target_handbrake,"
                "active_gear_ratio,shift_timer,shift_cooldown,last_shift_direction,engine_rotation,gearbox_input_angular_velocity,boost_pressure,motor_torque,engine_output_torque,axle_drive_torque,driveshaft_twist,driveshaft_torque,rev_limiter,downshift_blip_timer,abs_phase,vehicle_sleeping,vehicle_sleep_timer,drs_active,"
                "assist_engine_scale,fl_assist_brake_scale,fr_assist_brake_scale,rl_assist_brake_scale,rr_assist_brake_scale,"
                "aero_valid,aero_ride_height,aero_yaw_angle,aero_ground_effect,aero_drag_x,aero_drag_y,aero_drag_z,aero_front_downforce_x,aero_front_downforce_y,aero_front_downforce_z,aero_rear_downforce_x,aero_rear_downforce_y,aero_rear_downforce_z,aero_side_force_x,aero_side_force_y,aero_side_force_z,"
                "fl_rotation,fl_drive_torque,fl_brake_torque,fl_comp_velocity,fl_surface,fl_contact_x,fl_contact_y,fl_contact_z,fl_contact_nx,fl_contact_nz,fl_dynamic_toe,fl_bump_steer,fl_motion_ratio,fl_shock_length,fl_shock_rest_length,fl_shock_velocity,fl_temp_inside,fl_temp_middle,fl_temp_outside,fl_condition_grip,fl_condition_stiffness,fl_condition_relaxation,fl_wheel_moi,fl_spring_stiffness,fl_spring_damping,"
                "fr_rotation,fr_drive_torque,fr_brake_torque,fr_comp_velocity,fr_surface,fr_contact_x,fr_contact_y,fr_contact_z,fr_contact_nx,fr_contact_nz,fr_dynamic_toe,fr_bump_steer,fr_motion_ratio,fr_shock_length,fr_shock_rest_length,fr_shock_velocity,fr_temp_inside,fr_temp_middle,fr_temp_outside,fr_condition_grip,fr_condition_stiffness,fr_condition_relaxation,fr_wheel_moi,fr_spring_stiffness,fr_spring_damping,"
                "rl_rotation,rl_drive_torque,rl_brake_torque,rl_comp_velocity,rl_surface,rl_contact_x,rl_contact_y,rl_contact_z,rl_contact_nx,rl_contact_nz,rl_dynamic_toe,rl_bump_steer,rl_motion_ratio,rl_shock_length,rl_shock_rest_length,rl_shock_velocity,rl_temp_inside,rl_temp_middle,rl_temp_outside,rl_condition_grip,rl_condition_stiffness,rl_condition_relaxation,rl_wheel_moi,rl_spring_stiffness,rl_spring_damping,"
                "rr_rotation,rr_drive_torque,rr_brake_torque,rr_comp_velocity,rr_surface,rr_contact_x,rr_contact_y,rr_contact_z,rr_contact_nx,rr_contact_nz,rr_dynamic_toe,rr_bump_steer,rr_motion_ratio,rr_shock_length,rr_shock_rest_length,rr_shock_velocity,rr_temp_inside,rr_temp_middle,rr_temp_outside,rr_condition_grip,rr_condition_stiffness,rr_condition_relaxation,rr_wheel_moi,rr_spring_stiffness,rr_spring_damping,"
                "fl_hub_x,fl_hub_y,fl_hub_z,fl_hub_vx,fl_hub_vy,fl_hub_vz,fl_hub_wx,fl_hub_wy,fl_hub_wz,fr_hub_x,fr_hub_y,fr_hub_z,fr_hub_vx,fr_hub_vy,fr_hub_vz,fr_hub_wx,fr_hub_wy,fr_hub_wz,rl_hub_x,rl_hub_y,rl_hub_z,rl_hub_vx,rl_hub_vy,rl_hub_vz,rl_hub_wx,rl_hub_wy,rl_hub_wz,rr_hub_x,rr_hub_y,rr_hub_z,rr_hub_vx,rr_hub_vy,rr_hub_vz,rr_hub_wx,rr_hub_wy,rr_hub_wz\n");
            frame_counter = 0;
            elapsed_time  = 0.0f;
            return true;
        }

        void write_telemetry_wheel_state(int i)
        {
            const wheel& w = wheels[i];
            fprintf(file, "%.6g,%.6g,%.6g,%.6g,%d,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g", w.rotation, w.drive_torque, w.brake_torque, w.compression_velocity, static_cast<int>(w.contact_surface), w.contact_point.x, w.contact_point.y, w.contact_point.z, w.contact_normal.x, w.contact_normal.z, w.dynamic_toe, w.bump_steer, w.motion_ratio, w.shock_length, w.shock_rest_length, w.shock_velocity, w.thermal.surface[0], w.thermal.surface[1], w.thermal.surface[2], w.condition_grip, w.condition_stiffness, w.condition_relaxation, wheel_moi[i], spring_stiffness[i], spring_damping[i]);
        }

        void tick_telemetry(float dt, float speed_kmh)
        {
            if (!log_to_file)
            {
                close_telemetry();
                return;
            }
            if (!open_telemetry_if_needed())
            {
                return;
            }
            if (!body)
            {
                return;
            }

            elapsed_time += dt;

            PxTransform pose    = body->getGlobalPose();
            PxVec3      vel     = body->getLinearVelocity();
            PxVec3      ang_vel = body->getAngularVelocity();
            PxVec3      fwd     = pose.q.rotate(PxVec3(0, 0, 1));
            PxVec3      right   = pose.q.rotate(PxVec3(1, 0, 0));
            PxVec3      up      = pose.q.rotate(PxVec3(0, 1, 0));

            float forward_speed = vel.dot(fwd);
            float lateral_speed = vel.dot(right);
            float yaw_rate      = ang_vel.dot(up);

            // body slip: angle between velocity vector and car forward, in degrees
            // a stable car holds this near zero, a spinning car has it growing toward 90+
            float body_slip_deg = 0.0f;
            if (vel.magnitude() > 0.5f)
            {
                body_slip_deg = atan2f(lateral_speed, forward_speed) * 180.0f / PxPi;
            }

            fprintf(file,
                "%d,%.3f,%.4f,\"%s\","
                "%.2f,%.2f,%.2f,"
                "%.2f,%.3f,%.3f,"
                "%.4f,%.2f,"
                "%.3f,%.3f,"
                "%.3f,%.3f,"
                "%.3f,%.3f,%.3f,%.3f,"
                "%d,%.0f,%d,%.3f,%d,%.3f,"
                "%d,%d,%d,%d,"
                "%.4f,%.4f,%.4f,%.4f,"
                "%.4f,%.4f,%.4f,%.4f,"
                "%.1f,%.1f,%.1f,%.1f,"
                "%.1f,%.1f,%.1f,%.1f,"
                "%.1f,%.1f,%.1f,%.1f,"
                "%.3f,%.3f,%.3f,%.3f,"
                "%.1f,%.1f,%.1f,%.1f,"
                "%.1f,"
                "%.3f,%.3f,%.3f,%.3f,"
                "%.4f,%.4f,%.4f,%.4f,"
                "%.1f,%.1f,%.1f,%.1f,"
                "%.3f,%.3f,%.3f,%.3f,"
                // upgrades
                "%d,%d,%d,%d,%d,%d,"
                // brake wear
                "%.1f,%.1f,%.1f,%.1f,%.3f,%.3f,%.3f,%.3f,"
                // thermals
                "%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,"
                // eff grip factors
                "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,"
                // eff r dyn camb
                "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,"
                "%d,%d,%d,%d,"
                "%.1f,%.3f,%.1f,%.1f,"
                "%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,"
                "%.6g,%.6g,%.6g,%.6g,"
                "%.6g,%.6g,%.6g,%d,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%d,%.6g,%.6g,%d,%.6g,%d,"
                "%.6g,%.6g,%.6g,%.6g,%.6g,"
                "%d,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,",
                frame_counter, elapsed_time, dt, spec.name ? spec.name : "",
                pose.p.x, pose.p.y, pose.p.z,
                speed_kmh, forward_speed, lateral_speed,
                yaw_rate, body_slip_deg,
                longitudinal_accel, lateral_accel,
                vel.y, ang_vel.magnitude(),
                input.throttle, input.brake, input.steering, input.handbrake,
                current_gear, engine_rpm, is_shifting ? 1 : 0, clutch, tc_active ? 1 : 0, tc_reduction,
                wheels[front_left].grounded  ? 1 : 0,
                wheels[front_right].grounded ? 1 : 0,
                wheels[rear_left].grounded   ? 1 : 0,
                wheels[rear_right].grounded  ? 1 : 0,
                wheels[front_left].slip_angle,  wheels[front_right].slip_angle,
                wheels[rear_left].slip_angle,   wheels[rear_right].slip_angle,
                wheels[front_left].slip_ratio,  wheels[front_right].slip_ratio,
                wheels[rear_left].slip_ratio,   wheels[rear_right].slip_ratio,
                wheels[front_left].lateral_force,  wheels[front_right].lateral_force,
                wheels[rear_left].lateral_force,   wheels[rear_right].lateral_force,
                wheels[front_left].longitudinal_force,  wheels[front_right].longitudinal_force,
                wheels[rear_left].longitudinal_force,   wheels[rear_right].longitudinal_force,
                wheels[front_left].tire_load,  wheels[front_right].tire_load,
                wheels[rear_left].tire_load,   wheels[rear_right].tire_load,
                wheels[front_left].angular_velocity,  wheels[front_right].angular_velocity,
                wheels[rear_left].angular_velocity,   wheels[rear_right].angular_velocity,
                wheels[front_left].net_torque,  wheels[front_right].net_torque,
                wheels[rear_left].net_torque,   wheels[rear_right].net_torque,
                engine_brake_torque,
                wheels[front_left].compression,         wheels[front_right].compression,
                wheels[rear_left].compression,          wheels[rear_right].compression,
                sweep_distance[front_left],             sweep_distance[front_right],
                sweep_distance[rear_left],              sweep_distance[rear_right],
                spring_force[front_left],               spring_force[front_right],
                spring_force[rear_left],                spring_force[rear_right],
                wheels[front_left].contact_normal.y,    wheels[front_right].contact_normal.y,
                wheels[rear_left].contact_normal.y,     wheels[rear_right].contact_normal.y,
                // upgrades
                upgrades.engine, upgrades.suspension, upgrades.tires, upgrades.brakes, upgrades.aero, upgrades.weight,
                // brake temp wear
                wheels[front_left].brake_temp, wheels[front_right].brake_temp, wheels[rear_left].brake_temp, wheels[rear_right].brake_temp,
                wheels[front_left].wear,       wheels[front_right].wear,       wheels[rear_left].wear,       wheels[rear_right].wear,
                // thermals
                wheels[front_left].thermal.avg_surface(), wheels[front_right].thermal.avg_surface(),
                wheels[rear_left].thermal.avg_surface(),  wheels[rear_right].thermal.avg_surface(),
                wheels[front_left].thermal.core, wheels[front_right].thermal.core,
                wheels[rear_left].thermal.core,  wheels[rear_right].thermal.core,
                // effs and factors what actually multiplies grip brake this tick_telemetry
                wheels[front_left].brake_efficiency, wheels[front_right].brake_efficiency, wheels[rear_left].brake_efficiency, wheels[rear_right].brake_efficiency,
                wheels[front_left].temperature_grip, wheels[front_right].temperature_grip, wheels[rear_left].temperature_grip, wheels[rear_right].temperature_grip,
                wheels[front_left].wear_grip, wheels[front_right].wear_grip, wheels[rear_left].wear_grip, wheels[rear_right].wear_grip,
                // computed geometry used in this ticks slip and force
                wheels[front_left].effective_radius, wheels[front_right].effective_radius,
                wheels[rear_left].effective_radius,  wheels[rear_right].effective_radius,
                wheels[front_left].dynamic_camber,   wheels[front_right].dynamic_camber,
                wheels[rear_left].dynamic_camber,    wheels[rear_right].dynamic_camber,
                abs_active[front_left] ? 1 : 0, abs_active[front_right] ? 1 : 0,
                abs_active[rear_left] ? 1 : 0,  abs_active[rear_right] ? 1 : 0,
                cfg.mass, spec.tire_friction, spec.brake_force, spec.engine_peak_torque,
                pose.q.x, pose.q.y, pose.q.z, pose.q.w, vel.x, vel.y, vel.z, ang_vel.x, ang_vel.y, ang_vel.z,
                input_target.throttle, input_target.brake, input_target.steering, input_target.handbrake,
                spec.gear_ratios[current_gear], shift_timer, shift_cooldown, last_shift_direction, engine_rotation, gearbox_input_angular_velocity, boost_pressure, motor_torque, engine_output_torque, axle_drive_torque, driveshaft_twist, driveshaft_torque, rev_limiter_active ? 1 : 0, downshift_blip_timer, abs_phase, vehicle_sleeping ? 1 : 0, vehicle_sleep_timer, drs_active ? 1 : 0,
                assisted_actuators.engine_torque_scale, assisted_actuators.brake_torque_scale[front_left], assisted_actuators.brake_torque_scale[front_right], assisted_actuators.brake_torque_scale[rear_left], assisted_actuators.brake_torque_scale[rear_right],
                aero_debug.valid ? 1 : 0, aero_debug.ride_height, aero_debug.yaw_angle, aero_debug.ground_effect_factor, aero_debug.drag_force.x, aero_debug.drag_force.y, aero_debug.drag_force.z, aero_debug.front_downforce.x, aero_debug.front_downforce.y, aero_debug.front_downforce.z, aero_debug.rear_downforce.x, aero_debug.rear_downforce.y, aero_debug.rear_downforce.z, aero_debug.side_force.x, aero_debug.side_force.y, aero_debug.side_force.z);
            for (int i = 0; i < wheel_count; i++)
            {
                if (i > 0)
                {
                    fputc(',', file);
                }
                write_telemetry_wheel_state(i);
            }
            for (int i = 0; i < wheel_count; i++)
            {
                fprintf(file, ",%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g", wheels[i].hub_position.x, wheels[i].hub_position.y, wheels[i].hub_position.z, wheels[i].hub_linear_velocity.x, wheels[i].hub_linear_velocity.y, wheels[i].hub_linear_velocity.z, wheels[i].hub_angular_velocity.x, wheels[i].hub_angular_velocity.y, wheels[i].hub_angular_velocity.z);
            }
            fputc('\n', file);

            if (frame_counter % 200 == 0)
            {
                fflush(file);
            }
            frame_counter++;
        }
    

    // rejects invalid vectors before physx api calls
    inline bool is_finite_vec(const PxVec3& v)
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }


    inline void safe_add_force(PxRigidDynamic* body, const PxVec3& force, PxForceMode::Enum mode = PxForceMode::eFORCE)
    {
        if (!body)
        {
            return;
        }
        if (!is_finite_vec(force))
        {
            SP_LOG_WARNING("dropping non finite force (%.3f, %.3f, %.3f) before addForce", force.x, force.y, force.z);
            return;
        }
        body->addForce(force, mode);
    }


    inline void safe_add_torque(PxRigidDynamic* body, const PxVec3& torque, PxForceMode::Enum mode = PxForceMode::eFORCE)
    {
        if (!body)
        {
            return;
        }
        if (!is_finite_vec(torque))
        {
            SP_LOG_WARNING("dropping non finite torque (%.3f, %.3f, %.3f) before addTorque", torque.x, torque.y, torque.z);
            return;
        }
        body->addTorque(torque, mode);
    }


    inline void safe_add_force_at_pos(PxRigidDynamic* body, const PxVec3& force, const PxVec3& pos, PxForceMode::Enum mode = PxForceMode::eFORCE)
    {
        if (!body)
        {
            return;
        }
        if (!is_finite_vec(force))
        {
            SP_LOG_WARNING("dropping non finite force (%.3f, %.3f, %.3f) before addForceAtPos", force.x, force.y, force.z);
            return;
        }
        if (!is_finite_vec(pos))
        {
            SP_LOG_WARNING("dropping non finite position (%.3f, %.3f, %.3f) before addForceAtPos", pos.x, pos.y, pos.z);
            return;
        }
        PxRigidBodyExt::addForceAtPos(*body, force, pos, mode);
    }


    // function local storage avoids duplicate definitions
    inline shape_2d& shape_data_ref()
    {
        return shape_data;
    }


    inline bool is_in_reverse()      { return current_gear == 0; }

    inline bool is_in_neutral()      { return current_gear == 1; }

    inline bool is_in_forward_gear() { return current_gear >= 2; }


    // sanitization prevents one invalid wheel value from poisoning physx
    inline bool sanitize_float(float& v, float fallback = 0.0f)
    {
        if (!std::isfinite(v))
        {
            v = fallback;
            return true;
        }
        return false;
    }


    inline bool sanitize_vec(PxVec3& v, const PxVec3& fallback = PxVec3(0.0f))
    {
        bool fixed = false;
        if (!std::isfinite(v.x))
        {
            v.x = fallback.x;
            fixed = true;
        }
        if (!std::isfinite(v.y))
        {
            v.y = fallback.y;
            fixed = true;
        }
        if (!std::isfinite(v.z))
        {
            v.z = fallback.z;
            fixed = true;
        }
        return fixed;
    }


    inline bool sanitize_wheel_state(int i)
    {
        if (i < 0 || i >= wheel_count)
        {
            return false;
        }
        wheel& w = wheels[i];
        bool fixed = false;
        fixed |= sanitize_float(w.compression);
        fixed |= sanitize_float(w.compression_velocity);
        fixed |= sanitize_float(w.angular_velocity);
        fixed |= sanitize_float(w.rotation);
        fixed |= sanitize_float(w.tire_load);
        fixed |= sanitize_float(w.slip_angle);
        fixed |= sanitize_float(w.slip_ratio);
        fixed |= sanitize_float(w.lateral_force);
        fixed |= sanitize_float(w.longitudinal_force);
        fixed |= sanitize_float(w.net_torque);
        fixed |= sanitize_vec(w.contact_point);
        fixed |= sanitize_vec(w.contact_normal, PxVec3(0, 1, 0));
        fixed |= sanitize_float(w.wear);
        fixed |= sanitize_float(w.brake_temp);
        fixed |= sanitize_float(w.thermal.surface[0]);
        fixed |= sanitize_float(w.thermal.surface[1]);
        fixed |= sanitize_float(w.thermal.surface[2]);
        fixed |= sanitize_float(w.thermal.core);
        fixed |= sanitize_float(w.effective_radius);
        fixed |= sanitize_float(w.dynamic_camber);
        fixed |= sanitize_float(w.dynamic_toe);
        fixed |= sanitize_float(w.bump_steer);
        fixed |= sanitize_float(w.motion_ratio);
        fixed |= sanitize_float(w.drive_torque);
        return fixed;
    }


    inline bool  is_front(int i)                { return i == front_left || i == front_right; }

    inline bool  is_rear(int i)                 { return i == rear_left || i == rear_right; }

    inline bool  is_driven(int i)
    {
        if (spec.drivetrain_type == 0)
        {
            return is_rear(i);
        }
        if (spec.drivetrain_type == 1)
        {
            return is_front(i);
        }
        return true;
    }

    inline float lerp(float a, float b, float t){ return a + (b - a) * t; }

    inline float exp_decay(float rate, float dt){ return 1.0f - expf(-rate * dt); }


    inline bool        is_valid_wheel(int i) { return i >= 0 && i < wheel_count; }

    inline const char* get_wheel_name(int i) { return is_valid_wheel(i) ? wheel_names[i] : "??"; }


    inline void clear_abs_state()
    {
        for (int i = 0; i < wheel_count; i++)
        {
            abs_active[i] = false;
        }
    }


    inline float get_assisted_steering_target(float raw_input)
    {
        float deadzone = PxClamp(spec.steering_deadzone, 0.0f, 0.95f);
        float magnitude = fabsf(raw_input);
        float filtered_input = magnitude <= deadzone ? 0.0f : copysignf((magnitude - deadzone) / (1.0f - deadzone), raw_input);
        float speed_kmh = body ? body->getLinearVelocity().magnitude() * 3.6f : 0.0f;
        float speed_factor = PxClamp(speed_kmh / PxMax(spec.assists.steering_speed_reference, 1.0f), 0.0f, 1.0f);
        float steering_limit = 1.0f - spec.assists.steering_speed_reduction * speed_factor;
        return PxClamp(filtered_input, -steering_limit, steering_limit);
    }


    inline void update_assist_controller(bool traction_requested, bool braking_requested, float dt)
    {
        assisted_actuators = assist_command();
        tc_active = false;
        if (traction_requested && spec.tc_enabled && spec.assists.traction_control_level > 0.0f)
        {
            float max_slip = 0.0f;
            for (int i = 0; i < wheel_count; i++)
            {
                if (is_driven(i) && wheels[i].grounded)
                {
                    max_slip = PxMax(max_slip, wheels[i].slip_ratio);
                }
            }
            float target_reduction = 0.0f;
            if (max_slip > spec.tc_slip_threshold)
            {
                tc_active = true;
                float reduction_limit = spec.tc_power_reduction * spec.assists.traction_control_level;
                target_reduction = PxClamp((max_slip - spec.tc_slip_threshold) * 5.0f, 0.0f, reduction_limit);
            }
            tc_reduction = lerp(tc_reduction, target_reduction, exp_decay(spec.tc_response_rate, dt));
        }
        else
        {
            tc_reduction = lerp(tc_reduction, 0.0f, exp_decay(spec.tc_response_rate * 2.0f, dt));
        }
        assisted_actuators.engine_torque_scale = 1.0f - tc_reduction;

        if (!braking_requested)
        {
            clear_abs_state();
            return;
        }

        abs_phase += spec.abs_pulse_frequency * dt;
        abs_phase -= floorf(abs_phase);
        for (int i = 0; i < wheel_count; i++)
        {
            abs_active[i] = false;
            if (!spec.abs_enabled || spec.assists.abs_level <= 0.0f || !wheels[i].grounded)
            {
                continue;
            }
            float load_factor = PxClamp(wheels[i].tire_load / PxMax(spec.load_reference, 1000.0f), 0.6f, 1.6f);
            float threshold = spec.abs_slip_threshold * (1.0f - spec.abs_load_sensitivity * (load_factor - 1.0f));
            if (-wheels[i].slip_ratio > threshold)
            {
                abs_active[i] = true;
                float release_factor = lerp(1.0f, spec.abs_release_rate, spec.assists.abs_level);
                assisted_actuators.brake_torque_scale[i] = abs_phase < 0.5f ? release_factor : 1.0f;
            }
        }
    }


    // lateral grip peaks at a slightly negative camber and falls off quadratically
    inline float get_camber_grip_factor(float camber)
    {
        float dev = camber - tuning::camber_optimal;
        return PxClamp(1.0f - tuning::camber_grip_loss * dev * dev, 0.5f, 1.0f);
    }


    // derived from com z-offset and wheelbase, no need to store separately
    inline float get_weight_distribution_front()
    {
        if (cfg.wheelbase < 0.01f)
        {
            return 0.5f;
        }
        return PxClamp(0.5f + spec.center_of_mass_z / cfg.wheelbase, 0.0f, 1.0f);
    }


    inline float load_sensitive_grip(float load)
    {
        if (load <= 0.0f)
        {
            return 0.0f;
        }
        return load * powf(load / spec.load_reference, spec.load_sensitivity - 1.0f);
    }


    inline float get_tire_temp_grip_factor(float temperature)
    {
        float opt = spec.tire_optimal_temp;
        float range = PxMax(spec.tire_temp_range, 1.0f);
        float dev = fabsf(temperature - opt);
        float norm = PxClamp(dev / range, 0.0f, 1.0f);
        float penalty = norm * norm * spec.tire_grip_temp_factor;
        return 1.0f - penalty;
    }


    inline tire_condition_modifiers get_tire_condition_modifiers(float surface_temperature, float core_temperature, float wear, float load)
    {
        tire_condition_modifiers modifiers;
        float temperature_range = PxMax(spec.tire_temp_range, 1.0f);
        float core_deviation = PxClamp(fabsf(core_temperature - spec.tire_optimal_temp) / temperature_range, 0.0f, 1.5f);
        float pressure_ratio = PxClamp(spec.tire_pressure / PxMax(spec.tire_pressure_optimal, 0.1f), 0.6f, 1.4f);
        float pressure_error = pressure_ratio - 1.0f;
        float pressure_grip = PxClamp(1.0f - pressure_error * pressure_error * 0.45f, 0.75f, 1.0f);
        float pressure_stiffness = powf(pressure_ratio, 0.55f);
        float temperature_stiffness = PxClamp(1.0f - core_deviation * core_deviation * 0.22f, 0.55f, 1.0f);
        float wear_clamped = PxClamp(wear, 0.0f, 1.0f);
        modifiers.temperature_grip = get_tire_temp_grip_factor(surface_temperature);
        modifiers.wear_grip = PxClamp(1.0f - wear_clamped * spec.tire_grip_wear_loss, 0.20f, 1.0f);
        float wear_stiffness = 1.0f - wear_clamped * 0.18f;
        float load_ratio = PxClamp(load / PxMax(spec.load_reference, 1.0f), 0.25f, 3.0f);
        modifiers.peak_grip = PxClamp(modifiers.temperature_grip * pressure_grip * modifiers.wear_grip, 0.15f, 1.0f);
        modifiers.stiffness = PxClamp(temperature_stiffness * pressure_stiffness * wear_stiffness, 0.55f, 1.30f);
        modifiers.relaxation = PxClamp(powf(load_ratio, 0.12f) * (1.0f + wear_clamped * 0.20f) / modifiers.stiffness, 0.65f, 1.80f);
        return modifiers;
    }


    inline float get_surface_friction(surface_type surface)
    {
        static constexpr float friction[] = {
            tuning::surface_friction_asphalt,
            tuning::surface_friction_concrete,
            tuning::surface_friction_wet_asphalt,
            tuning::surface_friction_gravel,
            tuning::surface_friction_grass,
            tuning::surface_friction_ice
        };
        return (surface >= 0 && surface < surface_count) ? friction[surface] : 1.0f;
    }


    inline float get_brake_efficiency(float temp)
    {
        float amb = spec.brake_ambient_temp;
        float opt = PxMax(spec.brake_optimal_temp, amb + 10.0f);
        float fade = PxMax(spec.brake_fade_temp, opt + 10.0f);
        if (temp >= fade)
        {
            return 0.6f;
        }
        if (temp < opt)
        {
            float t = PxClamp((temp - amb) / (opt - amb), 0.0f, 1.0f);
            return 0.80f + 0.20f * t;
        }
        float t = (temp - opt) / (fade - opt);
        return PxClamp(1.0f - 0.4f * t, 0.5f, 1.0f);
    }


    inline void compute_aero_from_shape(const std::vector<PxVec3>& vertices)
    {
        if (vertices.size() < 4)
        {
            return;
        }

        PxVec3 min_pt(FLT_MAX, FLT_MAX, FLT_MAX);
        PxVec3 max_pt(-FLT_MAX, -FLT_MAX, -FLT_MAX);

        for (const PxVec3& v : vertices)
        {
            min_pt.x = PxMin(min_pt.x, v.x);
            min_pt.y = PxMin(min_pt.y, v.y);
            min_pt.z = PxMin(min_pt.z, v.z);
            max_pt.x = PxMax(max_pt.x, v.x);
            max_pt.y = PxMax(max_pt.y, v.y);
            max_pt.z = PxMax(max_pt.z, v.z);
        }

        float width  = max_pt.x - min_pt.x;
        float height = max_pt.y - min_pt.y;
        float length = max_pt.z - min_pt.z;

        float frontal_fill_factor = 0.82f;
        float computed_frontal_area = width * height * frontal_fill_factor;

        float side_fill_factor = 0.75f;
        float computed_side_area = length * height * side_fill_factor;

        float length_height_ratio = length / PxMax(height, 0.1f);
        float base_cd = 0.32f;
        float ratio_factor = PxClamp(2.5f / length_height_ratio, 0.8f, 1.3f);
        float computed_drag_coeff = base_cd * ratio_factor;

        // only fill fields the preset left at 0 (sentinel) - presets with hand-tuned
        // values must not be stomped on every chassis / preset change
        if (computed_frontal_area > 0.5f && computed_frontal_area < 10.0f && spec.frontal_area == 0.0f)
        {
            spec.frontal_area = computed_frontal_area;
            SP_LOG_INFO("aero: frontal area = %.2f m²", computed_frontal_area);
        }

        if (computed_side_area > 1.0f && computed_side_area < 20.0f && spec.side_area == 0.0f)
        {
            spec.side_area = computed_side_area;
            SP_LOG_INFO("aero: side area = %.2f m²", computed_side_area);
        }

        if (computed_drag_coeff > 0.2f && computed_drag_coeff < 0.6f && spec.drag_coeff == 0.0f)
        {
            spec.drag_coeff = computed_drag_coeff;
            SP_LOG_INFO("aero: drag coefficient = %.3f", computed_drag_coeff);
        }

        float centroid_y = 0.0f;
        float centroid_z = 0.0f;
        float front_area = 0.0f;
        float rear_area = 0.0f;
        float mid_z = (min_pt.z + max_pt.z) * 0.5f;

        for (const PxVec3& v : vertices)
        {
            float h = v.y - min_pt.y;
            float weight = h * h;
            centroid_y += v.y * weight;
            centroid_z += v.z * weight;

            if (v.z > mid_z)
            {
                front_area += weight;
            }
            else
            {
                rear_area += weight;
            }
        }

        float total_weight = 0.0f;
        for (const PxVec3& v : vertices)
        {
            float h = v.y - min_pt.y;
            total_weight += h * h;
        }

        if (total_weight > 0.0f)
        {
            centroid_y /= total_weight;
            centroid_z /= total_weight;
        }

        float total_area = front_area + rear_area;
        float front_bias = (total_area > 0.0f) ? front_area / total_area : 0.5f;

        float computed_aero_center_front_z = max_pt.z * 0.8f;
        float computed_aero_center_rear_z  = min_pt.z * 0.8f;

        // use mesh derived centres when the preset left them at the zero sentinel
        if (spec.aero_center_height == 0.0f)
        {
            spec.aero_center_height = centroid_y;
        }
        if (spec.aero_center_front_z == 0.0f && spec.aero_center_rear_z == 0.0f)
        {
            spec.aero_center_front_z = computed_aero_center_front_z;
            spec.aero_center_rear_z  = computed_aero_center_rear_z;
        }

        SP_LOG_INFO("aero: dimensions %.2f x %.2f x %.2f m (L x W x H)", length, width, height);
        SP_LOG_INFO("aero: center height=%.2f, front_z=%.2f, rear_z=%.2f",
            spec.aero_center_height, spec.aero_center_front_z, spec.aero_center_rear_z);
        SP_LOG_INFO("aero: front/rear area bias=%.0f%%/%.0f%%",
            front_bias * 100.0f, (1.0f - front_bias) * 100.0f);

        // visualization geometry is derived after aero inference
        compute_shape_visualization(vertices, min_pt, max_pt);
    }


    // convex hull supports side and front chassis silhouettes
    inline std::vector<std::pair<float, float>> graham_scan_hull_2d(std::vector<std::pair<float, float>> points)
    {
        if (points.size() < 3)
        {
            return points;
        }

        size_t pivot_idx = 0;
        for (size_t i = 1; i < points.size(); i++)
        {
            if (points[i].second < points[pivot_idx].second ||
                (points[i].second == points[pivot_idx].second && points[i].first < points[pivot_idx].first))
            {
                pivot_idx = i;
            }
        }
        std::swap(points[0], points[pivot_idx]);
        auto pivot = points[0];

        auto cross = [](const std::pair<float,float>& o, const std::pair<float,float>& a, const std::pair<float,float>& b) -> float
        {
            return (a.first - o.first) * (b.second - o.second) - (a.second - o.second) * (b.first - o.first);
        };

        std::sort(points.begin() + 1, points.end(), [&](const auto& a, const auto& b)
        {
            float c = cross(pivot, a, b);
            if (fabsf(c) < 1e-9f)
            {
                float da = (a.first - pivot.first) * (a.first - pivot.first) + (a.second - pivot.second) * (a.second - pivot.second);
                float db = (b.first - pivot.first) * (b.first - pivot.first) + (b.second - pivot.second) * (b.second - pivot.second);
                return da < db;
            }
            return c > 0;
        });

        std::vector<std::pair<float, float>> hull;
        for (const auto& pt : points)
        {
            while (hull.size() > 1 && cross(hull[hull.size()-2], hull[hull.size()-1], pt) <= 0)
                hull.pop_back();
            hull.push_back(pt);
        }
        return hull;
    }


    inline void compute_shape_visualization(const std::vector<PxVec3>& vertices, const PxVec3& min_pt, const PxVec3& max_pt)
    {
        shape_2d& sd = shape_data_ref();
        sd.min_x = min_pt.x; sd.max_x = max_pt.x;
        sd.min_y = min_pt.y; sd.max_y = max_pt.y;
        sd.min_z = min_pt.z; sd.max_z = max_pt.z;

        // side view: (z, y) silhouette
        std::vector<std::pair<float, float>> side_points;
        side_points.reserve(vertices.size());
        for (const PxVec3& v : vertices)
            side_points.push_back({v.z, v.y});
        sd.side_profile = graham_scan_hull_2d(std::move(side_points));

        // front view: (x, y) silhouette
        std::vector<std::pair<float, float>> front_points;
        front_points.reserve(vertices.size());
        for (const PxVec3& v : vertices)
            front_points.push_back({v.x, v.y});
        sd.front_profile = graham_scan_hull_2d(std::move(front_points));

        sd.valid = sd.side_profile.size() >= 3 && sd.front_profile.size() >= 3;
    }


    inline void apply_aero_and_resistance()
    {
        PxTransform pose = body->getGlobalPose();
        PxVec3 vel = body->getLinearVelocity();
        float speed = vel.magnitude();

        // aero positions are actor origin offsets because center of mass is independent
        float aero_height = spec.aero_center_height;
        PxVec3 front_pos = pose.p + pose.q.rotate(PxVec3(0, aero_height, spec.aero_center_front_z));
        PxVec3 rear_pos  = pose.p + pose.q.rotate(PxVec3(0, aero_height, spec.aero_center_rear_z));

        aero_debug.valid = false;
        aero_debug.position = pose.p;
        aero_debug.velocity = vel;
        aero_debug.front_aero_pos = front_pos;
        aero_debug.rear_aero_pos = rear_pos;
        aero_debug.side_aero_pos = (front_pos + rear_pos) * 0.5f;
        aero_debug.ride_height = cfg.suspension_height;
        aero_debug.ground_effect_factor = 1.0f;
        aero_debug.yaw_angle = 0.0f;
        aero_debug.drag_force = PxVec3(0);
        aero_debug.front_downforce = PxVec3(0);
        aero_debug.rear_downforce = PxVec3(0);
        aero_debug.side_force = PxVec3(0);

        PxVec3 local_fwd   = pose.q.rotate(PxVec3(0, 0, 1));
        PxVec3 local_up    = pose.q.rotate(PxVec3(0, 1, 0));
        PxVec3 local_right = pose.q.rotate(PxVec3(1, 0, 0));

        float forward_speed = vel.dot(local_fwd);
        float lateral_speed = vel.dot(local_right);

        float yaw_angle = 0.0f;
        if (speed > 1.0f)
        {
            PxVec3 vel_norm = vel.getNormalized();
            float cos_yaw = PxClamp(vel_norm.dot(local_fwd), -1.0f, 1.0f);
            yaw_angle = acosf(fabsf(cos_yaw));
        }

        float front_compression = (wheels[front_left].compression + wheels[front_right].compression) * 0.5f;
        float rear_compression  = (wheels[rear_left].compression + wheels[rear_right].compression) * 0.5f;
        float pitch_angle = (rear_compression - front_compression) * cfg.suspension_travel / PxMax(cfg.wheelbase, 0.1f);

        // ride height estimates the current underbody gap
        float avg_compression = (front_compression + rear_compression) * 0.5f;
        float ride_height = cfg.suspension_height - avg_compression * cfg.suspension_travel;

        // speed threshold avoids normalizing zero velocity
        PxVec3 drag_force_vec(0);
        if (speed > 0.5f)
        {
            float base_drag = 0.5f * tuning::air_density * spec.drag_coeff * spec.frontal_area * speed * speed;

            float yaw_drag_factor = 1.0f;
            if (spec.yaw_aero_enabled && yaw_angle > 0.01f)
            {
                float yaw_factor = sinf(yaw_angle);
                yaw_drag_factor = 1.0f + yaw_factor * (spec.yaw_drag_multiplier - 1.0f);
            }

            drag_force_vec = -vel.getNormalized() * base_drag * yaw_drag_factor;
            safe_add_force(body, drag_force_vec);
        }

        // side force acts at silhouette center to create weather vane yaw torque
        PxVec3 side_force_vec(0);
        if (spec.yaw_aero_enabled && fabsf(lateral_speed) > 1.0f)
        {
            float side_force = 0.5f * tuning::air_density * spec.yaw_side_force_coeff * spec.side_area * lateral_speed * fabsf(lateral_speed);
            side_force_vec = -local_right * side_force;
            float side_aero_z = (spec.aero_center_front_z + spec.aero_center_rear_z) * 0.5f;
            PxVec3 side_aero_pos = pose.p + pose.q.rotate(PxVec3(0, aero_height, side_aero_z));
            safe_add_force_at_pos(body, side_force_vec, side_aero_pos);
            aero_debug.side_aero_pos = side_aero_pos;
        }

        PxVec3 front_downforce_vec(0);
        PxVec3 rear_downforce_vec(0);
        float ground_effect_factor = 1.0f;

        // low threshold keeps quadratic downforce continuous near rest
        if (speed > 1.0f)
        {
            float dyn_pressure = 0.5f * tuning::air_density * speed * speed;

            float front_cl = spec.lift_coeff_front;
            float rear_cl  = spec.lift_coeff_rear;

            // drs reduces rear downforce for higher straight-line speed
            if (spec.drs_enabled && drs_active)
            {
                rear_cl *= spec.drs_rear_cl_factor;
            }

            if (spec.ground_effect_enabled)
            {
                if (ride_height < spec.ground_effect_height_max)
                {
                    float height_ratio = PxClamp((spec.ground_effect_height_max - ride_height) /
                                                 (spec.ground_effect_height_max - spec.ground_effect_height_ref), 0.0f, 1.0f);
                    ground_effect_factor = 1.0f + height_ratio * (spec.ground_effect_multiplier - 1.0f);
                }
            }

            if (spec.pitch_aero_enabled)
            {
                float pitch_shift = pitch_angle * spec.pitch_sensitivity;
                front_cl *= (1.0f - pitch_shift);
                rear_cl  *= (1.0f + pitch_shift);
            }

            float yaw_downforce_factor = 1.0f;
            if (spec.yaw_aero_enabled && yaw_angle > 0.1f)
            {
                yaw_downforce_factor = PxMax(0.3f, 1.0f - sinf(yaw_angle) * 0.7f);
            }

            float front_downforce = front_cl * dyn_pressure * spec.frontal_area * ground_effect_factor * yaw_downforce_factor;
            float rear_downforce  = rear_cl  * dyn_pressure * spec.frontal_area * ground_effect_factor * yaw_downforce_factor;

            front_downforce_vec = local_up * front_downforce;
            rear_downforce_vec  = local_up * rear_downforce;

            safe_add_force_at_pos(body, front_downforce_vec, front_pos);
            safe_add_force_at_pos(body, rear_downforce_vec, rear_pos);
        }

        // per-wheel rolling resistance: higher pressure = lower rr
        // fade through zero speed, a hard sign flip pushes a parked car with a constant force
        float rr_pressure_scale = 1.0f + (1.0f - spec.tire_pressure / PxMax(spec.tire_pressure_optimal, 0.1f)) * 0.3f;
        float rr_direction = -PxClamp(forward_speed / 0.5f, -1.0f, 1.0f);
        for (int i = 0; i < wheel_count; i++)
        {
            if (wheels[i].grounded && wheels[i].tire_load > 0.0f)
            {
                PxVec3 rr_force = local_fwd * rr_direction * spec.rolling_resistance * rr_pressure_scale * wheels[i].tire_load;
                safe_add_force_at_pos(body, rr_force, wheels[i].contact_point);
                if (const PxRigidDynamic* ground_actor = wheels[i].contact_actor ? wheels[i].contact_actor->is<PxRigidDynamic>() : nullptr)
                {
                    safe_add_force_at_pos(const_cast<PxRigidDynamic*>(ground_actor), -rr_force, wheels[i].contact_point);
                }
            }
        }

        aero_debug.drag_force = drag_force_vec;
        aero_debug.front_downforce = front_downforce_vec;
        aero_debug.rear_downforce = rear_downforce_vec;
        aero_debug.side_force = side_force_vec;
        aero_debug.front_aero_pos = front_pos;
        aero_debug.rear_aero_pos = rear_pos;
        aero_debug.ride_height = ride_height;
        aero_debug.yaw_angle = yaw_angle;
        aero_debug.ground_effect_factor = ground_effect_factor;
        aero_debug.valid = true;
    }


    inline PxU32 multibody_collision_group()
    {
        // matching groups suppress contacts between parts of the same vehicle
        const PxU32 collision_group = static_cast<PxU32>(reinterpret_cast<uintptr_t>(body) >> 4);
        return collision_group != 0 ? collision_group : 1;
    }


    inline actor_motion_state capture_actor_motion(PxRigidDynamic* actor)
    {
        actor_motion_state state;
        if (!actor || !body)
        {
            return state;
        }

        PxTransform chassis_pose = body->getGlobalPose();
        PxVec3 chassis_angular_velocity = body->getAngularVelocity();
        PxVec3 chassis_point_velocity = body->getLinearVelocity() + chassis_angular_velocity.cross(actor->getGlobalPose().p - chassis_pose.p);
        state.linear_velocity = chassis_pose.q.rotateInv(actor->getLinearVelocity() - chassis_point_velocity);
        state.angular_velocity = chassis_pose.q.rotateInv(actor->getAngularVelocity() - chassis_angular_velocity);
        state.valid = true;
        return state;
    }


    inline void restore_actor_motion(PxRigidDynamic* actor, const actor_motion_state& state)
    {
        if (!actor || !body || !state.valid)
        {
            return;
        }

        PxTransform chassis_pose = body->getGlobalPose();
        PxVec3 chassis_angular_velocity = body->getAngularVelocity();
        PxVec3 chassis_point_velocity = body->getLinearVelocity() + chassis_angular_velocity.cross(actor->getGlobalPose().p - chassis_pose.p);
        actor->setLinearVelocity(chassis_point_velocity + chassis_pose.q.rotate(state.linear_velocity));
        actor->setAngularVelocity(chassis_angular_velocity + chassis_pose.q.rotate(state.angular_velocity));
        actor->wakeUp();
    }


    inline multibody_motion_state capture_multibody_motion()
    {
        multibody_motion_state state;
        if (!multibody.initialized)
        {
            return state;
        }

        for (int i = 0; i < wheel_count; i++)
        {
            suspension_corner& corner = multibody.corners[i];
            corner_motion_state& corner_state = state.corners[i];
            corner_state.upright = capture_actor_motion(corner.upright);
            corner_state.wheel = capture_actor_motion(corner.wheel_body);
            corner_state.member_count = corner.member_count;
            for (int member_index = 0; member_index < corner.member_count; member_index++)
            {
                corner_state.members[member_index] = capture_actor_motion(corner.members[member_index].actor);
            }
        }
        state.rack = capture_actor_motion(multibody.rack);
        state.valid = true;
        return state;
    }


    inline void restore_multibody_motion(const multibody_motion_state& state)
    {
        if (!state.valid || !multibody.initialized)
        {
            return;
        }

        for (int i = 0; i < wheel_count; i++)
        {
            suspension_corner& corner = multibody.corners[i];
            const corner_motion_state& corner_state = state.corners[i];
            restore_actor_motion(corner.upright, corner_state.upright);
            restore_actor_motion(corner.wheel_body, corner_state.wheel);
            int member_count = PxMin(corner.member_count, corner_state.member_count);
            for (int member_index = 0; member_index < member_count; member_index++)
            {
                restore_actor_motion(corner.members[member_index].actor, corner_state.members[member_index]);
            }
        }
        restore_actor_motion(multibody.rack, state.rack);
    }


    inline PxTransform local_anchor(PxRigidActor* actor, const PxVec3& world_point)
    {
        return actor ? PxTransform(actor->getGlobalPose().transformInv(world_point)) : PxTransform(world_point);
    }


    inline void register_multibody_actor(PxRigidDynamic* actor)
    {
        if (actor && multibody.actor_count < static_cast<int>(std::size(multibody.actors)))
        {
            multibody.actors[multibody.actor_count++] = actor;
        }
    }


    inline void register_multibody_joint(PxJoint* joint)
    {
        if (joint && multibody.joint_count < max_suspension_joints)
        {
            multibody.joints[multibody.joint_count++] = joint;
        }
    }


    inline void configure_mechanism_shape(PxShape* shape)
    {
        if (!shape)
        {
            return;
        }

        // mechanism shapes carry inertia while custom suspension and tire forces handle contact
        shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
        shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, false);
        shape->setFlag(PxShapeFlag::eVISUALIZATION, true);
    }


    inline PxRigidDynamic* create_mechanism_actor(const PxTransform& pose, const PxGeometry& geometry, float mass, bool collision_guard = false)
    {
        PxRigidDynamic* actor = multibody.physics->createRigidDynamic(pose);
        if (!actor)
        {
            return nullptr;
        }

        PxMaterial* shape_material = collision_guard && wheel_guard_material ? wheel_guard_material : material;
        PxShape* shape = multibody.physics->createShape(geometry, *shape_material);
        if (!shape)
        {
            actor->release();
            return nullptr;
        }

        if (collision_guard)
        {
            // the inset guard prevents tunneling while sweep tire forces remain primary
            shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, false);
            shape->setContactOffset(0.005f);
            shape->setRestOffset(0.0f);
            PxFilterData filter_data = shape->getSimulationFilterData();
            filter_data.word2 = 2;
            filter_data.word3 = multibody_collision_group();
            shape->setSimulationFilterData(filter_data);
        }
        else
        {
            configure_mechanism_shape(shape);
        }
        actor->attachShape(*shape);
        shape->release();
        PxRigidBodyExt::setMassAndUpdateInertia(*actor, PxMax(mass, 0.1f));
        actor->setSolverIterationCounts(16, 4);
        actor->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
        multibody.scene->addActor(*actor);
        register_multibody_actor(actor);
        return actor;
    }


    inline PxSphericalJoint* create_spherical_joint(PxRigidActor* actor_a, PxRigidActor* actor_b, const PxVec3& world_anchor)
    {
        PxSphericalJoint* joint = PxSphericalJointCreate(*multibody.physics, actor_a, local_anchor(actor_a, world_anchor), actor_b, local_anchor(actor_b, world_anchor));
        if (joint)
        {
            joint->setConstraintFlag(PxConstraintFlag::eENABLE_EXTENDED_LIMITS, true);
            register_multibody_joint(joint);
        }
        return joint;
    }


    inline PxRigidDynamic* create_link(const PxVec3& world_start, const PxVec3& world_end, float mass, PxRigidDynamic* start_actor = nullptr)
    {
        PxVec3 delta = world_end - world_start;
        float length = delta.magnitude();
        if (length < 0.02f)
        {
            return nullptr;
        }

        PxVec3 direction = delta / length;
        PxVec3 cross = PxVec3(1.0f, 0.0f, 0.0f).cross(direction);
        float dot = PxVec3(1.0f, 0.0f, 0.0f).dot(direction);
        PxQuat rotation = dot < -0.9999f ? PxQuat(PxPi, PxVec3(0.0f, 1.0f, 0.0f)) : PxQuat(cross.x, cross.y, cross.z, 1.0f + dot).getNormalized();
        float radius = 0.018f;
        PxCapsuleGeometry geometry(radius, PxMax(length * 0.5f - radius, 0.01f));
        PxRigidDynamic* actor = create_mechanism_actor(PxTransform((world_start + world_end) * 0.5f, rotation), geometry, mass);
        if (actor)
        {
            PxRigidActor* anchor_actor = start_actor ? static_cast<PxRigidActor*>(start_actor) : static_cast<PxRigidActor*>(body);
            if (!create_spherical_joint(anchor_actor, actor, world_start))
            {
                multibody.actors[--multibody.actor_count] = nullptr;
                actor->release();
                return nullptr;
            }
        }
        return actor;
    }


    inline bool connect_link_to_upright(PxRigidDynamic* link, PxRigidDynamic* upright, const PxVec3& world_anchor)
    {
        if (!link || !upright)
        {
            return false;
        }
        return create_spherical_joint(link, upright, world_anchor) != nullptr;
    }


    inline bool add_link_member(suspension_corner& corner, const PxVec3& world_start, const PxVec3& world_end, float mass, PxRigidDynamic* start_actor = nullptr)
    {
        if (corner.member_count >= max_suspension_members)
        {
            return false;
        }

        PxRigidDynamic* link = create_link(world_start, world_end, mass, start_actor);
        if (!link)
        {
            return false;
        }

        if (!connect_link_to_upright(link, corner.upright, world_end))
        {
            return false;
        }
        suspension_member& member = corner.members[corner.member_count++];
        member.actor = link;
        member.local_start = link->getGlobalPose().transformInv(world_start);
        member.local_end = link->getGlobalPose().transformInv(world_end);
        return true;
    }


    inline bool add_wishbone(suspension_corner& corner, const PxVec3& inner_front, const PxVec3& inner_rear, const PxVec3& outer, float mass)
    {
        if (corner.member_count > max_suspension_members - 2)
        {
            return false;
        }

        PxVec3 center = (inner_front + inner_rear + outer) / 3.0f;
        PxVec3 extents = PxVec3(PxMax(fabsf(outer.x - inner_front.x) * 0.5f, 0.03f), 0.018f, PxMax(fabsf(inner_front.z - inner_rear.z) * 0.5f, 0.03f));
        PxRigidDynamic* arm = create_mechanism_actor(PxTransform(center), PxBoxGeometry(extents), mass);
        if (!arm)
        {
            return false;
        }

        if (!create_spherical_joint(body, arm, inner_front) || !create_spherical_joint(body, arm, inner_rear) || !create_spherical_joint(arm, corner.upright, outer))
        {
            return false;
        }
        suspension_member& front_member = corner.members[corner.member_count++];
        front_member.actor = arm;
        front_member.local_start = arm->getGlobalPose().transformInv(inner_front);
        front_member.local_end = arm->getGlobalPose().transformInv(outer);
        suspension_member& rear_member = corner.members[corner.member_count++];
        rear_member.actor = arm;
        rear_member.local_start = arm->getGlobalPose().transformInv(inner_rear);
        rear_member.local_end = arm->getGlobalPose().transformInv(outer);
        return true;
    }


    inline bool add_macpherson_strut(suspension_corner& corner, const PxVec3& top, const PxVec3& bottom, float mass)
    {
        if (corner.member_count >= max_suspension_members)
        {
            return false;
        }

        PxRigidDynamic* strut = create_link(top, bottom, mass);
        if (!strut)
        {
            return false;
        }

        PxTransform strut_pose = strut->getGlobalPose();
        PxTransform upright_pose = corner.upright->getGlobalPose();
        PxTransform strut_frame(strut_pose.transformInv(bottom), PxQuat(PxIdentity));
        PxTransform upright_frame(upright_pose.transformInv(bottom), upright_pose.q.getConjugate() * strut_pose.q);
        PxD6Joint* slider = PxD6JointCreate(*multibody.physics, strut, strut_frame, corner.upright, upright_frame);
        if (!slider)
        {
            return false;
        }

        slider->setMotion(PxD6Axis::eX, PxD6Motion::eLIMITED);
        slider->setMotion(PxD6Axis::eY, PxD6Motion::eLOCKED);
        slider->setMotion(PxD6Axis::eZ, PxD6Motion::eLOCKED);
        slider->setMotion(PxD6Axis::eTWIST, PxD6Motion::eFREE);
        slider->setMotion(PxD6Axis::eSWING1, PxD6Motion::eLOCKED);
        slider->setMotion(PxD6Axis::eSWING2, PxD6Motion::eLOCKED);
        slider->setLinearLimit(PxD6Axis::eX, PxJointLinearLimitPair(multibody.physics->getTolerancesScale(), -cfg.suspension_travel, cfg.suspension_travel));
        register_multibody_joint(slider);

        suspension_member& member = corner.members[corner.member_count++];
        member.actor = strut;
        member.local_start = strut_pose.transformInv(top);
        member.local_end = strut_pose.transformInv(bottom);
        return true;
    }


    inline bool add_steering_stop(PxRigidDynamic* upright, const PxTransform& chassis_pose, const PxVec3& wheel_world, float angle_limit)
    {
        PxQuat axis_frame(PxPi * 0.5f, PxVec3(0.0f, 0.0f, 1.0f));
        PxQuat world_frame_rotation = chassis_pose.q * axis_frame;
        PxTransform chassis_frame(chassis_pose.transformInv(wheel_world), axis_frame);
        PxTransform upright_pose = upright->getGlobalPose();
        PxTransform upright_frame(upright_pose.transformInv(wheel_world), upright_pose.q.getConjugate() * world_frame_rotation);
        PxD6Joint* stop = PxD6JointCreate(*multibody.physics, body, chassis_frame, upright, upright_frame);
        if (!stop)
        {
            return false;
        }

        stop->setMotion(PxD6Axis::eX, PxD6Motion::eFREE);
        stop->setMotion(PxD6Axis::eY, PxD6Motion::eFREE);
        stop->setMotion(PxD6Axis::eZ, PxD6Motion::eFREE);
        stop->setMotion(PxD6Axis::eSWING1, PxD6Motion::eFREE);
        stop->setMotion(PxD6Axis::eSWING2, PxD6Motion::eFREE);
        if (angle_limit > 0.0f)
        {
            stop->setMotion(PxD6Axis::eTWIST, PxD6Motion::eLIMITED);
            stop->setTwistLimit(PxJointAngularLimitPair(-angle_limit, angle_limit));
        }
        else
        {
            stop->setMotion(PxD6Axis::eTWIST, PxD6Motion::eLOCKED);
        }
        register_multibody_joint(stop);
        return true;
    }


    inline PxVec3 hardpoint_world(const PxTransform& chassis_pose, const PxVec3& local_point)
    {
        return chassis_pose.transform(local_point);
    }


    inline float mechanism_actor_mass()
    {
        auto link_count = [](suspension_mechanism mechanism)
        {
            if (mechanism == suspension_mechanism::multi_link)
            {
                return 4;
            }
            return 2;
        };

        int suspension_links = link_count(spec.front_geometry.mechanism) * 2 + link_count(spec.rear_geometry.mechanism) * 2 + 2;
        return (cfg.wheel_mass + spec.upright_mass) * static_cast<float>(wheel_count) + spec.suspension_link_mass * static_cast<float>(suspension_links) + spec.steering_rack_mass;
    }


    inline float chassis_mass()
    {
        return PxMax(cfg.mass - mechanism_actor_mass(), 100.0f);
    }


    inline void update_assembled_center_of_mass()
    {
        if (!body)
        {
            return;
        }

        float body_mass = body->getMass();
        float total_mass = body_mass;
        PxVec3 mechanism_moment = PxVec3(0.0f);
        for (int i = 0; i < multibody.actor_count; i++)
        {
            PxRigidDynamic* actor = multibody.actors[i];
            if (!actor)
            {
                continue;
            }
            float actor_mass = actor->getMass();
            PxVec3 actor_center = actor->getGlobalPose().transform(actor->getCMassLocalPose().p);
            total_mass += actor_mass;
            mechanism_moment += actor_center * actor_mass;
        }

        PxTransform chassis_pose = body->getGlobalPose();
        PxVec3 target_local(spec.center_of_mass_x, spec.center_of_mass_y, spec.center_of_mass_z);
        PxVec3 target_world = chassis_pose.transform(target_local);
        PxVec3 chassis_center_world = (target_world * total_mass - mechanism_moment) / PxMax(body_mass, 1.0f);
        PxVec3 chassis_center_local = chassis_pose.transformInv(chassis_center_world);
        PxRigidBodyExt::setMassAndUpdateInertia(*body, body_mass, &chassis_center_local);
    }


    inline bool create_suspension_corner(int wheel_index, const suspension_geometry& geometry)
    {
        suspension_corner& corner = multibody.corners[wheel_index];
        PxTransform chassis_pose = body->getGlobalPose();
        PxVec3 wheel_local = wheel_offsets[wheel_index];
        PxVec3 wheel_world = hardpoint_world(chassis_pose, wheel_local);
        float side = wheel_local.x < 0.0f ? -1.0f : 1.0f;
        float inner_x = wheel_local.x * geometry.chassis_inset;
        float arm_span = PxMax(geometry.arm_span, 0.05f);
        float camber = is_front(wheel_index) ? spec.front_camber : spec.rear_camber;
        float toe = is_front(wheel_index) ? spec.front_toe : spec.rear_toe;
        PxQuat alignment = PxQuat(side * toe, PxVec3(0.0f, 1.0f, 0.0f)) * PxQuat(side * camber, PxVec3(0.0f, 0.0f, 1.0f));
        PxTransform wheel_pose(wheel_world, chassis_pose.q * alignment);

        corner.upright = create_mechanism_actor(wheel_pose, PxBoxGeometry(0.04f, 0.18f, 0.06f), spec.upright_mass);
        // guard stays inside the sweep tire so custom contact remains primary
        corner.wheel_body = create_mechanism_actor(wheel_pose, PxSphereGeometry(cfg.wheel_radius_for(wheel_index) * wheel_guard_radius_scale), cfg.wheel_mass, true);
        if (!corner.upright || !corner.wheel_body)
        {
            return false;
        }
        float wheel_inertia = PxMax(wheel_moi[wheel_index], 0.1f);
        corner.wheel_body->setMassSpaceInertiaTensor(PxVec3(wheel_inertia, wheel_inertia * 0.65f, wheel_inertia * 0.65f));
        // the physx default angular limit prevents high gear upshifts
        corner.wheel_body->setMaxAngularVelocity(500.0f);
        // guard recovery is capped so deep contacts cannot launch the chassis
        corner.wheel_body->setMaxDepenetrationVelocity(2.0f);

        corner.wheel_joint = PxRevoluteJointCreate(*multibody.physics, corner.upright, PxTransform(PxIdentity), corner.wheel_body, PxTransform(PxIdentity));
        if (!corner.wheel_joint)
        {
            return false;
        }
        corner.wheel_joint->setConstraintFlag(PxConstraintFlag::eENABLE_EXTENDED_LIMITS, true);
        register_multibody_joint(corner.wheel_joint);
        float steering_limit = is_front(wheel_index) ? PxClamp(fabsf(spec.max_steer_angle), 0.1f, 1.2f) : 0.0f;
        if (!add_steering_stop(corner.upright, chassis_pose, wheel_world, steering_limit))
        {
            return false;
        }

        PxVec3 upper_outer_local = wheel_local + PxVec3(-side * 0.015f, geometry.upper_upright_y, 0.0f);
        PxVec3 lower_outer_local = wheel_local + PxVec3(-side * 0.015f, geometry.lower_upright_y, 0.0f);
        PxVec3 upper_inner_front_local(inner_x, wheel_local.y + geometry.upper_inner_y, wheel_local.z + arm_span);
        PxVec3 upper_inner_rear_local(inner_x, wheel_local.y + geometry.upper_inner_y, wheel_local.z - arm_span);
        PxVec3 lower_inner_front_local(inner_x, wheel_local.y + geometry.lower_inner_y, wheel_local.z + arm_span);
        PxVec3 lower_inner_rear_local(inner_x, wheel_local.y + geometry.lower_inner_y, wheel_local.z - arm_span);

        if (geometry.mechanism == suspension_mechanism::multi_link)
        {
            float spread_y = PxMax(geometry.link_spread_y, 0.05f);
            float spread_z = PxMax(geometry.link_spread_z, 0.05f);
            const PxVec3 inner_points[4] =
            {
                PxVec3(inner_x, wheel_local.y + geometry.upper_inner_y, wheel_local.z + spread_z),
                PxVec3(inner_x, wheel_local.y + geometry.upper_inner_y, wheel_local.z - spread_z),
                PxVec3(inner_x, wheel_local.y + geometry.lower_inner_y, wheel_local.z + spread_z),
                PxVec3(inner_x, wheel_local.y + geometry.lower_inner_y, wheel_local.z - spread_z)
            };
            const PxVec3 outer_points[4] =
            {
                wheel_local + PxVec3(0.0f, spread_y, spread_z * 0.45f),
                wheel_local + PxVec3(0.0f, spread_y, -spread_z * 0.45f),
                wheel_local + PxVec3(0.0f, -spread_y, spread_z * 0.45f),
                wheel_local + PxVec3(0.0f, -spread_y, -spread_z * 0.45f)
            };
            for (int i = 0; i < 4; i++)
            {
                if (!add_link_member(corner, hardpoint_world(chassis_pose, inner_points[i]), hardpoint_world(chassis_pose, outer_points[i]), spec.suspension_link_mass))
                {
                    return false;
                }
            }
        }
        else
        {
            if (!add_wishbone(corner, hardpoint_world(chassis_pose, lower_inner_front_local), hardpoint_world(chassis_pose, lower_inner_rear_local), hardpoint_world(chassis_pose, lower_outer_local), spec.suspension_link_mass))
            {
                return false;
            }
            if (geometry.mechanism == suspension_mechanism::double_wishbone)
            {
                if (!add_wishbone(corner, hardpoint_world(chassis_pose, upper_inner_front_local), hardpoint_world(chassis_pose, upper_inner_rear_local), hardpoint_world(chassis_pose, upper_outer_local), spec.suspension_link_mass))
                {
                    return false;
                }
            }
        }

        PxVec3 shock_top_local(side * (fabsf(wheel_local.x) - geometry.strut_top_inset), wheel_local.y + geometry.strut_top_y, wheel_local.z);
        PxVec3 shock_bottom_local = wheel_local + PxVec3(0.0f, geometry.lower_upright_y, 0.0f);
        PxVec3 shock_top_world = hardpoint_world(chassis_pose, shock_top_local);
        PxVec3 shock_bottom_world = hardpoint_world(chassis_pose, shock_bottom_local);
        if (geometry.mechanism == suspension_mechanism::macpherson && !add_macpherson_strut(corner, shock_top_world, shock_bottom_world, spec.suspension_link_mass))
        {
            return false;
        }
        corner.chassis_shock_anchor = shock_top_local;
        corner.upright_shock_anchor = corner.upright->getGlobalPose().transformInv(shock_bottom_world);
        float static_load = chassis_mass() * (is_front(wheel_index) ? get_weight_distribution_front() : 1.0f - get_weight_distribution_front()) * 0.5f * 9.81f;
        corner.shock_rest_length = (shock_top_world - shock_bottom_world).magnitude() + static_load / PxMax(spring_stiffness[wheel_index], 1.0f);
        corner.shock_length = (shock_top_world - shock_bottom_world).magnitude();

        corner.travel_joint = nullptr;
        if (geometry.mechanism != suspension_mechanism::macpherson)
        {
            corner.travel_joint = PxDistanceJointCreate(*multibody.physics, body, local_anchor(body, shock_top_world), corner.upright, local_anchor(corner.upright, shock_bottom_world));
            if (!corner.travel_joint)
            {
                return false;
            }
            corner.travel_joint->setMinDistance(PxMax(corner.shock_rest_length - cfg.suspension_travel, 0.05f));
            corner.travel_joint->setMaxDistance(corner.shock_rest_length + cfg.suspension_travel * 0.15f);
            corner.travel_joint->setDistanceJointFlag(PxDistanceJointFlag::eMIN_DISTANCE_ENABLED, true);
            corner.travel_joint->setDistanceJointFlag(PxDistanceJointFlag::eMAX_DISTANCE_ENABLED, true);
            register_multibody_joint(corner.travel_joint);
        }

        return true;
    }


    inline bool create_locked_differential(int left, int right)
    {
        suspension_corner& left_corner = multibody.corners[left];
        suspension_corner& right_corner = multibody.corners[right];
        PxGearJoint* differential = PxGearJointCreate(*multibody.physics, left_corner.wheel_body, PxTransform(PxIdentity), right_corner.wheel_body, PxTransform(PxIdentity));
        if (!differential || !differential->setHinges(left_corner.wheel_joint, right_corner.wheel_joint))
        {
            if (differential)
            {
                differential->release();
            }
            return false;
        }

        differential->setGearRatio(-1.0f);
        register_multibody_joint(differential);
        return true;
    }


    inline bool create_steering_rack()
    {
        PxTransform chassis_pose = body->getGlobalPose();
        multibody.rack_travel = PxClamp(tanf(fabsf(spec.max_steer_angle)) * 0.22f, 0.05f, 0.20f);
        float front_z = wheel_offsets[front_left].z + spec.front_geometry.tie_rod_z;
        float rack_y = wheel_offsets[front_left].y + spec.front_geometry.tie_rod_y;
        PxVec3 rack_world = hardpoint_world(chassis_pose, PxVec3(0.0f, rack_y, front_z));
        multibody.rack = create_mechanism_actor(PxTransform(rack_world, chassis_pose.q), PxBoxGeometry(cfg.track_front * 0.35f, 0.025f, 0.025f), spec.steering_rack_mass);
        if (!multibody.rack)
        {
            return false;
        }

        multibody.rack_joint = PxD6JointCreate(*multibody.physics, body, local_anchor(body, rack_world), multibody.rack, PxTransform(PxIdentity));
        if (!multibody.rack_joint)
        {
            return false;
        }
        multibody.rack_joint->setMotion(PxD6Axis::eX, PxD6Motion::eLIMITED);
        multibody.rack_joint->setMotion(PxD6Axis::eY, PxD6Motion::eLOCKED);
        multibody.rack_joint->setMotion(PxD6Axis::eZ, PxD6Motion::eLOCKED);
        multibody.rack_joint->setMotion(PxD6Axis::eTWIST, PxD6Motion::eLOCKED);
        multibody.rack_joint->setMotion(PxD6Axis::eSWING1, PxD6Motion::eLOCKED);
        multibody.rack_joint->setMotion(PxD6Axis::eSWING2, PxD6Motion::eLOCKED);
        multibody.rack_joint->setLinearLimit(PxD6Axis::eX, PxJointLinearLimitPair(multibody.physics->getTolerancesScale(), -multibody.rack_travel, multibody.rack_travel));
        multibody.rack_joint->setDrive(PxD6Drive::eX, PxD6JointDrive(18000.0f, 1800.0f, PX_MAX_F32, true));
        register_multibody_joint(multibody.rack_joint);

        for (int wheel_index : { front_left, front_right })
        {
            suspension_corner& corner = multibody.corners[wheel_index];
            float side = wheel_index == front_left ? -1.0f : 1.0f;
            PxVec3 rack_end_world = hardpoint_world(chassis_pose, PxVec3(side * cfg.track_front * 0.35f, rack_y, front_z));
            PxVec3 upright_anchor_world = hardpoint_world(chassis_pose, wheel_offsets[wheel_index] + PxVec3(0.0f, spec.front_geometry.tie_rod_y, spec.front_geometry.tie_rod_z));
            if (!add_link_member(corner, rack_end_world, upright_anchor_world, spec.suspension_link_mass, multibody.rack))
            {
                return false;
            }
        }
        return true;
    }


    inline void destroy_multibody()
    {
        for (int i = multibody.joint_count - 1; i >= 0; i--)
        {
            if (multibody.joints[i])
            {
                multibody.joints[i]->release();
            }
        }
        for (int i = multibody.actor_count - 1; i >= 0; i--)
        {
            if (multibody.actors[i])
            {
                multibody.actors[i]->release();
            }
        }
        multibody = multibody_state();
    }


    inline bool create_multibody(PxPhysics* physics, PxScene* scene, bool destroy_existing = true)
    {
        if (!body || !physics || !scene || !material)
        {
            return false;
        }

        if (destroy_existing)
        {
            destroy_multibody();
        }
        multibody.physics = physics;
        multibody.scene = scene;
        body->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, false);
        body->setSolverIterationCounts(16, 4);

        for (int i = 0; i < wheel_count; i++)
        {
            const suspension_geometry& geometry = is_front(i) ? spec.front_geometry : spec.rear_geometry;
            if (!create_suspension_corner(i, geometry))
            {
                destroy_multibody();
                return false;
            }
        }
        if (spec.diff_type == 1)
        {
            bool lock_front = spec.drivetrain_type == 1 || spec.drivetrain_type == 2;
            bool lock_rear = spec.drivetrain_type == 0 || spec.drivetrain_type == 2;
            if ((lock_front && !create_locked_differential(front_left, front_right)) || (lock_rear && !create_locked_differential(rear_left, rear_right)))
            {
                destroy_multibody();
                return false;
            }
        }
        if (!create_steering_rack())
        {
            destroy_multibody();
            return false;
        }

        update_assembled_center_of_mass();
        multibody.initialized = true;
        return true;
    }


    inline bool rebuild_multibody(bool preserve_motion = true)
    {
        if (!multibody_enabled)
        {
            return true;
        }
        PxPhysics* physics = multibody.physics;
        PxScene* scene = multibody.scene;
        multibody_motion_state motion = preserve_motion ? capture_multibody_motion() : multibody_motion_state();
        bool was_sleeping = preserve_motion && vehicle_sleeping;
        if (!physics || !scene)
        {
            return false;
        }
        multibody_state previous = multibody;
        multibody = multibody_state();
        if (!create_multibody(physics, scene, false))
        {
            multibody = previous;
            return false;
        }
        multibody_state replacement = multibody;
        multibody = previous;
        destroy_multibody();
        multibody = replacement;
        restore_multibody_motion(motion);
        if (was_sleeping)
        {
            sleep_vehicle_assembly();
        }
        return true;
    }


    inline PxVec3 actor_point_velocity(PxRigidBody* actor, const PxVec3& world_point)
    {
        return actor->getLinearVelocity() + actor->getAngularVelocity().cross(world_point - actor->getGlobalPose().p);
    }


    inline PxVec3 ground_point_velocity(const wheel& wheel_state)
    {
        const PxRigidDynamic* ground = wheel_state.contact_actor ? wheel_state.contact_actor->is<PxRigidDynamic>() : nullptr;
        return ground ? ground->getLinearVelocity() + ground->getAngularVelocity().cross(wheel_state.contact_point - ground->getGlobalPose().p) : PxVec3(0.0f);
    }


    inline void refresh_wheel_actor_state()
    {
        PxVec3 fallback_axis = body ? body->getGlobalPose().q.rotate(PxVec3(1.0f, 0.0f, 0.0f)) : PxVec3(1.0f, 0.0f, 0.0f);
        for (int i = 0; i < wheel_count; i++)
        {
            PxRigidDynamic* wheel_actor = multibody.corners[i].wheel_body;
            PxVec3 wheel_axis = wheel_actor ? wheel_actor->getGlobalPose().q.rotate(PxVec3(1.0f, 0.0f, 0.0f)) : fallback_axis;
            wheels[i].angular_velocity = wheel_actor ? wheel_actor->getAngularVelocity().dot(wheel_axis) : 0.0f;
            wheels[i].hub_position = wheel_actor ? wheel_actor->getGlobalPose().p : PxVec3(0.0f);
            wheels[i].hub_linear_velocity = wheel_actor ? wheel_actor->getLinearVelocity() : PxVec3(0.0f);
            wheels[i].hub_angular_velocity = wheel_actor ? wheel_actor->getAngularVelocity() : PxVec3(0.0f);
        }
    }


    inline void wake_vehicle_assembly()
    {
        if (body)
        {
            body->wakeUp();
        }
        for (int i = 0; i < multibody.actor_count; i++)
        {
            if (multibody.actors[i])
            {
                multibody.actors[i]->wakeUp();
            }
        }
        vehicle_sleep_timer = 0.0f;
        vehicle_sleeping = false;
    }


    inline bool vehicle_assembly_is_settled()
    {
        if (!body || body->getLinearVelocity().magnitudeSquared() > 0.0025f || body->getAngularVelocity().magnitudeSquared() > 0.0025f)
        {
            return false;
        }
        for (int i = 0; i < wheel_count; i++)
        {
            if (fabsf(wheels[i].angular_velocity) > 0.1f)
            {
                return false;
            }
        }
        for (int i = 0; i < multibody.actor_count; i++)
        {
            PxRigidDynamic* actor = multibody.actors[i];
            if (actor && (actor->getLinearVelocity().magnitudeSquared() > 0.01f || actor->getAngularVelocity().magnitudeSquared() > 0.0625f))
            {
                return false;
            }
        }
        return true;
    }


    inline void sleep_vehicle_assembly()
    {
        for (int i = 0; i < multibody.actor_count; i++)
        {
            if (multibody.actors[i])
            {
                multibody.actors[i]->putToSleep();
            }
        }
        if (body)
        {
            body->putToSleep();
        }
        for (int i = 0; i < wheel_count; i++)
        {
            wheels[i].angular_velocity = 0.0f;
            wheels[i].net_torque = 0.0f;
        }
        prev_velocity = PxVec3(0.0f);
        vehicle_sleep_timer = 0.0f;
        vehicle_sleeping = true;
    }


    inline float compute_damper_force(float velocity, float base_damping)
    {
        // negative velocity is bump and the knee blends low and high speed damping
        bool bump = velocity < 0.0f;
        float low_speed_ratio = bump ? spec.damping_bump_ratio : spec.damping_rebound_ratio;
        float high_speed_ratio = bump ? spec.damping_bump_high_speed_ratio : spec.damping_rebound_high_speed_ratio;
        float low_speed_damping = base_damping * low_speed_ratio;
        float high_speed_damping = base_damping * high_speed_ratio;
        float speed = fabsf(velocity);
        float knee_velocity = PxMax(spec.damper_knee_velocity, 0.01f);
        float force = high_speed_damping * speed + (low_speed_damping - high_speed_damping) * knee_velocity * (1.0f - expf(-speed / knee_velocity));
        return copysignf(force, velocity);
    }


    inline void update_multibody(float delta_time)
    {
        if (!multibody.initialized || delta_time <= 0.0f)
        {
            return;
        }

        if (multibody.rack_joint)
        {
            float curved_input = copysignf(powf(fabsf(PxClamp(input.steering, -1.0f, 1.0f)), spec.steering_linearity), input.steering);
            float rack_target = -curved_input * multibody.rack_travel;
            multibody.rack_joint->setDrivePosition(PxTransform(PxVec3(rack_target, 0.0f, 0.0f)));
        }

        for (int i = 0; i < wheel_count; i++)
        {
            suspension_corner& corner = multibody.corners[i];
            PxVec3 top = body->getGlobalPose().transform(corner.chassis_shock_anchor);
            PxVec3 bottom = corner.upright->getGlobalPose().transform(corner.upright_shock_anchor);
            PxVec3 delta = top - bottom;
            float length = PxMax(delta.magnitude(), 0.001f);
            PxVec3 direction = delta / length;
            PxVec3 top_velocity = actor_point_velocity(body, top);
            PxVec3 bottom_velocity = actor_point_velocity(corner.upright, bottom);
            float relative_speed = PxClamp((top_velocity - bottom_velocity).dot(direction), -spec.max_damper_velocity, spec.max_damper_velocity);
            float wheel_vertical_speed = (bottom_velocity - top_velocity).dot(body->getGlobalPose().q.rotate(PxVec3(0.0f, 1.0f, 0.0f)));
            if (fabsf(wheel_vertical_speed) > 0.1f)
            {
                float target_motion_ratio = PxClamp(fabsf(relative_speed / wheel_vertical_speed), 0.25f, 2.0f);
                wheels[i].motion_ratio = lerp(wheels[i].motion_ratio, target_motion_ratio, exp_decay(20.0f, delta_time));
            }
            float compression = corner.shock_rest_length - length;
            float force_magnitude = PxMax(spring_stiffness[i] * compression, 0.0f) - compute_damper_force(relative_speed, spring_damping[i]);
            float bump_start = cfg.suspension_travel * spec.bump_stop_threshold;
            if (compression > bump_start)
            {
                float bump_travel = PxMax(cfg.suspension_travel - bump_start, 0.001f);
                float bump_compression = compression - bump_start;
                float bump_progress = PxClamp(bump_compression / bump_travel, 0.0f, 1.0f);
                force_magnitude += bump_compression * spec.bump_stop_stiffness * (1.0f + spec.bump_stop_progression * bump_progress * bump_progress);
            }
            // packer threshold is a fraction of suspension travel
            float packer_start = cfg.suspension_travel * spec.packer_threshold;
            if (compression > packer_start)
            {
                force_magnitude += (compression - packer_start) * spec.packer_stiffness;
            }
            force_magnitude = PxClamp(force_magnitude, -spec.max_susp_force, spec.max_susp_force);
            PxVec3 force = direction * force_magnitude;
            PxRigidBodyExt::addForceAtPos(*body, force, top, PxForceMode::eFORCE);
            PxRigidBodyExt::addForceAtPos(*corner.upright, -force, bottom, PxForceMode::eFORCE);

            corner.shock_velocity = (length - corner.shock_length) / delta_time;
            corner.shock_length = length;
            wheels[i].shock_length = corner.shock_length;
            wheels[i].shock_rest_length = corner.shock_rest_length;
            wheels[i].shock_velocity = corner.shock_velocity;
            wheels[i].compression = PxClamp(compression / PxMax(cfg.suspension_travel, 0.01f), 0.0f, 1.5f);
            wheels[i].compression_velocity = -corner.shock_velocity / PxMax(cfg.suspension_travel, 0.01f);
            spring_force[i] = force_magnitude;
        }

        auto apply_anti_roll = [&](int left, int right, float stiffness)
        {
            suspension_corner& left_corner = multibody.corners[left];
            suspension_corner& right_corner = multibody.corners[right];
            float left_compression = left_corner.shock_rest_length - left_corner.shock_length;
            float right_compression = right_corner.shock_rest_length - right_corner.shock_length;
            float force_magnitude = PxClamp((left_compression - right_compression) * stiffness, -spec.max_susp_force, spec.max_susp_force);
            PxVec3 up = body->getGlobalPose().q.rotate(PxVec3(0.0f, 1.0f, 0.0f));
            PxVec3 left_bottom = left_corner.upright->getGlobalPose().transform(left_corner.upright_shock_anchor);
            PxVec3 right_bottom = right_corner.upright->getGlobalPose().transform(right_corner.upright_shock_anchor);
            PxVec3 left_top = body->getGlobalPose().transform(left_corner.chassis_shock_anchor);
            PxVec3 right_top = body->getGlobalPose().transform(right_corner.chassis_shock_anchor);
            PxVec3 left_force = -up * force_magnitude;
            PxVec3 right_force = up * force_magnitude;
            PxRigidBodyExt::addForceAtPos(*left_corner.upright, left_force, left_bottom, PxForceMode::eFORCE);
            PxRigidBodyExt::addForceAtPos(*right_corner.upright, right_force, right_bottom, PxForceMode::eFORCE);
            PxRigidBodyExt::addForceAtPos(*body, -left_force, left_top, PxForceMode::eFORCE);
            PxRigidBodyExt::addForceAtPos(*body, -right_force, right_top, PxForceMode::eFORCE);
        };
        apply_anti_roll(front_left, front_right, spec.front_arb_stiffness);
        apply_anti_roll(rear_left, rear_right, spec.rear_arb_stiffness);
    }


    inline void update_suspension(PxScene* scene, float dt)
    {
        PxTransform pose = body->getGlobalPose();
        PxVec3 local_down = pose.q.rotate(PxVec3(0, -1, 0));
        PxVec3 local_up = -local_down;

        PxQueryFilterData filter;
        filter.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;
        // only the chassis needs filtering because mechanism shapes are not scene queries
        self_filter.ignore = body;

        for (int i = 0; i < wheel_count; i++)
        {
            wheel& w = wheels[i];
            PxVec3 previous_normal = w.contact_normal;
            float wr_raw = cfg.wheel_radius_for(i);
            float wheel_radius = (std::isfinite(wr_raw) && wr_raw > 0.0f) ? PxMax(wr_raw, 0.05f) : 0.34f;
            float wheel_width = PxMax(cfg.wheel_width_for(i), 0.05f);
            PxRigidDynamic* wheel_actor = multibody.corners[i].wheel_body;
            PxTransform wheel_pose = wheel_actor ? wheel_actor->getGlobalPose() : PxTransform(pose.transform(wheel_offsets[i]), pose.q);
            PxVec3 wheel_center = wheel_pose.p;
            float downward_speed = wheel_actor ? PxMax(wheel_actor->getLinearVelocity().dot(local_down), 0.0f) : PxMax(body->getLinearVelocity().dot(local_down), 0.0f);
            // predicted travel extends detection only and must never add tire penetration
            float predicted_travel = PxMax(downward_speed * dt, 0.005f);
            PxQuat query_rotation = multibody.corners[i].upright ? multibody.corners[i].upright->getGlobalPose().q : pose.q;
            float source_radius = PxMax(PxMax(cfg.front_wheel_radius, cfg.rear_wheel_radius), 0.05f);
            float source_width = PxMax(PxMax(cfg.front_wheel_width, cfg.rear_wheel_width), 0.05f);
            // one cooked mesh is scaled per corner to match configured wheel dimensions
            PxMeshScale wheel_scale(PxVec3(wheel_width / source_width, wheel_radius / source_radius, wheel_radius / source_radius), PxQuat(PxIdentity));
            PxConvexMeshGeometry wheel_geometry(wheel_sweep_mesh, wheel_scale);
            float query_lift = 0.08f;
            float query_distance = query_lift + PxMax(0.08f, predicted_travel);
            PxTransform sweep_pose(wheel_center + local_up * query_lift, query_rotation);
            PxSweepBuffer hit;
            PxHitFlags hit_flags = PxHitFlag::eNORMAL | PxHitFlag::eMTD;
            bool swept = wheel_sweep_mesh && scene->sweep(wheel_geometry, sweep_pose, local_down, query_distance, hit, hit_flags, filter, &self_filter) && hit.block.actor;

            debug_sweep[i].origin = sweep_pose.p;
            debug_sweep[i].hit = false;
            debug_sweep[i].hit_point = sweep_pose.p + local_down * query_distance;
            debug_suspension_top[i] = body->getGlobalPose().transform(multibody.corners[i].chassis_shock_anchor);
            debug_suspension_bottom[i] = wheel_center;

            w.grounded = false;
            w.tire_load = 0.0f;
            w.contact_actor = nullptr;
            w.contact_normal = local_up;
            sweep_distance[i] = query_distance - query_lift;

            if (!swept || !std::isfinite(hit.block.distance) || hit.block.distance > query_lift + predicted_travel)
            {
                continue;
            }
            sweep_distance[i] = hit.block.distance - query_lift;

            PxVec3 normal = hit.block.normal;
            if (!is_finite_vec(normal) || normal.magnitudeSquared() < 1e-6f)
            {
                normal = local_up;
            }
            else
            {
                normal.normalize();
            }
            // wall and ceiling hits cannot support tire load
            if (normal.dot(local_up) < 0.5f)
            {
                continue;
            }
            // normal filtering prevents mesh seams from converting forward speed into launch force
            if (is_finite_vec(previous_normal) && previous_normal.magnitudeSquared() > 1e-6f)
            {
                previous_normal.normalize();
                float normal_blend = exp_decay(30.0f, dt);
                normal = previous_normal * (1.0f - normal_blend) + normal * normal_blend;
                normal.normalize();
            }

            float penetration = PxClamp(query_lift - hit.block.distance, 0.0f, PxMin(wheel_radius * 0.25f, 0.05f));
            // overlap sweep positions are unbounded so contact is reconstructed on the wheel support
            PxVec3 wheel_axis = query_rotation.rotate(PxVec3(1.0f, 0.0f, 0.0f));
            float axial_alignment = wheel_axis.dot(normal);
            PxVec3 radial_normal = normal - wheel_axis * axial_alignment;
            PxVec3 contact_offset = PxVec3(0.0f);
            if (radial_normal.normalize() > 1e-4f)
            {
                contact_offset -= radial_normal * wheel_radius;
            }
            if (fabsf(axial_alignment) > 1e-4f)
            {
                contact_offset -= wheel_axis * copysignf(wheel_width * 0.5f, axial_alignment);
            }
            PxVec3 contact_point = wheel_center + contact_offset;
            if (!is_finite_vec(contact_point))
            {
                continue;
            }
            debug_sweep[i].hit = true;
            debug_sweep[i].hit_point = contact_point;

            PxVec3 contact_velocity = wheel_actor ? actor_point_velocity(wheel_actor, contact_point) : actor_point_velocity(body, contact_point);
            if (const PxRigidDynamic* ground_actor = hit.block.actor->is<PxRigidDynamic>())
            {
                contact_velocity -= ground_actor->getLinearVelocity() + ground_actor->getAngularVelocity().cross(contact_point - ground_actor->getGlobalPose().p);
            }
            float vertical_velocity = contact_velocity.dot(normal);
            float tire_damping = 2.0f * 0.7f * sqrtf(PxMax(spec.tire_vertical_stiffness, 1.0f) * PxMax(cfg.wheel_mass, 1.0f));
            float normal_force = PxClamp(penetration * spec.tire_vertical_stiffness - vertical_velocity * tire_damping, 0.0f, spec.max_susp_force);

            w.grounded = true;
            w.contact_point = contact_point;
            w.contact_normal = normal;
            w.contact_actor = hit.block.actor;
            w.tire_load = normal_force;
            if (normal_force > 0.0f)
            {
                safe_add_force_at_pos(wheel_actor ? wheel_actor : body, normal * normal_force, contact_point);
                if (PxRigidDynamic* ground_actor = hit.block.actor->is<PxRigidDynamic>())
                {
                    safe_add_force_at_pos(ground_actor, -normal * normal_force, contact_point);
                }
            }
        }
    }


    inline float angular_velocity_to_rpm(float angular_velocity)
    {
        return angular_velocity * 60.0f / (2.0f * PxPi);
    }


    inline float get_driven_wheel_radius()
    {
        float raw_radius = cfg.rear_wheel_radius;
        if (spec.drivetrain_type == 1)
        {
            raw_radius = cfg.front_wheel_radius;
        }
        else if (spec.drivetrain_type == 2)
        {
            raw_radius = (cfg.front_wheel_radius + cfg.rear_wheel_radius) * 0.5f;
        }
        return std::isfinite(raw_radius) && raw_radius > 0.0f ? PxMax(raw_radius, 0.05f) : 0.34f;
    }


    inline float get_average_driven_angular_velocity(bool absolute, int* count = nullptr)
    {
        float angular_velocity = 0.0f;
        int driven_count = 0;
        for (int i = 0; i < wheel_count; i++)
        {
            if (is_driven(i))
            {
                angular_velocity += absolute ? fabsf(wheels[i].angular_velocity) : wheels[i].angular_velocity;
                driven_count++;
            }
        }
        if (count)
        {
            *count = driven_count;
        }
        return driven_count > 0 ? angular_velocity / static_cast<float>(driven_count) : 0.0f;
    }


    inline void update_boost(float throttle, float rpm, float dt)
    {
        if (!spec.turbo_enabled)
        {
            boost_pressure = lerp(boost_pressure, 0.0f, exp_decay(spec.boost_spool_rate * 3.0f, dt));
            return;
        }

        float target = 0.0f;
        if (throttle > 0.3f && rpm > spec.boost_min_rpm)
        {
            target = spec.boost_max_pressure * PxMin((rpm - spec.boost_min_rpm) / 4000.0f, 1.0f);

            if (rpm > spec.boost_wastegate_rpm)
            {
                target *= PxMax(0.0f, 1.0f - (rpm - spec.boost_wastegate_rpm) / 2000.0f);
            }
        }

        float rate = (target > boost_pressure) ? spec.boost_spool_rate : spec.boost_spool_rate * 2.0f;
        boost_pressure = lerp(boost_pressure, target, exp_decay(rate, dt));
    }


    inline float get_engine_torque(float rpm)
    {
        float idl = spec.engine_idle_rpm;
        float mx = spec.engine_max_rpm;
        if (idl > mx)
        {
            mx = idl + 1000.0f;
        }
        rpm = PxClamp(rpm, idl, mx);

        // breakpoints are relative to the engine's actual operating range
        float idle    = spec.engine_idle_rpm;
        float peak    = spec.engine_peak_torque_rpm;
        float redline = spec.engine_redline_rpm;
        float max_rpm = spec.engine_max_rpm;

        // split idle-to-peak into three progressive ramp zones
        float ramp_range = peak - idle;
        if (ramp_range <= 0.0f)
        {
            ramp_range = 1.0f;
        }
        float bp1 = idle + ramp_range * 0.30f; // low-end spool
        float bp2 = idle + ramp_range * 0.65f; // mid-range build

        float factor;
        if (rpm < bp1)
        {
            factor = 0.55f + ((rpm - idle) / (bp1 - idle)) * 0.15f;
        }
        else if (rpm < bp2)
        {
            factor = 0.70f + ((rpm - bp1) / (bp2 - bp1)) * 0.15f;
        }
        else if (rpm < peak)
        {
            factor = 0.85f + ((rpm - bp2) / (peak - bp2)) * 0.15f;
        }
        else if (rpm < redline)
        {
            float t = (rpm - peak) / (redline - peak);
            factor = 1.0f - t * t * 0.20f;
        }
        else
        {
            if (max_rpm <= redline)
            {
                max_rpm = redline + 1.0f;
            }
            factor = 0.80f * (1.0f - ((rpm - redline) / (max_rpm - redline)) * 0.8f);
        }

        return spec.engine_peak_torque * factor;
    }


    inline float get_electric_motor_torque(float rpm, float throttle)
    {
        if (!spec.electric_enabled || throttle <= spec.input_deadzone)
        {
            return 0.0f;
        }
        if (spec.electric_motor_max_rpm > 0.0f && rpm >= spec.electric_motor_max_rpm)
        {
            return 0.0f;
        }
        float tq = spec.electric_motor_torque;
        if (tq <= 0.0f)
        {
            return 0.0f;
        }
        float pkw = spec.electric_motor_power_kw;
        if (pkw > 0.0f && rpm > 50.0f)
        {
            float omega = rpm * (2.0f * 3.14159265f / 60.0f);
            float p_tq = (pkw * 1000.0f) / PxMax(omega, 1.0f);
            tq = PxMin(tq, p_tq);
        }
        return throttle * tq;
    }


    inline float wheel_rpm_to_engine_rpm(float wheel_rpm, int gear)
    {
        if (gear < 0 || gear >= spec.gear_count || gear == 1)
        {
            return spec.engine_idle_rpm;
        }
        return fabsf(wheel_rpm * spec.gear_ratios[gear] * spec.final_drive);
    }


    inline float get_upshift_speed(int from_gear, float throttle)
    {
        if (from_gear < 2 || from_gear >= spec.gear_count - 1)
        {
            return 999.0f;
        }
        float t = PxClamp((throttle - 0.3f) / 0.5f, 0.0f, 1.0f);
        return spec.upshift_speed_base[from_gear] + t * (spec.upshift_speed_sport[from_gear] - spec.upshift_speed_base[from_gear]);
    }


    inline float get_downshift_speed(int gear)
    {
        return (gear >= 2 && gear < spec.gear_count) ? spec.downshift_speeds[gear] : 0.0f;
    }


    inline void update_automatic_gearbox(float dt, float throttle, float forward_speed)
    {
        bool kickdown_requested = throttle > 0.9f && previous_automatic_throttle <= 0.75f;
        previous_automatic_throttle = throttle;

        if (shift_cooldown > 0.0f)
        {
            shift_cooldown -= dt;
        }

        if (is_shifting)
        {
            shift_timer -= dt;
            if (shift_timer <= 0.0f)
            {
                is_shifting = false;
                shift_timer = 0.0f;
                shift_cooldown = shift_cooldown_time;
            }
            return;
        }

        if (spec.manual_transmission)
        {
            return;
        }

        float speed_kmh = fabsf(forward_speed) * 3.6f;

        // reverse
        if (forward_speed < -1.0f && input.brake > 0.1f && throttle < 0.1f && current_gear != 0)
        {
            current_gear = 0;
            is_shifting = true;
            shift_timer = spec.shift_time * 2.0f;
            last_shift_direction = -1;
            return;
        }

        // neutral to first: clutch engagement, no shift delay
        if (current_gear == 1 && throttle > 0.1f && forward_speed >= -0.5f)
        {
            current_gear = 2;
            last_shift_direction = 1;
            return;
        }

        // reverse to first
        if (current_gear == 0)
        {
            if (throttle > 0.1f || forward_speed > 0.5f)
            {
                current_gear = 2;
                is_shifting = true;
                shift_timer = spec.shift_time * 2.0f;
                last_shift_direction = 1;
                return;
            }
        }

        if (current_gear >= 2)
        {
            bool can_shift = shift_cooldown <= 0.0f;

            float upshift_threshold = get_upshift_speed(current_gear, throttle);
            if (last_shift_direction == -1)
            {
                upshift_threshold += 10.0f;
            }

            bool speed_trigger = speed_kmh >= upshift_threshold;
            int driven_wheel_count = 0;
            float average_wheel_rpm = angular_velocity_to_rpm(get_average_driven_angular_velocity(true, &driven_wheel_count));
            float coupled_engine_rpm = driven_wheel_count > 0 ? wheel_rpm_to_engine_rpm(average_wheel_rpm, current_gear) : engine_rpm;
            float shift_rpm = PxMax(engine_rpm, coupled_engine_rpm);
            bool rpm_trigger = shift_rpm >= spec.shift_up_rpm;

            if (can_shift && (speed_trigger || rpm_trigger) && current_gear < spec.gear_count - 1 && throttle > 0.1f)
            {
                current_gear++;
                is_shifting = true;
                shift_timer = spec.shift_time;
                last_shift_direction = 1;
                return;
            }

            float downshift_threshold = get_downshift_speed(current_gear);
            if (last_shift_direction == 1)
            {
                downshift_threshold -= 10.0f;
            }

            if (can_shift && speed_kmh < downshift_threshold && current_gear > 2)
            {
                current_gear--;
                is_shifting = true;
                shift_timer = spec.shift_time;
                last_shift_direction = -1;
                downshift_blip_timer = spec.downshift_blip_duration;
                return;
            }

            // lugging protection avoids target gears near the upshift threshold
            if (can_shift && current_gear > 2 && engine_rpm < spec.shift_down_rpm)
            {
                float ratio          = fabsf(spec.gear_ratios[current_gear - 1]) * spec.final_drive;
                float potential_rpm  = angular_velocity_to_rpm(fabsf(forward_speed) / get_driven_wheel_radius()) * ratio;
                if (potential_rpm < spec.shift_up_rpm * 0.85f)
                {
                    current_gear--;
                    is_shifting = true;
                    shift_timer = spec.shift_time;
                    last_shift_direction = -1;
                    downshift_blip_timer = spec.downshift_blip_duration;
                    return;
                }
            }

            // kickdown only on a new full throttle request
            if (can_shift && kickdown_requested && current_gear > 2 && engine_rpm < spec.engine_peak_torque_rpm)
            {
                float avg_slip = 0.0f;
                int grounded = 0;
                for (int i = 0; i < wheel_count; i++)
                {
                    if (is_driven(i) && wheels[i].grounded)
                    {
                        avg_slip += fabsf(wheels[i].slip_ratio);
                        grounded++;
                    }
                }
                if (grounded > 0)
                {
                    avg_slip /= (float)grounded;
                }

                if (avg_slip < 0.15f)
                {
                    int target = current_gear;
                    for (int g = current_gear - 1; g >= 2; g--)
                    {
                        float ratio = fabsf(spec.gear_ratios[g]) * spec.final_drive;
                        float potential_rpm = angular_velocity_to_rpm(forward_speed / get_driven_wheel_radius()) * ratio;
                        if (potential_rpm < spec.shift_up_rpm * 0.85f)
                        {
                            target = g;
                        }
                        else
                        {
                            break;
                        }
                    }

                    if (target < current_gear)
                    {
                        current_gear = target;
                        is_shifting = true;
                        shift_timer = spec.shift_time;
                        last_shift_direction = -1;
                        downshift_blip_timer = spec.downshift_blip_duration;
                    }
                }
            }
        }
    }


    inline const char* get_gear_string()
    {
        static constexpr const char* names[] = { "R", "N", "1", "2", "3", "4", "5", "6", "7", "8", "9" };
        constexpr int name_count = static_cast<int>(sizeof(names) / sizeof(names[0]));
        return (current_gear >= 0 && current_gear < spec.gear_count && current_gear < name_count) ? names[current_gear] : "?";
    }


    // apply differential torque to a single axle (left/right wheel pair)
    inline void apply_axle_diff(int left, int right, float axle_torque)
    {
        float left_torque = axle_torque * 0.5f;
        float right_torque = axle_torque * 0.5f;
        if (spec.diff_type == 2)
        {
            float w_left  = wheels[left].angular_velocity;
            float w_right = wheels[right].angular_velocity;
            float delta_w = w_left - w_right;

            // smoothstep ramp (instead of a hard 0.5 rad/s gate) avoids on/off chatter
            // when the wheels oscillate around the threshold
            float ramp = PxClamp(fabsf(delta_w) / 0.5f, 0.0f, 1.0f);
            float smooth_ramp = ramp * ramp * (3.0f - 2.0f * ramp);
            float effective_delta = delta_w * smooth_ramp;

            float lock_ratio = (axle_torque >= 0.0f) ? spec.lsd_lock_ratio_accel : spec.lsd_lock_ratio_decel;
            float torque_lock = lock_ratio * fabsf(axle_torque);
            float viscous = fabsf(effective_delta) * spec.lsd_viscous;
            float lock_torque = spec.lsd_preload * smooth_ramp + torque_lock * smooth_ramp + viscous;
            float bias_sign = (delta_w > 0.0f) ? -1.0f : 1.0f;

            left_torque += bias_sign * lock_torque;
            right_torque -= bias_sign * lock_torque;
        }
        wheels[left].net_torque += left_torque;
        wheels[right].net_torque += right_torque;
        wheels[left].drive_torque += left_torque;
        wheels[right].drive_torque += right_torque;
    }


    // route torque to driven axle(s) based on drivetrain layout
    inline void apply_drive_torque(float total_torque)
    {
        if (spec.drivetrain_type == 2)
        {
            // awd - center diff torque split
            float front_torque = total_torque * spec.torque_split_front;
            float rear_torque  = total_torque * (1.0f - spec.torque_split_front);
            apply_axle_diff(front_left, front_right, front_torque);
            apply_axle_diff(rear_left,  rear_right,  rear_torque);
        }
        else if (spec.drivetrain_type == 1)
        {
            // fwd
            apply_axle_diff(front_left, front_right, total_torque);
        }
        else
        {
            // rwd
            apply_axle_diff(rear_left, rear_right, total_torque);
        }
    }


    inline void integrate_powertrain(float dt)
    {
        float ratio = is_in_neutral() ? 0.0f : spec.gear_ratios[current_gear] * spec.final_drive;
        float wheel_angular_velocity = get_average_driven_angular_velocity(false);
        if (!std::isfinite(engine_rpm) || !std::isfinite(gearbox_input_angular_velocity) || !std::isfinite(driveshaft_twist))
        {
            engine_rpm = spec.engine_idle_rpm;
            gearbox_input_angular_velocity = ratio * wheel_angular_velocity;
            driveshaft_twist = 0.0f;
        }
        float engine_angular_velocity = engine_rpm * PxPi * 2.0f / 60.0f;
        float engine_inertia = PxMax(spec.engine_inertia, 0.01f);
        float driveline_inertia = PxMax(spec.driveline_inertia, 0.001f);
        float stiffness = PxMax(spec.driveshaft_stiffness, 0.0f);
        float shaft_damping = PxMax(spec.driveshaft_damping, 0.0f);
        float drive_input = is_in_reverse() ? input.brake * spec.reverse_power_ratio : input.throttle;
        float blip = downshift_blip_timer > 0.0f ? spec.downshift_blip_amount * downshift_blip_timer / PxMax(spec.downshift_blip_duration, 0.001f) : 0.0f;
        float effective_throttle = PxMax(drive_input, blip);
        float boosted_torque = get_engine_torque(engine_rpm) * (1.0f + boost_pressure * spec.boost_torque_mult);
        float combustion_torque = rev_limiter_active ? 0.0f : boosted_torque * effective_throttle * assisted_actuators.engine_torque_scale;
        float idle_angular_velocity = spec.engine_idle_rpm * PxPi * 2.0f / 60.0f;
        float idle_torque = PxClamp((idle_angular_velocity - engine_angular_velocity) * engine_inertia * spec.engine_rpm_smoothing, 0.0f, spec.engine_peak_torque * 0.35f);
        float clutch_capacity = PxMax(spec.clutch_max_torque, 10.0f);
        float clutch_damping = clutch_capacity / 12.0f;
        float electric_target = is_in_forward_gear() ? get_electric_motor_torque(engine_rpm, input.throttle) * assisted_actuators.engine_torque_scale : 0.0f;
        float electric_rate = spec.electric_torque_response > 0.0f ? spec.electric_torque_response : 50.0f;
        motor_torque = lerp(motor_torque, electric_target, exp_decay(electric_rate, dt));
        engine_output_torque = combustion_torque + idle_torque;
        axle_drive_torque = 0.0f;
        engine_brake_torque = 0.0f;

        int substep_count = PxClamp(static_cast<int>(ceilf(dt / 0.0025f)), 1, 16);
        float substep = dt / static_cast<float>(substep_count);
        float accumulated_axle_torque = 0.0f;
        float accumulated_powertrain_reaction = 0.0f;
        for (int step = 0; step < substep_count; step++)
        {
            float clutch_slip = engine_angular_velocity - gearbox_input_angular_velocity;
            float clutch_torque = PxClamp(clutch_slip * clutch_damping, -clutch_capacity * clutch, clutch_capacity * clutch);
            float shaft_torque = 0.0f;
            float shaft_speed_difference = 0.0f;
            if (fabsf(ratio) > 0.001f)
            {
                shaft_speed_difference = gearbox_input_angular_velocity / ratio - wheel_angular_velocity;
                shaft_torque = stiffness > 0.0f ? driveshaft_twist * stiffness + shaft_speed_difference * shaft_damping : clutch_torque * ratio * spec.drivetrain_efficiency;
                driveshaft_twist += shaft_speed_difference * substep;
            }
            else
            {
                driveshaft_twist = 0.0f;
            }

            float friction_torque = spec.engine_friction * engine_angular_velocity;
            float engine_acceleration = (combustion_torque + idle_torque - friction_torque - clutch_torque) / engine_inertia;
            float previous_engine_angular_velocity = engine_angular_velocity;
            engine_angular_velocity = PxClamp(engine_angular_velocity + engine_acceleration * substep, 0.0f, spec.engine_max_rpm * PxPi * 4.0f / 60.0f);
            engine_acceleration = (engine_angular_velocity - previous_engine_angular_velocity) / substep;

            float efficiency = PxClamp(spec.drivetrain_efficiency, 0.1f, 1.0f);
            float gearbox_output_angular_velocity = fabsf(ratio) > 0.001f ? gearbox_input_angular_velocity / ratio : 0.0f;
            float efficiency_factor = shaft_torque * gearbox_output_angular_velocity >= 0.0f ? 1.0f / efficiency : efficiency;
            float shaft_reaction = fabsf(ratio) > 0.001f ? shaft_torque * efficiency_factor / ratio : 0.0f;
            float driveline_drag = gearbox_input_angular_velocity * spec.bearing_friction * driveline_inertia;
            float gearbox_acceleration = (clutch_torque - shaft_reaction - driveline_drag) / driveline_inertia;
            gearbox_input_angular_velocity += gearbox_acceleration * substep;
            accumulated_axle_torque += shaft_torque;
            accumulated_powertrain_reaction -= engine_inertia * engine_acceleration + driveline_inertia * gearbox_acceleration;
        }

        float mechanical_axle_torque = accumulated_axle_torque / static_cast<float>(substep_count);
        float electric_axle_torque = motor_torque * spec.final_drive * spec.drivetrain_efficiency;
        driveshaft_torque = mechanical_axle_torque;
        axle_drive_torque = mechanical_axle_torque + electric_axle_torque;
        PxVec3 crank_axis_local(spec.engine_crank_axis_x, spec.engine_crank_axis_y, spec.engine_crank_axis_z);
        PxVec3 crank_axis = body->getGlobalPose().q.rotate(crank_axis_local.getNormalized());
        safe_add_torque(body, crank_axis * (accumulated_powertrain_reaction / static_cast<float>(substep_count)));
        if (input.throttle <= spec.input_deadzone && mechanical_axle_torque * wheel_angular_velocity < 0.0f)
        {
            engine_brake_torque = fabsf(mechanical_axle_torque);
        }
        apply_drive_torque(axle_drive_torque);
        engine_rpm = engine_angular_velocity * 60.0f / (PxPi * 2.0f);
    }


    inline float brake_torque_sign(int wheel_index)
    {
        const wheel& wheel_state = wheels[wheel_index];
        if (fabsf(wheel_state.angular_velocity) > 0.1f)
        {
            return wheel_state.angular_velocity > 0.0f ? -1.0f : 1.0f;
        }

        PxRigidDynamic* wheel_actor = multibody.corners[wheel_index].wheel_body;
        if (!wheel_actor || !wheel_state.grounded)
        {
            return 0.0f;
        }

        PxVec3 wheel_axis = wheel_actor->getGlobalPose().q.rotate(PxVec3(1.0f, 0.0f, 0.0f));
        PxVec3 wheel_forward = wheel_axis.cross(wheel_state.contact_normal);
        if (wheel_forward.normalize() < 1e-4f)
        {
            return 0.0f;
        }
        float longitudinal_speed = (wheel_actor->getLinearVelocity() - ground_point_velocity(wheel_state)).dot(wheel_forward);
        return fabsf(longitudinal_speed) > 0.05f ? (longitudinal_speed > 0.0f ? -1.0f : 1.0f) : 0.0f;
    }


    // service brakes add wheel torque heat and abs modulation
    inline void apply_service_brakes(float forward_speed_ms, float dt)
    {
        if (input.brake <= spec.input_deadzone)
        {
            clear_abs_state();
            return;
        }

        // in reverse the brake pedal is the reverse throttle, apply_drivetrain already routed the drive torque
        if (is_in_reverse())
        {
            clear_abs_state();
            return;
        }

        bool reverse_requested = !spec.manual_transmission && fabsf(forward_speed_ms) < 0.5f && input.brake > 0.8f && input.throttle < spec.input_deadzone && is_in_forward_gear() && !is_shifting;
        if (reverse_requested)
        {
            clear_abs_state();
            current_gear = 0;
            is_shifting  = true;
            shift_timer  = spec.shift_time * 2.0f;
            return;
        }

        float front_t = spec.brake_force * cfg.front_wheel_radius * input.brake * spec.brake_bias_front * 0.5f;
        float rear_t  = spec.brake_force * cfg.rear_wheel_radius * input.brake * (1.0f - spec.brake_bias_front) * 0.5f;

        for (int i = 0; i < wheel_count; i++)
        {
            float t = is_front(i) ? front_t : rear_t;

            float brake_efficiency = get_brake_efficiency(wheels[i].brake_temp);
            t *= brake_efficiency * assisted_actuators.brake_torque_scale[i];
            wheels[i].brake_torque = t;

            float heat = fabsf(wheels[i].angular_velocity) * t * spec.brake_heat_coefficient * dt;
            wheels[i].brake_temp = PxMin(wheels[i].brake_temp + heat, spec.brake_max_temp);

            wheels[i].net_torque += brake_torque_sign(i) * t;
        }
    }


    inline void apply_drivetrain(float forward_speed_kmh, float dt)
    {
        float forward_speed_ms = forward_speed_kmh / 3.6f;

        update_automatic_gearbox(dt, input.throttle, forward_speed_ms);
        bool traction_requested = input.throttle > spec.input_deadzone && is_in_forward_gear();
        bool braking_requested = input.brake > spec.input_deadzone && !is_in_reverse() && fabsf(forward_speed_kmh) > spec.braking_speed_threshold;
        update_assist_controller(traction_requested, braking_requested, dt);

        if (downshift_blip_timer > 0.0f)
        {
            downshift_blip_timer -= dt;
        }

        if (is_shifting)
        {
            clutch = 0.0f;
        }
        else if (is_in_neutral())
        {
            clutch = 0.0f;
        }
        else
        {
            float drive_input = is_in_reverse() ? input.brake : input.throttle;
            float clutch_target = fabsf(forward_speed_ms) < 2.0f && drive_input <= spec.input_deadzone ? 0.0f : 1.0f;
            clutch = lerp(clutch, clutch_target, exp_decay(spec.clutch_engagement_rate, dt));
        }

        update_boost(input.throttle, engine_rpm, dt);

        if (engine_rpm >= spec.engine_redline_rpm)
        {
            rev_limiter_active = true;
        }
        else if (engine_rpm < spec.engine_redline_rpm - 200.0f)
        {
            rev_limiter_active = false;
        }

        integrate_powertrain(dt);
        apply_service_brakes(forward_speed_ms, dt);
    }


    inline float get_effective_wheel_radius(int wheel_index, float tire_load)
    {
        float raw_radius = cfg.wheel_radius_for(wheel_index);
        float radius = std::isfinite(raw_radius) && raw_radius > 0.0f ? PxMax(raw_radius, 0.05f) : 0.34f;
        float deflection = spec.tire_vertical_stiffness > 1000.0f ? PxClamp(tire_load / spec.tire_vertical_stiffness, 0.0f, 0.05f) : 0.0f;
        return PxMax(radius - deflection * 0.55f, 0.05f);
    }


    // full handbrake locks rear joints while partial input remains torque based
    inline void update_handbrake_wheel_locks()
    {
        const bool handbrake_locked = input.handbrake >= 0.999f;
        for (int i : { rear_left, rear_right })
        {
            suspension_corner& corner = multibody.corners[i];
            if (corner.wheel_joint)
            {
                corner.wheel_joint->setDriveVelocity(0.0f);
                corner.wheel_joint->setDriveForceLimit(handbrake_locked ? handbrake_lock_force : 0.0f);
                corner.wheel_joint->setRevoluteJointFlag(PxRevoluteJointFlag::eDRIVE_ENABLED, handbrake_locked);
            }
            if (handbrake_locked && corner.wheel_body)
            {
                const PxVec3 wheel_axis = corner.wheel_body->getGlobalPose().q.rotate(PxVec3(1.0f, 0.0f, 0.0f));
                const PxVec3 angular_velocity = corner.wheel_body->getAngularVelocity();
                corner.wheel_body->setAngularVelocity(angular_velocity - wheel_axis * angular_velocity.dot(wheel_axis));
                wheels[i].angular_velocity = 0.0f;
                wheels[i].net_torque = 0.0f;
            }
        }
    }


    inline void update_tire_slip_state(float dt)
    {
        PxTransform chassis_pose = body->getGlobalPose();
        for (int i = 0; i < wheel_count; i++)
        {
            wheel& w = wheels[i];
            PxRigidDynamic* wheel_actor = multibody.corners[i].wheel_body;
            tire_condition_modifiers condition = get_tire_condition_modifiers(w.thermal.avg_surface(), w.thermal.core, w.wear, w.tire_load);
            w.condition_grip = condition.peak_grip;
            w.condition_stiffness = condition.stiffness;
            w.condition_relaxation = condition.relaxation;
            w.temperature_grip = condition.temperature_grip;
            w.wear_grip = condition.wear_grip;
            if (!wheel_actor || !w.grounded || w.tire_load <= 0.0f)
            {
                w.slip_ratio = 0.0f;
                w.slip_angle = 0.0f;
                continue;
            }

            float effective_radius = get_effective_wheel_radius(i, w.tire_load);
            PxVec3 wheel_axis = wheel_actor->getGlobalPose().q.rotate(PxVec3(1.0f, 0.0f, 0.0f));
            PxVec3 wheel_forward = wheel_axis.cross(w.contact_normal);
            wheel_forward -= w.contact_normal * wheel_forward.dot(w.contact_normal);
            if (wheel_forward.normalize() < 1e-4f)
            {
                wheel_forward = chassis_pose.q.rotate(PxVec3(0.0f, 0.0f, 1.0f));
            }
            PxVec3 wheel_lateral = w.contact_normal.cross(wheel_forward).getNormalized();
            PxVec3 wheel_velocity = wheel_actor->getLinearVelocity() - ground_point_velocity(w);
            wheel_velocity -= w.contact_normal * wheel_velocity.dot(w.contact_normal);
            float longitudinal_speed = wheel_velocity.dot(wheel_forward);
            float lateral_speed = wheel_velocity.dot(wheel_lateral);
            float surface_speed = w.angular_velocity * effective_radius;
            float ground_speed = sqrtf(longitudinal_speed * longitudinal_speed + lateral_speed * lateral_speed);
            float absolute_longitudinal_speed = fabsf(longitudinal_speed);
            float absolute_surface_speed = fabsf(surface_speed);
            float rest_speed = PxMax(ground_speed, absolute_surface_speed);
            float slip_denominator = PxMax(PxMax(absolute_longitudinal_speed, absolute_surface_speed), 0.01f);
            float raw_slip_ratio = PxClamp((surface_speed - longitudinal_speed) / slip_denominator, -1.0f, 1.0f);
            float raw_slip_angle = atan2f(lateral_speed, PxMax(absolute_longitudinal_speed, 0.5f));
            float relaxation_length = PxMax(spec.tire_relaxation_length * condition.relaxation, 0.05f);
            float longitudinal_length = relaxation_length * (fabsf(raw_slip_ratio) > fabsf(w.slip_ratio) ? 0.85f : 0.65f);
            float lateral_length = relaxation_length * (fabsf(raw_slip_angle) > fabsf(w.slip_angle) ? 1.10f : 0.85f);
            float longitudinal_blend = 1.0f - expf(-PxMax(ground_speed, absolute_surface_speed) * dt / longitudinal_length);
            float lateral_blend = 1.0f - expf(-ground_speed * dt / lateral_length);
            w.slip_ratio = lerp(w.slip_ratio, raw_slip_ratio, longitudinal_blend);
            w.slip_angle = lerp(w.slip_angle, raw_slip_angle, lateral_blend);

            if (rest_speed < spec.min_slip_speed && input.throttle <= spec.input_deadzone)
            {
                float rest_factor = 1.0f - rest_speed / PxMax(spec.min_slip_speed, 0.01f);
                float rest_decay = exp_decay(24.0f * rest_factor, dt);
                w.slip_ratio = lerp(w.slip_ratio, 0.0f, rest_decay);
                w.slip_angle = lerp(w.slip_angle, 0.0f, rest_decay);
                if (rest_speed < 0.03f)
                {
                    w.slip_ratio = 0.0f;
                    w.slip_angle = 0.0f;
                }
            }
        }
    }


    inline void apply_tire_forces(float dt)
    {
        // --- setup ---
        PxTransform pose = body->getGlobalPose();
        PxVec3 chassis_right = pose.q.rotate(PxVec3(1, 0, 0));
        PxVec3 chassis_forward = pose.q.rotate(PxVec3(0, 0, 1));

        if (log_pacejka)
        {
            SP_LOG_INFO("=== tire forces: speed=%.1f m/s ===", body->getLinearVelocity().magnitude());
        }

        for (int i = 0; i < wheel_count; i++)
        {
            wheel& w = wheels[i];
            w.brake_efficiency = get_brake_efficiency(w.brake_temp);
            const char* wheel_name = wheel_names[i];
            PxRigidDynamic* wheel_actor = multibody.corners[i].wheel_body;
            PxVec3 wheel_axis = wheel_actor ? wheel_actor->getGlobalPose().q.rotate(PxVec3(1.0f, 0.0f, 0.0f)) : chassis_right;
            if (wheel_actor)
            {
                w.angular_velocity = wheel_actor->getAngularVelocity().dot(wheel_axis);
            }
            const bool handbrake_locked = is_rear(i) && input.handbrake >= 0.999f;
            if (handbrake_locked)
            {
                w.angular_velocity = 0.0f;
                w.net_torque = 0.0f;
            }

            float wmoi    = (std::isfinite(wheel_moi[i]) && wheel_moi[i] > 0.0f) ? wheel_moi[i] : 1.0f;
            float wr_eff = get_effective_wheel_radius(i, w.tire_load);

            float dyn_camb = is_front(i) ? spec.front_camber : spec.rear_camber;
            if (wheel_actor)
            {
                PxVec3 chassis_up = pose.q.rotate(PxVec3(0.0f, 1.0f, 0.0f));
                float measured_camber = asinf(PxClamp(wheel_axis.dot(chassis_up), -1.0f, 1.0f));
                dyn_camb = i == front_left || i == rear_left ? -measured_camber : measured_camber;
                PxVec3 alignment_forward = wheel_axis.cross(chassis_up);
                if (alignment_forward.normalize() > 1e-4f)
                {
                    w.dynamic_toe = atan2f(alignment_forward.dot(chassis_right), alignment_forward.dot(chassis_forward));
                }
            }

            w.effective_radius = wr_eff;
            w.dynamic_camber   = dyn_camb;

            // airborne branch: no tire force, but drivetrain and brake torque already sitting in
            // net_torque still integrate so mid air throttle spins the wheels and brakes stop them
            if (!w.grounded || w.tire_load <= 0.0f)
            {
                if (log_pacejka)
                {
                    SP_LOG_INFO("[%s] airborne: grounded=%d, tire_load=%.1f", wheel_name, w.grounded, w.tire_load);
                }
                w.slip_angle = w.slip_ratio = w.lateral_force = w.longitudinal_force = 0.0f;

                w.net_torque -= w.angular_velocity * spec.bearing_friction * wmoi;
                if (!handbrake_locked && input.handbrake > spec.input_deadzone && is_rear(i))
                {
                    float hb_sign = (w.angular_velocity > 0.0f) ? -1.0f : (w.angular_velocity < 0.0f) ? 1.0f : 0.0f;
                    w.net_torque += hb_sign * spec.handbrake_torque * input.handbrake;
                }

                float spin_retain = powf(PxClamp(spec.airborne_wheel_decay, 0.0f, 1.0f), dt * 200.0f);
                if (dt > 1e-5f)
                {
                    w.net_torque += w.angular_velocity * (spin_retain - 1.0f) * wmoi / dt;
                }
                if (wheel_actor)
                {
                    PxVec3 hub_torque = wheel_axis * w.net_torque;
                    safe_add_torque(wheel_actor, hub_torque);
                    safe_add_torque(multibody.corners[i].upright, -hub_torque);
                }

                // airborne cooling: all zones cool at 3x rate
                for (int z = 0; z < 3; z++)
                {
                    float s_above = w.thermal.surface[z] - spec.tire_ambient_temp;
                    if (s_above > 0.0f)
                    {
                        w.thermal.surface[z] -= spec.tire_cooling_rate * 3.0f * s_above / 30.0f * dt;
                    }
                    w.thermal.surface[z] = PxMax(w.thermal.surface[z], spec.tire_ambient_temp);
                }
                float c_above = w.thermal.core - spec.tire_ambient_temp;
                if (c_above > 0.0f)
                {
                    w.thermal.core -= spec.tire_cooling_rate * 1.0f * c_above / 30.0f * dt;
                }
                w.thermal.core = PxMax(w.thermal.core, spec.tire_ambient_temp);
                w.rotation += w.angular_velocity * dt;
                continue;
            }

            PxVec3 world_pos = wheel_actor ? wheel_actor->getGlobalPose().p : pose.transform(wheel_offsets[i]);
            PxVec3 wheel_vel = (wheel_actor ? wheel_actor->getLinearVelocity() : body->getLinearVelocity() + body->getAngularVelocity().cross(world_pos - pose.p)) - ground_point_velocity(w);
            wheel_vel -= w.contact_normal * wheel_vel.dot(w.contact_normal);

            PxVec3 wheel_lat = wheel_axis;
            PxVec3 wheel_fwd = wheel_lat.cross(w.contact_normal);

            // keep tire forces in the contact plane so body roll cannot turn lateral grip into lift
            wheel_fwd -= w.contact_normal * wheel_fwd.dot(w.contact_normal);
            wheel_lat -= w.contact_normal * wheel_lat.dot(w.contact_normal);
            if (wheel_fwd.magnitudeSquared() > 1e-6f)
            {
                wheel_fwd.normalize();
            }
            if (wheel_lat.magnitudeSquared() > 1e-6f)
            {
                wheel_lat.normalize();
            }
            // re-orthogonalise after projection
            wheel_lat = w.contact_normal.cross(wheel_fwd);
            if (wheel_lat.magnitudeSquared() > 1e-6f)
            {
                wheel_lat.normalize();
            }
            wheel_fwd = wheel_lat.cross(w.contact_normal);
            if (wheel_fwd.magnitudeSquared() > 1e-6f)
            {
                wheel_fwd.normalize();
            }

            float vx = wheel_vel.dot(wheel_fwd);
            float vy = wheel_vel.dot(wheel_lat);
            float wheel_speed  = w.angular_velocity * wr_eff;
            float ground_speed = sqrtf(vx * vx + vy * vy);
            float slip_v_long = fabsf(wheel_speed - vx);
            float slip_v_lat  = fabsf(vy);
            float slip_v      = sqrtf(slip_v_long * slip_v_long + slip_v_lat * slip_v_lat);
            // include lateral motion, otherwise a sideways slide reads as at rest and
            // falls back to the static friction model instead of the pacejka curves
            float max_v = PxMax(fabsf(wheel_speed), ground_speed);

            if (log_pacejka)
            {
                SP_LOG_INFO("[%s] vx=%.3f, vy=%.3f, ws=%.3f", wheel_name, vx, vy, wheel_speed);
            }

            float base_grip      = spec.tire_friction * load_sensitive_grip(PxMax(w.tire_load, 0.0f));
            float camber_factor  = get_camber_grip_factor(dyn_camb);
            float surface_factor = get_surface_friction(w.contact_surface);
            // rear grip ratio represents compound differences between axles
            float axle_grip_scale = is_rear(i) ? spec.rear_grip_ratio : 1.0f;
            // camber modifies lateral grip only
            float shared_grip     = base_grip * surface_factor * axle_grip_scale;
            float long_grip_scale = w.condition_grip;
            float lat_grip_scale  = w.condition_grip * camber_factor;
            float peak_force_long = shared_grip * long_grip_scale * fabsf(spec.long_D);
            float peak_force_lat  = shared_grip * lat_grip_scale * fabsf(spec.lat_D);
            float pressure_ratio = spec.tire_pressure / PxMax(spec.tire_pressure_optimal, 0.1f);

            if (log_pacejka)
            {
                SP_LOG_INFO("[%s] load=%.0f, peak_long=%.0f, peak_lat=%.0f", wheel_name, w.tire_load, peak_force_long, peak_force_lat);
            }

            float lat_f = 0.0f, long_f = 0.0f;

            // smoothstep blend: 0 at rest, 1 at full speed, smooth transition in between
            float blend_lo = 0.5f;
            float blend_hi = PxMax(spec.min_slip_speed * 2.0f, 1.0f);
            float blend_t  = PxClamp((max_v - blend_lo) / PxMax(blend_hi - blend_lo, 0.01f), 0.0f, 1.0f);
            float pacejka_weight = blend_t * blend_t * (3.0f - 2.0f * blend_t);

            // static friction model (dominant at rest / very low speed)
            float corner_mass = PxMax(w.tire_load / 9.81f, 1.0f);
            float effective_longitudinal_mass = 1.0f / (1.0f / corner_mass + wr_eff * wr_eff / wmoi);
            float inverse_dt = 1.0f / PxMax(dt, 0.001f);
            float static_lat_f = PxClamp(-vy * corner_mass * inverse_dt, -peak_force_lat * 0.8f, peak_force_lat * 0.8f);
            float static_long_f = PxClamp((wheel_speed - vx) * effective_longitudinal_mass * inverse_dt, -peak_force_long * 0.8f, peak_force_long * 0.8f);
            float static_x = static_long_f / PxMax(peak_force_long * 0.8f, 1.0f);
            float static_y = static_lat_f / PxMax(peak_force_lat * 0.8f, 1.0f);
            float static_magnitude = sqrtf(static_x * static_x + static_y * static_y);
            if (static_magnitude > 1.0f)
            {
                static_long_f /= static_magnitude;
                static_lat_f /= static_magnitude;
            }

            float effective_slip_angle = w.slip_angle;
            if (fabsf(effective_slip_angle) < spec.slip_angle_deadband)
            {
                float factor = fabsf(effective_slip_angle) / spec.slip_angle_deadband;
                effective_slip_angle *= factor * factor;
            }

            float pacejka_slip_angle = PxClamp(effective_slip_angle, -spec.max_slip_angle, spec.max_slip_angle);
            bool is_left_wheel = (i == front_left || i == rear_left);
            tire_force_result dynamic_force = evaluate_magic_formula(spec, w.slip_ratio, pacejka_slip_angle, dyn_camb, w.tire_load, peak_force_long, peak_force_lat, w.condition_stiffness, is_left_wheel ? -1.0f : 1.0f);

            // blend between static friction and pacejka
            lat_f  = static_lat_f  * (1.0f - pacejka_weight) + dynamic_force.lateral * pacejka_weight;
            long_f = static_long_f * (1.0f - pacejka_weight) + dynamic_force.longitudinal * pacejka_weight;

            if (log_pacejka)
            {
                SP_LOG_INFO("[%s] blend=%.2f, lat_f=%.1f, long_f=%.1f", wheel_name, pacejka_weight, lat_f, long_f);
            }

            // --- 3-zone surface + core thermal model ---
            // carcass flex peaks when pressure is below optimal; over-inflation should never make
            // the friction-work term negative, so clamp to a small positive floor
            float pressure_heat_mult = PxMax(1.0f + (1.0f - pressure_ratio) * 1.5f, 0.2f);
            float rolling_heat = fabsf(wheel_speed) * spec.tire_heat_from_rolling * pressure_heat_mult;
            float cooling_air = spec.tire_cooling_rate + ground_speed * spec.tire_cooling_airflow;
            float force_magnitude = sqrtf(long_f * long_f + lat_f * lat_f);
            float normalized_force = force_magnitude / spec.load_reference;
            float slip_ratio_eff = PxClamp(slip_v / PxMax(ground_speed, 0.5f), 0.0f, 2.0f);
            float speed_heat_scale = PxClamp(ground_speed / 2.0f, 0.0f, 1.0f);
            float friction_work = normalized_force * slip_ratio_eff * pacejka_weight * speed_heat_scale;
            float base_heat = friction_work * spec.tire_heat_from_slip * pressure_heat_mult + rolling_heat;

            // camber determines heat distribution across zones:
            // negative camber loads the inside zone more under cornering
            float camber_bias = -dyn_camb * 3.0f;
            float zone_heat[3] = {
                PxMax(base_heat * (1.0f + camber_bias), 0.0f),
                base_heat,
                PxMax(base_heat * (1.0f - camber_bias), 0.0f)
            };

            float surface_resp = spec.tire_surface_response;
            float core_rate = spec.tire_core_transfer_rate;
            for (int z = 0; z < 3; z++)
            {
                float s = w.thermal.surface[z];
                float s_delta = s - spec.tire_ambient_temp;
                float s_cooling = (s_delta > 0.0f) ? cooling_air * s_delta / 30.0f : 0.0f;
                float core_exchange = core_rate * (w.thermal.core - s);
                w.thermal.surface[z] += (zone_heat[z] * surface_resp - s_cooling + core_exchange) * dt;
                w.thermal.surface[z] = PxClamp(w.thermal.surface[z], spec.tire_min_temp, spec.tire_max_temp);
            }

            // core absorbs heat from surface average, cools slowly
            float avg_surf = w.thermal.avg_surface();
            float core_delta = w.thermal.core - spec.tire_ambient_temp;
            float core_cooling = (core_delta > 0.0f) ? spec.tire_cooling_rate * 0.3f * core_delta / 30.0f : 0.0f;
            w.thermal.core += (core_rate * (avg_surf - w.thermal.core) - core_cooling) * dt;
            w.thermal.core = PxClamp(w.thermal.core, spec.tire_min_temp, spec.tire_max_temp);

            // --- tire wear (per-zone based on local temperature) ---
            float total_wear = 0.0f;
            for (int z = 0; z < 3; z++)
            {
                float zone_excess = PxMax(w.thermal.surface[z] - spec.tire_optimal_temp, 0.0f) / spec.tire_temp_range;
                float zone_wear = spec.tire_wear_rate * (1.0f + zone_excess * spec.tire_wear_heat_mult);
                total_wear += zone_wear;
            }
            float wear_rate = total_wear / 3.0f;
            float wear_amount = wear_rate * slip_v * dt;
            w.wear = PxMin(w.wear + PxMax(wear_amount, 0.0f), 1.0f);

            w.lateral_force = lat_f;
            w.longitudinal_force = long_f;

            PxVec3 fpos = w.grounded ? w.contact_point : world_pos;
            PxVec3 tire_force = wheel_lat * lat_f + wheel_fwd * long_f;
            safe_add_force_at_pos(wheel_actor ? wheel_actor : body, tire_force, fpos);
            if (w.contact_actor)
            {
                if (const PxRigidDynamic* ground_actor = w.contact_actor->is<PxRigidDynamic>())
                {
                    safe_add_force_at_pos(const_cast<PxRigidDynamic*>(ground_actor), -tire_force, fpos);
                }
            }

            w.net_torque -= w.angular_velocity * spec.bearing_friction * wmoi; // bearing drag

            if (!handbrake_locked && is_rear(i) && input.handbrake > spec.input_deadzone)
            {
                float hb_sign = (w.angular_velocity > 0.0f) ? -1.0f : (w.angular_velocity < 0.0f) ? 1.0f : 0.0f;
                w.net_torque += hb_sign * spec.handbrake_torque * input.handbrake;
            }

            if (wheel_actor)
            {
                PxVec3 hub_torque = wheel_axis * w.net_torque;
                safe_add_torque(wheel_actor, hub_torque);
                safe_add_torque(multibody.corners[i].upright, -hub_torque);
            }

            w.rotation += w.angular_velocity * dt;

            if (log_pacejka)
            {
                SP_LOG_INFO("[%s] ang_vel=%.4f, lat_f=%.1f, long_f=%.1f", wheel_name, w.angular_velocity, lat_f, long_f);
            }
        }
        float front_steering_angle = (wheels[front_left].dynamic_toe + wheels[front_right].dynamic_toe) * 0.5f;
        wheels[front_left].bump_steer = wheels[front_left].dynamic_toe - front_steering_angle + spec.front_toe;
        wheels[front_right].bump_steer = wheels[front_right].dynamic_toe - front_steering_angle - spec.front_toe;
        wheels[rear_left].bump_steer = wheels[rear_left].dynamic_toe + spec.rear_toe;
        wheels[rear_right].bump_steer = wheels[rear_right].dynamic_toe - spec.rear_toe;
        if (log_pacejka)
        {
            SP_LOG_INFO("=== pacejka tick end ===\n");
        }
    }


    inline void apply_self_aligning_torque()
    {
        for (int i = 0; i < wheel_count; i++)
        {
            if (!wheels[i].grounded)
            {
                continue;
            }

            float trail = evaluate_pneumatic_trail(spec, wheels[i].slip_angle, wheels[i].tire_load, wheels[i].condition_stiffness);
            PxVec3 torque = wheels[i].contact_normal * (-wheels[i].lateral_force * trail * spec.self_align_gain);
            safe_add_torque(multibody.corners[i].upright, torque);
            if (const PxRigidDynamic* ground_actor = wheels[i].contact_actor ? wheels[i].contact_actor->is<PxRigidDynamic>() : nullptr)
            {
                safe_add_torque(const_cast<PxRigidDynamic*>(ground_actor), -torque);
            }
        }
    }


    inline void request_validation()
    {
        validation.completed = false;
        validation.requested = true;
    }


    inline void close_validation_report()
    {
        if (validation.report)
        {
            fclose(validation.report);
            validation.report = nullptr;
        }
    }


    inline void stop_validation(bool restore_vehicle)
    {
        input = input_state();
        input_target = input_state();
        close_validation_report();
        if (restore_vehicle && body)
        {
            PxTransform previous_pose = body->getGlobalPose();
            PxVec3 previous_linear_velocity = body->getLinearVelocity();
            PxVec3 previous_angular_velocity = body->getAngularVelocity();
            body->setGlobalPose(validation.start_pose);
            body->setLinearVelocity(PxVec3(0.0f));
            body->setAngularVelocity(PxVec3(0.0f));
            if (rebuild_multibody(false))
            {
                reset_drivetrain_transients();
                for (int i = 0; i < wheel_count; i++)
                {
                    wheels[i] = wheel();
                    wheels[i].effective_radius = cfg.wheel_radius_for(i);
                }
                reset_wheel_thermals();
            }
            else
            {
                body->setGlobalPose(previous_pose);
                body->setLinearVelocity(previous_linear_velocity);
                body->setAngularVelocity(previous_angular_velocity);
                SP_LOG_ERROR("car validation could not restore the vehicle pose");
            }
        }
        validation.initialized = false;
        validation.completed = true;
        validation.requested = false;
    }


    inline void shutdown_validation()
    {
        close_validation_report();
        validation = validation_state();
    }


    inline const char* validation_scenario_name(validation_scenario scenario)
    {
        switch (scenario)
        {
        case validation_scenario::settle:       return "settle";
        case validation_scenario::acceleration: return "acceleration";
        case validation_scenario::braking:      return "braking";
        case validation_scenario::coastdown:    return "coastdown";
        case validation_scenario::skidpad:      return "skidpad";
        case validation_scenario::step_steer:   return "step_steer";
        case validation_scenario::slalom:       return "slalom";
        case validation_scenario::curb:         return "curb";
        case validation_scenario::single_wheel_bump: return "single_wheel_bump";
        default:                                return "unknown";
        }
    }


    inline float validation_scenario_duration(validation_scenario scenario)
    {
        switch (scenario)
        {
        case validation_scenario::settle:       return 3.0f;
        case validation_scenario::acceleration: return 12.0f;
        case validation_scenario::braking:      return 6.0f;
        case validation_scenario::coastdown:    return 10.0f;
        case validation_scenario::skidpad:      return 8.0f;
        case validation_scenario::step_steer:   return 5.0f;
        case validation_scenario::slalom:       return 8.0f;
        case validation_scenario::curb:
        case validation_scenario::single_wheel_bump: return 5.0f;
        default:                                return 0.0f;
        }
    }


    inline float validation_initial_speed(validation_scenario scenario)
    {
        switch (scenario)
        {
        case validation_scenario::braking:
        case validation_scenario::coastdown: return 27.7778f;
        case validation_scenario::skidpad:
        case validation_scenario::step_steer: return 20.0f;
        case validation_scenario::slalom: return 18.0f;
        default: return 0.0f;
        }
    }


    inline void set_validation_speed(float speed)
    {
        PxTransform pose = body->getGlobalPose();
        PxVec3 forward = pose.q.rotate(PxVec3(0.0f, 0.0f, 1.0f));
        body->setLinearVelocity(forward * speed);
        body->setAngularVelocity(PxVec3(0.0f));
        for (int i = 0; i < multibody.actor_count; i++)
        {
            PxRigidDynamic* actor = multibody.actors[i];
            if (actor)
            {
                actor->setLinearVelocity(forward * speed);
                actor->setAngularVelocity(PxVec3(0.0f));
            }
        }
        for (int i = 0; i < wheel_count; i++)
        {
            PxRigidDynamic* wheel_actor = multibody.corners[i].wheel_body;
            if (wheel_actor)
            {
                PxVec3 wheel_axis = wheel_actor->getGlobalPose().q.rotate(PxVec3(1.0f, 0.0f, 0.0f));
                float angular_velocity = speed / PxMax(cfg.wheel_radius_for(i), 0.05f);
                wheel_actor->setAngularVelocity(wheel_axis * angular_velocity);
                wheels[i].angular_velocity = angular_velocity;
            }
        }
        prev_velocity = forward * speed;
    }


    inline bool begin_validation_scenario(validation_scenario scenario)
    {
        validation.scenario = scenario;
        validation.elapsed = 0.0f;
        validation.reached_speed_time = -1.0f;
        validation.max_lateral_g = 0.0f;
        validation.max_yaw_rate = 0.0f;
        validation.max_compression = 0.0f;
        validation.minimum_up = 1.0f;
        validation.event_applied = false;
        input = input_state();
        input_target = input_state();
        PxTransform previous_pose = body->getGlobalPose();
        PxVec3 previous_linear_velocity = body->getLinearVelocity();
        PxVec3 previous_angular_velocity = body->getAngularVelocity();
        bool previous_sleeping = vehicle_sleeping;
        float previous_sleep_timer = vehicle_sleep_timer;
        body->setGlobalPose(validation.start_pose);
        body->setLinearVelocity(PxVec3(0.0f));
        body->setAngularVelocity(PxVec3(0.0f));
        vehicle_sleeping = false;
        vehicle_sleep_timer = 0.0f;
        if (!rebuild_multibody(false))
        {
            body->setGlobalPose(previous_pose);
            body->setLinearVelocity(previous_linear_velocity);
            body->setAngularVelocity(previous_angular_velocity);
            vehicle_sleeping = previous_sleeping;
            vehicle_sleep_timer = previous_sleep_timer;
            SP_LOG_ERROR("car validation aborted because the multibody rebuild failed");
            stop_validation(false);
            return false;
        }
        reset_drivetrain_transients();
        for (int i = 0; i < wheel_count; i++)
        {
            wheels[i] = wheel();
            wheels[i].effective_radius = cfg.wheel_radius_for(i);
        }
        reset_wheel_thermals();
        wake_vehicle_assembly();
        set_validation_speed(validation_initial_speed(scenario));
        validation.scenario_start = body->getGlobalPose().p;
        return true;
    }


    inline void write_validation_result(bool passed, float value, float minimum, float maximum, const char* unit)
    {
        if (validation.report)
        {
            fprintf(validation.report, "%s,%s,%.4f,%.4f,%.4f,%s\n", validation_scenario_name(validation.scenario), passed ? "pass" : "fail", value, minimum, maximum, unit);
            fflush(validation.report);
        }
        SP_LOG_INFO("car validation %s %s value %.3f range %.3f to %.3f %s", validation_scenario_name(validation.scenario), passed ? "passed" : "failed", value, minimum, maximum, unit);
    }


    inline void finish_validation_scenario()
    {
        float speed = body->getLinearVelocity().magnitude();
        float distance = (body->getGlobalPose().p - validation.scenario_start).magnitude();
        const validation_targets& targets = spec.validation;
        switch (validation.scenario)
        {
        case validation_scenario::settle:
            write_validation_result(speed <= targets.settle_speed_max, speed, 0.0f, targets.settle_speed_max, "mps");
            break;
        case validation_scenario::acceleration:
            write_validation_result(validation.reached_speed_time >= targets.zero_to_100_min && validation.reached_speed_time <= targets.zero_to_100_max, validation.reached_speed_time, targets.zero_to_100_min, targets.zero_to_100_max, "s");
            break;
        case validation_scenario::braking:
            write_validation_result(speed < 1.0f && distance >= targets.braking_distance_min && distance <= targets.braking_distance_max, distance, targets.braking_distance_min, targets.braking_distance_max, "m");
            break;
        case validation_scenario::coastdown:
            write_validation_result(speed > 5.0f && speed < 27.7778f, speed, 5.0f, 27.7778f, "mps");
            break;
        case validation_scenario::skidpad:
            write_validation_result(validation.max_lateral_g >= targets.skidpad_g_min && validation.max_lateral_g <= targets.skidpad_g_max && validation.minimum_up > 0.5f, validation.max_lateral_g, targets.skidpad_g_min, targets.skidpad_g_max, "g");
            break;
        case validation_scenario::step_steer:
            write_validation_result(validation.max_yaw_rate > 0.05f && validation.minimum_up > 0.5f, validation.max_yaw_rate, 0.05f, 2.0f, "radps");
            break;
        case validation_scenario::slalom:
            write_validation_result(validation.max_lateral_g > 0.2f && validation.minimum_up > 0.5f, validation.max_lateral_g, 0.2f, targets.skidpad_g_max, "g");
            break;
        case validation_scenario::curb:
        case validation_scenario::single_wheel_bump:
            write_validation_result(validation.max_compression > 0.05f && fabsf(wheels[front_left].compression_velocity) < 0.2f && validation.minimum_up > 0.5f, validation.max_compression, 0.05f, 1.5f, "travel");
            break;
        default:
            break;
        }

        int next = static_cast<int>(validation.scenario) + 1;
        if (next >= static_cast<int>(validation_scenario::count))
        {
            stop_validation(true);
            return;
        }
        begin_validation_scenario(static_cast<validation_scenario>(next));
    }


    inline void tick_validation(float dt)
    {
        if (!validation.initialized)
        {
            if (validation.completed || !validation.requested || !body || !multibody.initialized)
            {
                return;
            }
            validation = validation_state();
            validation.initialized = true;
            validation.requested = true;
            validation.start_pose = body->getGlobalPose();
            fopen_s(&validation.report, "car_validation_report.csv", "w");
            if (validation.report)
            {
                fprintf(validation.report, "scenario,result,value,minimum,maximum,unit\n");
            }
            if (!begin_validation_scenario(validation_scenario::settle))
            {
                return;
            }
        }

        validation.elapsed += dt;
        float speed = body->getLinearVelocity().magnitude();
        if (validation.scenario == validation_scenario::acceleration && validation.reached_speed_time < 0.0f && speed >= 27.7778f)
        {
            validation.reached_speed_time = validation.elapsed;
        }
        validation.max_lateral_g = PxMax(validation.max_lateral_g, fabsf(lateral_accel) / 9.81f);
        for (int i = 0; i < wheel_count; i++)
        {
            validation.max_compression = PxMax(validation.max_compression, wheels[i].compression);
        }
        PxTransform pose = body->getGlobalPose();
        validation.max_yaw_rate = PxMax(validation.max_yaw_rate, fabsf(body->getAngularVelocity().dot(pose.q.rotate(PxVec3(0.0f, 1.0f, 0.0f)))));
        validation.minimum_up = PxMin(validation.minimum_up, pose.q.rotate(PxVec3(0.0f, 1.0f, 0.0f)).dot(PxVec3(0.0f, 1.0f, 0.0f)));

        input_target = input_state();
        switch (validation.scenario)
        {
        case validation_scenario::acceleration:
            input_target.throttle = 1.0f;
            break;
        case validation_scenario::braking:
            input_target.brake = 1.0f;
            break;
        case validation_scenario::skidpad:
            input_target.throttle = 0.25f;
            input_target.steering = 0.30f;
            break;
        case validation_scenario::step_steer:
            input_target.throttle = 0.20f;
            input_target.steering = validation.elapsed >= 1.0f ? 0.35f : 0.0f;
            break;
        case validation_scenario::slalom:
            input_target.throttle = 0.20f;
            input_target.steering = sinf(validation.elapsed * 2.0f) * 0.35f;
            break;
        case validation_scenario::curb:
        case validation_scenario::single_wheel_bump:
            if (!validation.event_applied && validation.elapsed >= 1.0f)
            {
                PxVec3 up = body->getGlobalPose().q.rotate(PxVec3(0.0f, 1.0f, 0.0f));
                PxRigidDynamic* front_wheel = multibody.corners[front_left].wheel_body;
                PxRigidDynamic* rear_wheel = multibody.corners[rear_left].wheel_body;
                if (front_wheel)
                {
                    front_wheel->addForce(up * 250.0f, PxForceMode::eIMPULSE);
                }
                if (validation.scenario == validation_scenario::curb && rear_wheel)
                {
                    rear_wheel->addForce(up * 250.0f, PxForceMode::eIMPULSE);
                }
                validation.event_applied = true;
            }
            break;
        default:
            break;
        }

        if (validation.elapsed >= validation_scenario_duration(validation.scenario))
        {
            finish_validation_scenario();
        }
    }


    // refresh after any direct wheel offset change
    inline void refresh_geometry_cache()
    {
        cfg.wheelbase   = wheel_offsets[front_left].z - wheel_offsets[rear_left].z;
        cfg.track_front = wheel_offsets[front_right].x - wheel_offsets[front_left].x;
        cfg.track_rear  = wheel_offsets[rear_right].x  - wheel_offsets[rear_left].x;
    }


    inline void apply_positive_override(float& target, float value)
    {
        if (value > 0.0f)
        {
            target = value;
        }
    }


    // zero preset dimensions preserve safe engine defaults
    inline void apply_preset_geometry(const car_preset& spec)
    {
        apply_positive_override(cfg.mass, spec.mass);
        apply_positive_override(cfg.wheelbase, spec.wheelbase);
        apply_positive_override(cfg.track_front, spec.track_front);
        apply_positive_override(cfg.track_rear, spec.track_rear);
        apply_positive_override(cfg.length, spec.length);
        apply_positive_override(cfg.width, spec.width);
        apply_positive_override(cfg.height, spec.height);
        apply_positive_override(cfg.suspension_height, spec.suspension_height);
        apply_positive_override(cfg.suspension_travel, spec.suspension_travel);
        apply_positive_override(cfg.front_wheel_radius, spec.front_wheel_radius);
        apply_positive_override(cfg.rear_wheel_radius, spec.rear_wheel_radius);
        apply_positive_override(cfg.front_wheel_width, spec.front_wheel_width);
        apply_positive_override(cfg.rear_wheel_width, spec.rear_wheel_width);
        apply_positive_override(cfg.wheel_mass, spec.wheel_mass);
    }


    inline void compute_constants()
    {
        // geometry floors keep wheel placement and force math finite
        float wb_safe = PxMax(cfg.wheelbase,   0.5f);
        float tf_safe = PxMax(cfg.track_front, 0.5f);
        float tr_safe = PxMax(cfg.track_rear,  0.5f);

        float front_z = wb_safe * 0.5f;
        float rear_z  = -wb_safe * 0.5f;
        float half_tf = tf_safe * 0.5f;
        float half_tr = tr_safe * 0.5f;
        float y       = -cfg.suspension_height;

        wheel_offsets[front_left]  = PxVec3(-half_tf, y, front_z);
        wheel_offsets[front_right] = PxVec3( half_tf, y, front_z);
        wheel_offsets[rear_left]   = PxVec3(-half_tr, y, rear_z);
        wheel_offsets[rear_right]  = PxVec3( half_tr, y, rear_z);

        refresh_geometry_cache();

        // finite wheel inertia is required before torque integration
        float wheel_mass_safe = (std::isfinite(cfg.wheel_mass) && cfg.wheel_mass > 0.0f) ? cfg.wheel_mass : 20.0f;

        float wdf = get_weight_distribution_front();
        float sprung_mass = chassis_mass();
        float axle_mass[2] = { sprung_mass * wdf * 0.5f, sprung_mass * (1.0f - wdf) * 0.5f };
        float freq[2]      = { spec.front_spring_freq, spec.rear_spring_freq };

        for (int i = 0; i < wheel_count; i++)
        {
            int axle   = is_front(i) ? 0 : 1;
            float mass = axle_mass[axle];
            float omega = 2.0f * PxPi * freq[axle];

            float r_raw  = cfg.wheel_radius_for(i);
            float r      = (std::isfinite(r_raw) && r_raw > 0.0f) ? r_raw : 0.34f;
            float r_safe = PxMax(r, 0.05f);

            wheel_moi[i]        = 0.7f * wheel_mass_safe * r_safe * r_safe;
            spring_stiffness[i] = mass * omega * omega;
            float dr = is_front(i) ? spec.front_damping_ratio : spec.rear_damping_ratio;
            spring_damping[i]   = 2.0f * dr * sqrtf(spring_stiffness[i] * mass);
        }
    }


    inline void destroy()
    {
        shutdown_validation();
        destroy_multibody();
        if (body)             { body->release();             body = nullptr; }
        if (material)         { material->release();         material = nullptr; }
        if (wheel_guard_material)
        {
            wheel_guard_material->release();
            wheel_guard_material = nullptr;
        }
        if (wheel_sweep_mesh)
        {
            wheel_sweep_mesh->release();
            wheel_sweep_mesh = nullptr;
        }
    }


    // sweep geometry must match the physical wheel dimensions
    inline bool rebuild_wheel_sweep_mesh()
    {
        const int segments = 32;
        std::vector<PxVec3> cyl_verts;
        cyl_verts.reserve(segments * 2);
        float sweep_r = PxMax(PxMax(cfg.front_wheel_radius, cfg.rear_wheel_radius), 0.05f);
        float sweep_w = PxMax(PxMax(cfg.front_wheel_width,  cfg.rear_wheel_width),  0.05f);
        float half_w  = sweep_w * 0.5f;
        for (int s = 0; s < segments; s++)
        {
            float angle = (2.0f * PxPi * s) / segments;
            float cy = cosf(angle) * sweep_r;
            float cz = sinf(angle) * sweep_r;
            cyl_verts.push_back(PxVec3(-half_w, cy, cz));
            cyl_verts.push_back(PxVec3( half_w, cy, cz));
        }

        PxTolerancesScale px_scale;
        px_scale.length = 1.0f;
        px_scale.speed  = 9.81f;
        PxCookingParams cook_params(px_scale);
        cook_params.convexMeshCookingType = PxConvexMeshCookingType::eQUICKHULL;

        PxConvexMeshDesc desc;
        desc.points.count  = static_cast<PxU32>(cyl_verts.size());
        desc.points.stride = sizeof(PxVec3);
        desc.points.data   = cyl_verts.data();
        desc.flags         = PxConvexFlag::eCOMPUTE_CONVEX;

        PxConvexMeshCookingResult::Enum cook_result;
        PxConvexMesh* replacement = PxCreateConvexMesh(cook_params, desc, *PxGetStandaloneInsertionCallback(), &cook_result);
        if (!replacement || cook_result != PxConvexMeshCookingResult::eSUCCESS)
        {
            if (replacement)
            {
                replacement->release();
            }
            SP_LOG_WARNING("failed to create wheel sweep cylinder mesh (r=%.3f, w=%.3f)", sweep_r, sweep_w);
            return false;
        }
        if (wheel_sweep_mesh)
        {
            wheel_sweep_mesh->release();
        }
        wheel_sweep_mesh = replacement;
        SP_LOG_INFO("wheel sweep cylinder mesh cooked: r=%.3f, w=%.3f", sweep_r, sweep_w);
        return true;
    }


    inline bool setup(const setup_params& params)
    {
        if (!params.physics || !params.scene)
        {
            return false;
        }
        if (body)
        {
            SP_LOG_ERROR("car setup supports one active vehicle");
            return false;
        }

        cfg = params.car_config;
        multibody_enabled = params.multibody_enabled;
        simulation_enabled = true;
        // preset geometry must be applied before deriving body suspension and wheel constants
        apply_car_spec(spec, true);

        for (int i = 0; i < wheel_count; i++)
        {
            wheels[i] = wheel();
            abs_active[i] = false;
            wheels[i].effective_radius = cfg.wheel_radius_for(i);
        }
        input = input_state();
        input_target = input_state();
        reset_drivetrain_transients();
        reset_wheel_thermals();

        // chassis material uses friction without restitution
        material = params.physics->createMaterial(0.8f, 0.7f, 0.0f);
        if (!material)
        {
            return false;
        }
        material->setRestitutionCombineMode(PxCombineMode::eMIN);
        wheel_guard_material = params.physics->createMaterial(0.0f, 0.0f, 0.0f);
        if (!wheel_guard_material)
        {
            material->release();
            material = nullptr;
            return false;
        }
        wheel_guard_material->setFrictionCombineMode(PxCombineMode::eMIN);
        wheel_guard_material->setRestitutionCombineMode(PxCombineMode::eMIN);

        // spawn near spring equilibrium with sweep clearance to avoid initial overlap
        float front_mass_per_wheel = chassis_mass() * get_weight_distribution_front() * 0.5f;
        float front_omega = 2.0f * PxPi * spec.front_spring_freq;
        float front_stiffness = front_mass_per_wheel * front_omega * front_omega;
        float expected_sag = PxClamp((front_mass_per_wheel * 9.81f) / front_stiffness, 0.0f, cfg.suspension_travel * 0.8f);
        float avg_wheel_r = (cfg.front_wheel_radius + cfg.rear_wheel_radius) * 0.5f;
        float spawn_y = avg_wheel_r + cfg.suspension_height - expected_sag + 0.02f;

        body = params.physics->createRigidDynamic(PxTransform(PxVec3(0, spawn_y, 0)));
        if (!body)
        {
            material->release();
            material = nullptr;
            wheel_guard_material->release();
            wheel_guard_material = nullptr;
            return false;
        }

        // attach chassis shape
        if (params.chassis_mesh)
        {
            PxConvexMeshGeometry geometry(params.chassis_mesh);
            PxShape* shape = params.physics->createShape(geometry, *material);
            if (shape)
            {
                shape->setFlag(PxShapeFlag::eVISUALIZATION, true);
                body->attachShape(*shape);
                shape->release();
            }
        }
        else
        {
            PxShape* chassis = params.physics->createShape(
                PxBoxGeometry(cfg.width * 0.5f, cfg.height * 0.5f, cfg.length * 0.5f),
                *material
            );
            if (chassis)
            {
                body->attachShape(*chassis);
                chassis->release();
            }
        }

        PxVec3 com(spec.center_of_mass_x, spec.center_of_mass_y, spec.center_of_mass_z);
        PxRigidBodyExt::setMassAndUpdateInertia(*body, chassis_mass(), &com);
        body->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, false);
        body->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, multibody_enabled);
        body->setLinearDamping(spec.linear_damping);
        body->setAngularDamping(spec.angular_damping);

        params.scene->addActor(*body);

        if (!params.vertices.empty())
        {
            compute_aero_from_shape(params.vertices);
        }

        if (multibody_enabled && !rebuild_wheel_sweep_mesh())
        {
            SP_LOG_ERROR("failed to create wheel contact geometry");
            destroy();
            return false;
        }
        if (multibody_enabled && !create_multibody(params.physics, params.scene))
        {
            SP_LOG_ERROR("failed to create car suspension assembly");
            destroy();
            return false;
        }

        SP_LOG_INFO("car setup complete: mass=%.0f kg", cfg.mass);
        return true;
    }


    inline bool set_chassis(PxConvexMesh* mesh, const std::vector<PxVec3>& vertices, PxPhysics* physics)
    {
        if (!body || !physics)
        {
            return false;
        }

        PxU32 shape_count = body->getNbShapes();
        if (shape_count > 0)
        {
            std::vector<PxShape*> shapes(shape_count);
            body->getShapes(shapes.data(), shape_count);
            for (PxShape* shape : shapes)
                body->detachShape(*shape);
        }

        if (mesh && material)
        {
            PxConvexMeshGeometry geometry(mesh);
            PxShape* shape = physics->createShape(geometry, *material);
            if (shape)
            {
                shape->setFlag(PxShapeFlag::eVISUALIZATION, true);
                body->attachShape(*shape);
                shape->release();
            }
        }

        PxVec3 com(spec.center_of_mass_x, spec.center_of_mass_y, spec.center_of_mass_z);
        PxRigidBodyExt::setMassAndUpdateInertia(*body, chassis_mass(), &com);
        if (multibody.initialized)
        {
            update_assembled_center_of_mass();
        }

        if (!vertices.empty())
        {
            compute_aero_from_shape(vertices);
        }

        return true;
    }


    inline void update_mass_properties()
    {
        if (!body)
        {
            return;
        }

        PxVec3 com(spec.center_of_mass_x, spec.center_of_mass_y, spec.center_of_mass_z);
        PxRigidBodyExt::setMassAndUpdateInertia(*body, chassis_mass(), &com);
        if (multibody.initialized)
        {
            update_assembled_center_of_mass();
        }

        SP_LOG_INFO("car center of mass set to (%.2f, %.2f, %.2f)", com.x, com.y, com.z);
    }


    inline void apply_car_spec(const car_preset& preset, bool set_as_base)
    {
        spec = preset;
        if (set_as_base)
        {
            base_spec = spec;
        }
        apply_preset_geometry(spec);
        compute_constants();
        update_mass_properties();
    }


    inline bool rebuild_vehicle_geometry()
    {
        compute_constants();
        update_mass_properties();
        if (!rebuild_wheel_sweep_mesh())
        {
            return false;
        }
        return !multibody.initialized || rebuild_multibody();
    }


    inline void reset_drivetrain_transients()
    {
        engine_rpm = spec.engine_idle_rpm;
        engine_rotation = 0.0f;
        current_gear = (spec.gear_count > 2) ? 2 : 1;
        shift_timer = 0.0f;
        is_shifting = false;
        clutch = 0.0f;
        shift_cooldown = 0.0f;
        last_shift_direction = 0;
        previous_automatic_throttle = 0.0f;
        boost_pressure = 0.0f;
        motor_torque = 0.0f;
        engine_output_torque = 0.0f;
        axle_drive_torque = 0.0f;
        rev_limiter_active = false;
        downshift_blip_timer = 0.0f;
        driveshaft_twist = 0.0f;
        driveshaft_torque = 0.0f;
        gearbox_input_angular_velocity = 0.0f;
        tc_reduction = 0.0f;
        tc_active = false;
        abs_phase = 0.0f;
        for (int i = 0; i < wheel_count; i++)
        {
            abs_active[i] = false;
        }
        longitudinal_accel = 0.0f;
        lateral_accel = 0.0f;
        prev_velocity = PxVec3(0);
        vehicle_sleep_timer = 0.0f;
        vehicle_sleeping = false;
        drs_active = false;
        engine_brake_torque = 0.0f;
    }


    inline void reset_wheel_thermals()
    {
        for (int i = 0; i < wheel_count; i++)
        {
            wheels[i].brake_temp = PxMax(spec.brake_ambient_temp, 0.0f);
            wheels[i].wear = 0.0f;
            wheels[i].thermal.surface[0] = PxMax(spec.tire_ambient_temp, 0.0f);
            wheels[i].thermal.surface[1] = PxMax(spec.tire_ambient_temp, 0.0f);
            wheels[i].thermal.surface[2] = PxMax(spec.tire_ambient_temp, 0.0f);
            wheels[i].thermal.core = PxMax(spec.tire_ambient_temp, 0.0f);
            wheels[i].effective_radius = cfg.wheel_radius_for(i);
            wheels[i].dynamic_camber = 0.0f;
            wheels[i].dynamic_toe = 0.0f;
            wheels[i].bump_steer = 0.0f;
            wheels[i].motion_ratio = 1.0f;
            wheels[i].condition_grip = 1.0f;
            wheels[i].condition_stiffness = 1.0f;
            wheels[i].condition_relaxation = 1.0f;
            wheels[i].temperature_grip = 1.0f;
            wheels[i].wear_grip = 1.0f;
            wheels[i].brake_efficiency = 1.0f;
        }
    }


    // runtime preset swaps rebuild all geometry and reset transient state
    inline void load_car(const car_preset& new_spec)
    {
        active_upgrades previous_upgrades = upgrades;
        car_preset previous_base = base_spec;
        car_preset previous_spec = spec;
        config previous_config = cfg;
        upgrades = active_upgrades{};
        apply_car_spec(new_spec, true);

        if (!rebuild_vehicle_geometry())
        {
            SP_LOG_ERROR("failed to rebuild suspension for car preset");
            upgrades = previous_upgrades;
            base_spec = previous_base;
            spec = previous_spec;
            cfg = previous_config;
            compute_constants();
            update_mass_properties();
            rebuild_wheel_sweep_mesh();
            return;
        }
        reset_drivetrain_transients();
        prev_velocity = body ? body->getLinearVelocity() : PxVec3(0.0f);
        reset_wheel_thermals();

        SP_LOG_INFO("loaded car preset: %s (mass=%.0f kg, wheelbase=%.3f m, track f/r=%.3f/%.3f m, drivetrain=%s)",
            new_spec.name ? new_spec.name : "?",
            cfg.mass, cfg.wheelbase, cfg.track_front, cfg.track_rear,
            new_spec.drivetrain_type == 0 ? "rwd" : new_spec.drivetrain_type == 1 ? "fwd" : "awd");
    }


    inline car_preset make_upgraded_spec(const car_preset& base, const active_upgrades& ups)
    {
        car_preset p = base;
        if (ups.engine > 0 && base.engine_stage_max > 0)
        {
            float m = 1.0f + 0.05f * ups.engine;
            p.engine_peak_torque *= m;
            if (ups.engine >= 2)
            {
                p.engine_redline_rpm += 250.0f * (ups.engine - 1);
            }
            if (ups.engine >= 3)
            {
                p.engine_max_rpm += 300.0f;
            }
        }
        if (ups.suspension > 0 && base.suspension_stage_max > 0)
        {
            float s = 1.0f + 0.08f * ups.suspension;
            p.front_spring_freq *= s;
            p.rear_spring_freq *= s;
            p.front_arb_stiffness *= s;
            p.rear_arb_stiffness *= s;
        }
        if (ups.tires > 0 && base.tires_stage_max > 0)
        {
            p.tire_friction += 0.05f * ups.tires;
            p.tire_optimal_temp += 5.0f * ups.tires;
            p.tire_vertical_stiffness *= (1.0f + 0.05f * ups.tires);
        }
        if (ups.brakes > 0 && base.brakes_stage_max > 0)
        {
            p.brake_force *= (1.0f + 0.08f * ups.brakes);
            p.brake_cooling_airflow *= (1.0f + 0.10f * ups.brakes);
            p.abs_load_sensitivity *= (1.0f + 0.1f * ups.brakes);
        }
        if (ups.aero > 0 && base.aero_stage_max > 0)
        {
            float d = 0.10f * ups.aero;
            p.lift_coeff_front -= d;
            p.lift_coeff_rear -= d * 1.2f;
        }
        if (ups.weight > 0 && base.weight_stage_max > 0)
        {
            float wm = 1.0f - 0.015f * ups.weight;
            p.mass *= wm;
        }

        p.mass = PxMax(p.mass, 200.0f);
        p.engine_peak_torque = PxMax(p.engine_peak_torque, 10.0f);
        p.engine_redline_rpm = PxMax(p.engine_redline_rpm, p.engine_idle_rpm + 100.0f);
        p.tire_friction = PxMax(p.tire_friction, 0.1f);

        return p;
    }


    inline void clamp_upgrade_stage(int& stage, int max_stage)
    {
        if (stage > max_stage)
        {
            stage = max_stage;
        }
        if (stage < 0)
        {
            stage = 0;
        }
    }


    inline void reapply_upgrades()
    {
        car_preset previous_spec = spec;
        config previous_config = cfg;
        clamp_upgrade_stage(upgrades.engine, base_spec.engine_stage_max);
        clamp_upgrade_stage(upgrades.suspension, base_spec.suspension_stage_max);
        clamp_upgrade_stage(upgrades.tires, base_spec.tires_stage_max);
        clamp_upgrade_stage(upgrades.brakes, base_spec.brakes_stage_max);
        clamp_upgrade_stage(upgrades.aero, base_spec.aero_stage_max);
        clamp_upgrade_stage(upgrades.weight, base_spec.weight_stage_max);

        bool save_abs = spec.abs_enabled;
        bool save_tc = spec.tc_enabled;
        bool save_manual = spec.manual_transmission;
        bool save_turbo = spec.turbo_enabled;
        bool save_drs = spec.drs_enabled;
        int save_diff = spec.diff_type;
        assist_settings save_assists = spec.assists;

        float save_abs_th = spec.abs_slip_threshold;
        float save_abs_release = spec.abs_release_rate;
        float save_abs_pulse = spec.abs_pulse_frequency;

        float save_tc_th = spec.tc_slip_threshold;
        float save_tc_pwr = spec.tc_power_reduction;
        float save_tc_rate = spec.tc_response_rate;

        float save_bst_max = spec.boost_max_pressure;
        float save_bst_spool = spec.boost_spool_rate;
        float save_bst_waste = spec.boost_wastegate_rpm;
        float save_bst_torq = spec.boost_torque_mult;
        float save_bst_min = spec.boost_min_rpm;

        car_preset eff = make_upgraded_spec(base_spec, upgrades);
        apply_car_spec(eff, false);

        spec.abs_enabled = save_abs;
        spec.tc_enabled = save_tc;
        spec.manual_transmission = save_manual;
        spec.turbo_enabled = save_turbo;
        spec.drs_enabled = save_drs;
        spec.diff_type = save_diff;
        spec.assists = save_assists;

        spec.abs_slip_threshold = save_abs_th;
        spec.abs_release_rate = save_abs_release;
        spec.abs_pulse_frequency = save_abs_pulse;

        spec.tc_slip_threshold = save_tc_th;
        spec.tc_power_reduction = save_tc_pwr;
        spec.tc_response_rate = save_tc_rate;

        spec.boost_max_pressure = save_bst_max;
        spec.boost_spool_rate = save_bst_spool;
        spec.boost_wastegate_rpm = save_bst_waste;
        spec.boost_torque_mult = save_bst_torq;
        spec.boost_min_rpm = save_bst_min;
        if (!rebuild_vehicle_geometry())
        {
            spec = previous_spec;
            cfg = previous_config;
            compute_constants();
            update_mass_properties();
            rebuild_wheel_sweep_mesh();
            SP_LOG_ERROR("failed to rebuild vehicle upgrades");
        }
    }


    inline void reset_upgrades()
    {
        upgrades = active_upgrades{};
        reapply_upgrades();
    }


    inline void set_center_of_mass(float x, float y, float z)
    {
        spec.center_of_mass_x = x;
        spec.center_of_mass_y = y;
        spec.center_of_mass_z = z;
        compute_constants();
        update_mass_properties();
    }


    inline void set_center_of_mass_x(float x) { set_center_of_mass(x, spec.center_of_mass_y, spec.center_of_mass_z); }

    inline void set_center_of_mass_y(float y) { set_center_of_mass(spec.center_of_mass_x, y, spec.center_of_mass_z); }

    inline void set_center_of_mass_z(float z) { set_center_of_mass(spec.center_of_mass_x, spec.center_of_mass_y, z); }


    inline float get_center_of_mass_x() { return spec.center_of_mass_x; }

    inline float get_center_of_mass_y() { return spec.center_of_mass_y; }

    inline float get_center_of_mass_z() { return spec.center_of_mass_z; }


    inline float get_frontal_area()     { return spec.frontal_area; }

    inline float get_side_area()        { return spec.side_area; }

    inline float get_drag_coeff()       { return spec.drag_coeff; }

    inline float get_lift_coeff_front() { return spec.lift_coeff_front; }

    inline float get_lift_coeff_rear()  { return spec.lift_coeff_rear; }


    inline void set_frontal_area(float area)   { spec.frontal_area = area; }

    inline void set_side_area(float area)      { spec.side_area = area; }

    inline void set_drag_coeff(float cd)       { spec.drag_coeff = cd; }

    inline void set_lift_coeff_front(float cl) { spec.lift_coeff_front = cl; }

    inline void set_lift_coeff_rear(float cl)  { spec.lift_coeff_rear = cl; }


    inline void  set_ground_effect_enabled(bool enabled)  { spec.ground_effect_enabled = enabled; }

    inline bool  get_ground_effect_enabled()              { return spec.ground_effect_enabled; }

    inline void  set_ground_effect_multiplier(float mult) { spec.ground_effect_multiplier = mult; }

    inline float get_ground_effect_multiplier()           { return spec.ground_effect_multiplier; }


    inline void set_throttle(float v)  { input_target.throttle  = PxClamp(v, 0.0f, 1.0f); }

    inline void set_brake(float v)     { input_target.brake     = PxClamp(v, 0.0f, 1.0f); }

    inline void set_steering(float v)  { input_target.steering  = PxClamp(v, -1.0f, 1.0f); }

    inline void set_handbrake(float v) { input_target.handbrake = PxClamp(v, 0.0f, 1.0f); }


    inline void update_input(float dt)
    {
        float steering_target = get_assisted_steering_target(input_target.steering);
        float diff = steering_target - input.steering;
        float max_change = spec.steering_rate * dt;
        input.steering = (fabsf(diff) <= max_change) ? steering_target : input.steering + ((diff > 0) ? max_change : -max_change);

        // pedals smooth application but release immediately
        input.throttle = (input_target.throttle < input.throttle) ? input_target.throttle
            : lerp(input.throttle, input_target.throttle, exp_decay(spec.throttle_smoothing, dt));
        input.brake = (input_target.brake < input.brake) ? input_target.brake
            : lerp(input.brake, input_target.brake, exp_decay(spec.brake_smoothing, dt));

        input.handbrake = input_target.handbrake;
    }


    inline void tick_low_quality(float dt)
    {
        update_input(dt);
        PxTransform pose = body->getGlobalPose();
        PxVec3 forward = pose.q.rotate(PxVec3(0.0f, 0.0f, 1.0f));
        PxVec3 right = pose.q.rotate(PxVec3(1.0f, 0.0f, 0.0f));
        PxVec3 up = pose.q.rotate(PxVec3(0.0f, 1.0f, 0.0f));
        PxVec3 velocity = body->getLinearVelocity();
        float mass = body->getMass();
        float forward_speed = velocity.dot(forward);
        float lateral_speed = velocity.dot(right);
        float drive_acceleration = input.throttle * 6.0f;
        float brake = PxMax(input.brake, input.handbrake);
        if (brake > 0.0f)
        {
            drive_acceleration -= fabsf(forward_speed) > 0.1f ? copysignf(brake * 10.0f, forward_speed) : drive_acceleration;
        }
        if (fabsf(forward_speed) > 28.0f && drive_acceleration * forward_speed > 0.0f)
        {
            drive_acceleration = 0.0f;
        }

        body->addForce(forward * (drive_acceleration * mass), PxForceMode::eFORCE);
        body->addForce(-right * (lateral_speed * mass * 4.0f), PxForceMode::eFORCE);
        body->addForce(-velocity * (mass * 0.08f), PxForceMode::eFORCE);

        PxVec3 angular_velocity = body->getAngularVelocity();
        float steering_speed = PxMin(fabsf(forward_speed) * 0.12f, 1.4f);
        float steering_direction = forward_speed >= 0.0f ? 1.0f : -1.0f;
        float target_yaw_speed = input.steering * steering_speed * steering_direction;
        float yaw_speed = angular_velocity.dot(up);
        body->addTorque(up * ((target_yaw_speed - yaw_speed) * mass * 3.0f), PxForceMode::eFORCE);

        PxVec3 upright_axis = up.cross(PxVec3(0.0f, 1.0f, 0.0f));
        body->addTorque(upright_axis * (mass * 18.0f), PxForceMode::eFORCE);
        body->addTorque(-(angular_velocity - up * yaw_speed) * (mass * 2.0f), PxForceMode::eFORCE);

        if (input.throttle > spec.input_deadzone || input.brake > spec.input_deadzone || fabsf(input.steering) > spec.input_deadzone)
        {
            body->wakeUp();
        }
        for (int i = 0; i < wheel_count; i++)
        {
            float radius = PxMax(cfg.wheel_radius_for(i), 0.05f);
            wheels[i].angular_velocity = forward_speed / radius;
            wheels[i].rotation = fmodf(wheels[i].rotation + wheels[i].angular_velocity * dt, PxPi * 2.0f);
        }
    }


    inline void tick(float dt)
    {
        if (!body)
        {
            return;
        }
        if (!multibody_enabled)
        {
            tick_low_quality(dt);
            return;
        }

        // sanitize wheel state before it contaminates physx actors
        for (int i = 0; i < wheel_count; i++)
        {
            if (sanitize_wheel_state(i))
            {
                SP_LOG_WARNING("car::tick: scrubbed non finite state from wheel %d before tick", i);
            }
        }

        // reset invalid body state before it contaminates every wheel
        {
            PxTransform pose = body->getGlobalPose();
            PxVec3 lin = body->getLinearVelocity();
            PxVec3 ang = body->getAngularVelocity();
            bool pose_bad = !is_finite_vec(pose.p) || !std::isfinite(pose.q.x) || !std::isfinite(pose.q.y) || !std::isfinite(pose.q.z) || !std::isfinite(pose.q.w);
            bool lin_bad  = !is_finite_vec(lin);
            bool ang_bad  = !is_finite_vec(ang);
            if (pose_bad)
            {
                SP_LOG_WARNING("car::tick: body pose is non finite, resetting to identity");
                body->setGlobalPose(PxTransform(PxVec3(0, 1.0f, 0)));
            }
            if (lin_bad)
            {
                SP_LOG_WARNING("car::tick: body linear velocity is non finite, zeroing");
                body->setLinearVelocity(PxVec3(0));
            }
            if (ang_bad)
            {
                SP_LOG_WARNING("car::tick: body angular velocity is non finite, zeroing");
                body->setAngularVelocity(PxVec3(0));
            }
            if (!std::isfinite(prev_velocity.x) || !std::isfinite(prev_velocity.y) || !std::isfinite(prev_velocity.z))
            {
                prev_velocity = PxVec3(0);
            }
        }

        // caller supplies the fixed step duration
        tick_validation(dt);
        update_input(dt);
        update_handbrake_wheel_locks();
        PxScene* scene = body->getScene();
        if (!scene)
        {
            return;
        }

        bool steering_adjusting = fabsf(input_target.steering - input.steering) > 0.001f;
        bool motion_requested = input.throttle > spec.input_deadzone || (is_in_reverse() && input.brake > spec.input_deadzone) || steering_adjusting;
        if (vehicle_sleeping)
        {
            if (motion_requested || !body->isSleeping())
            {
                wake_vehicle_assembly();
            }
            else
            {
                return;
            }
        }

        PxTransform pose = body->getGlobalPose();
        PxVec3 fwd = pose.q.rotate(PxVec3(0, 0, 1));
        PxVec3 vel = body->getLinearVelocity();
        float forward_speed = vel.dot(fwd);
        float speed_kmh = vel.magnitude() * 3.6f;

        // filtered acceleration feeds telemetry and validation only
        PxVec3 right = pose.q.rotate(PxVec3(1, 0, 0));
        PxVec3 accel_vec = (vel - prev_velocity) / PxMax(dt, 0.001f);
        float raw_accel = accel_vec.dot(fwd);
        float raw_lat_accel = accel_vec.dot(right);
        constexpr float acceleration_filter_rate = 20.0f;
        longitudinal_accel = lerp(longitudinal_accel, raw_accel, exp_decay(acceleration_filter_rate, dt));
        lateral_accel      = lerp(lateral_accel, raw_lat_accel, exp_decay(acceleration_filter_rate, dt));
        prev_velocity = vel;

        // brake cooling
        float airspeed = vel.magnitude();
        for (int i = 0; i < wheel_count; i++)
        {
            float temp_above_ambient = wheels[i].brake_temp - spec.brake_ambient_temp;
            if (temp_above_ambient > 0.0f)
            {
                float h = spec.brake_cooling_base + airspeed * spec.brake_cooling_airflow;
                float cooling_power = h * temp_above_ambient;
                float temp_drop = (cooling_power / spec.brake_thermal_mass) * dt;
                wheels[i].brake_temp -= temp_drop;
                wheels[i].brake_temp = PxMax(wheels[i].brake_temp, spec.brake_ambient_temp);
            }
        }

        refresh_wheel_actor_state();
        for (int i = 0; i < wheel_count; i++)
        {
            wheels[i].net_torque = 0.0f;
            wheels[i].drive_torque = 0.0f;
            wheels[i].brake_torque = 0.0f;
        }

        update_multibody(dt);
        update_suspension(scene, dt);
        update_tire_slip_state(dt);
        apply_drivetrain(forward_speed * 3.6f, dt);
        engine_rotation = fmodf(engine_rotation + engine_rpm * PxPi * 2.0f / 60.0f * dt, PxPi * 2.0f);
        if (!std::isfinite(engine_rotation))
        {
            engine_rotation = 0.0f;
        }

        apply_tire_forces(dt);
        apply_self_aligning_torque();

        apply_aero_and_resistance();
        if (!motion_requested && vehicle_assembly_is_settled())
        {
            vehicle_sleep_timer += dt;
            if (vehicle_sleep_timer >= 0.5f)
            {
                sleep_vehicle_assembly();
            }
        }
        else
        {
            vehicle_sleep_timer = 0.0f;
        }

        if (log_telemetry)
        {
            float wheel_surface_speed = get_average_driven_angular_velocity(false) * get_driven_wheel_radius() * 3.6f;
            SP_LOG_INFO("rpm=%.0f, speed=%.0f km/h, gear=%s%s, wheel_speed=%.0f km/h, throttle=%.0f%%",
                engine_rpm, speed_kmh, get_gear_string(), is_shifting ? "(shifting)" : "",
                wheel_surface_speed, input.throttle * 100.0f);
        }

        tick_telemetry(dt, speed_kmh);
    }


    inline float get_speed_kmh()        { return body ? body->getLinearVelocity().magnitude() * 3.6f : 0.0f; }

    inline float get_throttle()         { return input.throttle; }

    inline float get_brake()            { return input.brake; }

    inline float get_steering()         { return input.steering; }


    // visual setters also repair the underlying simulation state
    inline void set_wheel_rotation(int i, float v)
    {
        if (is_valid_wheel(i))
        {
            wheels[i].rotation = std::isfinite(v) ? v : 0.0f;
        }
    }


    inline void set_wheel_angular_velocity(int i, float v)
    {
        if (is_valid_wheel(i))
        {
            float angular_velocity = std::isfinite(v) ? v : 0.0f;
            wheels[i].angular_velocity = angular_velocity;
            if (PxRigidDynamic* wheel_actor = multibody.corners[i].wheel_body)
            {
                PxVec3 wheel_axis = wheel_actor->getGlobalPose().q.rotate(PxVec3(1.0f, 0.0f, 0.0f));
                PxVec3 actor_velocity = wheel_actor->getAngularVelocity();
                wheel_actor->setAngularVelocity(actor_velocity + wheel_axis * (angular_velocity - actor_velocity.dot(wheel_axis)));
            }
        }
    }


    inline float get_handbrake()        { return input.handbrake; }

    inline float get_suspension_travel(){ return cfg.suspension_travel; }

    inline float get_wheel_compression(int i) { return is_valid_wheel(i) ? wheels[i].compression : 0.0f; }

    inline float get_wheel_slip_angle(int i) { return is_valid_wheel(i) ? wheels[i].slip_angle : 0.0f; }

    inline float get_wheel_slip_ratio(int i) { return is_valid_wheel(i) ? wheels[i].slip_ratio : 0.0f; }

    inline float get_wheel_tire_load(int i) { return is_valid_wheel(i) ? wheels[i].tire_load : 0.0f; }

    inline float get_wheel_lateral_force(int i) { return is_valid_wheel(i) ? wheels[i].lateral_force : 0.0f; }

    inline float get_wheel_longitudinal_force(int i) { return is_valid_wheel(i) ? wheels[i].longitudinal_force : 0.0f; }

    inline float get_wheel_angular_velocity(int i) { return is_valid_wheel(i) ? wheels[i].angular_velocity : 0.0f; }

    inline float get_wheel_rotation(int i) { return is_valid_wheel(i) ? wheels[i].rotation : 0.0f; }

    inline float get_wheel_effective_radius(int i) { return is_valid_wheel(i) ? wheels[i].effective_radius : 0.0f; }

    inline float get_wheel_dynamic_camber(int i) { return is_valid_wheel(i) ? wheels[i].dynamic_camber : 0.0f; }

    inline float get_wheel_dynamic_toe(int i) { return is_valid_wheel(i) ? wheels[i].dynamic_toe : 0.0f; }

    inline float get_wheel_bump_steer(int i) { return is_valid_wheel(i) ? wheels[i].bump_steer : 0.0f; }

    inline float get_wheel_motion_ratio(int i) { return is_valid_wheel(i) ? wheels[i].motion_ratio : 0.0f; }

inline float get_wheel_temperature(int i) { return is_valid_wheel(i) ? wheels[i].thermal.avg_surface() : 0.0f; }


    inline bool is_wheel_grounded(int i) { return is_valid_wheel(i) && wheels[i].grounded; }


    inline float get_wheel_suspension_force(int i)
    {
        return is_valid_wheel(i) ? spring_force[i] : 0.0f;
    }


    inline float get_axle_roll_stiffness(bool front)
    {
        int left = front ? front_left : rear_left;
        int right = front ? front_right : rear_right;
        float track = front ? cfg.track_front : cfg.track_rear;
        float anti_roll = front ? spec.front_arb_stiffness : spec.rear_arb_stiffness;
        float left_wheel_rate = spring_stiffness[left] * wheels[left].motion_ratio * wheels[left].motion_ratio;
        float right_wheel_rate = spring_stiffness[right] * wheels[right].motion_ratio * wheels[right].motion_ratio;
        return ((left_wheel_rate + right_wheel_rate) * 0.5f + anti_roll) * track * track * 0.5f;
    }


    inline float get_wheel_temp_grip_factor(int i)
    {
        return is_valid_wheel(i) ? wheels[i].condition_grip : 1.0f;
    }


    inline float get_wheel_surface_temp(int i, int zone)
    {
        return (is_valid_wheel(i) && zone >= 0 && zone < 3) ? wheels[i].thermal.surface[zone] : 0.0f;
    }


    inline float get_wheel_core_temp(int i)
    {
        return is_valid_wheel(i) ? wheels[i].thermal.core : 0.0f;
    }


    inline float get_tire_pressure()         { return spec.tire_pressure; }

    inline float get_tire_pressure_optimal() { return spec.tire_pressure_optimal; }


    inline float get_chassis_visual_offset_y()
    {
        const float offset = 0.1f;
        return -(cfg.height * 0.5f + cfg.suspension_height) + offset;
    }


    inline void set_abs_enabled(bool enabled) { spec.abs_enabled = enabled; }

    inline bool get_abs_enabled()             { return spec.abs_enabled; }

    inline bool is_abs_active(int i)          { return is_valid_wheel(i) && abs_active[i]; }

    inline bool is_abs_active_any()
    {
        for (int i = 0; i < wheel_count; i++)
        {
            if (abs_active[i])
            {
                return true;
            }
        }
        return false;
    }

    inline float get_abs_phase()              { return abs_phase; }


    inline void  set_tc_enabled(bool enabled) { spec.tc_enabled = enabled; }

    inline bool  get_tc_enabled()             { return spec.tc_enabled; }

    inline bool  is_tc_active()               { return tc_active; }

    inline float get_tc_reduction()           { return tc_reduction; }


    inline void set_manual_transmission(bool enabled) { spec.manual_transmission = enabled; }

    inline bool get_manual_transmission()             { return spec.manual_transmission; }


    inline void begin_shift(int direction)
    {
        is_shifting = true;
        shift_timer = spec.shift_time;
        last_shift_direction = direction;
    }


    inline void shift_up()
    {
        if (!spec.manual_transmission || is_shifting || current_gear >= spec.gear_count - 1)
        {
            return;
        }
        current_gear = (current_gear == 0) ? 1 : current_gear + 1;
        begin_shift(1);
    }


    inline void shift_down()
    {
        if (!spec.manual_transmission || is_shifting || current_gear <= 0)
        {
            return;
        }
        if (current_gear > 2)
        {
            downshift_blip_timer = spec.downshift_blip_duration;
        }
        current_gear = (current_gear == 1) ? 0 : current_gear - 1;
        begin_shift(-1);
    }


    inline void shift_to_neutral()
    {
        if (!spec.manual_transmission || is_shifting)
        {
            return;
        }
        current_gear = 1;
        begin_shift(0);
    }


    inline int         get_current_gear()          { return current_gear; }

    inline float       get_current_engine_rpm()    { return engine_rpm; }

    inline bool        get_is_shifting()           { return is_shifting; }

    inline float       get_clutch()                { return clutch; }

    inline float       get_engine_torque_current() { return get_engine_torque(engine_rpm) * (1.0f + boost_pressure * spec.boost_torque_mult); }

    inline float       get_motor_torque()          { return motor_torque; }

    inline float       get_driveshaft_twist()      { return driveshaft_twist; }

    inline float       get_driveshaft_torque()     { return driveshaft_torque; }

    inline float       get_gearbox_input_angular_velocity() { return gearbox_input_angular_velocity; }

    inline float       get_motor_power_kw()        { float w = motor_torque * engine_rpm * (2.0f * 3.14159265f / 60.0f); return w / 1000.0f; }

    inline float       get_redline_rpm()           { return spec.engine_redline_rpm; }

    inline float       get_max_rpm()               { return spec.engine_max_rpm; }

    inline float       get_idle_rpm()              { return spec.engine_idle_rpm; }


    inline void  set_turbo_enabled(bool enabled)
    {
        spec.turbo_enabled = enabled;
        // presets without turbo data receive stable defaults when enabled at runtime
        if (enabled && spec.boost_max_pressure <= 0.0f)
        {
            spec.boost_max_pressure  = 1.0f;
            spec.boost_spool_rate    = 3.0f;
            spec.boost_torque_mult   = 0.35f;
            spec.boost_min_rpm       = 2000.0f;
            spec.boost_wastegate_rpm = spec.engine_redline_rpm > 0.0f ? spec.engine_redline_rpm - 500.0f : 6500.0f;
        }
    }

    inline bool  get_turbo_enabled()             { return spec.turbo_enabled; }

    inline float get_boost_pressure()            { return boost_pressure; }

    inline float get_boost_max_pressure()        { return spec.boost_max_pressure; }


    inline void set_drs_enabled(bool enabled) { spec.drs_enabled = enabled; }

    inline bool get_drs_enabled()             { return spec.drs_enabled; }

    inline void set_drs_active(bool active)   { drs_active = active; }

    inline bool get_drs_active()              { return drs_active; }


    inline void set_diff_type(int type)
    {
        int new_type = PxClamp(type, 0, 2);
        if (new_type == spec.diff_type)
        {
            return;
        }
        int previous_type = spec.diff_type;
        spec.diff_type = new_type;
        if (multibody.initialized && !rebuild_multibody())
        {
            spec.diff_type = previous_type;
            SP_LOG_ERROR("failed to rebuild physical differential");
        }
    }

    inline int  get_diff_type()               { return spec.diff_type; }

    inline const char* get_diff_type_name()
    {
        static constexpr const char* names[] = { "Open", "Locked", "LSD" };
        return (spec.diff_type >= 0 && spec.diff_type <= 2) ? names[spec.diff_type] : "?";
    }


    inline float get_wheel_wear(int i)              { return is_valid_wheel(i) ? wheels[i].wear : 0.0f; }

    inline void reset_tire_wear()
    {
        for (int i = 0; i < wheel_count; i++)
        {
            wheels[i].wear = 0.0f;
        }
    }

    inline float get_wheel_wear_grip_factor(int i)  { return is_valid_wheel(i) ? wheels[i].wear_grip : 1.0f; }


    inline float get_wheel_brake_temp(int i)       { return is_valid_wheel(i) ? wheels[i].brake_temp : 0.0f; }

    inline float get_wheel_brake_efficiency(int i) { return is_valid_wheel(i) ? wheels[i].brake_efficiency : 1.0f; }


    inline void set_wheel_surface(int i, surface_type surface)
    {
        if (is_valid_wheel(i))
        {
            wheels[i].contact_surface = surface;
        }
    }

    inline surface_type get_wheel_surface(int i) { return is_valid_wheel(i) ? wheels[i].contact_surface : surface_asphalt; }

    inline const char* get_surface_name(surface_type surface)
    {
        static constexpr const char* names[] = { "Asphalt", "Concrete", "Wet", "Gravel", "Grass", "Ice" };
        return (surface >= 0 && surface < surface_count) ? names[surface] : "Unknown";
    }


    inline float get_front_camber() { return spec.front_camber; }

    inline float get_rear_camber()  { return spec.rear_camber; }

    inline float get_front_toe()    { return spec.front_toe; }

    inline float get_rear_toe()     { return spec.rear_toe; }


    inline void set_wheel_offset(int wheel, float x, float z)
    {
        if (wheel >= 0 && wheel < wheel_count)
        {
            wheel_offsets[wheel].x = x;
            wheel_offsets[wheel].z = z;
            refresh_geometry_cache();
        }
    }


    inline PxVec3 get_wheel_offset(int wheel)
    {
        if (wheel >= 0 && wheel < wheel_count)
        {
            return wheel_offsets[wheel];
        }
        return PxVec3(0);
    }


    inline const aero_debug_data& get_aero_debug() { return aero_debug; }

    inline const shape_2d& get_shape_data() { return shape_data_ref(); }


    inline void get_debug_sweep(int wheel, PxVec3& origin, PxVec3& hit_point, bool& hit)
    {
        if (wheel >= 0 && wheel < wheel_count)
        {
            origin    = debug_sweep[wheel].origin;
            hit_point = debug_sweep[wheel].hit_point;
            hit       = debug_sweep[wheel].hit;
        }
    }


    inline void get_debug_suspension(int wheel, PxVec3& top, PxVec3& bottom)
    {
        if (wheel >= 0 && wheel < wheel_count)
        {
            top    = debug_suspension_top[wheel];
            bottom = debug_suspension_bottom[wheel];
        }
    }


    inline float get_wheel_radius()    { return (cfg.front_wheel_radius + cfg.rear_wheel_radius) * 0.5f; }

    inline float get_wheel_width()     { return (cfg.front_wheel_width + cfg.rear_wheel_width) * 0.5f; }

    inline PxTransform get_body_pose() { return body ? body->getGlobalPose() : PxTransform(PxIdentity); }
