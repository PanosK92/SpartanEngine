/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "AiDriver.h"
#include "Car.h"
#include "CarPresets.h"
#include "CarSimulation.h"
#include "RacingLine.h"
#include "../world/Entity.h"
#include "../world/components/Physics.h"

using namespace std;
using namespace spartan::math;

namespace spartan
{
    namespace
    {
        constexpr float gravity = 9.81f;

        // lateral tracking, natural frequency in rad/s, damping ratio and a slow integral for the understeer a steady corner leaves
        constexpr float line_frequency     = 1.5f;
        constexpr float line_damping       = 1.0f;
        constexpr float line_integral_gain = 0.4f;

        // traffic
        constexpr float traffic_range   = 250.0f; // meters, cars further away are ignored
        constexpr float car_length      = 4.6f;   // center to center when two cars touch nose to tail
        constexpr float corridor        = 2.3f;   // lateral center to center distance two cars pass each other at
        constexpr float pass_spacing    = 3.4f;   // how far beside a car the driver passes it
        constexpr float edge_clearance  = 1.3f;   // meters between the car's center and the edge of the road when passing

        Vector3 planar(Vector3 value)
        {
            value.y = 0.0f;
            return value;
        }

        Vector3 planar_direction(Vector3 value)
        {
            value.y = 0.0f;
            if (value.LengthSquared() > 1e-8f)
            {
                value.Normalize();
            }
            return value;
        }

        constexpr uint32_t wheel_count = static_cast<uint32_t>(WheelIndex::Count);
    }

    void AiDriver::CarModel::Seed(Physics* physics)
    {
        car::Simulation* simulation = physics ? physics->GetVehicleSimulation() : nullptr;
        if (!simulation)
        {
            return;
        }
        const car::car_preset& spec = simulation->get_spec();
        if (spec.mass > 100.0f)
        {
            mass = spec.mass;
        }
        const float lift = -(spec.lift_coeff_front + spec.lift_coeff_rear);
        aero_prior       = clamp(0.5f * 1.225f * lift * spec.frontal_area / (mass * gravity), 0.0f, 5e-4f);
        aero             = aero_prior;
        grip_lateral     = clamp(spec.tire_friction * fabsf(spec.lat_D) * 0.8f, 0.4f, 2.5f) * gravity;
        long_ratio       = clamp(fabsf(spec.long_D) / max(fabsf(spec.lat_D), 0.1f), 0.7f, 1.4f);
        // the magic formula peaks where B * slip reaches tan(pi / 2C)
        const float shape = max(spec.long_C, 1.05f);
        peak_slip         = clamp(tanf(3.14159265f / (2.0f * shape)) / max(spec.long_B, 1.0f), 0.04f, 0.3f);
    }

    void AiDriver::CarModel::Measure(Physics* physics, float delta_time, float speed, bool on_road)
    {
        // every tire reports the peak force it could make right now, their sum over the mass is what the car could corner with
        float peak_lateral      = 0.0f;
        float peak_longitudinal = 0.0f;
        float load              = 0.0f;
        uint32_t grounded       = 0;
        // an axle is as busy as its force over its peak, the light inside wheel saturating alone does not stop the car turning
        float front_used = 0.0f, front_peak = 0.0f, rear_used = 0.0f, rear_peak = 0.0f, front_lateral = 0.0f, rear_lateral = 0.0f;
        for (uint32_t w = 0; w < wheel_count; w++)
        {
            const WheelIndex wheel = static_cast<WheelIndex>(w);
            const float peak       = physics->GetWheelPeakLateralForce(wheel);
            const float used       = physics->GetWheelFrictionUse(wheel) * peak;
            const float lateral    = fabsf(physics->GetWheelLateralForce(wheel));
            if (wheel == WheelIndex::FrontLeft || wheel == WheelIndex::FrontRight)
            {
                front_used    += used;
                front_peak    += peak;
                front_lateral += lateral;
            }
            else
            {
                rear_used    += used;
                rear_peak    += peak;
                rear_lateral += lateral;
            }
            if (physics->GetWheelTireLoad(wheel) > 50.0f)
            {
                grounded++;
                load              += physics->GetWheelTireLoad(wheel);
                peak_lateral      += peak;
                peak_longitudinal += physics->GetWheelPeakLongitudinalForce(wheel);
            }
        }
        const float smoothing = min(1.0f, delta_time / 0.12f);
        front_use            += ((front_peak > 1.0f ? front_used / front_peak : 0.0f) - front_use) * smoothing;
        rear_use             += ((rear_peak > 1.0f ? rear_used / rear_peak : 0.0f) - rear_use) * smoothing;
        lateral_use          += (max(front_peak > 1.0f ? front_lateral / front_peak : 0.0f, rear_peak > 1.0f ? rear_lateral / rear_peak : 0.0f) - lateral_use) * smoothing;
        if (grounded < 4 || !on_road || peak_lateral <= 0.0f)
        {
            return;
        }

        // standing still the tires carry exactly the weight they will have to turn, unsprung parts included
        if (fabsf(speed) < 0.3f)
        {
            mass += (load / gravity - mass) * min(1.0f, delta_time * 2.0f);
        }

        // weighted least squares of grip against v^2 that slowly forgets, so warming or wearing tires move it
        const float forget = expf(-delta_time / 40.0f);
        const float x      = speed * speed;
        const float y      = peak_lateral / mass;
        fit_weight         = fit_weight * forget + delta_time;
        fit_x              = fit_x * forget + delta_time * x;
        fit_y              = fit_y * forget + delta_time * y;
        fit_xx             = fit_xx * forget + delta_time * x * x;
        fit_xy             = fit_xy * forget + delta_time * x * y;
        long_ratio        += (clamp(peak_longitudinal / peak_lateral, 0.5f, 2.0f) - long_ratio) * min(1.0f, delta_time);
        SolveFit();
    }

    void AiDriver::CarModel::SolveFit()
    {
        // the prior pulls the slope toward the lift coefficients until the car has been fast enough to measure it
        const float reference = 2500.0f;
        const float prior     = max(fit_weight, 0.05f) * reference * reference * 0.05f;
        const float a11       = fit_weight;
        const float a12       = fit_x;
        const float a22       = fit_xx + prior;
        const float r1        = fit_y;
        const float r2        = fit_xy + prior * grip_lateral * aero_prior;
        const float det       = a11 * a22 - a12 * a12;
        if (a11 < 0.05f || fabsf(det) < 1e-9f * a22)
        {
            return;
        }
        const float intercept = (r1 * a22 - a12 * r2) / det;
        const float slope     = (a11 * r2 - a12 * r1) / det;
        if (intercept > 2.0f && intercept < 40.0f)
        {
            grip_lateral = intercept;
            aero         = clamp(slope / intercept, -5e-5f, 5e-4f);
        }
    }

