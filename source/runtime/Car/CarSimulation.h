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
#include "CarPacejka.h"
#include "CarAero.h"
#include "CarSuspension.h"
#include "CarDrivetrain.h"
#include "CarTires.h"
//================================

// vehicle dynamics simulation
//
// this header is the public api entry point. it owns setup/teardown, the per-tick
// orchestration, input smoothing, and the user-facing get/set accessors. the heavy
// physics live in companion headers:
//
//   - CarState.h       types, globals, telemetry, debug plumbing
//   - CarPacejka.h     tire math (pacejka, load/temp/camber/surface factors)
//   - CarAero.h        drag, downforce, ground effect, drs, rolling resistance,
//                      mesh-based aero inference, 2d silhouette hull
//   - CarSuspension.h  sweep-based ground detection, spring/damper + bump stops,
//                      anti-roll bars, longitudinal + lateral weight transfer,
//                      ackermann + bump steer
//   - CarDrivetrain.h  engine torque, turbo, automatic/manual gearbox, clutch,
//                      driveshaft compliance, differentials, traction control,
//                      engine braking, service brakes
//   - CarTires.h       per-wheel tire forces, thermal/wear model, semi-implicit
//                      euler wheel spin integration, self-aligning torque
//
// - runs within the physx 200 hz fixed-timestep loop
//   all vehicle physics run inside the physx fixed-step update at 200 hz rather
//   than at the variable render framerate. this keeps the simulation deterministic
//   and stable regardless of fps, and avoids the jitter that comes from coupling
//   stiff spring-damper systems to a variable timestep.
//
// - pacejka magic formula tires with friction-circle combined slip
//   the pacejka model describes how tire grip varies with slip using empirical
//   curve-fit coefficients (B, C, D, E). combined slip is handled by evaluating
//   each pacejka curve at the combined slip magnitude and projecting the result
//   onto the longitudinal / lateral axes (sigma_x / sigma_y weighting). it is not
//   the full MF 5.2 g_xa/g_ya combined model, but it produces the correct friction
//   circle behavior: braking mid-corner reduces cornering grip and vice versa.
//
// - 3-zone surface + core tire thermal model
//   each tire tracks three surface zones (inside, middle, outside) plus a core
//   temperature. heat flows from friction work into the surface, then conducts
//   into the core. grip peaks at an optimal temperature and falls off on either
//   side, so driving style and camber affect grip through temperature.
//
// - tire pressure, wear, relaxation length, per-axle dimensions
//   tire pressure affects cornering stiffness and heat generation — under-inflated
//   tires flex more, build heat faster, and have lower peak grip. wear accumulates
//   from slip and temperature, gradually reducing grip. relaxation length models
//   the lag between a change in slip and the force response, preventing instant
//   force spikes. front and rear axles can have different tire widths and radii.
//
// - load-sensitive grip with nonlinear Fz scaling
//   real tires produce diminishing grip returns as vertical load increases — a tire
//   at twice the load does not have twice the grip. this is modeled by scaling the
//   pacejka output with a power-law function of load, so heavily loaded tires
//   (outside wheels in a corner) are less efficient than lightly loaded ones.
//
// - spring-damper suspension with front/rear damping ratios
//   each wheel has a spring and damper defined by natural frequency and damping
//   ratio rather than raw stiffness values, making it easier to tune. front and
//   rear axles have independent frequencies and ratios, and bump/rebound damping
//   are split so compression can be softer than extension for better compliance.
//
// - anti-roll bars (displacement-based), progressive bump stops
//   anti-roll bars connect left and right wheels on each axle and resist body roll
//   by transferring load based on the difference in suspension compression. bump
//   stops engage near full compression with a steep progressive stiffness curve,
//   preventing metal-to-metal contact and limiting suspension travel.
//
// - lateral weight transfer: geometric + elastic split via roll center heights
//   cornering forces shift load from inner to outer wheels. this transfer is split
//   into a geometric component that acts instantly through the roll center, and an
//   elastic component that passes through the springs and anti-roll bars. the ratio
//   between front and rear transfer is governed by roll stiffness distribution,
//   which directly controls the understeer/oversteer balance.
//
// - longitudinal weight transfer from body acceleration
//   under acceleration, load shifts rearward onto the driven wheels; under braking,
//   it shifts forward. the amount depends on the center of mass height and the
//   wheelbase length. this changes tire grip per axle dynamically — rear tires gain
//   grip under throttle while front tires gain grip under braking.
//
// - ackermann steering with bump steer and toe
//   ackermann geometry turns the inner front wheel more than the outer so both
//   follow concentric arcs, reducing scrub at low speed. bump steer adds small
//   toe changes as the suspension compresses, simulating real steering geometry
//   side effects. static toe settings (toe-in or toe-out) affect straight-line
//   stability and turn-in response.
//
// - self-aligning torque via pneumatic trail
//   the tire contact patch generates a restoring yaw moment because the resultant
//   lateral force acts behind the steering axis by a distance called pneumatic
//   trail. this creates a natural centering feel that fades as slip angle increases
//   and the contact patch distorts, giving progressive feedback through the
//   steering about how close the front tires are to their grip limit.
//
// - engine torque curve, turbo/wastegate, rev limiter
//   engine output follows a torque-vs-rpm curve with a defined peak. turbo adds
//   boost pressure that spools up with rpm and is capped by a wastegate threshold.
//   the rev limiter cuts fuel delivery above max rpm, and engine inertia and
//   internal friction affect how quickly the engine responds to throttle changes.
//
// - 7-speed auto/manual gearbox with rev-match downshifts
//   the gearbox supports both automatic and manual shifting. automatic mode uses
//   speed and rpm thresholds with hysteresis to decide shifts, and blocks upshifts
//   during wheelspin. downshifts trigger a throttle blip to match engine rpm to
//   wheel speed, reducing driveline shock and rear-end instability on corner entry.
//
// - driveshaft torsional compliance
//   the driveshaft is modeled as a torsional spring rather than a rigid link. this
//   introduces a small delay between engine torque changes and wheel response,
//   smoothing out sudden throttle inputs and preventing unrealistic instant torque
//   delivery that would cause abrupt weight shifts.
//
// - open/locked/lsd differentials, rwd/fwd/awd layouts
//   open diffs split torque equally regardless of wheel speed. locked diffs force
//   both wheels to the same speed, maximizing traction but causing understeer. lsd
//   (limited-slip) diffs transfer torque toward the slower wheel based on preload
//   and lock ratios for acceleration and deceleration independently. the drivetrain
//   layout determines which axles receive power — rwd, fwd, or awd with a
//   configurable front/rear torque split.
//
// - brake thermal model with front/rear bias and abs
//   brakes generate heat proportional to rotational speed and applied torque, and
//   cool via convection from airflow. brake efficiency drops as temperature exceeds
//   an optimal range, simulating brake fade on long descents or repeated hard stops.
//   front/rear bias controls the torque distribution. abs pulses brake pressure on
//   individual wheels when slip ratio exceeds a threshold, preventing lockup.
//
// - traction control with slip-based power reduction
//   traction control monitors driven wheel slip ratios and progressively reduces
//   engine torque when they exceed a threshold. the response rate controls how
//   aggressively it intervenes — too slow and the wheels spin, too fast and it
//   feels intrusive. it only acts on engine output, not brakes.
//
// - aerodynamics: drag, downforce, ground effect, drs, pitch/yaw sensitivity
//   drag opposes motion proportional to speed squared and frontal area. downforce
//   pushes the car into the ground, increasing tire load and grip at speed, applied
//   separately at front and rear aero centers. ground effect increases downforce as
//   ride height decreases. drs reduces rear downforce for higher straight-line
//   speed. yaw angle increases drag and reduces downforce, and pitch angle shifts
//   the front/rear aero balance.
//
// - semi-implicit euler wheel spin integration
//   wheel angular velocity is integrated using semi-implicit euler — the new
//   velocity is computed from net torque first, then used to advance the rotation
//   angle. this is more stable than explicit euler for stiff systems like brake
//   lockup and clutch engagement where forces change rapidly within a single step.
//
// - convex hull sweep for ground contact
//   each wheel casts a convex shape sweep downward from the suspension top mount
//   to detect ground contact, rather than using a simple raycast. this handles
//   curbs, bumps, and uneven terrain more accurately because the contact point
//   and normal reflect the actual wheel shape interacting with the surface geometry.
//
// - multiple surface types (asphalt, concrete, wet, gravel, grass, ice)
//   each surface type has a friction multiplier that scales the tire's peak grip.
//   the wheel's ground contact query identifies which surface material it is on,
//   allowing grip to change dynamically as the car moves across different terrain
//   — for example, dropping two wheels onto grass mid-corner reduces grip on that
//   side and induces a spin if the driver doesn't correct.

