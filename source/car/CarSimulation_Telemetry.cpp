#include "pch.h"
#include "CarSimulation.h"

namespace car
{
    void Simulation::set_telemetry_path(const std::string& path)
    { close_telemetry(); telemetry_path = path.empty() ? "car_telemetry.csv" : path; }


    void Simulation::close_telemetry()
    {
                if (file)
                {
                    fclose(file);
                    file = nullptr;
                }
                frame_counter = 0;
                elapsed_time  = 0.0f;
            }


    void Simulation::flush_telemetry()
    {
                if (file)
                {
                    fflush(file);
                }
            }


    std::string Simulation::get_telemetry_path() const
    {
                char abs_path[1024] = {};
                if (_fullpath(abs_path, telemetry_path.c_str(), sizeof(abs_path)))
                {
                    return abs_path;
                }
                return telemetry_path;
            }


    bool Simulation::reopen_telemetry_append()
    {
                if (file)
                {
                    return true;
                }
                fopen_s(&file, telemetry_path.c_str(), "a");
                return file != nullptr;
            }


    bool Simulation::snapshot_telemetry_tail(int max_rows, std::string& out_text, std::string& out_path, int& out_total_lines)
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


    bool Simulation::open_telemetry_if_needed()
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
                if (_fullpath(abs_path, telemetry_path.c_str(), sizeof(abs_path)))
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
                    // contact_ny is the contact normal y, which is how tilted and degenerate ground hits get spotted
                    "fl_comp,fr_comp,rl_comp,rr_comp,"
                    "fl_sweep_dist,fr_sweep_dist,rl_sweep_dist,rr_sweep_dist,"
                    "fl_spring_force,fr_spring_force,rl_spring_force,rr_spring_force,"
                    "fl_contact_ny,fr_contact_ny,rl_contact_ny,rr_contact_ny,"
                    // upgrades levels and key handling state that changes at runtime
                    "eng_up,susp_up,tire_up,brake_up,aero_up,weight_up,exh_up,int_up,turbo_up,"
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
                    "active_gear_ratio,shift_timer,shift_cooldown,last_shift_direction,engine_rotation,gearbox_input_angular_velocity,boost_pressure,motor_torque,engine_output_torque,axle_drive_torque,driveshaft_twist,driveshaft_torque,rev_limiter,downshift_blip_timer,abs_phase,vehicle_sleeping,vehicle_sleep_timer,drs_active,burnout,"
                    "assist_engine_scale,fl_assist_brake_scale,fr_assist_brake_scale,rl_assist_brake_scale,rr_assist_brake_scale,"
                    "aero_valid,aero_ride_height,aero_yaw_angle,aero_ground_effect,aero_drag_x,aero_drag_y,aero_drag_z,aero_front_downforce_x,aero_front_downforce_y,aero_front_downforce_z,aero_rear_downforce_x,aero_rear_downforce_y,aero_rear_downforce_z,aero_side_force_x,aero_side_force_y,aero_side_force_z,"
                    "fl_rotation,fl_drive_torque,fl_brake_torque,fl_comp_velocity,fl_surface,fl_contact_x,fl_contact_y,fl_contact_z,fl_contact_nx,fl_contact_nz,fl_dynamic_toe,fl_bump_steer,fl_motion_ratio,fl_shock_length,fl_shock_rest_length,fl_shock_velocity,fl_temp_inside,fl_temp_middle,fl_temp_outside,fl_condition_grip,fl_condition_stiffness,fl_condition_relaxation,fl_wheel_moi,fl_spring_stiffness,fl_spring_damping,"
                    "fr_rotation,fr_drive_torque,fr_brake_torque,fr_comp_velocity,fr_surface,fr_contact_x,fr_contact_y,fr_contact_z,fr_contact_nx,fr_contact_nz,fr_dynamic_toe,fr_bump_steer,fr_motion_ratio,fr_shock_length,fr_shock_rest_length,fr_shock_velocity,fr_temp_inside,fr_temp_middle,fr_temp_outside,fr_condition_grip,fr_condition_stiffness,fr_condition_relaxation,fr_wheel_moi,fr_spring_stiffness,fr_spring_damping,"
                    "rl_rotation,rl_drive_torque,rl_brake_torque,rl_comp_velocity,rl_surface,rl_contact_x,rl_contact_y,rl_contact_z,rl_contact_nx,rl_contact_nz,rl_dynamic_toe,rl_bump_steer,rl_motion_ratio,rl_shock_length,rl_shock_rest_length,rl_shock_velocity,rl_temp_inside,rl_temp_middle,rl_temp_outside,rl_condition_grip,rl_condition_stiffness,rl_condition_relaxation,rl_wheel_moi,rl_spring_stiffness,rl_spring_damping,"
                    "rr_rotation,rr_drive_torque,rr_brake_torque,rr_comp_velocity,rr_surface,rr_contact_x,rr_contact_y,rr_contact_z,rr_contact_nx,rr_contact_nz,rr_dynamic_toe,rr_bump_steer,rr_motion_ratio,rr_shock_length,rr_shock_rest_length,rr_shock_velocity,rr_temp_inside,rr_temp_middle,rr_temp_outside,rr_condition_grip,rr_condition_stiffness,rr_condition_relaxation,rr_wheel_moi,rr_spring_stiffness,rr_spring_damping,"
                    "fl_hub_x,fl_hub_y,fl_hub_z,fl_hub_vx,fl_hub_vy,fl_hub_vz,fl_hub_wx,fl_hub_wy,fl_hub_wz,fr_hub_x,fr_hub_y,fr_hub_z,fr_hub_vx,fr_hub_vy,fr_hub_vz,fr_hub_wx,fr_hub_wy,fr_hub_wz,rl_hub_x,rl_hub_y,rl_hub_z,rl_hub_vx,rl_hub_vy,rl_hub_vz,rl_hub_wx,rl_hub_wy,rl_hub_wz,rr_hub_x,rr_hub_y,rr_hub_z,rr_hub_vx,rr_hub_vy,rr_hub_vz,rr_hub_wx,rr_hub_wy,rr_hub_wz");
                fprintf(file, ",simulation_version,calibration_id,event_flags,reset_count,distance_m,contact_impulse_x,contact_impulse_y,contact_impulse_z,assembled_ixx,assembled_iyy,assembled_izz,assembled_ixy,assembled_ixz,assembled_iyz,battery_soc,battery_temp,battery_power_w,battery_loss_w,engine_running,clutch_heat_j,gearbox_loss_j");
                for (const char* prefix : {"fl", "fr", "rl", "rr"}) fprintf(file, ",%s_pressure_bar,%s_damage,%s_water_depth,%s_slip_energy_j", prefix, prefix, prefix, prefix);
                fputc('\n', file);
                frame_counter = 0;
                elapsed_time  = 0.0f;
                return true;
            }


    void Simulation::write_telemetry_wheel_state(int i)
    {
                const wheel& w = wheels[i];
                fprintf(file, "%.6g,%.6g,%.6g,%.6g,%d,%.6f,%.6f,%.6f,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g", w.rotation, w.drive_torque, w.brake_torque, w.compression_velocity, static_cast<int>(w.contact_surface),
                    static_cast<double>(w.contact_point.x) + scene_origin.x, static_cast<double>(w.contact_point.y) + scene_origin.y, static_cast<double>(w.contact_point.z) + scene_origin.z,
                    w.contact_normal.x, w.contact_normal.z, w.dynamic_toe, w.bump_steer, w.motion_ratio, w.shock_length, w.shock_rest_length, w.shock_velocity, w.thermal.surface[0], w.thermal.surface[1], w.thermal.surface[2], w.condition_grip, w.condition_stiffness, w.condition_relaxation, wheel_moi[i], spring_stiffness[i], spring_damping[i]);
            }


    void Simulation::tick_telemetry(float dt, float speed_kmh)
    {
                if (!log_to_file)
                {
                    close_telemetry();
                    event_flags = 0; contact_impulse = PxVec3(0);
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
                    "%.6f,%.6f,%.6f,"
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
                    "%d,%d,%d,%d,%d,%d,%d,%d,%d,"
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
                    "%.6g,%.6g,%.6g,%d,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%d,%.6g,%.6g,%d,%.6g,%d,%d,"
                    "%.6g,%.6g,%.6g,%.6g,%.6g,"
                    "%d,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,",
                    frame_counter, elapsed_time, dt, spec.name ? spec.name : "",
                    static_cast<double>(pose.p.x) + scene_origin.x,
                    static_cast<double>(pose.p.y) + scene_origin.y,
                    static_cast<double>(pose.p.z) + scene_origin.z,
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
                    upgrades.exhaust, upgrades.intake, upgrades.turbo,
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
                    spec.gear_ratios[current_gear], shift_timer, shift_cooldown, last_shift_direction, engine_rotation, gearbox_input_angular_velocity, boost_pressure, motor_torque, engine_output_torque, axle_drive_torque, driveshaft_twist, driveshaft_torque, rev_limiter_active ? 1 : 0, downshift_blip_timer, abs_phase, vehicle_sleeping ? 1 : 0, vehicle_sleep_timer, drs_active ? 1 : 0, burnout_active ? 1 : 0,
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
                    fprintf(file, ",%.6f,%.6f,%.6f,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g",
                        static_cast<double>(wheels[i].hub_position.x) + scene_origin.x, static_cast<double>(wheels[i].hub_position.y) + scene_origin.y, static_cast<double>(wheels[i].hub_position.z) + scene_origin.z,
                        wheels[i].hub_linear_velocity.x, wheels[i].hub_linear_velocity.y, wheels[i].hub_linear_velocity.z, wheels[i].hub_angular_velocity.x, wheels[i].hub_angular_velocity.y, wheels[i].hub_angular_velocity.z);
                }
                PxMat33 inertia = get_assembled_inertia();
                fprintf(file, ",2,%s,%u,%u,%.9g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%d,%.9g,%.9g",
                    spec.calibration_id, event_flags, reset_count, distance_m, contact_impulse.x, contact_impulse.y, contact_impulse.z,
                    inertia.column0.x, inertia.column1.y, inertia.column2.z, inertia.column1.x, inertia.column2.x, inertia.column2.y,
                    battery.energy_j / (spec.battery_capacity_kwh * 3600000.0f), battery.temperature, battery.electrical_power_w, battery.loss_power_w,
                    engine_running ? 1 : 0, clutch_heat_j, gearbox_loss_j);
                for (const auto& w : wheels) fprintf(file, ",%.6g,%.6g,%.6g,%.9g", w.pressure_bar, w.damage, w.water_depth, w.dissipated_energy_j);
                event_flags = 0; contact_impulse = PxVec3(0);
                fputc('\n', file);

                if (frame_counter % 200 == 0)
                {
                    fflush(file);
                }
                frame_counter++;
            }
}