    float AiDriver::CarModel::CorneringLimit(float speed) const
    {
        return min(grip_lateral * balance * (1.0f + aero * speed * speed), roll_limit);
    }

    float AiDriver::CarModel::BrakingLimit(float speed) const
    {
        return grip_lateral * long_ratio * brake_efficiency * (1.0f + aero * speed * speed);
    }

    float AiDriver::CarModel::Key() const
    {
        return CorneringLimit(30.0f) + BrakingLimit(30.0f) + aero * 1e4f;
    }

    AiDriver::AiDriver(shared_ptr<RacingLine> line, const AiDriverSettings& settings) : m_line(move(line)), m_settings(settings)
    {
        m_settings.skill     = clamp(m_settings.skill, 0.0f, 1.0f);
        m_settings.max_speed = clamp(m_settings.max_speed, 5.0f, 150.0f);
    }

    AiDriver::~AiDriver()
    {
        if (m_trace)
        {
            fclose(m_trace);
        }
    }

    Physics* AiDriver::GetPhysics() const
    {
        Entity* vehicle = m_car ? m_car->GetRootEntity() : nullptr;
        return vehicle ? vehicle->GetComponent<Physics>() : nullptr;
    }

    void AiDriver::Possess(Car* car)
    {
        m_car  = car;
        m_name = car->GetDisplayName();

        Physics* physics                  = GetPhysics();
        m_handback.externally_controlled  = car->IsExternallyControlled();
        car->SetExternallyControlled(true);
        if (physics)
        {
            m_handback.manual_transmission = physics->GetManualTransmission();
            m_handback.brake_reverse       = physics->GetVehicleBrakeReverseEnabled();
            m_handback.abs                 = physics->GetAbsEnabled();
            m_handback.tc                  = physics->GetTcEnabled();
            physics->SetManualTransmission(false);
            physics->SetVehicleBrakeReverseEnabled(false);
            physics->SetAbsEnabled(true);
            physics->SetTcEnabled(true);
            if (car::Simulation* simulation = physics->GetVehicleSimulation())
            {
                car::car_preset& spec                = simulation->get_spec();
                m_handback.steering_speed_reduction  = spec.assists.steering_speed_reduction;
                m_handback.yaw_control               = spec.yaw_control_enabled;
                // the speed steering limit is a keyboard aid that would cap the driver's lock, the yaw controller is the car's own stability system
                spec.assists.steering_speed_reduction = 0.0f;
                spec.yaw_control_enabled              = true;
            }
        }

        m_model = CarModel();
        m_model.Seed(physics);
        m_plan.assign(m_line->GetCount(), Plan());
        m_stats = AiDriverStats();
        BuildPlan();

        // take over from where the car is, one off the road or facing backwards starts on the line instead
        Entity* vehicle = car->GetRootEntity();
        const RacingLine::Projection projection = m_line->Project(vehicle->GetPosition(), 0, true);
        const RacingLine::Point& point          = m_line->GetPoint(projection.index);
        const bool on_road                      = projection.center_offset > -point.room_left - 2.0f && projection.center_offset < point.room_right + 2.0f;
        const bool facing                       = planar_direction(vehicle->GetForward()).Dot(m_line->DirectionAt(projection.index)) > 0.3f;
        PlaceOnLine(projection.index);
        if (on_road && facing)
        {
            m_distance      = projection.distance;
            m_line_error    = projection.line_error;
            m_center_offset = projection.center_offset;
        }
        else
        {
            m_car->PlaceAt(point.position, Quaternion::FromLookRotation(m_line->DirectionAt(projection.index), Vector3::Up));
        }

        m_hold_time = m_settings.launch_delay;
        m_timing    = min(m_distance, m_line->GetLength() - m_distance) < 10.0f;
        if (m_settings.verbose && !m_trace)
        {
            fopen_s(&m_trace, "ai_driver_trace.csv", "w");
            if (m_trace)
            {
                fprintf(m_trace, "time,lap,distance,speed,target,plan,line_error,curvature,feedforward,steer,throttle,brake,front_use,rear_use,lateral_use,body_slip,lateral_accel,long_accel,oversteer,wide,gear\n");
            }
        }
        SP_LOG_INFO("ai_driver %s: taking over at %.0f m%s, mass %.0f kg, grip %.2f g, aero %.2e, ideal lap %.2f s",
            m_name.c_str(), m_distance, on_road && facing ? "" : " (placed on the line)", m_model.mass, m_model.grip_lateral / gravity, m_model.aero, m_stats.ideal_lap);
    }

    void AiDriver::Release()
    {
        if (!m_car)
        {
            return;
        }
        m_car->SetThrottle(0.0f);
        m_car->SetBrake(0.0f);
        m_car->SetSteering(0.0f);
        m_car->SetHandbrake(0.0f);
        if (Physics* physics = GetPhysics())
        {
            physics->SetManualTransmission(m_handback.manual_transmission);
            physics->SetVehicleBrakeReverseEnabled(m_handback.brake_reverse);
            physics->SetAbsEnabled(m_handback.abs);
            physics->SetTcEnabled(m_handback.tc);
            if (car::Simulation* simulation = physics->GetVehicleSimulation())
            {
                car::car_preset& spec                = simulation->get_spec();
                spec.assists.steering_speed_reduction = m_handback.steering_speed_reduction;
                spec.yaw_control_enabled              = m_handback.yaw_control;
            }
        }
        m_car->SetExternallyControlled(m_handback.externally_controlled);
        SP_LOG_INFO("ai_driver %s: handing the car back after %u laps", m_name.c_str(), m_stats.lap);
        m_car = nullptr;
    }

    float AiDriver::Margin() const
    {
        return 0.84f + 0.12f * m_settings.skill;
    }

    float AiDriver::SpeedAt(float distance) const
    {
        float fraction = 0.0f;
        const size_t i = m_line->IndexAt(distance, fraction);
        return m_plan[i].speed + (m_plan[m_line->Wrap(static_cast<int64_t>(i) + 1)].speed - m_plan[i].speed) * fraction;
    }