namespace car
{
    // derive the cached wheelbase and track widths from whatever is currently in wheel_offsets.
    // callers that reposition wheels via set_wheel_offset must invoke this afterwards.
    inline void refresh_geometry_cache()
    {
        cfg.wheelbase   = wheel_offsets[front_left].z - wheel_offsets[rear_left].z;
        cfg.track_front = wheel_offsets[front_right].x - wheel_offsets[front_left].x;
        cfg.track_rear  = wheel_offsets[rear_right].x  - wheel_offsets[rear_left].x;
    }

    inline void compute_constants()
    {
        float front_z = cfg.length * 0.35f;
        float rear_z  = -cfg.length * 0.35f;
        float half_w  = cfg.width * 0.5f - (cfg.front_wheel_width + cfg.rear_wheel_width) * 0.25f;
        float y       = -cfg.suspension_height;

        wheel_offsets[front_left]  = PxVec3(-half_w, y, front_z);
        wheel_offsets[front_right] = PxVec3( half_w, y, front_z);
        wheel_offsets[rear_left]   = PxVec3(-half_w, y, rear_z);
        wheel_offsets[rear_right]  = PxVec3( half_w, y, rear_z);

        refresh_geometry_cache();

        float wdf = get_weight_distribution_front();
        float axle_mass[2] = { cfg.mass * wdf * 0.5f, cfg.mass * (1.0f - wdf) * 0.5f };
        float freq[2]      = { tuning::spec.front_spring_freq, tuning::spec.rear_spring_freq };

        for (int i = 0; i < wheel_count; i++)
        {
            int axle   = is_front(i) ? 0 : 1;
            float mass = axle_mass[axle];
            float omega = 2.0f * PxPi * freq[axle];

            float r = cfg.wheel_radius_for(i);
            wheel_moi[i]        = 0.7f * cfg.wheel_mass * r * r;
            spring_stiffness[i] = mass * omega * omega;
            float dr = is_front(i) ? tuning::spec.front_damping_ratio : tuning::spec.rear_damping_ratio;
            spring_damping[i]   = 2.0f * dr * sqrtf(spring_stiffness[i] * mass);
        }
    }

