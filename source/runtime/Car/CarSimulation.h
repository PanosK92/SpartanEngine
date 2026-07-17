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

//= INCLUDES ====================
#pragma once
#include "CarState.h"
#include "CarAssists.h"
#include "CarPacejka.h"
#include "CarAero.h"
#include "CarMultibody.h"
#include "CarSuspension.h"
#include "CarDrivetrain.h"
#include "CarTires.h"
#include "CarValidation.h"
//================================

// public vehicle simulation api and fixed step orchestration
// include order is intentional because companion headers share namespace state
// mutable simulation state is process global and supports one active vehicle
// coordinates use x right y up z forward and physics lengths use metres
// tick must run once before each physx simulation step
// fixed order is multibody sweep slip drivetrain tires aligning torque and aero

namespace car
{
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
        float freq[2]      = { tuning::spec.front_spring_freq, tuning::spec.rear_spring_freq };

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
            float dr = is_front(i) ? tuning::spec.front_damping_ratio : tuning::spec.rear_damping_ratio;
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

    struct setup_params
    {
        PxPhysics*              physics      = nullptr;
        PxScene*                scene        = nullptr;
        PxConvexMesh*           chassis_mesh = nullptr;  // convex hull for collision
        std::vector<PxVec3>     vertices;                // original mesh verts for aero calculation
        config                  car_config;
    };

    inline void apply_car_spec(const car_preset& spec, bool set_as_base);

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
        // preset geometry must be applied before deriving body suspension and wheel constants
        apply_car_spec(tuning::spec, true);

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
        float front_omega = 2.0f * PxPi * tuning::spec.front_spring_freq;
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