    void AiDriver::BuildPlan()
    {
        // corners cap the speed at what the grip holds, then a backward pass adds the braking zones before them
        // braking shares the tires with cornering, so a car still turning brakes softer (trail braking)
        const size_t count = m_plan.size();
        const float margin = Margin();
        for (size_t i = 0; i < count; i++)
        {
            // downforce grows with v^2 like the corner's demand, v^2 kappa = g0 (1 + aero v^2), so a fast enough car holds a sweeper flat
            Plan& plan        = m_plan[i];
            const float grip  = m_model.grip_lateral * m_model.balance * margin * plan.grip_scale;
            const float kappa = max(fabsf(m_line->GetPoint(i).curvature), 1e-5f);
            const float room  = kappa - grip * m_model.aero;
            float speed       = room > 1e-6f ? sqrtf(grip / room) : m_settings.max_speed;
            speed             = min(speed, sqrtf(m_model.roll_limit * margin * plan.grip_scale / kappa));
            plan.speed        = min(m_settings.max_speed, speed);
        }
        // a route ends standing still, a closed loop needs a second lap of passes to carry braking zones across the line
        const bool closed = m_line->IsClosed();
        if (!closed)
        {
            m_plan[count - 1].speed = 0.0f;
        }
        for (size_t pass = 0; pass < (closed ? count * 2 : count); pass++)
        {
            const size_t i                 = count - 1 - (pass % count);
            const RacingLine::Point& point = m_line->GetPoint(i);
            Plan& plan                     = m_plan[i];
            const Plan& next               = m_plan[m_line->Wrap(static_cast<int64_t>(i) + 1)];
            const float grip               = m_model.CorneringLimit(next.speed) * margin * plan.grip_scale;
            const float lateral_use        = min(next.speed * next.speed * fabsf(point.curvature) / max(grip, 0.1f), 1.0f);
            const float decel              = m_model.BrakingLimit(next.speed) * margin * sqrtf(max(1.0f - lateral_use * lateral_use, 0.15f));
            plan.speed                     = min(plan.speed, sqrtf(next.speed * next.speed + 2.0f * decel * point.step));
        }
        m_plan_key          = m_model.Key();
        m_stats.cornering_g = m_model.CorneringLimit(30.0f) * margin / gravity;
        m_stats.braking_g   = m_model.BrakingLimit(30.0f) * margin / gravity;

        m_stats.ideal_lap = 0.0f;
        for (size_t i = 0; i + (closed ? 0 : 1) < count; i++)
        {
            const float speed  = (m_plan[i].speed + m_plan[m_line->Wrap(static_cast<int64_t>(i) + 1)].speed) * 0.5f;
            m_stats.ideal_lap += m_line->GetPoint(i).step / max(speed, 1.0f);
        }
    }

    void AiDriver::PlaceOnLine(size_t index)
    {
        m_index          = index;
        m_distance       = m_line->GetPoint(index).distance;
        m_line_error     = 0.0f;
        m_center_offset  = m_line->GetPoint(index).offset;
        m_steering       = 0.0f;
        m_line_integral  = 0.0f;
        m_throttle       = 0.0f;
        m_stuck_time     = 0.0f;
        m_upside_time    = 0.0f;
        m_wrong_way_time = 0.0f;
        m_previous_speed = 0.0f;
        m_yaw_rate       = 0.0f;
        m_drift_accel    = 0.0f;
        m_previous_delta = 0.0f;
        m_decel          = 0.0f;
        m_saturated_time = 0.0f;
        m_brake_limit    = 1.0f;
        m_pass_target    = 0.0f;
        m_pass_offset    = 0.0f;
        m_pass_rate      = 0.0f;
        m_blocked_time   = 0.0f;
        m_blocker        = Blocker();
    }