    inline void destroy()
    {
        if (body)             { body->release();             body = nullptr; }
        if (material)         { material->release();         material = nullptr; }
        if (wheel_sweep_mesh) { wheel_sweep_mesh->release(); wheel_sweep_mesh = nullptr; }
    }

    struct setup_params
    {
        PxPhysics*              physics      = nullptr;
        PxScene*                scene        = nullptr;
        PxConvexMesh*           chassis_mesh = nullptr;  // convex hull for collision
        std::vector<PxVec3>     vertices;                // original mesh verts for aero calculation
        config                  car_config;
    };

    inline bool setup(const setup_params& params)
    {
        if (!params.physics || !params.scene)
        {
            return false;
        }

        cfg = params.car_config;
        compute_constants();

        for (int i = 0; i < wheel_count; i++)
        {
            wheels[i] = wheel();
            abs_active[i] = false;
        }
        input = input_state();
        input_target = input_state();
        abs_phase = 0.0f;
        tc_reduction = 0.0f;
        tc_active = false;
        engine_rpm = tuning::spec.engine_idle_rpm;
        current_gear = 2;
        shift_timer = 0.0f;
        is_shifting = false;
        clutch = 1.0f;
        shift_cooldown = 0.0f;
        last_shift_direction = 0;
        boost_pressure = 0.0f;
        rev_limiter_active = false;
        downshift_blip_timer = 0.0f;
        drs_active = false;
        longitudinal_accel = 0.0f;
        lateral_accel = 0.0f;
        road_bump_phase = 0.0f;
        driveshaft_twist = 0.0f;
        prev_velocity = PxVec3(0);

        material = params.physics->createMaterial(0.8f, 0.7f, 0.1f);
        if (!material)
        {
            return false;
        }

        float front_mass_per_wheel = cfg.mass * get_weight_distribution_front() * 0.5f;
        float front_omega = 2.0f * PxPi * tuning::spec.front_spring_freq;
        float front_stiffness = front_mass_per_wheel * front_omega * front_omega;
        float expected_sag = PxClamp((front_mass_per_wheel * 9.81f) / front_stiffness, 0.0f, cfg.suspension_travel * 0.8f);
        float avg_wheel_r = (cfg.front_wheel_radius + cfg.rear_wheel_radius) * 0.5f;
        float spawn_y = avg_wheel_r + cfg.suspension_height + expected_sag;

        body = params.physics->createRigidDynamic(PxTransform(PxVec3(0, spawn_y, 0)));
        if (!body)
        {
            material->release();
            material = nullptr;
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
        PxRigidBodyExt::setMassAndUpdateInertia(*body, cfg.mass, &com);
        body->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, true);
        body->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
        body->setLinearDamping(tuning::spec.linear_damping);
        body->setAngularDamping(tuning::spec.angular_damping);

        params.scene->addActor(*body);

        if (!params.vertices.empty())
        {
            compute_aero_from_shape(params.vertices);
        }

        // cook a convex cylinder for wheel sweep queries
        if (!wheel_sweep_mesh)
        {
            const int segments = 16;
            std::vector<PxVec3> cyl_verts;
            cyl_verts.reserve(segments * 2);
            float sweep_r = PxMax(cfg.front_wheel_radius, cfg.rear_wheel_radius);
            float sweep_w = PxMax(cfg.front_wheel_width, cfg.rear_wheel_width);
            float half_w = sweep_w * 0.5f;
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
            wheel_sweep_mesh = PxCreateConvexMesh(cook_params, desc, *PxGetStandaloneInsertionCallback(), &cook_result);
            if (!wheel_sweep_mesh || cook_result != PxConvexMeshCookingResult::eSUCCESS)
            {
                SP_LOG_WARNING("failed to create wheel sweep cylinder mesh");
            }
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
        PxRigidBodyExt::setMassAndUpdateInertia(*body, cfg.mass, &com);

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
        PxRigidBodyExt::setMassAndUpdateInertia(*body, cfg.mass, &com);

        SP_LOG_INFO("car center of mass set to (%.2f, %.2f, %.2f)", com.x, com.y, com.z);
    }

    inline void set_center_of_mass(float x, float y, float z)
    {
        tuning::spec.center_of_mass_x = x;
        tuning::spec.center_of_mass_y = y;
        tuning::spec.center_of_mass_z = z;
        update_mass_properties();
    }

    inline void set_center_of_mass_x(float x) { tuning::spec.center_of_mass_x = x; update_mass_properties(); }
    inline void set_center_of_mass_y(float y) { tuning::spec.center_of_mass_y = y; update_mass_properties(); }
    inline void set_center_of_mass_z(float z) { tuning::spec.center_of_mass_z = z; update_mass_properties(); }

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
        float diff = input_target.steering - input.steering;
        float max_change = tuning::spec.steering_rate * dt;
        input.steering = (fabsf(diff) <= max_change) ? input_target.steering : input.steering + ((diff > 0) ? max_change : -max_change);

        // rising edge: first-order smoothing with per-pedal rate
        // falling edge: instantaneous (lift-off should always be felt immediately)
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

        // caller (physics::tickvehicle) already runs this at a fixed sub-step, no clamp needed
        update_input(dt);
        PxScene* scene = body->getScene();
        if (!scene)
        {
            return;
        }

        PxTransform pose = body->getGlobalPose();
        PxVec3 fwd = pose.q.rotate(PxVec3(0, 0, 1));
        PxVec3 vel = body->getLinearVelocity();
        float forward_speed = vel.dot(fwd);
        float speed_kmh = vel.magnitude() * 3.6f;

        // accel for weight transfer - short smoothing (~50 ms tau) keeps turn-in and
        // trail-braking feel crisp without introducing step-response noise
        PxVec3 right = pose.q.rotate(PxVec3(1, 0, 0));
        PxVec3 accel_vec = (vel - prev_velocity) / PxMax(dt, 0.001f);
        float raw_accel = accel_vec.dot(fwd);
        float raw_lat_accel = accel_vec.dot(right);
        constexpr float weight_transfer_rate = 20.0f;
        longitudinal_accel = lerp(longitudinal_accel, raw_accel, exp_decay(weight_transfer_rate, dt));
        lateral_accel      = lerp(lateral_accel, raw_lat_accel, exp_decay(weight_transfer_rate, dt));
        prev_velocity = vel;

        // advance road bump phase based on travel distance
        road_bump_phase += vel.magnitude() * tuning::road_bump_frequency * dt;

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

        // --- physics subsystems ---
        for (int i = 0; i < wheel_count; i++)
            wheels[i].net_torque = 0.0f;

        float wheel_angles[wheel_count];
        calculate_steering(forward_speed, speed_kmh, wheel_angles);

        update_suspension(scene, dt);
        apply_suspension_forces(dt);
        apply_drivetrain(forward_speed * 3.6f, dt);

        apply_tire_forces(wheel_angles, dt);
        apply_self_aligning_torque();

        // speed-proportional yaw damping (tire scrub, suspension compliance, chassis flex)
        {
            PxVec3 up = pose.q.rotate(PxVec3(0, 1, 0));
            float yaw_rate      = body->getAngularVelocity().dot(up);
            float speed_scale   = PxClamp(speed_kmh / 60.0f, 0.0f, 1.0f);
            float yaw_damp_torque = -yaw_rate * tuning::spec.yaw_damping * speed_scale;
            body->addTorque(up * yaw_damp_torque, PxForceMode::eFORCE);
        }

        apply_aero_and_resistance();

        body->addForce(PxVec3(0, -9.81f * cfg.mass, 0), PxForceMode::eFORCE);


        // --- telemetry ---
        if (tuning::log_telemetry)
        {
            float avg_wheel_w = 0.0f;
            { int dc = 0; for (int i = 0; i < wheel_count; i++) if (is_driven(i)) { avg_wheel_w += wheels[i].angular_velocity; dc++; } if (dc > 0)
            {
                avg_wheel_w /= dc;
            } }
            float driven_r_tel = (tuning::spec.drivetrain_type == 1) ? cfg.front_wheel_radius : cfg.rear_wheel_radius;
            float wheel_surface_speed = avg_wheel_w * driven_r_tel * 3.6f;
            SP_LOG_INFO("rpm=%.0f, speed=%.0f km/h, gear=%s%s, wheel_speed=%.0f km/h, throttle=%.0f%%",
                engine_rpm, speed_kmh, get_gear_string(), is_shifting ? "(shifting)" : "",
                wheel_surface_speed, input.throttle * 100.0f);
        }

        telemetry.tick(dt, speed_kmh);
    }