        PxVec3 com(tuning::spec.center_of_mass_x, tuning::spec.center_of_mass_y, tuning::spec.center_of_mass_z);
        PxRigidBodyExt::setMassAndUpdateInertia(*body, chassis_mass(), &com);
        body->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, false);
        body->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
        body->setLinearDamping(tuning::spec.linear_damping);
        body->setAngularDamping(tuning::spec.angular_damping);

        params.scene->addActor(*body);

        if (!params.vertices.empty())
        {
            compute_aero_from_shape(params.vertices);
        }

        if (!rebuild_wheel_sweep_mesh())
        {
            SP_LOG_ERROR("failed to create wheel contact geometry");
            destroy();
            return false;
        }
        if (!create_multibody(params.physics, params.scene))
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

        PxVec3 com(tuning::spec.center_of_mass_x, tuning::spec.center_of_mass_y, tuning::spec.center_of_mass_z);
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

        PxVec3 com(tuning::spec.center_of_mass_x, tuning::spec.center_of_mass_y, tuning::spec.center_of_mass_z);
        PxRigidBodyExt::setMassAndUpdateInertia(*body, chassis_mass(), &com);
        if (multibody.initialized)
        {
            update_assembled_center_of_mass();
        }

        SP_LOG_INFO("car center of mass set to (%.2f, %.2f, %.2f)", com.x, com.y, com.z);
    }

    inline void apply_car_spec(const car_preset& spec, bool set_as_base)
    {
        tuning::spec = spec;
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
        engine_rpm = tuning::spec.engine_idle_rpm;
        engine_rotation = 0.0f;
        current_gear = (tuning::spec.gear_count > 2) ? 2 : 1;
        shift_timer = 0.0f;
        is_shifting = false;
        clutch = 1.0f;
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
        reflected_engine_inertia = 0.0f;
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
            wheels[i].brake_temp = PxMax(tuning::spec.brake_ambient_temp, 0.0f);
            wheels[i].wear = 0.0f;
            wheels[i].thermal.surface[0] = PxMax(tuning::spec.tire_ambient_temp, 0.0f);
            wheels[i].thermal.surface[1] = PxMax(tuning::spec.tire_ambient_temp, 0.0f);
            wheels[i].thermal.surface[2] = PxMax(tuning::spec.tire_ambient_temp, 0.0f);
            wheels[i].thermal.core = PxMax(tuning::spec.tire_ambient_temp, 0.0f);
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
        car_preset previous_spec = tuning::spec;
        config previous_config = cfg;
        upgrades = active_upgrades{};
        apply_car_spec(new_spec, true);

        if (!rebuild_vehicle_geometry())
        {
            SP_LOG_ERROR("failed to rebuild suspension for car preset");
            upgrades = previous_upgrades;
            base_spec = previous_base;
            tuning::spec = previous_spec;
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
        car_preset previous_spec = tuning::spec;
        config previous_config = cfg;
        clamp_upgrade_stage(upgrades.engine, base_spec.engine_stage_max);
        clamp_upgrade_stage(upgrades.suspension, base_spec.suspension_stage_max);
        clamp_upgrade_stage(upgrades.tires, base_spec.tires_stage_max);
        clamp_upgrade_stage(upgrades.brakes, base_spec.brakes_stage_max);
        clamp_upgrade_stage(upgrades.aero, base_spec.aero_stage_max);
        clamp_upgrade_stage(upgrades.weight, base_spec.weight_stage_max);

        bool save_abs = tuning::spec.abs_enabled;
        bool save_tc = tuning::spec.tc_enabled;
        bool save_manual = tuning::spec.manual_transmission;
        bool save_turbo = tuning::spec.turbo_enabled;
        bool save_drs = tuning::spec.drs_enabled;
        int save_diff = tuning::spec.diff_type;
        assist_settings save_assists = tuning::spec.assists;

        float save_abs_th = tuning::spec.abs_slip_threshold;
        float save_abs_release = tuning::spec.abs_release_rate;
        float save_abs_pulse = tuning::spec.abs_pulse_frequency;

        float save_tc_th = tuning::spec.tc_slip_threshold;
        float save_tc_pwr = tuning::spec.tc_power_reduction;
        float save_tc_rate = tuning::spec.tc_response_rate;

        float save_bst_max = tuning::spec.boost_max_pressure;
        float save_bst_spool = tuning::spec.boost_spool_rate;
        float save_bst_waste = tuning::spec.boost_wastegate_rpm;
        float save_bst_torq = tuning::spec.boost_torque_mult;
        float save_bst_min = tuning::spec.boost_min_rpm;

        car_preset eff = make_upgraded_spec(base_spec, upgrades);
        apply_car_spec(eff, false);

        tuning::spec.abs_enabled = save_abs;
        tuning::spec.tc_enabled = save_tc;
        tuning::spec.manual_transmission = save_manual;
        tuning::spec.turbo_enabled = save_turbo;
        tuning::spec.drs_enabled = save_drs;
        tuning::spec.diff_type = save_diff;
        tuning::spec.assists = save_assists;

        tuning::spec.abs_slip_threshold = save_abs_th;
        tuning::spec.abs_release_rate = save_abs_release;
        tuning::spec.abs_pulse_frequency = save_abs_pulse;

        tuning::spec.tc_slip_threshold = save_tc_th;
        tuning::spec.tc_power_reduction = save_tc_pwr;
        tuning::spec.tc_response_rate = save_tc_rate;

        tuning::spec.boost_max_pressure = save_bst_max;
        tuning::spec.boost_spool_rate = save_bst_spool;
        tuning::spec.boost_wastegate_rpm = save_bst_waste;
        tuning::spec.boost_torque_mult = save_bst_torq;
        tuning::spec.boost_min_rpm = save_bst_min;
        if (!rebuild_vehicle_geometry())
        {
            tuning::spec = previous_spec;
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
        tuning::spec.center_of_mass_x = x;
        tuning::spec.center_of_mass_y = y;
        tuning::spec.center_of_mass_z = z;
        compute_constants();
        update_mass_properties();
    }

    inline void set_center_of_mass_x(float x) { set_center_of_mass(x, tuning::spec.center_of_mass_y, tuning::spec.center_of_mass_z); }
    inline void set_center_of_mass_y(float y) { set_center_of_mass(tuning::spec.center_of_mass_x, y, tuning::spec.center_of_mass_z); }
    inline void set_center_of_mass_z(float z) { set_center_of_mass(tuning::spec.center_of_mass_x, tuning::spec.center_of_mass_y, z); }

    inline float get_center_of_mass_x() { return tuning::spec.center_of_mass_x; }
    inline float get_center_of_mass_y() { return tuning::spec.center_of_mass_y; }
    inline float get_center_of_mass_z() { return tuning::spec.center_of_mass_z; }

    inline float get_frontal_area()     { return tuning::spec.frontal_area; }
    inline float get_side_area()        { return tuning::spec.side_area; }
    inline float get_drag_coeff()       { return tuning::spec.drag_coeff; }
    inline float get_lift_coeff_front() { return tuning::spec.lift_coeff_front; }
    inline float get_lift_coeff_rear()  { return tuning::spec.lift_coeff_rear; }

    inline void set_frontal_area(float area)   { tuning::spec.frontal_area = area; }
    inline void set_side_area(float area)      { tuning::spec.side_area = area; }
    inline void set_drag_coeff(float cd)       { tuning::spec.drag_coeff = cd; }
    inline void set_lift_coeff_front(float cl) { tuning::spec.lift_coeff_front = cl; }
    inline void set_lift_coeff_rear(float cl)  { tuning::spec.lift_coeff_rear = cl; }

    inline void  set_ground_effect_enabled(bool enabled)  { tuning::spec.ground_effect_enabled = enabled; }
    inline bool  get_ground_effect_enabled()              { return tuning::spec.ground_effect_enabled; }
    inline void  set_ground_effect_multiplier(float mult) { tuning::spec.ground_effect_multiplier = mult; }
    inline float get_ground_effect_multiplier()           { return tuning::spec.ground_effect_multiplier; }

    inline void set_throttle(float v)  { input_target.throttle  = PxClamp(v, 0.0f, 1.0f); }
    inline void set_brake(float v)     { input_target.brake     = PxClamp(v, 0.0f, 1.0f); }
    inline void set_steering(float v)  { input_target.steering  = PxClamp(v, -1.0f, 1.0f); }
    inline void set_handbrake(float v) { input_target.handbrake = PxClamp(v, 0.0f, 1.0f); }

    inline void update_input(float dt)
    {
        float steering_target = get_assisted_steering_target(input_target.steering);
        float diff = steering_target - input.steering;
        float max_change = tuning::spec.steering_rate * dt;
        input.steering = (fabsf(diff) <= max_change) ? steering_target : input.steering + ((diff > 0) ? max_change : -max_change);

        // pedals smooth application but release immediately
        input.throttle = (input_target.throttle < input.throttle) ? input_target.throttle
            : lerp(input.throttle, input_target.throttle, exp_decay(tuning::spec.throttle_smoothing, dt));
        input.brake = (input_target.brake < input.brake) ? input_target.brake
            : lerp(input.brake, input_target.brake, exp_decay(tuning::spec.brake_smoothing, dt));

        input.handbrake = input_target.handbrake;
    }

    inline void tick(float dt)
    {
        if (!body)
        {
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
        bool motion_requested = input.throttle > tuning::spec.input_deadzone || (is_in_reverse() && input.brake > tuning::spec.input_deadzone) || steering_adjusting;
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
            float temp_above_ambient = wheels[i].brake_temp - tuning::spec.brake_ambient_temp;
            if (temp_above_ambient > 0.0f)
            {
                float h = tuning::spec.brake_cooling_base + airspeed * tuning::spec.brake_cooling_airflow;
                float cooling_power = h * temp_above_ambient;
                float temp_drop = (cooling_power / tuning::spec.brake_thermal_mass) * dt;
                wheels[i].brake_temp -= temp_drop;
                wheels[i].brake_temp = PxMax(wheels[i].brake_temp, tuning::spec.brake_ambient_temp);
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

        if (tuning::log_telemetry)
        {
            float wheel_surface_speed = get_average_driven_angular_velocity(false) * get_driven_wheel_radius() * 3.6f;
            SP_LOG_INFO("rpm=%.0f, speed=%.0f km/h, gear=%s%s, wheel_speed=%.0f km/h, throttle=%.0f%%",
                engine_rpm, speed_kmh, get_gear_string(), is_shifting ? "(shifting)" : "",
                wheel_surface_speed, input.throttle * 100.0f);
        }

        telemetry.tick(dt, speed_kmh);
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

    #define WHEEL_GETTER(name, field) inline float get_wheel_##name(int i) { return is_valid_wheel(i) ? wheels[i].field : 0.0f; }
    WHEEL_GETTER(compression, compression)
    WHEEL_GETTER(slip_angle, slip_angle)
    WHEEL_GETTER(slip_ratio, slip_ratio)
    WHEEL_GETTER(tire_load, tire_load)
    WHEEL_GETTER(lateral_force, lateral_force)
    WHEEL_GETTER(longitudinal_force, longitudinal_force)
    WHEEL_GETTER(angular_velocity, angular_velocity)
    WHEEL_GETTER(rotation, rotation)
    WHEEL_GETTER(effective_radius, effective_radius)
    WHEEL_GETTER(dynamic_camber, dynamic_camber)
    WHEEL_GETTER(dynamic_toe, dynamic_toe)
    WHEEL_GETTER(bump_steer, bump_steer)
    WHEEL_GETTER(motion_ratio, motion_ratio)
    #undef WHEEL_GETTER
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
        float anti_roll = front ? tuning::spec.front_arb_stiffness : tuning::spec.rear_arb_stiffness;
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

    inline float get_tire_pressure()         { return tuning::spec.tire_pressure; }
    inline float get_tire_pressure_optimal() { return tuning::spec.tire_pressure_optimal; }

    inline float get_chassis_visual_offset_y()
    {
        const float offset = 0.1f;
        return -(cfg.height * 0.5f + cfg.suspension_height) + offset;
    }

    inline void set_abs_enabled(bool enabled) { tuning::spec.abs_enabled = enabled; }
    inline bool get_abs_enabled()             { return tuning::spec.abs_enabled; }
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

    inline void  set_tc_enabled(bool enabled) { tuning::spec.tc_enabled = enabled; }
    inline bool  get_tc_enabled()             { return tuning::spec.tc_enabled; }
    inline bool  is_tc_active()               { return tc_active; }
    inline float get_tc_reduction()           { return tc_reduction; }

    inline void set_manual_transmission(bool enabled) { tuning::spec.manual_transmission = enabled; }
    inline bool get_manual_transmission()             { return tuning::spec.manual_transmission; }

    inline void begin_shift(int direction)
    {
        is_shifting = true;
        shift_timer = tuning::spec.shift_time;
        last_shift_direction = direction;
    }

    inline void shift_up()
    {
        if (!tuning::spec.manual_transmission || is_shifting || current_gear >= tuning::spec.gear_count - 1)
        {
            return;
        }
        current_gear = (current_gear == 0) ? 1 : current_gear + 1;
        begin_shift(1);
    }

    inline void shift_down()
    {
        if (!tuning::spec.manual_transmission || is_shifting || current_gear <= 0)
        {
            return;
        }
        current_gear = (current_gear == 1) ? 0 : current_gear - 1;
        begin_shift(-1);
    }

    inline void shift_to_neutral()
    {
        if (!tuning::spec.manual_transmission || is_shifting)
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
    inline float       get_engine_torque_current() { return get_engine_torque(engine_rpm) * (1.0f + boost_pressure * tuning::spec.boost_torque_mult); }
    inline float       get_motor_torque()          { return motor_torque; }
    inline float       get_driveshaft_twist()      { return driveshaft_twist; }
    inline float       get_driveshaft_torque()     { return driveshaft_twist * tuning::spec.driveshaft_stiffness; }
    inline float       get_motor_power_kw()        { float w = motor_torque * engine_rpm * (2.0f * 3.14159265f / 60.0f); return w / 1000.0f; }
    inline float       get_redline_rpm()           { return tuning::spec.engine_redline_rpm; }
    inline float       get_max_rpm()               { return tuning::spec.engine_max_rpm; }
    inline float       get_idle_rpm()              { return tuning::spec.engine_idle_rpm; }

    inline void  set_turbo_enabled(bool enabled)
    {
        tuning::spec.turbo_enabled = enabled;
        // presets without turbo data receive stable defaults when enabled at runtime
        if (enabled && tuning::spec.boost_max_pressure <= 0.0f)
        {
            tuning::spec.boost_max_pressure  = 1.0f;
            tuning::spec.boost_spool_rate    = 3.0f;
            tuning::spec.boost_torque_mult   = 0.35f;
            tuning::spec.boost_min_rpm       = 2000.0f;
            tuning::spec.boost_wastegate_rpm = tuning::spec.engine_redline_rpm > 0.0f ? tuning::spec.engine_redline_rpm - 500.0f : 6500.0f;
        }
    }
    inline bool  get_turbo_enabled()             { return tuning::spec.turbo_enabled; }
    inline float get_boost_pressure()            { return boost_pressure; }
    inline float get_boost_max_pressure()        { return tuning::spec.boost_max_pressure; }

    inline void set_drs_enabled(bool enabled) { tuning::spec.drs_enabled = enabled; }
    inline bool get_drs_enabled()             { return tuning::spec.drs_enabled; }
    inline void set_drs_active(bool active)   { drs_active = active; }
    inline bool get_drs_active()              { return drs_active; }

    inline void set_diff_type(int type)
    {
        int new_type = PxClamp(type, 0, 2);
        if (new_type == tuning::spec.diff_type)
        {
            return;
        }
        int previous_type = tuning::spec.diff_type;
        tuning::spec.diff_type = new_type;
        if (multibody.initialized && !rebuild_multibody())
        {
            tuning::spec.diff_type = previous_type;
            SP_LOG_ERROR("failed to rebuild physical differential");
        }
    }
    inline int  get_diff_type()               { return tuning::spec.diff_type; }
    inline const char* get_diff_type_name()
    {
        static const char* names[] = { "Open", "Locked", "LSD" };
        return (tuning::spec.diff_type >= 0 && tuning::spec.diff_type <= 2) ? names[tuning::spec.diff_type] : "?";
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
        static const char* names[] = { "Asphalt", "Concrete", "Wet", "Gravel", "Grass", "Ice" };
        return (surface >= 0 && surface < surface_count) ? names[surface] : "Unknown";
    }

    inline float get_front_camber() { return tuning::spec.front_camber; }
    inline float get_rear_camber()  { return tuning::spec.rear_camber; }
    inline float get_front_toe()    { return tuning::spec.front_toe; }
    inline float get_rear_toe()     { return tuning::spec.rear_toe; }

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
}