    void AiDriver::WatchTraffic(float delta_time, float speed, float plan_speed)
    {
        const Vector3 position = m_car->GetRootEntity()->GetPosition();
        const float length     = m_line->GetLength();

        // lateral is right of the racing line, speed is along the line
        struct Nearby
        {
            float along   = 0.0f;
            float lateral = 0.0f;
            float speed   = 0.0f;
            size_t index  = 0;
        };
        vector<Nearby> nearby;
        unordered_map<const Car*, TrafficCar> seen;
        for (Car* other : Car::GetAll())
        {
            Entity* root = other && other != m_car ? other->GetRootEntity() : nullptr;
            if (!root || !root->IsActive())
            {
                continue;
            }
            const Vector3 at = root->GetPosition();
            if (Vector3::DistanceSquared(at, position) > traffic_range * traffic_range)
            {
                continue;
            }
            TrafficCar track = { at, Vector3::Zero };
            const auto known = m_traffic.find(other);
            if (known != m_traffic.end())
            {
                // a respawn or a streaming jump is not a speed
                const Vector3 measured = (at - known->second.position) / delta_time;
                track.velocity         = measured.Length() < 90.0f ? known->second.velocity + (measured - known->second.velocity) * min(1.0f, delta_time / 0.4f) : known->second.velocity;
            }
            seen[other] = track;

            // the projection only searches a window ahead, a second one further on reaches oncoming cars in time
            RacingLine::Projection projection = m_line->Project(at, m_index, false);
            if (projection.distance - m_distance > 100.0f)
            {
                projection = m_line->Project(at, m_line->Wrap(static_cast<int64_t>(m_index) + 55), false);
            }
            float along = projection.distance - m_distance;
            if (m_line->IsClosed() && fabsf(along) > length * 0.5f)
            {
                along -= copysignf(length, along);
            }
            const RacingLine::Point& point = m_line->GetPoint(projection.index);
            const bool on_line = projection.center_offset > -point.room_left - 1.5f && projection.center_offset < point.room_right + 1.5f && fabsf(at.y - point.position.y) < 4.0f;
            if (on_line && along > -15.0f && along < 230.0f)
            {
                nearby.push_back({ along, projection.line_error, track.velocity.Dot(m_line->DirectionAt(projection.index)), projection.index });
                continue;
            }

            // a car off the path now may be on it soon, pulling out of a side road or crossing a junction
            // it counts where it will be if it gets there around when this car does
            if (planar(track.velocity).LengthSquared() < 1.0f)
            {
                continue;
            }
            for (float t : { 0.75f, 1.5f, 2.25f })
            {
                const Vector3 future                   = at + planar(track.velocity) * t;
                const RacingLine::Projection crossing  = m_line->Project(future, m_index, false);
                const RacingLine::Point& future_point  = m_line->GetPoint(crossing.index);
                const bool crosses = crossing.center_offset > -future_point.room_left - 0.5f && crossing.center_offset < future_point.room_right + 0.5f && fabsf(future.y - future_point.position.y) < 4.0f;
                float future_along = crossing.distance - m_distance;
                if (m_line->IsClosed() && fabsf(future_along) > length * 0.5f)
                {
                    future_along -= copysignf(length, future_along);
                }
                const float arrival = future_along / max(speed, 3.0f);
                if (crosses && future_along > 0.0f && future_along < 120.0f && fabsf(arrival - t) < 1.5f)
                {
                    nearby.push_back({ future_along, crossing.line_error, track.velocity.Dot(m_line->DirectionAt(crossing.index)), crossing.index });
                    break;
                }
            }
        }
        m_traffic = move(seen);

        // an offset fits where the car keeps its clearance from both road edges, here and beside the car it passes
        auto fits = [&](float offset, size_t index)
        {
            for (size_t i : { m_index, index })
            {
                const RacingLine::Point& point = m_line->GetPoint(i);
                const float center             = point.offset + offset;
                if (center < -point.room_left + edge_clearance || center > point.room_right - edge_clearance)
                {
                    return false;
                }
            }
            return true;
        };

        // the car in the way: in the path the driver is heading for, or still in the one it is leaving
        const Nearby* blocker = nullptr;
        for (const Nearby& car : nearby)
        {
            const bool ahead = car.along > 0.0f;
            const bool path  = fabsf(car.lateral - m_pass_target) < corridor || (fabsf(car.lateral - m_pass_offset) < corridor && car.along < 12.0f);
            if (ahead && path && (!blocker || car.along < blocker->along))
            {
                blocker = &car;
            }
        }

        // pass on the left first like a driver on a right hand road, the right if only that side is open
        // the side lane must be free of cars going the same way beside the one being passed, and of oncoming cars for as long as the pass takes
        if (blocker && m_pass_target == 0.0f && blocker->along < 60.0f && blocker->speed < plan_speed - 3.0f && speed > 3.0f)
        {
            const float closing   = max(speed - max(blocker->speed, 0.0f), 4.0f);
            const float pass_time = min((blocker->along + car_length * 2.0f + 10.0f) / closing, 12.0f);
            for (float side : { -1.0f, 1.0f })
            {
                const float offset = blocker->lateral + side * pass_spacing;
                bool clear         = fits(offset, blocker->index);
                for (const Nearby& car : nearby)
                {
                    if (!clear)
                    {
                        break;
                    }
                    if (&car == blocker || fabsf(car.lateral - offset) > corridor + 0.5f)
                    {
                        continue;
                    }
                    if (car.speed > -1.0f)
                    {
                        clear = car.along < -10.0f || car.along > blocker->along + 25.0f;
                    }
                    else
                    {
                        clear = car.along < 0.0f || car.along > blocker->along + 30.0f + (speed - car.speed) * pass_time;
                    }
                }
                if (clear)
                {
                    m_pass_target = offset;
                    SP_LOG_INFO("ai_driver %s: passing a car %.0f m ahead doing %.0f km/h, %.1f m to the %s", m_name.c_str(), blocker->along, blocker->speed * 3.6f, fabsf(offset), offset < 0.0f ? "left" : "right");
                    blocker = nullptr;
                    break;
                }
            }
        }
        else if (m_pass_target != 0.0f)
        {
            // back to the line once nothing on it would start a pass again, a car length behind to past where passes start
            // or at once when an oncoming car shows up
            bool line_clear = true;
            bool danger     = false;
            for (const Nearby& car : nearby)
            {
                const bool close = car.along > -car_length - 3.0f && car.along < car_length + 5.0f;
                const bool slow  = car.along > 0.0f && car.along < 70.0f && car.speed < plan_speed - 1.0f;
                if (fabsf(car.lateral) < corridor + 0.3f && (close || slow))
                {
                    line_clear = false;
                }
                if (fabsf(car.lateral - m_pass_target) < corridor && car.speed < -1.0f && car.along > 0.0f && car.along < (speed - car.speed) * 2.5f + 20.0f)
                {
                    danger = true;
                }
            }
            if (line_clear || danger || !fits(m_pass_target, m_index))
            {
                SP_LOG_INFO("ai_driver %s: %s", m_name.c_str(), danger ? "oncoming car, pulling back in" : "passed, back on the line");
                m_pass_target = 0.0f;
            }
        }

        const float rate = clamp(speed * 0.12f, 1.5f, 3.5f);
        const float step = clamp(m_pass_target - m_pass_offset, -rate * delta_time, rate * delta_time);
        m_pass_offset   += step;
        m_pass_rate      = step / delta_time;

        m_blocker = Blocker();
        if (blocker)
        {
            m_blocker = { true, max(blocker->along - car_length, 0.0f), blocker->speed };
        }
        m_blocked_time       = m_blocker.found && speed < 1.0f ? m_blocked_time + delta_time : 0.0f;
        m_stats.passing      = m_pass_target != 0.0f || fabsf(m_pass_offset) > 0.2f;
        m_stats.following    = m_blocker.found && m_blocker.gap < 60.0f;
    }