    // --- public accessors ---

    inline float get_speed_kmh()        { return body ? body->getLinearVelocity().magnitude() * 3.6f : 0.0f; }
    inline float get_throttle()         { return input.throttle; }
    inline float get_brake()            { return input.brake; }
    inline float get_steering()         { return input.steering; }
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
    #undef WHEEL_GETTER
    inline float get_wheel_temperature(int i) { return is_valid_wheel(i) ? wheels[i].thermal.avg_surface() : 0.0f; }

    inline bool is_wheel_grounded(int i) { return is_valid_wheel(i) && wheels[i].grounded; }

    inline float get_wheel_suspension_force(int i)
    {
        if (!is_valid_wheel(i) || !wheels[i].grounded)
        {
            return 0.0f;
        }
        return spring_stiffness[i] * wheels[i].compression * cfg.suspension_travel;
    }

    inline float get_wheel_temp_grip_factor(int i)
    {
        return is_valid_wheel(i) ? get_tire_temp_grip_factor(wheels[i].thermal.avg_surface()) : 1.0f;
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
    inline bool is_abs_active_any()           { for (int i = 0; i < wheel_count; i++) if (abs_active[i])
    {
        return true;
    } return false; }

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
        current_gear = (current_gear == 0) ? 1 : current_gear + 1; // from reverse, go to neutral first
        begin_shift(1);
    }

