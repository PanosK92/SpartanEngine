#pragma once

#include <cstdio>
#include "../Core/Engine.h"

namespace car
{
    inline void reset_drivetrain_transients();
    inline void reset_wheel_thermals();

    enum class validation_scenario
    {
        settle,
        acceleration,
        braking,
        coastdown,
        skidpad,
        step_steer,
        slalom,
        curb,
        single_wheel_bump,
        count
    };

    struct validation_state
    {
        bool initialized = false;
        bool completed = false;
        bool requested = false;
        validation_scenario scenario = validation_scenario::settle;
        float elapsed = 0.0f;
        float reached_speed_time = -1.0f;
        float max_lateral_g = 0.0f;
        float max_yaw_rate = 0.0f;
        float max_compression = 0.0f;
        float minimum_up = 1.0f;
        bool event_applied = false;
        PxTransform start_pose = PxTransform(PxIdentity);
        PxVec3 scenario_start = PxVec3(0.0f);
        FILE* report = nullptr;
    };

    inline validation_state validation;

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
        const validation_targets& targets = tuning::spec.validation;
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
}