    void AiDriver::Tick(float delta_time)
    {
        Physics* physics = GetPhysics();
        Entity* vehicle  = m_car ? m_car->GetRootEntity() : nullptr;
        if (!physics || !vehicle || m_plan.empty() || delta_time <= 0.0f)
        {
            return;
        }
        m_line->KeepCollisionLoaded();
        // leaving the car hands its controls to the player, the driver keeps them until it is released
        m_car->SetExternallyControlled(true);

        float wheelbase = 2.65f, max_steer = 0.6f, linearity = 1.0f;
        m_car->GetSteeringGeometry(wheelbase, max_steer, linearity);

        const Vector3 position = vehicle->GetPosition();
        const Vector3 forward  = vehicle->GetForward();
        const Vector3 velocity = physics->GetLinearVelocity();
        const float speed      = velocity.Dot(forward);

        const size_t previous_index = m_index;
        RacingLine::Projection projection = m_line->Project(position, m_index, false);
        if (fabsf(projection.line_error) > 30.0f)
        {
            projection = m_line->Project(position, m_index, true);
        }
        float moved = projection.distance - m_distance;
        if (m_line->IsClosed() && fabsf(moved) > m_line->GetLength() * 0.5f)
        {
            moved -= copysignf(m_line->GetLength(), moved);
        }
        m_stats.travelled             += moved;
        m_index                        = projection.index;
        m_distance                     = projection.distance;
        m_line_error                   = projection.line_error;
        m_center_offset                = projection.center_offset;
        const size_t count             = m_line->GetCount();
        const RacingLine::Point& here  = m_line->GetPoint(m_index);
        Plan& plan_here                = m_plan[m_index];

        const bool on_road = m_center_offset > -here.room_left + 1.0f && m_center_offset < here.room_right - 1.0f;
        m_model.Measure(physics, delta_time, speed, on_road);

        // yaw rate from the body, frame to frame heading jitters whenever a frame sees two physics steps or none
        // both positive to the right like the curvature
        const Vector3 flat_forward = planar_direction(forward);
        const Vector3 flat_right   = Vector3::Up.Cross(flat_forward);
        m_yaw_rate                += (physics->GetAngularVelocity().y - m_yaw_rate) * min(1.0f, delta_time / 0.06f);
        const float body_slip      = speed > 3.0f ? atan2f(planar(velocity).Dot(flat_right), speed) : 0.0f;
        const float lateral_accel  = speed * m_yaw_rate;

        // grid: hold the car on the handbrake, then launch and start the lap clock
        if (m_hold_time > 0.0f)
        {
            m_car->SetThrottle(0.0f);
            m_car->SetBrake(0.0f);
            m_car->SetSteering(0.0f);
            m_car->SetHandbrake(1.0f);
            m_hold_time -= delta_time;
            if (m_hold_time <= 0.0f)
            {
                m_stats.lap_time = 0.0f;
                BuildPlan();
                SP_LOG_INFO("ai_driver %s: launch, mass %.0f kg, grip %.2f g, aero %.2e, long ratio %.2f, ideal lap %.2f s", m_name.c_str(), m_model.mass, m_model.grip_lateral / gravity, m_model.aero, m_model.long_ratio, m_stats.ideal_lap);
            }
            return;
        }

        // the end of a route: stop there and keep the car parked
        const float to_finish = m_line->GetLength() - m_distance;
        if (!m_line->IsClosed() && !m_stats.finished && (to_finish < 1.0f || (to_finish < 8.0f && fabsf(speed) < 0.5f)))
        {
            m_stats.finished      = true;
            m_stats.last_lap_time = m_stats.lap_time;
            SP_LOG_INFO("ai_driver %s: route finished, %.0f m in %.1f s, %u resets", m_name.c_str(), m_stats.travelled, m_stats.lap_time, m_stats.resets);
        }
        if (m_stats.finished)
        {
            m_car->SetThrottle(0.0f);
            m_car->SetBrake(1.0f);
            m_car->SetSteering(0.0f);
            m_car->SetHandbrake(1.0f);
            m_stats.speed_kmh  = speed * 3.6f;
            m_stats.target_kmh = 0.0f;
            return;
        }
        m_stats.lap_time += delta_time;

        WatchTraffic(delta_time, speed, SpeedAt(m_distance + max(speed, 0.0f) * 0.4f));
        // the line is followed at the passing offset, the error the controller sees is from there
        const float tracking_error = m_line_error - m_pass_offset;

        // steering: the line's curvature a moment ahead is the feedforward, a critically damped lateral acceleration pulls the car back onto the line
        // commanding acceleration instead of an angle keeps the gain right at every speed, where pure pursuit weaves once the car is quick
        // the lateral velocity term uses the real velocity, so a sliding car is caught by where it is going rather than where it points
        // the car turns its wheel at a limited rate like a player's, so a command arrives late and a loop that ignores it weaves
        // the controller acts on where the car will be once the wheel gets there, and the wheel angle it tracks is the car's
        float steering_rate    = 20.0f;
        float applied_steering = m_steering;
        float deadzone         = 0.0f;
        if (car::Simulation* simulation = physics->GetVehicleSimulation())
        {
            steering_rate    = max(simulation->get_spec().steering_rate, 0.1f);
            deadzone         = clamp(simulation->get_spec().steering_deadzone, 0.0f, 0.5f);
            const float raw  = simulation->get_steering();
            applied_steering = copysignf(max(fabsf(raw) - deadzone, 0.0f) / (1.0f - deadzone), raw);
        }
        // the rack's travel to the last command, then the tires and the body take about a tenth of a second to answer
        const float response_lag  = clamp(fabsf(m_steering - applied_steering) / steering_rate, 0.0f, 0.25f) + 0.1f;

        const float preview       = max(speed, 0.0f) * (0.15f + response_lag);
        const float feedforward   = m_line->CurvatureAt(m_distance + preview);
        // measured against the line's own direction, where the line crosses the road the road's normal would read its sweep as drift
        const float lateral_speed_now = planar(velocity).Dot(here.line_right);
        // sideways acceleration off the line keeps acting through the lag, measured from the velocity itself
        // speed times yaw rate is only the path's acceleration while the body slip holds still, a swinging tail reads as a g of drift that is not there
        if (m_previous_delta > 0.0f)
        {
            const float measured = (lateral_speed_now - m_previous_lateral_speed) / delta_time;
            m_drift_accel       += (clamp(measured, -30.0f, 30.0f) - m_drift_accel) * min(1.0f, delta_time / 0.1f);
        }
        m_previous_lateral_speed  = lateral_speed_now;
        m_previous_delta          = delta_time;
        const float drift_accel   = m_drift_accel;
        const float lateral_speed = lateral_speed_now + drift_accel * response_lag;
        const float predicted     = tracking_error + lateral_speed_now * response_lag + 0.5f * drift_accel * response_lag * response_lag;
        m_line_integral           = clamp(m_line_integral + tracking_error * delta_time, -4.0f, 4.0f);
        // a fast car's yaw answers later, a softer loop at speed keeps the correction from feeding a weave
        const float frequency     = clamp(line_frequency - (speed - 20.0f) * 0.012f, 0.9f, line_frequency);
        // the integral's lag and the car's own yaw lag add up at speed, so it shrinks with the loop and the damping grows
        const float slowdown      = frequency / line_frequency;
        const float damping       = line_damping + clamp((speed - 30.0f) * 0.02f, 0.0f, 0.5f);
        // far off the line the car closes in at a capped lateral speed, rushing back builds a sideways speed the tires then cannot stop
        const float approach      = max(1.5f, speed * 0.06f);
        const float desired_drift = m_pass_rate - clamp(frequency / (2.0f * damping) * predicted, -approach, approach);
        const float correction    = 2.0f * damping * frequency * (lateral_speed - desired_drift) + line_integral_gain * slowdown * slowdown * slowdown * m_line_integral;
        // never ask for more turn than the tires can give, past that the only thing extra steering buys is a spin
        const float max_curvature = m_model.CorneringLimit(speed) * 1.3f / max(speed * speed, 25.0f);
        const float wanted        = clamp(feedforward - correction / max(speed * speed, 25.0f), -max_curvature, max_curvature);
        // a driver feels the tail start to swing long before it slides: yaw beyond what the wheel asks for is taken back off the wheel
        // one sided, a car turning less than asked is understeer, which the slip gain below answers
        const float yawing        = speed > 8.0f ? m_yaw_rate / speed : 0.0f;
        const float yaw_excess    = yawing - wanted;
        const float curvature     = yaw_excess * yawing > 0.0f ? wanted - 0.6f * yaw_excess : wanted;
        // understeer is a steady corner's slip, so only the corner itself gets the boost, corrections stay kinematic or they multiply the loop gain
        // saturated fronts make no more force with more angle, only more scrub, so the boost stops there too
        const float front_room    = clamp((0.98f - m_model.front_use) / 0.08f, 0.0f, 1.0f);
        const float corner        = clamp(feedforward, -max_curvature, max_curvature);
        const float slip_gain     = 1.0f + min(m_model.understeer * speed * speed * fabsf(corner), 1.5f) * front_room;
        const float curved        = wheelbase * (curvature + corner * (slip_gain - 1.0f)) / max(tanf(max_steer), 0.05f);
        float steer               = copysignf(powf(min(fabsf(curved), 1.0f), 1.0f / max(linearity, 0.1f)), curved);

        // oversteer: the tail swings past the path, steer toward where the car is going like a driver catching a slide
        // tire force falls once a patch slides, so friction use alone cannot tell a slide from grip, body slip beyond this car's normal can
        // the normal slip is what a limit corner shows, a straight or a gentle bend asks for a fraction of it
        const float demand         = clamp(fabsf(here.curvature) * speed * speed / max(m_model.CorneringLimit(speed), 1.0f), 0.2f, 1.0f);
        const float slip_threshold = m_model.slip_normal * demand * 1.3f + 0.03f;
        const float rear_limit     = body_slip * m_yaw_rate < 0.0f ? clamp((fabsf(body_slip) - slip_threshold) / 0.06f, 0.0f, 1.0f) : 0.0f;
        const bool oversteer       = rear_limit > 0.0f;
        const bool spinning        = fabsf(body_slip) > 0.6f;
        if (oversteer)
        {
            const float excess = body_slip - copysignf(slip_threshold, body_slip);
            steer              = clamp(steer + (excess * 1.5f + body_slip * 0.3f) * rear_limit / max(max_steer, 0.1f), -1.0f, 1.0f);
        }
        m_steering += (steer - m_steering) * min(1.0f, delta_time * (oversteer ? 30.0f : 20.0f));

        // pedals: every speed the plan asks for within stopping distance sets the deceleration needed now
        // braking from that forecast instead of from the speed error means the car is never late into a corner
        float target_speed       = SpeedAt(m_distance + max(speed, 0.0f) * 0.4f);
        const float capacity     = m_model.BrakingLimit(speed);
        float needed             = 0.0f;
        // a slower car ahead: stop within the gap to it if it stopped dead, and settle at a gap that grows with speed
        float follow_speed       = numeric_limits<float>::max();
        if (m_blocker.found)
        {
            const float other    = max(m_blocker.speed, 0.0f);
            // racers run close, on a public road the gap is the one a careful driver keeps, room to react to a sudden stop
            const float time_gap = m_line->IsClosed() ? 0.6f : 1.4f;
            const float headway  = max(m_blocker.gap - 3.0f - max(speed, 0.0f) * time_gap, 0.0f);
            follow_speed         = sqrtf(other * other + capacity * 0.5f * 2.0f * headway);
            target_speed         = min(target_speed, follow_speed);
            if (speed > other)
            {
                needed = (speed * speed - other * other) / (2.0f * max(m_blocker.gap - 2.0f, 0.5f));
            }
        }
        {
            const float reaction = max(speed, 0.0f) * 0.2f;
            const float horizon  = min(speed * speed / max(capacity, 1.0f) + reaction + 20.0f, 600.0f);
            float ahead          = here.distance + here.step - m_distance;
            size_t k             = 1;
            const size_t reach = m_line->IsClosed() ? count : count - 1 - m_index;
            while (ahead < horizon && k <= reach)
            {
                const size_t i = m_line->Wrap(static_cast<int64_t>(m_index + k));
                if (m_plan[i].speed < speed)
                {
                    const float room = max(ahead - reaction, 1.0f);
                    needed           = max(needed, (speed * speed - m_plan[i].speed * m_plan[i].speed) / (2.0f * room));
                }
                ahead += m_line->GetPoint(i).step;
                k++;
            }
        }
        // the plan's braking zones are drawn at margin * capacity, so the need reaches that exactly at the braking point
        const float braking_point = capacity * Margin();
        const float error         = target_speed - speed;
        float throttle            = needed > braking_point * 0.75f ? 0.0f : clamp(0.3f + 0.35f * error, 0.0f, 1.0f);
        float brake               = needed > braking_point * 0.85f ? clamp(needed / max(capacity, 1.0f) * 1.15f, 0.0f, 1.0f) : 0.0f;
        brake                     = max(brake, clamp((speed - SpeedAt(m_distance) - 2.0f) * 0.2f, 0.0f, 1.0f));
        brake                     = max(brake, clamp((speed - follow_speed - 1.5f) * 0.15f, 0.0f, 0.6f));
        if (follow_speed < 1.0f)
        {
            throttle = 0.0f;
            brake    = max(brake, 0.3f);
        }

        // running wide: the front tires are saturated, lifting gives them back the grip the throttle was using
        const bool drifting_out = fabsf(here.curvature) > 0.004f && fabsf(tracking_error) > 1.0f && tracking_error * lateral_speed_now > 0.0f;
        const float wide        = drifting_out ? clamp((fabsf(tracking_error) - 1.0f) * 0.3f + fabsf(lateral_speed_now) * 0.25f, 0.0f, 1.0f) : 0.0f;
        throttle               *= 1.0f - wide;

        // the tires share one grip budget, power and brakes get what cornering leaves over, read from the tires rather than the plan
        // off the line the car turns less than the plan assumes, the plan would release a brake the tires still have room for
        const float lateral_use = clamp(m_model.lateral_use, 0.0f, 1.0f);
        const float remaining   = sqrtf(max(1.0f - lateral_use * lateral_use, 0.0f));
        throttle               *= max(remaining, 0.25f);
        brake                  *= max(remaining, 0.4f);

        // running wide with grip to spare: scrub speed, the corner only closes if the car slows
        if (drifting_out && lateral_use < 0.9f)
        {
            brake = max(brake, clamp((fabsf(tracking_error) - 1.0f) * 0.15f, 0.0f, 0.6f) * max(remaining, 0.4f));
        }
        // fronts at the limit while braking into a turn: ease the pedal so they can steer
        if (m_model.front_use > 0.97f && lateral_use > 0.3f)
        {
            brake *= 0.8f;
        }
        // spinning: both feet in, the slide ends sooner and slower
        if (spinning)
        {
            brake    = 1.0f;
            throttle = 0.0f;
        }

        // back off while the driven wheels spin or the rear steps out, easing rather than snapping so a lift does not swing the tail
        // past the tire's peak slip a spinning tire loses side grip first, which is what lets the tail wander on a straight
        float spin = 0.0f;
        for (uint32_t w = 0; w < wheel_count; w++)
        {
            spin = max(spin, physics->GetWheelSlipRatio(static_cast<WheelIndex>(w)));
        }
        float excess = max(spin - m_model.peak_slip * 1.1f, 0.0f) / m_model.peak_slip;
        if (oversteer)
        {
            excess += rear_limit * 0.6f;
        }
        throttle *= clamp(1.0f - excess, 0.15f, 1.0f);
        if (!on_road)
        {
            throttle *= 0.6f;
        }

        // the tires slip, so the car turns less than its wheel angle says, the yaw it really gets tells by how much
        // the wheel angle is the one the car has, a rack still travelling would read as understeer
        if (speed > 15.0f && fabsf(steer - m_steering) < 0.03f && fabsf(m_steering - applied_steering) < 0.02f && !oversteer)
        {
            const float achieved  = fabsf(m_yaw_rate) / speed;
            const float commanded = tanf(max_steer) * powf(fabsf(applied_steering), linearity) / wheelbase;
            const float lateral   = fabsf(lateral_accel);
            if (commanded > 0.004f && achieved > 0.002f && lateral > 3.0f && m_model.front_use < 0.95f)
            {
                const float measured = clamp((commanded / achieved - 1.0f) / lateral, 0.0f, 0.3f);
                m_model.understeer  += (measured - m_model.understeer) * min(1.0f, delta_time * 0.5f);
            }
        }

        // threshold braking: a locked tire neither stops nor steers and the car's abs may not free it, so find the pedal the wheels take
        // weighted by load, a light wheel skipping over its peak costs little stopping, the car sliding on all four is what matters
        float slip_load = 0.0f, load_sum = 0.0f;
        for (uint32_t w = 0; w < wheel_count; w++)
        {
            const WheelIndex wheel = static_cast<WheelIndex>(w);
            if (physics->IsWheelGrounded(wheel))
            {
                const float load  = physics->GetWheelTireLoad(wheel);
                slip_load        += min(physics->GetWheelSlipRatio(wheel), 0.0f) * load;
                load_sum         += load;
            }
        }
        const float wheel_lock = load_sum > 1.0f ? slip_load / load_sum : 0.0f;
        // past the peak the force falls slowly and abs hunts around it, a car heading for lock is one far beyond it
        const float lock_slip = max(m_model.peak_slip * 4.0f, 0.3f);
        if (brake > 0.0f && wheel_lock < -lock_slip)
        {
            m_brake_limit = max(min(m_brake_limit, brake) - delta_time * 3.0f, 0.5f);
        }
        else if (wheel_lock > -lock_slip * 0.85f)
        {
            m_brake_limit = min(m_brake_limit + delta_time * 2.0f, 1.0f);
        }
        brake = min(brake, wheel_lock < -0.7f ? 0.2f : m_brake_limit);

        // squeeze the pedal in, lift at once
        const float rate  = throttle > m_throttle ? 4.0f : 20.0f;
        m_throttle       += (throttle - m_throttle) * min(1.0f, delta_time * rate);

        // the controls have a deadzone, start past it so small corrections are not swallowed
        const float steering_input = fabsf(m_steering) > 1e-4f ? copysignf(deadzone + (1.0f - deadzone) * min(fabsf(m_steering), 1.0f), m_steering) : 0.0f;
        m_car->SetSteering(steering_input);
        m_car->SetThrottle(brake > 0.0f ? 0.0f : m_throttle);
        m_car->SetBrake(brake);
        m_car->SetHandbrake(0.0f);

        // the chassis: how much of the tires' total grip it reaches before the busier axle saturates, measured in any steady corner
        const bool steady = brake < 0.05f && m_throttle < 0.5f && fabsf(steer - m_steering) < 0.05f && fabsf(tracking_error) < 1.0f;
        if (on_road && steady && speed > 12.0f && !oversteer && fabsf(here.curvature) > 0.003f && m_model.lateral_use > 0.5f && m_model.lateral_use < 0.95f)
        {
            const float total      = m_model.grip_lateral * (1.0f + m_model.aero * speed * speed);
            const float reach      = fabsf(lateral_accel) / m_model.lateral_use / max(total, 0.1f);
            m_model.balance       += (clamp(reach, 0.5f, 1.0f) - m_model.balance) * min(1.0f, delta_time * 0.4f);
            // body slip grows with how hard the tires work, scaled to the limit it is this car's normal cornering slip
            m_model.slip_normal   += (clamp(fabsf(body_slip) / m_model.lateral_use, 0.0f, 0.25f) - m_model.slip_normal) * min(1.0f, delta_time * 0.4f);
        }

        // the brakes: deceleration in a straight hard stop against what the tires could give
        // only a pedal held down long enough for pressure and the car's pitch to settle, the ramp in reads low and would feed on itself
        m_decel           += ((m_previous_speed - speed) / delta_time - m_decel) * min(1.0f, delta_time * 10.0f);
        m_full_brake_time  = brake >= 0.95f ? m_full_brake_time + delta_time : 0.0f;
        if (on_road && m_full_brake_time > 0.3f && speed > 15.0f && fabsf(here.curvature) < 0.002f && wheel_lock > -0.3f && fabsf(body_slip) < 0.05f)
        {
            const float possible        = m_model.grip_lateral * m_model.long_ratio * (1.0f + m_model.aero * speed * speed);
            const float measured        = clamp(m_decel / max(possible, 0.1f), 0.25f, 1.2f);
            m_model.brake_efficiency   += (measured - m_model.brake_efficiency) * min(1.0f, delta_time * (measured < m_model.brake_efficiency ? 1.0f : 0.3f));
        }
        m_previous_speed = speed;

        // tall cars lean before they slide, what they cornered at when the lean got dangerous becomes their limit
        if (fabsf(vehicle->GetRight().y) > 0.2f && speed > 8.0f && fabsf(lateral_accel) > 3.0f)
        {
            m_model.roll_limit = min(m_model.roll_limit, fabsf(lateral_accel) * 0.9f);
            plan_here.trouble  = true;
        }

        // the car learned something new about itself, fold it into the plan
        m_rebuild_time += delta_time;
        if (m_rebuild_time > 0.5f)
        {
            m_rebuild_time = 0.0f;
            if (fabsf(m_model.Key() - m_plan_key) > m_plan_key * 0.015f)
            {
                BuildPlan();
            }
        }

        // near misses teach as much as crashes: running wide, a slide, saturated fronts pushing off line, a wheel on the grass
        m_lap_error_max     = max(m_lap_error_max, fabsf(tracking_error));
        const bool off_road = m_center_offset < -here.room_left - 0.3f || m_center_offset > here.room_right + 0.3f;
        m_saturated_time    = m_model.front_use > 0.97f && drifting_out ? m_saturated_time + delta_time : 0.0f;
        if (fabsf(tracking_error) > 2.0f || (oversteer && rear_limit >= 1.0f) || m_saturated_time > 0.3f || off_road)
        {
            plan_here.trouble = true;
        }
        if (!m_excursion && fabsf(tracking_error) > 4.0f)
        {
            m_excursion = true;
            SP_LOG_WARNING("ai_driver %s: off line at %.0f m, error %.1f m, speed %.0f km/h, target %.0f km/h, brake %.2f, throttle %.2f, steer %.2f, use f %.2f r %.2f, slip %.3f, oversteer %d",
                m_name.c_str(), m_distance, tracking_error, speed * 3.6f, SpeedAt(m_distance) * 3.6f, brake, m_throttle, m_steering, m_model.front_use, m_model.rear_use, body_slip, oversteer ? 1 : 0);
        }
        else if (m_excursion && fabsf(tracking_error) < 1.5f)
        {
            m_excursion = false;
        }

        m_stats.speed_kmh  = speed * 3.6f;
        m_stats.target_kmh = target_speed * 3.6f;
        m_stats.distance   = m_distance;
        m_stats.line_error = m_line_error;
        m_stats.front_use  = m_model.front_use;
        m_stats.rear_use   = m_model.rear_use;
        m_stats.body_slip  = body_slip;

        // lap: the line index wraps from the end back to the start, the time guard ignores jitter around the line
        if (m_line->IsClosed() && previous_index > count * 3 / 4 && m_index < count / 4 && m_stats.lap_time > 10.0f)
        {
            CompleteLap();
        }

        // recovery: put the car back on the line when it is stuck, upside down, far off the road or facing backwards
        // waiting in a queue is not being stuck, unless the queue never moves
        const bool queued = m_blocker.found && m_blocker.gap < 12.0f && m_blocked_time < 45.0f;
        m_stuck_time      = speed < 1.0f && !queued ? m_stuck_time + delta_time : 0.0f;
        m_upside_time     = vehicle->GetUp().y < 0.3f ? m_upside_time + delta_time : 0.0f;
        m_wrong_way_time  = flat_forward.Dot(m_line->DirectionAt(m_index)) < -0.2f ? m_wrong_way_time + delta_time : 0.0f;
        const bool lost   = m_center_offset < -here.room_left - 15.0f || m_center_offset > here.room_right + 15.0f;
        if (m_stuck_time > 3.0f || m_upside_time > 2.0f || m_wrong_way_time > 2.5f || lost)
        {
            SP_LOG_WARNING("ai_driver %s: recovering at %.0f m (stuck %.1f s, upside %.1f s, wrong way %.1f s, offset %.1f m)", m_name.c_str(), m_distance, m_stuck_time, m_upside_time, m_wrong_way_time, m_center_offset);
            // a reset is a mistake worth learning from right away, not a lap later
            if (m_settings.learning)
            {
                for (int64_t k = -60; k <= 10; k++)
                {
                    float& scale = m_plan[m_line->Wrap(static_cast<int64_t>(m_index) + k)].grip_scale;
                    scale        = max(scale * 0.9f, 0.6f);
                }
                BuildPlan();
            }
            plan_here.trouble = true;
            const size_t index = m_line->Wrap(static_cast<int64_t>(m_index) - 5);
            PlaceOnLine(index);
            m_car->PlaceAt(m_line->GetPoint(index).position, Quaternion::FromLookRotation(m_line->DirectionAt(index), Vector3::Up));
            m_stats.resets++;
            return;
        }

        m_trace_time += delta_time;
        if (m_trace && m_trace_time >= 0.05f)
        {
            m_trace_time = 0.0f;
            fprintf(m_trace, "%.3f,%u,%.1f,%.2f,%.2f,%.2f,%.3f,%.5f,%.5f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f,%.2f,%.2f,%d,%.2f,%d\n",
                m_stats.lap_time, m_stats.lap, m_distance, speed, target_speed, plan_here.speed, tracking_error, here.curvature, feedforward, m_steering, m_throttle, brake,
                m_model.front_use, m_model.rear_use, m_model.lateral_use, body_slip, lateral_accel, -m_decel, oversteer ? 1 : 0, wide, physics->GetCurrentGear());
        }

        m_log_time += delta_time;
        if (m_settings.verbose && m_log_time >= 1.0f)
        {
            m_log_time = 0.0f;
            SP_LOG_INFO("ai_driver %s: lap %u %.1f s, %.0f m, speed %.0f km/h, target %.0f km/h, line error %.2f m, throttle %.2f, brake %.2f (limit %.2f), steer %.2f, use f %.2f r %.2f lat %.2f, slip %.3f, understeer %.3f, balance %.2f, gear %d",
                m_name.c_str(), m_stats.lap + 1, m_stats.lap_time, m_distance, m_stats.speed_kmh, m_stats.target_kmh, m_line_error, m_throttle, brake, m_brake_limit, m_steering,
                m_model.front_use, m_model.rear_use, m_model.lateral_use, body_slip, m_model.understeer, m_model.balance, physics->GetCurrentGear());
        }
    }