    inline void shift_down()
    {
        if (!tuning::spec.manual_transmission || is_shifting || current_gear <= 0)
        {
            return;
        }
        current_gear = (current_gear == 1) ? 0 : current_gear - 1; // from neutral, go to reverse
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
    inline const char* get_current_gear_string()   { return get_gear_string(); }
    inline float       get_current_engine_rpm()    { return engine_rpm; }
    inline bool        get_is_shifting()           { return is_shifting; }
    inline float       get_clutch()                { return clutch; }
    inline float       get_engine_torque_current() { return get_engine_torque(engine_rpm) * (1.0f + boost_pressure * tuning::spec.boost_torque_mult); }
    inline float       get_redline_rpm()           { return tuning::spec.engine_redline_rpm; }
    inline float       get_max_rpm()               { return tuning::spec.engine_max_rpm; }
    inline float       get_idle_rpm()              { return tuning::spec.engine_idle_rpm; }

    inline void  set_turbo_enabled(bool enabled)
    {
        tuning::spec.turbo_enabled = enabled;
        // if enabling turbo on a preset that has no turbo configured, fall back to sensible defaults
        // so the gauge actually reads non-zero when the engine spools
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

    // drs
    inline void set_drs_enabled(bool enabled) { tuning::spec.drs_enabled = enabled; }
    inline bool get_drs_enabled()             { return tuning::spec.drs_enabled; }
    inline void set_drs_active(bool active)   { drs_active = active; }
    inline bool get_drs_active()              { return drs_active; }

    // differential type
    inline void set_diff_type(int type)       { tuning::spec.diff_type = PxClamp(type, 0, 2); }
    inline int  get_diff_type()               { return tuning::spec.diff_type; }
    inline const char* get_diff_type_name()
    {
        static const char* names[] = { "Open", "Locked", "LSD" };
        return (tuning::spec.diff_type >= 0 && tuning::spec.diff_type <= 2) ? names[tuning::spec.diff_type] : "?";
    }

    // tire wear
    inline float get_wheel_wear(int i)              { return is_valid_wheel(i) ? wheels[i].wear : 0.0f; }
    inline void  reset_tire_wear()                  { for (int i = 0; i < wheel_count; i++) wheels[i].wear = 0.0f; }
    inline float get_wheel_wear_grip_factor(int i)  { return is_valid_wheel(i) ? (1.0f - wheels[i].wear * tuning::spec.tire_grip_wear_loss) : 1.0f; }

    inline float get_wheel_brake_temp(int i)       { return is_valid_wheel(i) ? wheels[i].brake_temp : 0.0f; }
    inline float get_wheel_brake_efficiency(int i) { return is_valid_wheel(i) ? get_brake_efficiency(wheels[i].brake_temp) : 1.0f; }

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

    inline void set_draw_raycasts(bool enabled)   { tuning::draw_raycasts = enabled; }
    inline bool get_draw_raycasts()               { return tuning::draw_raycasts; }
    inline void set_draw_suspension(bool enabled) { tuning::draw_suspension = enabled; }
    inline bool get_draw_suspension()             { return tuning::draw_suspension; }

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
