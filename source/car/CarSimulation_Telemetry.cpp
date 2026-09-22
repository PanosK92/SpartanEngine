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

#include "pch.h"
#include "CarSimulation.h"
#include <deque>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <locale>

namespace car
{
    // A topology-aware snapshot: indices are stable until the assembly is rebuilt.
    // Never serialize addresses, and preserve invalid measurements as JSON null.
    std::string Simulation::get_physics_telemetry_json() const
    {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setprecision(9);
        auto number = [&](double value) { if (std::isfinite(value)) out << value; else out << "null"; };
        auto vec = [&](const PxVec3& v) { out << '['; number(v.x); out << ','; number(v.y); out << ','; number(v.z); out << ']'; };
        auto pose = [&](const PxTransform& p, bool world = false) {
            out << "{\"p\":["; number(double(p.p.x) + (world ? scene_origin.x : 0)); out << ',';
            number(double(p.p.y) + (world ? scene_origin.y : 0)); out << ',';
            number(double(p.p.z) + (world ? scene_origin.z : 0)); out << "],\"q\":[";
            number(p.q.x); out << ','; number(p.q.y); out << ','; number(p.q.z); out << ','; number(p.q.w); out << "]}";
        };
        auto actor_id = [&](const PxRigidActor* actor) {
            if (!actor) return -1; // world attachment
            if (actor == body) return 0;
            for (int i = 0; i < multibody.actor_count; ++i) if (multibody.actors[i] == actor) return i + 1;
            return -2; // external actor
        };
        auto joint_id = [&](const PxJoint* joint) {
            for (int i = 0; joint && i < multibody.joint_count; ++i) if (multibody.joints[i] == joint) return i;
            return -1;
        };
        out << "{\"schema\":1,\"state_phase\":\"latest_available_physx_state\",\"reaction_phase\":\"last_completed_solve\","
               "\"applied_force_phase\":\"latest_vehicle_tick\","
               "\"multibody_initialized\":" << (multibody.initialized ? "true" : "false")
            << ",\"fallback_chassis\":" << (fallback_chassis ? "true" : "false") << ",\"actors\":[";
        bool first = true;
        for (int i = -1; i < multibody.actor_count; ++i)
        {
            const PxRigidDynamic* actor = i < 0 ? body : multibody.actors[i];
            if (!actor) continue;
            if (!first) out << ','; first = false;
            out << "{\"id\":" << i + 1 << ",\"pose\":"; pose(actor->getGlobalPose(), true);
            out << ",\"linear_velocity\":"; vec(actor->getLinearVelocity());
            out << ",\"angular_velocity\":"; vec(actor->getAngularVelocity());
            out << ",\"mass\":"; number(actor->getMass());
            out << ",\"linear_damping_per_s\":"; number(actor->getLinearDamping());
            out << ",\"angular_damping_per_s\":"; number(actor->getAngularDamping());
            out << ",\"mass_frame_local\":"; pose(actor->getCMassLocalPose());
            out << ",\"inertia_mass_frame\":"; vec(actor->getMassSpaceInertiaTensor());
            PxU32 position_iterations, velocity_iterations;
            actor->getSolverIterationCounts(position_iterations, velocity_iterations);
            out << ",\"sleeping\":" << (actor->isSleeping() ? "true" : "false")
                << ",\"body_flags\":" << unsigned(actor->getRigidBodyFlags())
                << ",\"lock_flags\":" << unsigned(actor->getRigidDynamicLockFlags())
                << ",\"solver_iterations\":[" << position_iterations << ',' << velocity_iterations << "]"
                << ",\"shapes\":[";
            std::vector<PxShape*> shapes(actor->getNbShapes());
            actor->getShapes(shapes.data(), static_cast<PxU32>(shapes.size()));
            for (size_t s = 0; s < shapes.size(); ++s)
            {
                if (s) out << ',';
                const auto* shape = shapes[s];
                out << "{\"type\":" << int(shape->getGeometry().getType()) << ",\"local_pose\":"; pose(shape->getLocalPose());
                out << ",\"flags\":" << unsigned(shape->getFlags()) << ",\"contact_offset\":"; number(shape->getContactOffset());
                out << ",\"rest_offset\":"; number(shape->getRestOffset());
                const auto filter = shape->getSimulationFilterData();
                out << ",\"simulation_filter\":[" << filter.word0 << ',' << filter.word1 << ',' << filter.word2 << ',' << filter.word3 << ']';
                const auto& geometry = shape->getGeometry();
                if (geometry.getType() == PxGeometryType::eBOX) {
                    out << ",\"box_half_extents\":"; vec(static_cast<const PxBoxGeometry&>(geometry).halfExtents);
                } else if (geometry.getType() == PxGeometryType::eSPHERE) {
                    out << ",\"radius\":"; number(static_cast<const PxSphereGeometry&>(geometry).radius);
                } else if (geometry.getType() == PxGeometryType::eCAPSULE) {
                    const auto& capsule = static_cast<const PxCapsuleGeometry&>(geometry);
                    out << ",\"radius\":"; number(capsule.radius); out << ",\"half_height\":"; number(capsule.halfHeight);
                } else if (geometry.getType() == PxGeometryType::eCONVEXMESH) {
                    const auto& convex = static_cast<const PxConvexMeshGeometry&>(geometry);
                    out << ",\"mesh_scale\":"; vec(convex.scale.scale);
                    out << ",\"mesh_vertices\":" << convex.convexMesh->getNbVertices();
                }
                PxBounds3 bounds = PxBounds3::empty();
                const bool bounds_valid = PxGeometryQuery::computeGeomBounds(bounds, shape->getGeometry(), shape->getLocalPose());
                out << ",\"bounds_valid\":" << (bounds_valid ? "true" : "false");
                out << ",\"actor_local_bounds_min\":"; vec(bounds.minimum);
                out << ",\"actor_local_bounds_max\":"; vec(bounds.maximum); out << '}';
            }
            out << "]}";
        }
        out << "],\"joints\":[";
        first = true;
        for (int i = 0; i < multibody.joint_count; ++i)
        {
            const PxJoint* joint = multibody.joints[i];
            if (!joint) continue;
            if (!first) out << ','; first = false;
            PxRigidActor *a, *b; joint->getActors(a, b);
            const PxTransform local_a = joint->getLocalPose(PxJointActorIndex::eACTOR0);
            const PxTransform local_b = joint->getLocalPose(PxJointActorIndex::eACTOR1);
            const PxTransform world_a = a ? a->getGlobalPose() * local_a : local_a;
            const PxTransform world_b = b ? b->getGlobalPose() * local_b : local_b;
            out << "{\"id\":" << i << ",\"type\":\"" << joint->getConcreteTypeName() << "\",\"actors\":[" << actor_id(a) << ',' << actor_id(b) << ']';
            out << ",\"local_a\":"; pose(local_a); out << ",\"local_b\":"; pose(local_b);
            out << ",\"world_a\":"; pose(world_a, true); out << ",\"world_b\":"; pose(world_b, true);
            // Separation includes permitted motion; it is not universally a constraint error.
            out << ",\"anchor_separation_m\":"; number((world_b.p - world_a.p).magnitude());
            out << ",\"relative_pose\":"; pose(joint->getRelativeTransform());
            out << ",\"relative_linear_velocity\":"; vec(joint->getRelativeLinearVelocity());
            out << ",\"relative_angular_velocity\":"; vec(joint->getRelativeAngularVelocity());
            PxVec3 force(0), torque(0);
            if (joint->getConstraint()) joint->getConstraint()->getForce(force, torque);
            out << ",\"reaction_available\":" << (joint->getConstraint() ? "true" : "false");
            out << ",\"reaction_force_world\":"; vec(force); out << ",\"reaction_torque_world\":"; vec(torque);
            out << ",\"flags\":" << unsigned(joint->getConstraintFlags());
            if (const auto* d6 = joint->is<PxD6Joint>())
            {
                out << ",\"angular_drive_config\":" << int(d6->getAngularDriveConfig()) << ",\"motion\":[";
                for (int axis = 0; axis < 6; ++axis) { if (axis) out << ','; out << int(d6->getMotion(PxD6Axis::Enum(axis))); }
                out << "],\"drives\":[";
                for (int drive = 0; drive < PxD6Drive::eCOUNT; ++drive)
                {
                    if (drive) out << ',';
                    const auto d = d6->getDrive(PxD6Drive::Enum(drive));
                    out << '['; number(d.stiffness); out << ','; number(d.damping); out << ','; number(d.forceLimit); out << ',' << unsigned(d.flags) << ']';
                }
                out << "],\"drive_pose\":"; pose(d6->getDrivePosition());
                PxVec3 drive_linear, drive_angular; d6->getDriveVelocity(drive_linear, drive_angular);
                out << ",\"drive_linear_velocity\":"; vec(drive_linear);
                out << ",\"drive_angular_velocity\":"; vec(drive_angular);
                out << ",\"linear_limits\":[";
                for (int axis = 0; axis < 3; ++axis) {
                    if (axis) out << ','; const auto limit = d6->getLinearLimit(PxD6Axis::Enum(axis));
                    out << '['; number(limit.lower); out << ','; number(limit.upper); out << ']';
                }
                const auto twist = d6->getTwistLimit();
                const auto swing = d6->getSwingLimit();
                out << "],\"twist_limit\":["; number(twist.lower); out << ','; number(twist.upper);
                out << "],\"swing_limit\":["; number(swing.yAngle); out << ','; number(swing.zAngle); out << ']';
            }
            if (const auto* distance = joint->is<PxDistanceJoint>()) {
                out << ",\"distance\":"; number(distance->getDistance());
                out << ",\"distance_limits\":["; number(distance->getMinDistance()); out << ','; number(distance->getMaxDistance());
                out << "],\"distance_flags\":" << unsigned(distance->getDistanceJointFlags());
            }
            if (const auto* revolute = joint->is<PxRevoluteJoint>()) {
                out << ",\"angle\":"; number(revolute->getAngle()); out << ",\"velocity\":"; number(revolute->getVelocity());
            }
            out << '}';
        }
        out << "],\"corners\":[";
        for (int i = 0; i < wheel_count; ++i)
        {
            if (i) out << ',';
            const auto& c = multibody.corners[i];
            out << "{\"wheel\":" << i << ",\"upright\":" << actor_id(c.upright) << ",\"wheel_body\":" << actor_id(c.wheel_body)
                << ",\"wheel_joint\":" << joint_id(c.wheel_joint) << ",\"travel_joint\":" << joint_id(c.travel_joint)
                << ",\"steering_stop\":" << joint_id(c.steering_stop) << ",\"coilover_bodies\":[" << actor_id(c.coilover_unit.tube) << ',' << actor_id(c.coilover_unit.rod)
                << "],\"coilover_joint\":" << joint_id(c.coilover_unit.spring_joint) << ",\"shock_top_local\":";
            vec(c.chassis_shock_anchor); out << ",\"shock_bottom_local\":"; vec(c.upright_shock_anchor);
            out << ",\"shock_stiffness\":"; number(c.shock_stiffness); out << ",\"shock_damping\":"; number(c.shock_damping);
            out << ",\"forces_applied_this_tick\":" << (c.forces.applied ? "true" : "false");
            out << ",\"elastic_force_n\":"; number(c.forces.elastic);
            out << ",\"damper_force_n\":"; number(c.forces.damper);
            out << ",\"bump_stop_force_n\":"; number(c.forces.bump_stop);
            out << ",\"packer_force_n\":"; number(c.forces.packer);
            out << ",\"unclamped_shock_force_n\":"; number(c.forces.unclamped);
            out << ",\"shock_force_on_chassis_world\":"; vec(c.forces.shock_on_chassis);
            out << ",\"arb_force_on_upright_world\":"; vec(c.forces.arb_on_upright);
            out << ",\"rolling_resistance_torque_world_nm\":"; vec(wheels[i].force_debug.rolling_torque);
            out << ",\"members\":[";
            for (int m = 0; m < c.member_count; ++m) {
                if (m) out << ','; const auto& member = c.members[m];
                out << "{\"actor\":" << actor_id(member.actor) << ",\"pivot\":" << joint_id(member.pivot_joint)
                    << ",\"bushing\":" << (member.pivot_is_bushing ? "true" : "false") << ",\"local_start\":";
                vec(member.local_start); out << ",\"local_end\":"; vec(member.local_end); out << '}';
            }
            out << "]}";
        }
        out << "],\"rack\":" << actor_id(multibody.rack) << ",\"rack_joint\":" << joint_id(multibody.rack_joint);
        auto arb = [&](const char* name, const anti_roll_bar& bar) {
            out << ",\"" << name << "\":{\"actors\":[" << actor_id(bar.left_half) << ',' << actor_id(bar.right_half) << ','
                << actor_id(bar.left_drop) << ',' << actor_id(bar.right_drop) << "],\"torsion_joint\":" << joint_id(bar.torsion_joint) << '}';
        };
        arb("front_arb", multibody.front_arb); arb("rear_arb", multibody.rear_arb);
        const auto& d = multibody.driveline;
        out << ",\"driveline\":{\"gearbox_output\":" << actor_id(d.gearbox_output) << ",\"axle_input\":" << actor_id(d.axle_input)
            << ",\"torsion_joint\":" << joint_id(d.torsion_joint) << ",\"propshafts\":[" << actor_id(d.propshaft[0]) << ',' << actor_id(d.propshaft[1])
            << "],\"differentials\":[" << actor_id(d.differential[0]) << ',' << actor_id(d.differential[1]) << "],\"halfshafts\":[";
        for (int i = 0; i < wheel_count; ++i) { if (i) out << ','; out << actor_id(d.halfshaft[i]); }
        out << "]},\"gravity\":"; vec(body && body->getScene() ? body->getScene()->getGravity() : PxVec3(0));
        out << ",\"reset_count\":" << reset_count << ",\"contact_reports\":[";
        for (size_t i = 0; i < contact_reports.size(); ++i)
        {
            if (i) out << ',';
            const auto& report = contact_reports[i];
            out << "{\"other_entity\":\"" << report.other_entity << "\",\"point_count\":" << report.point_count
                << ",\"pair_flags\":" << report.pair_flags << ",\"impulse_world\":";
            vec(report.impulse); out << '}';
        }
        out << "],\"contact_reports_dropped\":" << contact_reports_dropped << '}';
        return out.str();
    }
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
                const bool was_writing = file != nullptr;
                flush_telemetry();
                if (file)
                {
                    fclose(file);
                    file = nullptr;
                }