    void AiDriver::CompleteLap()
    {
        const size_t count = m_plan.size();
        size_t trouble     = 0;
        for (const Plan& plan : m_plan)
        {
            trouble += plan.trouble ? 1 : 0;
        }

        if (m_settings.learning)
        {
            // trouble costs grip from the braking zone before it to just past it, clean corners earn some back
            vector<uint8_t> penalized(count, 0);
            for (size_t i = 0; i < count; i++)
            {
                if (m_plan[i].trouble)
                {
                    for (int64_t k = -40; k <= 8; k++)
                    {
                        penalized[m_line->Wrap(static_cast<int64_t>(i) + k)] = 1;
                    }
                }
            }
            // the model already knows the car, so a corner only needs a little shaved, and earns it back slowly
            for (size_t i = 0; i < count; i++)
            {
                float& scale = m_plan[i].grip_scale;
                scale        = penalized[i] ? max(scale * 0.94f, 0.6f) : min(scale * 1.01f, 1.05f);
            }

            // keep neighbouring corners from diverging into steps
            vector<float> smoothed(count);
            for (size_t i = 0; i < count; i++)
            {
                float sum = 0.0f;
                for (int64_t k = -5; k <= 5; k++)
                {
                    sum += m_plan[m_line->Wrap(static_cast<int64_t>(i) + k)].grip_scale;
                }
                smoothed[i] = sum / 11.0f;
            }
            for (size_t i = 0; i < count; i++)
            {
                m_plan[i].grip_scale = smoothed[i];
            }
            BuildPlan();
        }
        for (Plan& plan : m_plan)
        {
            plan.trouble = false;
        }

        // a car taken over mid lap reaches the line once before its first timed lap
        if (m_timing)
        {
            m_stats.lap++;
            m_stats.last_lap_time = m_stats.lap_time;
            if (m_stats.best_lap_time <= 0.0f || m_stats.lap_time < m_stats.best_lap_time)
            {
                m_stats.best_lap_time = m_stats.lap_time;
            }
            SP_LOG_INFO("ai_driver %s: lap %u %.3f s (best %.3f s), worst line error %.2f m, %zu trouble points, resets %u, cornering %.2f g, braking %.2f g (brake efficiency %.2f), balance %.2f, aero %.2e, next ideal lap %.2f s",
                m_name.c_str(), m_stats.lap, m_stats.last_lap_time, m_stats.best_lap_time, m_lap_error_max, trouble, m_stats.resets, m_stats.cornering_g, m_stats.braking_g, m_model.brake_efficiency, m_model.balance, m_model.aero, m_stats.ideal_lap);
        }
        else
        {
            SP_LOG_INFO("ai_driver %s: out lap done, timing from here", m_name.c_str());
        }
        m_timing          = true;
        m_stats.lap_time  = 0.0f;
        m_lap_error_max   = 0.0f;
    }
}