                std::ifstream read_file(telemetry_path);
                if (!read_file.is_open())
                {
                    if (was_writing) reopen_telemetry_append();
                    return false;
                }

                // Headers and rows can exceed 8 KiB. Read logical CSV lines, and
                // keep only the requested tail even for a long driving session.
                std::string header, line;
                std::deque<std::string> tail;
                if (std::getline(read_file, header))
                {
                    out_total_lines = 1;
                    while (std::getline(read_file, line))
                    {
                        ++out_total_lines;
                        if (max_rows > 0)
                        {
                            tail.push_back(line);
                            if (tail.size() > static_cast<size_t>(max_rows)) tail.pop_front();
                        }
                    }
                }
                read_file.close();

                if (out_total_lines > 0)
                {
                    out_text = header + '\n';
                    for (const auto& row : tail) out_text += row + '\n';
                }
                // A read before the first sample must not create an empty append
                // stream: that used to skip the CSV header on the first tick.
                return !was_writing || reopen_telemetry_append();
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
                fputs(",auto_shift_enabled", file);
                for (const char* prefix : {"fl", "fr", "rl", "rr"})
                    fprintf(file, ",%s_surface_name,%s_surface_grip,%s_surface_rolling,%s_surface_mixed", prefix, prefix, prefix, prefix);
                fputs(",stability_active,target_yaw_rate,fl_stability_brake_torque,fr_stability_brake_torque,rl_stability_brake_torque,rr_stability_brake_torque", file);
                fputs(",engine_net_output_torque", file);
                fputs(",physics_skeleton_json", file);
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
                    contact_reports.clear(); contact_reports_dropped = 0;
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
                fprintf(file, ",%d", manual_shifting ? 0 : 1);
                for (const auto& w : wheels)
                    fprintf(file, ",%s,%.6g,%.6g,%d", w.grounded ? get_surface_name(w.contact_surface) : "Air",
                        w.surface_grip, w.surface_rolling, w.mixed_surface ? 1 : 0);
                fprintf(file, ",%d,%.6g,%.6g,%.6g,%.6g,%.6g", assisted_actuators.stability_active ? 1 : 0,
                    assisted_actuators.target_yaw_rate, assisted_actuators.stability_brake_torque[0],
                    assisted_actuators.stability_brake_torque[1], assisted_actuators.stability_brake_torque[2],
                    assisted_actuators.stability_brake_torque[3]);
                fprintf(file, ",%.6g", engine_net_output_torque);
                fputs(",\"", file);
                const std::string skeleton = get_physics_telemetry_json();
                std::string escaped;
                escaped.reserve(skeleton.size() + skeleton.size() / 4);
                for (char c : skeleton) { if (c == '"') escaped += '"'; escaped += c; }
                fwrite(escaped.data(), 1, escaped.size(), file);
                event_flags = 0; contact_impulse = PxVec3(0);
                contact_reports.clear(); contact_reports_dropped = 0;
                fputc('"', file);
                fputc('\n', file);

                if (frame_counter % 200 == 0)
                {
                    fflush(file);
                }
                frame_counter++;
            }
}
