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

//= INCLUDES ============
#include "pch.h"
#include "CarSimulation.h"
//=======================

namespace car
{
    Simulation::~Simulation()
    { close_telemetry(); destroy(); }


    PxRigidDynamic* Simulation::get_body() const
    { return body; }


    const config& Simulation::get_config() const
    { return cfg; }


    config& Simulation::get_config()
    { return cfg; }


    const car_preset& Simulation::get_spec() const
    { return spec; }


    car_preset& Simulation::get_spec()
    { return spec; }


    const car_preset& Simulation::get_base_spec() const
    { return base_spec; }


    const active_upgrades& Simulation::get_upgrades() const
    { return upgrades; }


    active_upgrades& Simulation::get_upgrades()
    { return upgrades; }


    const wheel& Simulation::get_wheel_state(int i) const
    { return wheels[PxClamp(i, 0, wheel_count - 1)]; }


    wheel& Simulation::get_wheel_state(int i)
    { return wheels[PxClamp(i, 0, wheel_count - 1)]; }


    const multibody_state& Simulation::get_multibody_state() const
    { return multibody; }


    bool Simulation::get_log_to_file() const
    { return log_to_file; }


    void Simulation::set_log_to_file(bool enabled)
    {
        log_to_file = enabled;
        if (!enabled)
        {
            close_telemetry();
        }
    }


    bool Simulation::get_rev_limiter_active() const
    { return rev_limiter_active; }


    float Simulation::get_clutch() const
    { return clutch; }


    float Simulation::get_engine_rotation() const
    { return engine_rotation; }


    void Simulation::set_simulation_enabled(bool enabled)
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

    void Simulation::set_mechanism_simulation_enabled(bool enabled)
    {
            for (int i = 0; i < multibody.actor_count; i++)
            {
                if (multibody.actors[i])
                {
                    multibody.actors[i]->setActorFlag(PxActorFlag::eDISABLE_SIMULATION, !enabled);
                }
            }
    }

    bool Simulation::has_multibody() const
    {
            return multibody.initialized && multibody.actor_count > 0;
    }

    bool Simulation::ensure_multibody(PxPhysics* physics, PxScene* scene)
    {
            if (has_multibody())
            {
                return true;
            }
            return create_multibody(physics, scene, true);
    }


    void Simulation::set_force_retention(bool enabled)
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


    void Simulation::clear_force_accumulators()
    {
            // skip disabled or released actors, clearForce crashes once eDISABLE_SIMULATION is set
            if (can_apply_force(body))
            {
                body->clearForce();
                body->clearTorque();
            }
            for (int i = 0; i < multibody.actor_count; i++)
            {
                if (can_apply_force(multibody.actors[i]))
                {
                    multibody.actors[i]->clearForce();
                    multibody.actors[i]->clearTorque();
                }
            }
    }


    bool Simulation::is_finite_vec(const PxVec3& v)
    {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }


    bool Simulation::can_apply_force(PxRigidDynamic* body)
    {
            return body && body->getScene() && !body->getActorFlags().isSet(PxActorFlag::eDISABLE_SIMULATION) && !body->getRigidBodyFlags().isSet(PxRigidBodyFlag::eKINEMATIC);
    }


    void Simulation::safe_add_force(PxRigidDynamic* body, const PxVec3& force, PxForceMode::Enum mode )
    {
            if (!can_apply_force(body))
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


    void Simulation::safe_add_torque(PxRigidDynamic* body, const PxVec3& torque, PxForceMode::Enum mode )
    {
            if (!can_apply_force(body))
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


    void Simulation::safe_add_force_at_pos(PxRigidDynamic* body, const PxVec3& force, const PxVec3& pos, PxForceMode::Enum mode )
    {
            if (!can_apply_force(body))
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


    shape_2d& Simulation::shape_data_ref()
    {
            return shape_data;
    }


    bool Simulation::is_in_reverse()
    { return current_gear == 0; }


    bool Simulation::is_in_neutral()
    { return current_gear == 1; }


    bool Simulation::is_in_forward_gear()
    { return current_gear >= 2; }


    bool Simulation::sanitize_float(float& v, float fallback )
    {
            if (!std::isfinite(v))
            {
                v = fallback;
                return true;
            }
            return false;
    }


    bool Simulation::sanitize_vec(PxVec3& v, const PxVec3& fallback )
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


    bool Simulation::sanitize_wheel_state(int i)
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


    bool Simulation::is_front(int i) const
    { return i == front_left || i == front_right; }


    bool Simulation::is_rear(int i)
    { return i == rear_left || i == rear_right; }


    bool Simulation::is_driven(int i)
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


    float Simulation::lerp(float a, float b, float t)
    { return a + (b - a) * t; }


    float Simulation::exp_decay(float rate, float dt)
    { return 1.0f - expf(-rate * dt); }


    bool Simulation::is_valid_wheel(int i)
    { return i >= 0 && i < wheel_count; }


    const char* Simulation::get_wheel_name(int i)
    { return is_valid_wheel(i) ? wheel_names[i] : "??"; }


    void Simulation::clear_abs_state()
    {
            for (int i = 0; i < wheel_count; i++)
            {
                abs_active[i] = false;
            }
    }


    float Simulation::get_assisted_steering_target(float raw_input)
    {
            float deadzone = PxClamp(spec.steering_deadzone, 0.0f, 0.95f);
            float magnitude = fabsf(raw_input);
            float filtered_input = magnitude <= deadzone ? 0.0f : copysignf((magnitude - deadzone) / (1.0f - deadzone), raw_input);
            float speed_kmh = body ? body->getLinearVelocity().magnitude() * 3.6f : 0.0f;
            float speed_factor = PxClamp(speed_kmh / PxMax(spec.assists.steering_speed_reference, 1.0f), 0.0f, 1.0f);
            float steering_limit = 1.0f - spec.assists.steering_speed_reduction * speed_factor;
            return PxClamp(filtered_input, -steering_limit, steering_limit);
    }


    void Simulation::update_assist_controller(bool traction_requested, bool braking_requested, float dt)
    {
            assisted_actuators = assist_command();
            tc_active = false;
            if (traction_requested && !burnout_active && spec.tc_enabled && spec.assists.traction_control_level > 0.0f)
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
                    // a soft gain let the rears settle far past the threshold where lateral grip is already gone
                    target_reduction = PxClamp((max_slip - spec.tc_slip_threshold) * 20.0f, 0.0f, reduction_limit);
                }
                // cut fast, restore slowly, the reverse let the slip spike again the moment torque came back
                float rate = target_reduction > tc_reduction ? spec.tc_response_rate * 2.0f : spec.tc_response_rate * 0.5f;
                tc_reduction = lerp(tc_reduction, target_reduction, exp_decay(rate, dt));
            }
            else
            {
                tc_reduction = lerp(tc_reduction, 0.0f, exp_decay(spec.tc_response_rate, dt));
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


    void Simulation::update_burnout(float forward_speed_ms)
    {
            // pinning both pedals is the line lock request, the front circuit takes all the pressure and
            // traction control steps aside so the driven wheels can actually break traction, the pedal
            // bar is high so a part pressed brake cannot ask for a hold it has no grip to deliver
            bool pedals_held = input.throttle > 0.8f
                && input.brake > 0.8f
                && is_in_forward_gear()
                && !is_shifting;
            if (!pedals_held)
            {
                burnout_active = false;
                return;
            }

            // engage from a near stop only, then hold as it creeps so the rear brakes cannot cut back in
            float speed_kmh = fabsf(forward_speed_ms) * 3.6f;
            float release_speed_kmh = burnout_active ? 60.0f : 5.0f;
            burnout_active = speed_kmh < release_speed_kmh;
    }


    float Simulation::get_camber_grip_factor(float camber)
    {
            float dev = camber - tuning::camber_optimal;
            return PxClamp(1.0f - tuning::camber_grip_loss * dev * dev, 0.5f, 1.0f);
    }


    float Simulation::get_weight_distribution_front()
    {
            if (cfg.wheelbase < 0.01f)
            {
                return 0.5f;
            }
            return PxClamp(0.5f + spec.center_of_mass_z / cfg.wheelbase, 0.0f, 1.0f);
    }


    float Simulation::load_sensitive_grip(float load)
    {
            if (load <= 0.0f)
            {
                return 0.0f;
            }
            return load * powf(load / PxMax(spec.load_reference, 1.0f), spec.load_sensitivity - 1.0f);
    }


    float Simulation::get_tire_temp_grip_factor(float temperature)
    {
            float opt = spec.tire_optimal_temp;
            float range = PxMax(spec.tire_temp_range, 1.0f);
            float dev = fabsf(temperature - opt);
            float norm = PxClamp(dev / range, 0.0f, 1.0f);
            float penalty = norm * norm * spec.tire_grip_temp_factor;
            // a preset factor above one would otherwise drive grip to zero or negative
            return PxClamp(1.0f - penalty, 0.1f, 1.0f);
    }


    tire_condition_modifiers Simulation::get_tire_condition_modifiers(float surface_temperature, float core_temperature, float wear, float load)
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


    float Simulation::get_surface_friction(surface_type surface)
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


    float Simulation::get_brake_efficiency(float temp)
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


    void Simulation::compute_aero_from_shape(const std::vector<PxVec3>& vertices)
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


    std::vector<std::pair<float, float>> Simulation::graham_scan_hull_2d(std::vector<std::pair<float, float>> points)
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


    void Simulation::compute_shape_visualization(const std::vector<PxVec3>& vertices, const PxVec3& min_pt, const PxVec3& max_pt)
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


    void Simulation::apply_aero_and_resistance()
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
                    PxRigidDynamic* rr_body = multibody.corners[i].wheel_body ? multibody.corners[i].wheel_body : body;
                    safe_add_force_at_pos(rr_body, rr_force, wheels[i].contact_point);
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


    PxU32 Simulation::multibody_collision_group()
    {
            // matching groups suppress contacts between parts of the same vehicle
            const PxU32 collision_group = static_cast<PxU32>(reinterpret_cast<uintptr_t>(body) >> 4);
            return collision_group != 0 ? collision_group : 1;
    }


    actor_motion_state Simulation::capture_actor_motion(PxRigidDynamic* actor)
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


    void Simulation::restore_actor_motion(PxRigidDynamic* actor, const actor_motion_state& state)
    {
            if (!actor || !body || !state.valid
                || actor->getRigidBodyFlags().isSet(PxRigidBodyFlag::eKINEMATIC))
            {
                return;
            }

            PxTransform chassis_pose = body->getGlobalPose();
            PxVec3 chassis_angular_velocity = body->getAngularVelocity();
            PxVec3 chassis_point_velocity = body->getLinearVelocity()
                + chassis_angular_velocity.cross(actor->getGlobalPose().p - chassis_pose.p);
            actor->setLinearVelocity(
                chassis_point_velocity + chassis_pose.q.rotate(state.linear_velocity));
            actor->setAngularVelocity(
                chassis_angular_velocity + chassis_pose.q.rotate(state.angular_velocity));
            actor->wakeUp();
    }


    multibody_motion_state Simulation::capture_multibody_motion()
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


    void Simulation::restore_multibody_motion(const multibody_motion_state& state)
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


    PxTransform Simulation::local_anchor(PxRigidActor* actor, const PxVec3& world_point)
    {
            return actor ? PxTransform(actor->getGlobalPose().transformInv(world_point)) : PxTransform(world_point);
    }


    void Simulation::register_multibody_actor(PxRigidDynamic* actor)
    {
            if (actor && multibody.actor_count < max_multibody_actors)
            {
                multibody.actors[multibody.actor_count++] = actor;
            }
    }


    void Simulation::register_multibody_joint(PxJoint* joint)
    {
            if (joint && multibody.joint_count < max_suspension_joints)
            {
                multibody.joints[multibody.joint_count++] = joint;
            }
    }


    void Simulation::configure_mechanism_shape(PxShape* shape)
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


    PxRigidDynamic* Simulation::create_mechanism_actor(const PxTransform& pose, const PxGeometry& geometry, float mass)
    {
            PxRigidDynamic* actor = multibody.physics->createRigidDynamic(pose);
            if (!actor)
            {
                return nullptr;
            }

            PxShape* shape = multibody.physics->createShape(geometry, *material);
            if (!shape)
            {
                actor->release();
                return nullptr;
            }

            configure_mechanism_shape(shape);
            actor->attachShape(*shape);
            shape->release();
            PxRigidBodyExt::setMassAndUpdateInertia(*actor, PxMax(mass, 0.1f));
            actor->setSolverIterationCounts(16, 4);
            actor->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
            multibody.scene->addActor(*actor);
            register_multibody_actor(actor);
            return actor;
    }


    PxRigidDynamic* Simulation::create_segment_actor(
        const PxVec3& world_start,
        const PxVec3& world_end,
        float radius,
        float mass)
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
            PxQuat rotation = dot < -0.9999f
                ? PxQuat(PxPi, PxVec3(0.0f, 1.0f, 0.0f))
                : PxQuat(cross.x, cross.y, cross.z, 1.0f + dot).getNormalized();
            PxCapsuleGeometry geometry(radius, PxMax(length * 0.5f - radius, 0.01f));
            PxRigidDynamic* actor = create_mechanism_actor(
                PxTransform((world_start + world_end) * 0.5f, rotation),
                geometry,
                mass);
            if (!actor)
            {
                return nullptr;
            }
            actor->setAngularDamping(1.5f);
            actor->setLinearDamping(0.5f);
            return actor;
    }


    PxSphericalJoint* Simulation::create_spherical_joint(PxRigidActor* actor_a, PxRigidActor* actor_b, const PxVec3& world_anchor)
    {
            PxSphericalJoint* joint = PxSphericalJointCreate(*multibody.physics, actor_a, local_anchor(actor_a, world_anchor), actor_b, local_anchor(actor_b, world_anchor));
            if (joint)
            {
                joint->setConstraintFlag(PxConstraintFlag::eENABLE_EXTENDED_LIMITS, true);
                register_multibody_joint(joint);
            }
            return joint;
    }


    PxJoint* Simulation::create_bushing_joint(PxRigidActor* actor_a, PxRigidActor* actor_b, const PxVec3& world_anchor, const PxVec3& load_direction)
    {
            float radial = spec.bushing_stiffness_radial;
            float axial  = spec.bushing_stiffness_axial;
            float travel = spec.bushing_max_deflection;
            PxVec3 along = load_direction;
            if (!(radial > 0.0f) || !(axial > 0.0f) || !(travel > 0.0f) || !actor_a || !actor_b || along.normalize() < 1e-4f)
            {
                return create_spherical_joint(actor_a, actor_b, world_anchor);
            }
            travel = PxClamp(travel, 0.0002f, 0.02f);

            // x runs down the load path so the two rates land on the axes they belong to
            PxVec3 reference_axis = fabsf(along.y) < 0.9f ? PxVec3(0.0f, 1.0f, 0.0f) : PxVec3(1.0f, 0.0f, 0.0f);
            PxVec3 side           = along.cross(reference_axis);
            if (side.normalize() < 1e-4f)
            {
                return create_spherical_joint(actor_a, actor_b, world_anchor);
            }
            PxVec3 up = side.cross(along);
            PxQuat world_rotation = PxQuat(PxMat33(along, up, side)).getNormalized();
            PxTransform pose_a = actor_a->getGlobalPose();
            PxTransform pose_b = actor_b->getGlobalPose();
            PxTransform frame_a(pose_a.transformInv(world_anchor), (pose_a.q.getConjugate() * world_rotation).getNormalized());
            PxTransform frame_b(pose_b.transformInv(world_anchor), (pose_b.q.getConjugate() * world_rotation).getNormalized());

            PxD6Joint* joint = PxD6JointCreate(*multibody.physics, actor_a, frame_a, actor_b, frame_b);
            if (!joint)
            {
                return create_spherical_joint(actor_a, actor_b, world_anchor);
            }

            float damping = PxMax(spec.bushing_damping, 0.0f);
            joint->setMotion(PxD6Axis::eX, PxD6Motion::eLIMITED);
            joint->setMotion(PxD6Axis::eY, PxD6Motion::eLIMITED);
            joint->setMotion(PxD6Axis::eZ, PxD6Motion::eLIMITED);
            joint->setMotion(PxD6Axis::eTWIST, PxD6Motion::eFREE);
            joint->setMotion(PxD6Axis::eSWING1, PxD6Motion::eFREE);
            joint->setMotion(PxD6Axis::eSWING2, PxD6Motion::eFREE);
            // a hard stop one millimetre out sat inside pgs solver error, every snap against it flung the
            // unsprung mass and kicked the chassis sideways, so the stop is a soft progressive stage instead
            const float stop_travel = travel * 3.0f;
            PxJointLinearLimitPair stop(-stop_travel, stop_travel, PxSpring(radial * 10.0f, damping * 4.0f));
            joint->setLinearLimit(PxD6Axis::eX, stop);
            joint->setLinearLimit(PxD6Axis::eY, stop);
            joint->setLinearLimit(PxD6Axis::eZ, stop);
            joint->setDrive(PxD6Drive::eX, PxD6JointDrive(radial, damping, PX_MAX_F32, false));
            joint->setDrive(PxD6Drive::eY, PxD6JointDrive(axial, damping, PX_MAX_F32, false));
            joint->setDrive(PxD6Drive::eZ, PxD6JointDrive(axial, damping, PX_MAX_F32, false));
            joint->setDrivePosition(PxTransform(PxIdentity));
            joint->setDriveVelocity(PxVec3(0.0f), PxVec3(0.0f));
            joint->setConstraintFlag(PxConstraintFlag::eENABLE_EXTENDED_LIMITS, true);
            register_multibody_joint(joint);
            return joint;
    }


    PxRigidDynamic* Simulation::create_link(const PxVec3& world_start, const PxVec3& world_end, float mass, PxRigidDynamic* start_actor , PxJoint** out_anchor_joint , bool* out_anchor_is_bushing )
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
                // only the chassis end is a bush, a rod end onto the rack or another member is a bearing
                bool inboard = anchor_actor == static_cast<PxRigidActor*>(body);
                PxJoint* anchor_joint = inboard
                    ? create_bushing_joint(anchor_actor, actor, world_start, direction)
                    : static_cast<PxJoint*>(create_spherical_joint(anchor_actor, actor, world_start));
                if (!anchor_joint)
                {
                    multibody.actors[--multibody.actor_count] = nullptr;
                    actor->release();
                    return nullptr;
                }
                if (out_anchor_joint)
                {
                    *out_anchor_joint = anchor_joint;
                }
                if (out_anchor_is_bushing)
                {
                    // the fallback path hands back a spherical joint, so ask the joint and not the intent
                    *out_anchor_is_bushing = inboard && anchor_joint->is<PxD6Joint>() != nullptr;
                }
            }
            return actor;
    }


    bool Simulation::connect_link_to_upright(PxRigidDynamic* link, PxRigidDynamic* upright, const PxVec3& world_anchor)
    {
            if (!link || !upright)
            {
                return false;
            }
            return create_spherical_joint(link, upright, world_anchor) != nullptr;
    }


    bool Simulation::add_link_member(suspension_corner& corner, const PxVec3& world_start, const PxVec3& world_end, float mass, PxRigidDynamic* start_actor )
    {
            if (corner.member_count >= max_suspension_members)
            {
                return false;
            }

            PxJoint* anchor_joint = nullptr;
            bool anchor_is_bushing = false;
            PxRigidDynamic* link = create_link(world_start, world_end, mass, start_actor, &anchor_joint, &anchor_is_bushing);
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
            member.pivot_joint = anchor_joint;
            member.pivot_is_bushing = anchor_is_bushing;
            return true;
    }


    bool Simulation::add_wishbone(suspension_corner& corner, const PxVec3& inner_front, const PxVec3& inner_rear, const PxVec3& outer, float mass)
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

            // both inner pivots are bushed and the load path through each is the line out to the ball
            // joint, the outer joint itself stays a bearing because that is what it is
            PxJoint* front_bush = create_bushing_joint(body, arm, inner_front, outer - inner_front);
            PxJoint* rear_bush = create_bushing_joint(body, arm, inner_rear, outer - inner_rear);
            if (!front_bush || !rear_bush || !create_spherical_joint(arm, corner.upright, outer))
            {
                return false;
            }
            suspension_member& front_member = corner.members[corner.member_count++];
            front_member.actor = arm;
            front_member.local_start = arm->getGlobalPose().transformInv(inner_front);
            front_member.local_end = arm->getGlobalPose().transformInv(outer);
            front_member.pivot_joint = front_bush;
            front_member.pivot_is_bushing = front_bush->is<PxD6Joint>() != nullptr;
            suspension_member& rear_member = corner.members[corner.member_count++];
            rear_member.actor = arm;
            rear_member.local_start = arm->getGlobalPose().transformInv(inner_rear);
            rear_member.local_end = arm->getGlobalPose().transformInv(outer);
            rear_member.pivot_joint = rear_bush;
            rear_member.pivot_is_bushing = rear_bush->is<PxD6Joint>() != nullptr;
            return true;
    }


    bool Simulation::add_macpherson_strut(suspension_corner& corner, const PxVec3& top, const PxVec3& bottom, float mass)
    {
            if (corner.member_count >= max_suspension_members)
            {
                return false;
            }

            PxJoint* anchor_joint = nullptr;
            bool anchor_is_bushing = false;
            PxRigidDynamic* strut = create_link(top, bottom, mass, nullptr, &anchor_joint, &anchor_is_bushing);
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
            member.pivot_joint = anchor_joint;
            member.pivot_is_bushing = anchor_is_bushing;
            return true;
    }


    bool Simulation::add_steering_stop(suspension_corner& corner, PxRigidDynamic* upright, const PxTransform& chassis_pose, const PxVec3& wheel_world, float angle_limit)
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
            corner.steering_stop = stop;
            corner.steering_limit = PxMax(angle_limit, 0.0f);
            return true;
    }


    PxVec3 Simulation::hardpoint_world(const PxTransform& chassis_pose, const PxVec3& local_point) const
    {
            return chassis_pose.transform(local_point);
    }


    float Simulation::mechanism_actor_mass()
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
            float mass = (cfg.wheel_mass + spec.upright_mass) * static_cast<float>(wheel_count)
                + spec.suspension_link_mass * static_cast<float>(suspension_links)
                + spec.steering_rack_mass;
            if (spec.front_arb_stiffness > 0.0f)
            {
                mass += spec.arb_mass;
            }
            if (spec.rear_arb_stiffness > 0.0f)
            {
                mass += spec.arb_mass;
            }
            mass += spec.coilover_mass * static_cast<float>(wheel_count);
            bool drives_front = spec.drivetrain_type == 1 || spec.drivetrain_type == 2;
            bool drives_rear = spec.drivetrain_type == 0 || spec.drivetrain_type == 2;
            int driven_wheels = (drives_front ? 2 : 0) + (drives_rear ? 2 : 0);
            int driven_axles = (drives_front ? 1 : 0) + (drives_rear ? 1 : 0);
            mass += spec.halfshaft_mass * static_cast<float>(driven_wheels);
            mass += spec.driveshaft_mass * static_cast<float>(PxMax(driven_axles, 1));
            mass += spec.differential_mass * static_cast<float>(driven_axles);
            // gearbox and axle flanges carry driveline inertia, token mass for the actors
            mass += 4.0f;
            return mass;
    }


    float Simulation::unsprung_mass()
    {
            // outboard mass only, chassis mounted mechanisms still ride on the springs
            bool drives_front = spec.drivetrain_type == 1 || spec.drivetrain_type == 2;
            bool drives_rear = spec.drivetrain_type == 0 || spec.drivetrain_type == 2;
            int driven_wheels = (drives_front ? 2 : 0) + (drives_rear ? 2 : 0);
            float mass = (cfg.wheel_mass + spec.upright_mass) * static_cast<float>(wheel_count);
            mass += spec.halfshaft_mass * static_cast<float>(driven_wheels);
            mass += spec.coilover_mass * 0.45f * static_cast<float>(wheel_count);
            return mass;
    }


    float Simulation::chassis_mass()
    {
            return PxMax(cfg.mass - mechanism_actor_mass(), 100.0f);
    }


    float Simulation::sprung_mass()
    {
            return PxMax(cfg.mass - unsprung_mass(), 100.0f);
    }


    bool Simulation::has_authored_inertia() const
    {
            return spec.inertia_xx > 0.0f && spec.inertia_yy > 0.0f && spec.inertia_zz > 0.0f;
    }


    void Simulation::apply_chassis_mass_properties(float mass, const PxVec3& com_local)
    {
            if (!body)
            {
                return;
            }

            float safe_mass = PxMax(mass, 0.1f);
            PxVec3 safe_com = com_local;
            if (has_authored_inertia())
            {
                body->setMass(safe_mass);
                body->setCMassLocalPose(PxTransform(safe_com));
                body->setMassSpaceInertiaTensor(PxVec3(
                    PxMax(spec.inertia_xx, 0.1f),
                    PxMax(spec.inertia_yy, 0.1f),
                    PxMax(spec.inertia_zz, 0.1f)));
            }
            else
            {
                PxRigidBodyExt::setMassAndUpdateInertia(*body, safe_mass, &safe_com);
            }
    }


    void Simulation::update_assembled_center_of_mass()
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
            apply_chassis_mass_properties(body_mass, chassis_center_local);
    }


    bool Simulation::create_suspension_corner(int wheel_index, const suspension_geometry& geometry)
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
            // negative camber must lean the wheel top inboard and positive toe must point the leading
            // edge inboard, the unnegated rotation produced the mirror of both on each side
            PxQuat alignment = PxQuat(-side * toe, PxVec3(0.0f, 1.0f, 0.0f)) * PxQuat(-side * camber, PxVec3(0.0f, 0.0f, 1.0f));
            PxTransform wheel_pose(wheel_world, chassis_pose.q * alignment);

            // the upright spans its two ball joints, a token box gave it far less inertia than the casting
            // it stands in for and left the solver fighting long levers on a body that barely resisted them
            float upright_half_x = PxMax(PxMax(geometry.upper_upright_inset, geometry.lower_upright_inset) * 0.5f, 0.04f);
            float upright_half_y = PxMax((geometry.upper_upright_y - geometry.lower_upright_y) * 0.5f, 0.10f);
            corner.upright = create_mechanism_actor(wheel_pose, PxBoxGeometry(upright_half_x, upright_half_y, 0.06f), spec.upright_mass);
            corner.wheel_body = create_mechanism_actor(wheel_pose, PxSphereGeometry(cfg.wheel_radius_for(wheel_index) * wheel_inertia_shape_radius_scale), cfg.wheel_mass);
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
            if (!add_steering_stop(corner, corner.upright, chassis_pose, wheel_world, steering_limit))
            {
                return false;
            }

            // short upper arm, long lower arm, the length difference is what pulls the top of the upright
            // inboard as the wheel rises and recovers the camber the body loses in roll
            float upper_inner_x = wheel_local.x * geometry.upper_chassis_inset;
            float upper_outer_x = wheel_local.x - side * PxMax(geometry.upper_upright_inset, 0.0f);
            float lower_outer_x = wheel_local.x - side * PxMax(geometry.lower_upright_inset, 0.0f);

            PxVec3 upper_outer_local(upper_outer_x, wheel_local.y + geometry.upper_upright_y, wheel_local.z);
            PxVec3 lower_outer_local(lower_outer_x, wheel_local.y + geometry.lower_upright_y, wheel_local.z);
            PxVec3 upper_inner_front_local(upper_inner_x, wheel_local.y + geometry.upper_inner_y, wheel_local.z + arm_span);
            PxVec3 upper_inner_rear_local(upper_inner_x, wheel_local.y + geometry.upper_inner_y, wheel_local.z - arm_span);
            PxVec3 lower_inner_front_local(inner_x, wheel_local.y + geometry.lower_inner_y, wheel_local.z + arm_span);
            PxVec3 lower_inner_rear_local(inner_x, wheel_local.y + geometry.lower_inner_y, wheel_local.z - arm_span);

            if (geometry.mechanism == suspension_mechanism::multi_link)
            {
                float spread_y = PxMax(geometry.link_spread_y, 0.05f);
                float spread_z = PxMax(geometry.link_spread_z, 0.05f);
                // the upper pair is the short one, same reason as the wishbone case
                const PxVec3 inner_points[4] =
                {
                    PxVec3(upper_inner_x, wheel_local.y + geometry.upper_inner_y, wheel_local.z + spread_z),
                    PxVec3(upper_inner_x, wheel_local.y + geometry.upper_inner_y, wheel_local.z - spread_z),
                    PxVec3(inner_x, wheel_local.y + geometry.lower_inner_y, wheel_local.z + spread_z),
                    PxVec3(inner_x, wheel_local.y + geometry.lower_inner_y, wheel_local.z - spread_z)
                };
                const PxVec3 outer_points[4] =
                {
                    PxVec3(upper_outer_x, wheel_local.y + spread_y, wheel_local.z + spread_z * 0.45f),
                    PxVec3(upper_outer_x, wheel_local.y + spread_y, wheel_local.z - spread_z * 0.45f),
                    PxVec3(lower_outer_x, wheel_local.y - spread_y, wheel_local.z + spread_z * 0.45f),
                    PxVec3(lower_outer_x, wheel_local.y - spread_y, wheel_local.z - spread_z * 0.45f)
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
            float static_load = sprung_mass()
                * (is_front(wheel_index) ? get_weight_distribution_front() : 1.0f - get_weight_distribution_front())
                * 0.5f
                * 9.81f;
            corner.shock_rest_length = (shock_top_world - shock_bottom_world).magnitude()
                + static_load / PxMax(spring_stiffness[wheel_index], 1.0f);
            corner.shock_length = (shock_top_world - shock_bottom_world).magnitude();

            corner.travel_joint = nullptr;
            // coilover prismatic already limits shock travel, a second distance joint on the
            // same anchors fights physx and can dump chassis velocity in one frame
            if (geometry.mechanism != suspension_mechanism::macpherson)
            {
                if (!create_coilover(wheel_index))
                {
                    return false;
                }
            }
            if (geometry.mechanism != suspension_mechanism::macpherson
                && !has_physical_coilover(wheel_index))
            {
                corner.travel_joint = PxDistanceJointCreate(
                    *multibody.physics,
                    body,
                    local_anchor(body, shock_top_world),
                    corner.upright,
                    local_anchor(corner.upright, shock_bottom_world));
                if (!corner.travel_joint)
                {
                    return false;
                }
                corner.travel_joint->setMinDistance(
                    PxMax(corner.shock_rest_length - cfg.suspension_travel, 0.05f));
                corner.travel_joint->setMaxDistance(
                    corner.shock_rest_length + cfg.suspension_travel * 0.15f);
                corner.travel_joint->setDistanceJointFlag(
                    PxDistanceJointFlag::eMIN_DISTANCE_ENABLED, true);
                corner.travel_joint->setDistanceJointFlag(
                    PxDistanceJointFlag::eMAX_DISTANCE_ENABLED, true);
                register_multibody_joint(corner.travel_joint);
            }

            return true;
    }


    bool Simulation::create_coilover(int wheel_index)
    {
            if (!is_valid_wheel(wheel_index))
            {
                return false;
            }

            suspension_corner& corner = multibody.corners[wheel_index];
            if (!corner.upright || !body)
            {
                return false;
            }

            PxTransform chassis_pose = body->getGlobalPose();
            PxVec3 top = chassis_pose.transform(corner.chassis_shock_anchor);
            PxVec3 bottom = corner.upright->getGlobalPose().transform(corner.upright_shock_anchor);
            PxVec3 delta = bottom - top;
            float length = delta.magnitude();
            if (length < 0.08f)
            {
                return false;
            }
            PxVec3 direction = delta / length;

            PxVec3 cross = PxVec3(1.0f, 0.0f, 0.0f).cross(direction);
            float dot = PxVec3(1.0f, 0.0f, 0.0f).dot(direction);
            PxQuat shock_rotation = dot < -0.9999f
                ? PxQuat(PxPi, PxVec3(0.0f, 1.0f, 0.0f))
                : PxQuat(cross.x, cross.y, cross.z, 1.0f + dot).getNormalized();

            float tube_length = length * 0.55f;
            float rod_length = length * 0.55f;
            float tube_mass = PxMax(spec.coilover_mass * 0.55f, 0.5f);
            float rod_mass = PxMax(spec.coilover_mass * 0.45f, 0.4f);
            float tube_radius = 0.028f;
            float rod_radius = 0.014f;

            PxVec3 tube_start = top;
            PxVec3 tube_end = top + direction * tube_length;
            PxVec3 rod_start = bottom - direction * rod_length;
            PxVec3 rod_end = bottom;

            auto make_segment = [&](const PxVec3& a, const PxVec3& b, float radius, float mass) -> PxRigidDynamic*
            {
                float segment_length = (b - a).magnitude();
                PxCapsuleGeometry geometry(radius, PxMax(segment_length * 0.5f - radius, 0.01f));
                return create_mechanism_actor(PxTransform((a + b) * 0.5f, shock_rotation), geometry, mass);
            };

            corner.coilover_unit.tube = make_segment(tube_start, tube_end, tube_radius, tube_mass);
            corner.coilover_unit.rod = make_segment(rod_start, rod_end, rod_radius, rod_mass);
            if (!corner.coilover_unit.tube || !corner.coilover_unit.rod)
            {
                return false;
            }

            // light damping on free floating mechanism bodies, same as create_segment_actor
            corner.coilover_unit.tube->setAngularDamping(1.5f);
            corner.coilover_unit.tube->setLinearDamping(0.5f);
            corner.coilover_unit.rod->setAngularDamping(1.5f);
            corner.coilover_unit.rod->setLinearDamping(0.5f);

            if (!create_spherical_joint(body, corner.coilover_unit.tube, top)
                || !create_spherical_joint(corner.upright, corner.coilover_unit.rod, bottom))
            {
                return false;
            }

            // prismatic telescope between tube and rod, x runs down the shock, frames share the midpoint
            PxVec3 joint_world = top + direction * (length * 0.5f);
            PxTransform tube_frame(
                corner.coilover_unit.tube->getGlobalPose().transformInv(joint_world),
                (corner.coilover_unit.tube->getGlobalPose().q.getConjugate() * shock_rotation).getNormalized());
            PxTransform rod_frame(
                corner.coilover_unit.rod->getGlobalPose().transformInv(joint_world),
                (corner.coilover_unit.rod->getGlobalPose().q.getConjugate() * shock_rotation).getNormalized());
            corner.coilover_unit.spring_joint = PxD6JointCreate(
                *multibody.physics,
                corner.coilover_unit.tube,
                tube_frame,
                corner.coilover_unit.rod,
                rod_frame);
            if (!corner.coilover_unit.spring_joint)
            {
                return false;
            }

            float rest_length = corner.shock_rest_length;
            corner.coilover_unit.rest_length = rest_length;
            float min_x = (rest_length - cfg.suspension_travel) - length;
            float max_x = (rest_length + cfg.suspension_travel * 0.15f) - length;
            // slide on x only, lock swing so tube and rod stay coaxial like a real damper
            // twist stays free so the rod can spin in the tube, same as halfshaft plunge
            corner.coilover_unit.spring_joint->setMotion(PxD6Axis::eX, PxD6Motion::eLIMITED);
            corner.coilover_unit.spring_joint->setMotion(PxD6Axis::eY, PxD6Motion::eLOCKED);
            corner.coilover_unit.spring_joint->setMotion(PxD6Axis::eZ, PxD6Motion::eLOCKED);
            corner.coilover_unit.spring_joint->setMotion(PxD6Axis::eTWIST, PxD6Motion::eFREE);
            corner.coilover_unit.spring_joint->setMotion(PxD6Axis::eSWING1, PxD6Motion::eLOCKED);
            corner.coilover_unit.spring_joint->setMotion(PxD6Axis::eSWING2, PxD6Motion::eLOCKED);
            corner.coilover_unit.spring_joint->setLinearLimit(
                PxD6Axis::eX,
                PxJointLinearLimitPair(multibody.physics->getTolerancesScale(), min_x, max_x));
            corner.coilover_unit.spring_joint->setConstraintFlag(PxConstraintFlag::eENABLE_EXTENDED_LIMITS, true);
            register_multibody_joint(corner.coilover_unit.spring_joint);
            return true;
    }


    bool Simulation::has_physical_coilover(int wheel_index) const
    {
            if (wheel_index < 0 || wheel_index >= wheel_count)
            {
                return false;
            }
            const suspension_corner& corner = multibody.corners[wheel_index];
            return corner.coilover_unit.spring_joint != nullptr
                && corner.coilover_unit.tube != nullptr
                && corner.coilover_unit.rod != nullptr;
    }


    bool Simulation::create_locked_differential(int left, int right)
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

            // both hubs share local +x, positive ratio keeps them co-rotating
            differential->setGearRatio(1.0f);
            register_multibody_joint(differential);
            return true;
    }


    bool Simulation::create_steering_rack()
    {
            PxTransform chassis_pose = body->getGlobalPose();
            const suspension_geometry& geometry = spec.front_geometry;
            // the steering arm is the tie rod z offset, so rack travel maps onto the requested lock
            const float steering_arm = PxMax(fabsf(geometry.tie_rod_z), 0.05f);
            multibody.rack_travel = PxClamp(tanf(fabsf(spec.max_steer_angle)) * steering_arm, 0.03f, 0.20f);
            const PxVec3& wheel_local = wheel_offsets[front_left];
            const float half_track = fabsf(wheel_local.x);
            float front_z = wheel_local.z + geometry.tie_rod_z;
            float rack_y = wheel_local.y + geometry.tie_rod_y;

            // zero bump steer needs the tie rod to sweep the same arc as the arms, so both of its
            // pivots sit on the line between the lower and upper pivots at the tie rod height
            float lower_inner_x = half_track * geometry.chassis_inset;
            float upper_inner_x = geometry.mechanism == suspension_mechanism::macpherson
                ? half_track - geometry.strut_top_inset
                : half_track * geometry.upper_chassis_inset;
            float upper_inner_y = geometry.mechanism == suspension_mechanism::macpherson
                ? geometry.strut_top_y
                : geometry.upper_inner_y;
            float lower_outer_x = half_track - PxMax(geometry.lower_upright_inset, 0.0f);
            float upper_outer_x = geometry.mechanism == suspension_mechanism::macpherson
                ? half_track
                : half_track - PxMax(geometry.upper_upright_inset, 0.0f);
            float upper_outer_y = geometry.mechanism == suspension_mechanism::macpherson
                ? geometry.strut_top_y
                : geometry.upper_upright_y;

            float inner_span_y = PxMax(upper_inner_y - geometry.lower_inner_y, 0.01f);
            float inner_t = PxClamp((geometry.tie_rod_y - geometry.lower_inner_y) / inner_span_y, 0.0f, 1.0f);
            float rack_half_length = lerp(lower_inner_x, upper_inner_x, inner_t);
            rack_half_length = PxClamp(rack_half_length, 0.1f, half_track - 0.1f);

            float outer_span_y = PxMax(upper_outer_y - geometry.lower_upright_y, 0.01f);
            float outer_t = PxClamp((geometry.tie_rod_y - geometry.lower_upright_y) / outer_span_y, 0.0f, 1.0f);
            float tie_rod_upright_inset = half_track - lerp(lower_outer_x, upper_outer_x, outer_t);
            tie_rod_upright_inset = PxMax(tie_rod_upright_inset, 0.0f);

            PxVec3 rack_world = hardpoint_world(chassis_pose, PxVec3(0.0f, rack_y, front_z));
            multibody.rack = create_mechanism_actor(PxTransform(rack_world, chassis_pose.q), PxBoxGeometry(rack_half_length, 0.025f, 0.025f), spec.steering_rack_mass);
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
            // the drive target reaches rack_travel at full lock, a hard stop exactly there chatters
            const float rack_stop = multibody.rack_travel * 1.15f;
            multibody.rack_joint->setLinearLimit(PxD6Axis::eX, PxJointLinearLimitPair(multibody.physics->getTolerancesScale(), -rack_stop, rack_stop));
            // force drive must out-stiff tire sat through the tie rods, soft accel drive let the rack steer itself into a brake weave
            const float rack_mass = PxMax(spec.steering_rack_mass, 0.5f);
            const float rack_hold_stiffness = 500000.0f;
            const float rack_hold_damping = 2.0f * sqrtf(rack_hold_stiffness * rack_mass);
            multibody.rack_joint->setDrive(
                PxD6Drive::eX,
                PxD6JointDrive(rack_hold_stiffness, rack_hold_damping, PX_MAX_F32, false));
            register_multibody_joint(multibody.rack_joint);

            for (int wheel_index : { front_left, front_right })
            {
                suspension_corner& corner = multibody.corners[wheel_index];
                float side = wheel_index == front_left ? -1.0f : 1.0f;
                PxVec3 rack_end_world = hardpoint_world(chassis_pose, PxVec3(side * rack_half_length, rack_y, front_z));
                PxVec3 upright_anchor_world = hardpoint_world(chassis_pose, wheel_offsets[wheel_index] + PxVec3(-side * tie_rod_upright_inset, geometry.tie_rod_y, geometry.tie_rod_z));
                if (!add_link_member(corner, rack_end_world, upright_anchor_world, spec.suspension_link_mass, multibody.rack))
                {
                    return false;
                }
            }
            return true;
    }


    bool Simulation::create_anti_roll_bar(anti_roll_bar& arb, int left, int right, float stiffness)
    {
            if (stiffness <= 0.0f)
            {
                return true;
            }

            suspension_corner& left_corner = multibody.corners[left];
            suspension_corner& right_corner = multibody.corners[right];
            if (!left_corner.upright || !right_corner.upright)
            {
                return false;
            }

            PxTransform chassis_pose = body->getGlobalPose();
            float bar_y = (left_corner.chassis_shock_anchor.y + right_corner.chassis_shock_anchor.y) * 0.5f;
            float bar_z = (left_corner.chassis_shock_anchor.z + right_corner.chassis_shock_anchor.z) * 0.5f;
            float track = is_front(left) ? cfg.track_front : cfg.track_rear;
            float half_span = track * 0.42f;
            float arm_length = 0.16f;
            arb.arm_length = arm_length;
            float arm_z = is_front(left) ? -arm_length : arm_length;

            PxVec3 left_inboard_world = hardpoint_world(chassis_pose, PxVec3(-0.04f, bar_y, bar_z));
            PxVec3 right_inboard_world = hardpoint_world(chassis_pose, PxVec3(0.04f, bar_y, bar_z));
            PxVec3 left_arm_world = hardpoint_world(
                chassis_pose, PxVec3(-half_span, bar_y, bar_z + arm_z));
            PxVec3 right_arm_world = hardpoint_world(
                chassis_pose, PxVec3(half_span, bar_y, bar_z + arm_z));
            PxVec3 center_world = hardpoint_world(chassis_pose, PxVec3(0.0f, bar_y, bar_z));

            auto make_half = [&](const PxVec3& inboard_world, const PxVec3& arm_world, float mass) -> PxRigidDynamic*
            {
                PxRigidDynamic* half = create_segment_actor(inboard_world, arm_world, 0.016f, mass);
                if (!half)
                {
                    return nullptr;
                }
                // revolute about chassis lateral so each half follows wheel travel
                PxQuat bar_rotation = chassis_pose.q;
                PxTransform body_frame(
                    body->getGlobalPose().transformInv(inboard_world),
                    (body->getGlobalPose().q.getConjugate() * bar_rotation).getNormalized());
                PxTransform half_frame(
                    half->getGlobalPose().transformInv(inboard_world),
                    (half->getGlobalPose().q.getConjugate() * bar_rotation).getNormalized());
                PxRevoluteJoint* bearing = PxRevoluteJointCreate(
                    *multibody.physics,
                    body,
                    body_frame,
                    half,
                    half_frame);
                if (!bearing)
                {
                    return nullptr;
                }
                bearing->setConstraintFlag(PxConstraintFlag::eENABLE_EXTENDED_LIMITS, true);
                register_multibody_joint(bearing);
                return half;
            };

            float half_mass = PxMax(spec.arb_mass * 0.45f, 0.5f);
            float drop_mass = PxMax(spec.arb_mass * 0.05f, 0.2f);
            arb.left_half = make_half(left_inboard_world, left_arm_world, half_mass);
            arb.right_half = make_half(right_inboard_world, right_arm_world, half_mass);
            if (!arb.left_half || !arb.right_half)
            {
                return false;
            }

            PxQuat bar_rotation = chassis_pose.q;
            PxTransform left_torsion_frame(
                arb.left_half->getGlobalPose().transformInv(center_world),
                (arb.left_half->getGlobalPose().q.getConjugate() * bar_rotation).getNormalized());
            PxTransform right_torsion_frame(
                arb.right_half->getGlobalPose().transformInv(center_world),
                (arb.right_half->getGlobalPose().q.getConjugate() * bar_rotation).getNormalized());
            arb.torsion_joint = PxD6JointCreate(
                *multibody.physics,
                arb.left_half,
                left_torsion_frame,
                arb.right_half,
                right_torsion_frame);
            if (!arb.torsion_joint)
            {
                return false;
            }
            // free twist, roll stiffness stays in the force couple path
            arb.torsion_joint->setMotion(PxD6Axis::eX, PxD6Motion::eLOCKED);
            arb.torsion_joint->setMotion(PxD6Axis::eY, PxD6Motion::eLOCKED);
            arb.torsion_joint->setMotion(PxD6Axis::eZ, PxD6Motion::eLOCKED);
            arb.torsion_joint->setMotion(PxD6Axis::eTWIST, PxD6Motion::eFREE);
            arb.torsion_joint->setMotion(PxD6Axis::eSWING1, PxD6Motion::eLOCKED);
            arb.torsion_joint->setMotion(PxD6Axis::eSWING2, PxD6Motion::eLOCKED);
            arb.torsion_joint->setConstraintFlag(PxConstraintFlag::eENABLE_EXTENDED_LIMITS, true);
            register_multibody_joint(arb.torsion_joint);

            PxVec3 left_bottom = left_corner.upright->getGlobalPose().transform(
                left_corner.upright_shock_anchor);
            PxVec3 right_bottom = right_corner.upright->getGlobalPose().transform(
                right_corner.upright_shock_anchor);
            arb.left_drop = create_link(left_arm_world, left_bottom, drop_mass, arb.left_half);
            arb.right_drop = create_link(right_arm_world, right_bottom, drop_mass, arb.right_half);
            if (!arb.left_drop || !arb.right_drop)
            {
                return false;
            }
            if (!connect_link_to_upright(arb.left_drop, left_corner.upright, left_bottom)
                || !connect_link_to_upright(arb.right_drop, right_corner.upright, right_bottom))
            {
                return false;
            }
            return true;
    }


    bool Simulation::create_anti_roll_bars()
    {
            return create_anti_roll_bar(multibody.front_arb, front_left, front_right, spec.front_arb_stiffness)
                && create_anti_roll_bar(multibody.rear_arb, rear_left, rear_right, spec.rear_arb_stiffness);
    }


    bool Simulation::create_driveline_shafts()
    {
            driveline_assembly& driveline = multibody.driveline;
            PxTransform chassis_pose = body->getGlobalPose();
            bool drives_front = spec.drivetrain_type == 1 || spec.drivetrain_type == 2;
            bool drives_rear = spec.drivetrain_type == 0 || spec.drivetrain_type == 2;
            // hub height, shafts hang under the floor beside the axles
            float axle_y = (wheel_offsets[front_left].y + wheel_offsets[rear_left].y) * 0.5f;
            float front_z = (wheel_offsets[front_left].z + wheel_offsets[front_right].z) * 0.5f;
            float rear_z = (wheel_offsets[rear_left].z + wheel_offsets[rear_right].z) * 0.5f;
            // rwd/fwd package on the driven axle, awd power unit near the com
            float gearbox_z = spec.center_of_mass_z;
            if (drives_front && !drives_rear)
            {
                gearbox_z = front_z;
            }
            else if (drives_rear && !drives_front)
            {
                gearbox_z = rear_z;
            }
            {
                float driven_axle_z = drives_rear ? rear_z : front_z;
                if (fabsf(gearbox_z - driven_axle_z) < 0.08f)
                {
                    float toward_center = (driven_axle_z >= 0.0f) ? -1.0f : 1.0f;
                    gearbox_z = driven_axle_z + toward_center * 0.10f;
                }
            }
            float gearbox_y = axle_y;

            auto add_axle = [&](bool front) -> bool
            {
                int left = front ? front_left : rear_left;
                int right = front ? front_right : rear_right;
                float axle_z = (wheel_offsets[left].z + wheel_offsets[right].z) * 0.5f;
                PxVec3 diff_local(0.0f, axle_y, axle_z);
                PxVec3 diff_world = hardpoint_world(chassis_pose, diff_local);
                if (driveline.differential_count >= 2)
                {
                    return false;
                }
                PxRigidDynamic* differential = create_mechanism_actor(
                    PxTransform(diff_world, chassis_pose.q),
                    PxSphereGeometry(0.08f),
                    PxMax(spec.differential_mass, 1.0f));
                if (!differential)
                {
                    return false;
                }
                PxD6Joint* mount = PxD6JointCreate(
                    *multibody.physics,
                    body,
                    local_anchor(body, diff_world),
                    differential,
                    PxTransform(PxIdentity));
                if (!mount)
                {
                    return false;
                }
                mount->setMotion(PxD6Axis::eX, PxD6Motion::eLOCKED);
                mount->setMotion(PxD6Axis::eY, PxD6Motion::eLOCKED);
                mount->setMotion(PxD6Axis::eZ, PxD6Motion::eLOCKED);
                mount->setMotion(PxD6Axis::eTWIST, PxD6Motion::eLOCKED);
                mount->setMotion(PxD6Axis::eSWING1, PxD6Motion::eLOCKED);
                mount->setMotion(PxD6Axis::eSWING2, PxD6Motion::eLOCKED);
                register_multibody_joint(mount);
                driveline.differential[driveline.differential_count++] = differential;

                for (int wheel_index : { left, right })
                {
                    suspension_corner& corner = multibody.corners[wheel_index];
                    if (!corner.wheel_body)
                    {
                        return false;
                    }
                    // plunging cv shaft, dynamic, length free so suspension can travel
                    PxVec3 hub_world = corner.wheel_body->getGlobalPose().p;
                    PxVec3 delta = hub_world - diff_world;
                    float length = delta.magnitude();
                    if (length < 0.08f)
                    {
                        return false;
                    }
                    PxVec3 direction = delta / length;
                    PxVec3 cross = PxVec3(1.0f, 0.0f, 0.0f).cross(direction);
                    float dot = PxVec3(1.0f, 0.0f, 0.0f).dot(direction);
                    PxQuat shaft_rotation = dot < -0.9999f
                        ? PxQuat(PxPi, PxVec3(0.0f, 1.0f, 0.0f))
                        : PxQuat(cross.x, cross.y, cross.z, 1.0f + dot).getNormalized();

                    float shaft_mass = PxMax(spec.halfshaft_mass, 0.5f);
                    float inner_length = length * 0.55f;
                    float outer_length = length * 0.55f;
                    PxRigidDynamic* inner = create_segment_actor(
                        diff_world,
                        diff_world + direction * inner_length,
                        0.018f,
                        shaft_mass * 0.5f);
                    PxRigidDynamic* outer = create_segment_actor(
                        hub_world - direction * outer_length,
                        hub_world,
                        0.018f,
                        shaft_mass * 0.5f);
                    if (!inner || !outer)
                    {
                        return false;
                    }
                    if (!create_spherical_joint(differential, inner, diff_world)
                        || !create_spherical_joint(outer, corner.wheel_body, hub_world))
                    {
                        return false;
                    }

                    PxVec3 joint_world = diff_world + direction * (length * 0.5f);
                    PxTransform inner_frame(
                        inner->getGlobalPose().transformInv(joint_world),
                        (inner->getGlobalPose().q.getConjugate() * shaft_rotation).getNormalized());
                    PxTransform outer_frame(
                        outer->getGlobalPose().transformInv(joint_world),
                        (outer->getGlobalPose().q.getConjugate() * shaft_rotation).getNormalized());
                    PxD6Joint* plunge = PxD6JointCreate(
                        *multibody.physics,
                        inner,
                        inner_frame,
                        outer,
                        outer_frame);
                    if (!plunge)
                    {
                        return false;
                    }
                    float plunge_travel = cfg.suspension_travel + 0.15f;
                    plunge->setMotion(PxD6Axis::eX, PxD6Motion::eLIMITED);
                    plunge->setMotion(PxD6Axis::eY, PxD6Motion::eLOCKED);
                    plunge->setMotion(PxD6Axis::eZ, PxD6Motion::eLOCKED);
                    plunge->setMotion(PxD6Axis::eTWIST, PxD6Motion::eFREE);
                    plunge->setMotion(PxD6Axis::eSWING1, PxD6Motion::eLOCKED);
                    plunge->setMotion(PxD6Axis::eSWING2, PxD6Motion::eLOCKED);
                    plunge->setLinearLimit(
                        PxD6Axis::eX,
                        PxJointLinearLimitPair(
                            multibody.physics->getTolerancesScale(),
                            -plunge_travel,
                            plunge_travel));
                    plunge->setConstraintFlag(PxConstraintFlag::eENABLE_EXTENDED_LIMITS, true);
                    register_multibody_joint(plunge);
                    driveline.halfshaft[wheel_index] = outer;
                }

                PxVec3 gearbox_world = hardpoint_world(chassis_pose, PxVec3(0.0f, gearbox_y, gearbox_z));
                // skip the short transaxle flange gap, only span real longitudinal runs
                if ((diff_world - gearbox_world).magnitude() > 0.25f && driveline.propshaft_count < 2)
                {
                    PxRigidDynamic* prop = create_link(
                        gearbox_world,
                        diff_world,
                        PxMax(spec.driveshaft_mass, 0.5f));
                    if (!prop)
                    {
                        return false;
                    }
                    if (!create_spherical_joint(prop, differential, diff_world))
                    {
                        return false;
                    }
                    prop->setAngularDamping(1.5f);
                    prop->setLinearDamping(0.5f);
                    driveline.propshaft[driveline.propshaft_count++] = prop;
                }
                return true;
            };

            if (drives_front && !add_axle(true))
            {
                return false;
            }
            if (drives_rear && !add_axle(false))
            {
                return false;
            }
            return true;
    }


    bool Simulation::create_driveline()
    {
            driveline_assembly& driveline = multibody.driveline;
            if (!create_driveline_shafts())
            {
                return false;
            }

            PxTransform chassis_pose = body->getGlobalPose();
            // joint twist is about x, so rotate frames so x runs down the propshaft (chassis z)
            PxQuat shaft_rotation = (chassis_pose.q * PxQuat(PxPi * 0.5f, PxVec3(0.0f, 1.0f, 0.0f))).getNormalized();
            bool drives_rear = spec.drivetrain_type == 0 || spec.drivetrain_type == 2;
            bool drives_front = spec.drivetrain_type == 1 || spec.drivetrain_type == 2;
            float axle_y = (wheel_offsets[front_left].y + wheel_offsets[rear_left].y) * 0.5f;
            float front_z = (wheel_offsets[front_left].z + wheel_offsets[front_right].z) * 0.5f;
            float rear_z = (wheel_offsets[rear_left].z + wheel_offsets[rear_right].z) * 0.5f;
            float gearbox_z = spec.center_of_mass_z;
            if (drives_front && !drives_rear)
            {
                gearbox_z = front_z;
            }
            else if (drives_rear && !drives_front)
            {
                gearbox_z = rear_z;
            }
            float axle_z = drives_rear ? rear_z : front_z;
            if (drives_front && drives_rear)
            {
                axle_z = rear_z;
            }
            // keep a short flange gap so the torsion joint is not degenerate on transaxles
            if (fabsf(gearbox_z - axle_z) < 0.08f)
            {
                float toward_center = (axle_z >= 0.0f) ? -1.0f : 1.0f;
                gearbox_z = axle_z + toward_center * 0.10f;
            }
            PxVec3 gearbox_local(0.0f, axle_y, gearbox_z);
            PxVec3 axle_local(0.0f, axle_y, axle_z);
            PxVec3 gearbox_world = hardpoint_world(chassis_pose, gearbox_local);
            PxVec3 axle_world = hardpoint_world(chassis_pose, axle_local);

            driveline.gearbox_output = create_mechanism_actor(
                PxTransform(gearbox_world, shaft_rotation),
                PxCapsuleGeometry(0.06f, 0.04f),
                2.0f);
            driveline.axle_input = create_mechanism_actor(
                PxTransform(axle_world, shaft_rotation),
                PxCapsuleGeometry(0.05f, 0.03f),
                2.0f);
            if (!driveline.gearbox_output || !driveline.axle_input)
            {
                return false;
            }

            float driveline_inertia = PxMax(spec.driveline_inertia, 0.001f);
            driveline.gearbox_output->setMassSpaceInertiaTensor(PxVec3(driveline_inertia, driveline_inertia * 0.35f, driveline_inertia * 0.35f));
            driveline.axle_input->setMassSpaceInertiaTensor(PxVec3(0.02f, 0.01f, 0.01f));
            driveline.gearbox_output->setMaxAngularVelocity(1000.0f);
            driveline.axle_input->setMaxAngularVelocity(1000.0f);

            auto make_bearing = [&](PxRigidDynamic* shaft, const PxVec3& world_pos) -> bool
            {
                PxTransform world_frame(world_pos, shaft_rotation);
                PxTransform body_frame(
                    body->getGlobalPose().transformInv(world_pos),
                    (body->getGlobalPose().q.getConjugate() * shaft_rotation).getNormalized());
                PxTransform shaft_frame(
                    shaft->getGlobalPose().transformInv(world_pos),
                    (shaft->getGlobalPose().q.getConjugate() * shaft_rotation).getNormalized());
                PxRevoluteJoint* bearing = PxRevoluteJointCreate(
                    *multibody.physics,
                    body,
                    body_frame,
                    shaft,
                    shaft_frame);
                if (!bearing)
                {
                    return false;
                }
                bearing->setConstraintFlag(PxConstraintFlag::eENABLE_EXTENDED_LIMITS, true);
                register_multibody_joint(bearing);
                return true;
            };

            if (!make_bearing(driveline.gearbox_output, gearbox_world)
                || !make_bearing(driveline.axle_input, axle_world))
            {
                return false;
            }

            PxTransform gearbox_joint_frame(
                driveline.gearbox_output->getGlobalPose().transformInv(gearbox_world),
                (driveline.gearbox_output->getGlobalPose().q.getConjugate() * shaft_rotation).getNormalized());
            PxTransform axle_joint_frame(
                driveline.axle_input->getGlobalPose().transformInv(axle_world),
                (driveline.axle_input->getGlobalPose().q.getConjugate() * shaft_rotation).getNormalized());
            driveline.torsion_joint = PxD6JointCreate(
                *multibody.physics,
                driveline.gearbox_output,
                gearbox_joint_frame,
                driveline.axle_input,
                axle_joint_frame);
            if (!driveline.torsion_joint)
            {
                return false;
            }
            // bearings hold position, twist drive is the physical driveshaft spring
            driveline.torsion_joint->setMotion(PxD6Axis::eX, PxD6Motion::eFREE);
            driveline.torsion_joint->setMotion(PxD6Axis::eY, PxD6Motion::eFREE);
            driveline.torsion_joint->setMotion(PxD6Axis::eZ, PxD6Motion::eFREE);
            driveline.torsion_joint->setMotion(PxD6Axis::eTWIST, PxD6Motion::eFREE);
            driveline.torsion_joint->setMotion(PxD6Axis::eSWING1, PxD6Motion::eFREE);
            driveline.torsion_joint->setMotion(PxD6Axis::eSWING2, PxD6Motion::eFREE);
            // flanges carry inertia, twist compliance stays in the powertrain step
            driveline.torsion_joint->setConstraintFlag(PxConstraintFlag::eENABLE_EXTENDED_LIMITS, true);
            register_multibody_joint(driveline.torsion_joint);
            driveline.initialized = true;
            return true;
    }


    bool Simulation::has_physical_driveline() const
    {
            return multibody.driveline.initialized
                && multibody.driveline.torsion_joint
                && multibody.driveline.gearbox_output
                && multibody.driveline.axle_input;
    }


    float Simulation::read_driveline_twist() const
    {
            if (!has_physical_driveline())
            {
                return driveshaft_twist;
            }
            PxQuat relative = multibody.driveline.torsion_joint->getRelativeTransform().q;
            // twist about joint x, small angle extract
            float twist = 2.0f * atanf(relative.x / PxMax(relative.w, 1e-6f));
            if (!std::isfinite(twist))
            {
                return 0.0f;
            }
            return twist;
    }


    float Simulation::read_driveline_gearbox_speed() const
    {
            if (!has_physical_driveline() || !body)
            {
                return gearbox_input_angular_velocity;
            }
            PxVec3 axis = multibody.driveline.gearbox_output->getGlobalPose().q.rotate(PxVec3(1.0f, 0.0f, 0.0f));
            // spin is relative to the chassis, the writers set chassis angular velocity plus axis spin
            // so reading the absolute value back fed the chassis roll rate into the gearbox every substep
            PxVec3 spin = multibody.driveline.gearbox_output->getAngularVelocity() - body->getAngularVelocity();
            return spin.dot(axis);
    }


    void Simulation::sync_driveline_axle_speed(float axle_speed)
    {
            if (!has_physical_driveline())
            {
                return;
            }
            PxVec3 axis = multibody.driveline.axle_input->getGlobalPose().q.rotate(PxVec3(1.0f, 0.0f, 0.0f));
            PxVec3 chassis_angular = body->getAngularVelocity();
            multibody.driveline.axle_input->setAngularVelocity(chassis_angular + axis * axle_speed);
    }


    void Simulation::set_driveline_torsion_enabled(bool enabled)
    {
            if (!has_physical_driveline() || enabled)
            {
                return;
            }
            // open clutch or neutral, align flanges so stored twist cannot shove the axle
            multibody.driveline.gearbox_output->setGlobalPose(
                PxTransform(
                    multibody.driveline.gearbox_output->getGlobalPose().p,
                    multibody.driveline.axle_input->getGlobalPose().q));
            driveshaft_twist = 0.0f;
    }


    void Simulation::destroy_multibody()
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


    bool Simulation::create_multibody(PxPhysics* physics, PxScene* scene, bool destroy_existing )
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
            if (!create_anti_roll_bars())
            {
                destroy_multibody();
                return false;
            }
            if (!create_driveline())
            {
                destroy_multibody();
                return false;
            }

            update_assembled_center_of_mass();
            multibody.initialized = true;
            return true;
    }


    bool Simulation::rebuild_multibody(bool preserve_motion )
    {
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


    PxVec3 Simulation::actor_point_velocity(PxRigidBody* actor, const PxVec3& world_point)
    {
            return actor->getLinearVelocity() + actor->getAngularVelocity().cross(world_point - actor->getGlobalPose().p);
    }


    PxVec3 Simulation::ground_point_velocity(const wheel& wheel_state)
    {
            const PxRigidDynamic* ground = wheel_state.contact_actor ? wheel_state.contact_actor->is<PxRigidDynamic>() : nullptr;
            return ground ? ground->getLinearVelocity() + ground->getAngularVelocity().cross(wheel_state.contact_point - ground->getGlobalPose().p) : PxVec3(0.0f);
    }


    void Simulation::refresh_wheel_actor_state()
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


    void Simulation::wake_vehicle_assembly()
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


    bool Simulation::vehicle_assembly_is_settled()
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


    void Simulation::sleep_vehicle_assembly()
    {
            // hard zero first, puttosleep alone leaves residual crawl from contacts
            if (body)
            {
                body->setLinearVelocity(PxVec3(0.0f));
                body->setAngularVelocity(PxVec3(0.0f));
                body->clearForce(PxForceMode::eFORCE);
                body->clearTorque(PxForceMode::eFORCE);
                body->putToSleep();
            }
            for (int i = 0; i < multibody.actor_count; i++)
            {
                PxRigidDynamic* actor = multibody.actors[i];
                if (!actor || actor->getRigidBodyFlags().isSet(PxRigidBodyFlag::eKINEMATIC))
                {
                    continue;
                }
                actor->setLinearVelocity(PxVec3(0.0f));
                actor->setAngularVelocity(PxVec3(0.0f));
                actor->clearForce(PxForceMode::eFORCE);
                actor->clearTorque(PxForceMode::eFORCE);
                actor->putToSleep();
            }
            for (int i = 0; i < wheel_count; i++)
            {
                wheels[i].angular_velocity = 0.0f;
                wheels[i].net_torque = 0.0f;
                wheels[i].drive_torque = 0.0f;
                wheels[i].brake_torque = 0.0f;
            }
            driveshaft_twist = 0.0f;
            axle_drive_torque = 0.0f;
            prev_velocity = PxVec3(0.0f);
            vehicle_sleep_timer = 0.0f;
            vehicle_sleeping = true;
    }


    float Simulation::compute_damper_force(float velocity, float base_damping)
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


    void Simulation::update_multibody(float delta_time)
    {
            if (!multibody.initialized || delta_time <= 0.0f)
            {
                return;
            }

            if (multibody.rack_joint)
            {
                float curved_input = copysignf(powf(fabsf(PxClamp(input.steering, -1.0f, 1.0f)), spec.steering_linearity), input.steering);
                // minus matches trailing-rod + input convention, verify in-game before flipping
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
                float compression = PxClamp(corner.shock_rest_length - length, 0.0f, cfg.suspension_travel);
                float damper = compute_damper_force(relative_speed, spring_damping[i]);
                float bump_extra = 0.0f;
                float bump_start = cfg.suspension_travel * spec.bump_stop_threshold;
                if (compression > bump_start)
                {
                    float bump_travel = PxMax(cfg.suspension_travel - bump_start, 0.001f);
                    float bump_compression = compression - bump_start;
                    float bump_progress = PxClamp(bump_compression / bump_travel, 0.0f, 1.0f);
                    bump_extra += bump_compression * spec.bump_stop_stiffness * (1.0f + spec.bump_stop_progression * bump_progress * bump_progress);
                }
                float packer_start = cfg.suspension_travel * spec.packer_threshold;
                if (compression > packer_start)
                {
                    bump_extra += (compression - packer_start) * spec.packer_stiffness;
                }

                float force_magnitude = PxMax(spring_stiffness[i] * compression, 0.0f) - damper + bump_extra;
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

            // physical arb bars carry mass only, roll stiffness and bush damping live in the force couple
            auto apply_anti_roll = [&](int left, int right, float stiffness)
            {
                if (stiffness <= 0.0f)
                {
                    return;
                }
                suspension_corner& left_corner = multibody.corners[left];
                suspension_corner& right_corner = multibody.corners[right];
                float left_compression = PxClamp(
                    left_corner.shock_rest_length - left_corner.shock_length,
                    -cfg.suspension_travel,
                    cfg.suspension_travel);
                float right_compression = PxClamp(
                    right_corner.shock_rest_length - right_corner.shock_length,
                    -cfg.suspension_travel,
                    cfg.suspension_travel);
                // shock_velocity > 0 on extension, compression rate delta matches the spring term sign
                float compression_delta = left_compression - right_compression;
                float compression_rate_delta =
                    right_corner.shock_velocity - left_corner.shock_velocity;
                float arb_mass = PxMax(spec.arb_mass, 1.0f);
                float roll_mass = PxMax(cfg.mass * 0.05f, arb_mass);
                float arb_damping = 2.0f * 0.4f * sqrtf(stiffness * roll_mass);
                float force_magnitude = PxClamp(
                    compression_delta * stiffness + compression_rate_delta * arb_damping,
                    -spec.max_susp_force,
                    spec.max_susp_force);
                PxVec3 up = body->getGlobalPose().q.rotate(PxVec3(0.0f, 1.0f, 0.0f));
                PxVec3 left_bottom = left_corner.upright->getGlobalPose().transform(
                    left_corner.upright_shock_anchor);
                PxVec3 right_bottom = right_corner.upright->getGlobalPose().transform(
                    right_corner.upright_shock_anchor);
                PxVec3 left_top = body->getGlobalPose().transform(
                    left_corner.chassis_shock_anchor);
                PxVec3 right_top = body->getGlobalPose().transform(
                    right_corner.chassis_shock_anchor);
                PxVec3 left_force = -up * force_magnitude;
                PxVec3 right_force = up * force_magnitude;
                PxRigidBodyExt::addForceAtPos(
                    *left_corner.upright, left_force, left_bottom, PxForceMode::eFORCE);
                PxRigidBodyExt::addForceAtPos(
                    *right_corner.upright, right_force, right_bottom, PxForceMode::eFORCE);
                PxRigidBodyExt::addForceAtPos(
                    *body, -left_force, left_top, PxForceMode::eFORCE);
                PxRigidBodyExt::addForceAtPos(
                    *body, -right_force, right_top, PxForceMode::eFORCE);
            };
            apply_anti_roll(front_left, front_right, spec.front_arb_stiffness);
            apply_anti_roll(rear_left, rear_right, spec.rear_arb_stiffness);
    }


    tire_probe_row Simulation::probe_tread_row(PxScene* scene, const PxVec3& row_center, const PxVec3& plane_down, const PxVec3& wheel_axis, const PxVec3& local_up, float row_radius, float ray_length, float max_penetration, int column_count, float arc, const PxQueryFilterData& filter)
    {
            tire_probe_row row;
            float height_sum = 0.0f;
            float weight_sum = 0.0f;
            float best_weight = 0.0f;
            PxVec3 normal_sum = PxVec3(0.0f);
            PxVec3 point_sum = PxVec3(0.0f);

            for (int c = 0; c < column_count; c++)
            {
                float theta = column_count > 1 ? (static_cast<float>(c) / static_cast<float>(column_count - 1) - 0.5f) * 2.0f * arc : 0.0f;
                PxVec3 direction = PxQuat(theta, wheel_axis).rotate(plane_down);
                if (direction.normalize() < 1e-4f)
                {
                    continue;
                }

                PxRaycastBuffer probe;
                if (!scene->raycast(row_center, direction, ray_length, probe, PxHitFlag::eNORMAL | PxHitFlag::ePOSITION, filter, &self_filter) || !probe.block.actor)
                {
                    continue;
                }
                if (!std::isfinite(probe.block.distance) || probe.block.distance < 0.0f)
                {
                    continue;
                }

                PxVec3 probe_normal = probe.block.normal;
                if (!is_finite_vec(probe_normal) || probe_normal.magnitudeSquared() < 1e-6f)
                {
                    continue;
                }
                probe_normal.normalize();
                // a wall or a ceiling cannot hold the car up
                if (probe_normal.dot(local_up) < 0.5f)
                {
                    continue;
                }

                PxVec3 probe_point = row_center + direction * probe.block.distance;
                if (!is_finite_vec(probe_point))
                {
                    continue;
                }
                // perpendicular distance to the surface the probe found, this is the number the tire
                // deflects against and it stays honest on a banked road where a vertical drop does not
                float height = (row_center - probe_point).dot(probe_normal);
                if (!std::isfinite(height))
                {
                    continue;
                }

                float weight = PxMax(cosf(theta), 0.05f);
                height_sum += height * weight;
                weight_sum += weight;
                normal_sum += probe_normal * weight;
                point_sum += probe_point * weight;
                if (weight > best_weight)
                {
                    best_weight = weight;
                    row.actor = probe.block.actor;
                }
            }

            if (weight_sum < 1e-4f)
            {
                return row;
            }

            float penetration = PxClamp(row_radius - height_sum / weight_sum, 0.0f, max_penetration);
            if (penetration <= 0.0f)
            {
                return row;
            }
            PxVec3 normal = normal_sum / weight_sum;
            if (normal.normalize() < 1e-4f)
            {
                return row;
            }
            PxVec3 point = point_sum / weight_sum;
            if (!is_finite_vec(point))
            {
                return row;
            }

            row.hit = true;
            row.penetration = penetration;
            row.normal = normal;
            row.point = point;
            return row;
    }


    void Simulation::update_suspension(PxScene* scene, float dt)
    {
            PxTransform pose = body->getGlobalPose();
            PxVec3 local_down = pose.q.rotate(PxVec3(0, -1, 0));
            PxVec3 local_up = -local_down;

            PxQueryFilterData filter;
            filter.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;
            // only the chassis needs filtering because mechanism shapes are not scene queries
            self_filter.ignore = body;

            const int row_count = PxClamp(spec.tire_probe_rows, 1, max_tire_probe_rows);
            const int column_count = PxClamp(spec.tire_probe_columns, 1, max_tire_probe_columns);
            const float arc = PxClamp(spec.tire_probe_arc, 0.0f, 0.7f);
            const float rows_inverse = 1.0f / static_cast<float>(row_count);

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
                PxQuat query_rotation = multibody.corners[i].upright ? multibody.corners[i].upright->getGlobalPose().q : pose.q;
                PxVec3 wheel_axis = query_rotation.rotate(PxVec3(1.0f, 0.0f, 0.0f));

                // the probes belong in the wheel plane, so they measure against the chassis down with
                // the spin axis component taken out rather than against world down
                PxVec3 plane_down = local_down - wheel_axis * local_down.dot(wheel_axis);
                if (plane_down.normalize() < 1e-4f)
                {
                    plane_down = local_down;
                }

                float downward_speed = wheel_actor ? PxMax(wheel_actor->getLinearVelocity().dot(local_down), 0.0f) : PxMax(body->getLinearVelocity().dot(local_down), 0.0f);
                // predicted travel extends detection only and must never add tire penetration
                float predicted_travel = PxMax(downward_speed * dt, 0.005f);
                float max_penetration = PxMin(wheel_radius * 0.25f, 0.05f);
                float ray_length = wheel_radius + PxMax(0.08f, predicted_travel);
                float crown = PxClamp(spec.tire_crown_drop, 0.0f, wheel_radius * 0.15f);

                w.grounded = false;
                w.tire_load = 0.0f;
                w.contact_actor = nullptr;
                w.contact_normal = local_up;
                w.row_count = row_count;
                for (int r = 0; r < max_tire_probe_rows; r++)
                {
                    w.row_load[r] = 0.0f;
                }
                sweep_distance[i] = ray_length - wheel_radius;
                debug_sweep[i].origin = wheel_center;
                debug_sweep[i].hit = false;
                debug_sweep[i].hit_point = wheel_center + plane_down * wheel_radius;
                debug_sweep[i].row_count = 0;
                debug_suspension_top[i] = pose.transform(multibody.corners[i].chassis_shock_anchor);
                debug_suspension_bottom[i] = wheel_center;

                tire_probe_row rows[max_tire_probe_rows];
                int hit_count = 0;
                for (int r = 0; r < row_count; r++)
                {
                    float offset = (static_cast<float>(r) + 0.5f) * rows_inverse - 0.5f;
                    // a tread is crowned, and the mean radius is held at nominal so a flat road at zero
                    // camber still loads exactly as the single point model did before
                    float row_radius = wheel_radius + crown * (1.0f / 3.0f - 4.0f * offset * offset);
                    PxVec3 row_center = wheel_center + wheel_axis * (offset * wheel_width);
                    rows[r] = probe_tread_row(scene, row_center, plane_down, wheel_axis, local_up, row_radius, ray_length, max_penetration, column_count, arc, filter);
                    hit_count += rows[r].hit ? 1 : 0;
                }

                if (hit_count == 0)
                {
                    continue;
                }

                // each row carries its share of the carcass rate, so an asymmetric patch produces a real
                // moment on the upright instead of one force through the wheel centre line
                float row_stiffness = PxMax(spec.tire_vertical_stiffness, 1.0f) * rows_inverse;
                float row_damping = 2.0f * 0.7f * sqrtf(PxMax(spec.tire_vertical_stiffness, 1.0f) * PxMax(cfg.wheel_mass, 1.0f)) * rows_inverse;
                float row_force_limit = spec.max_susp_force * rows_inverse;
                PxVec3 total_force = PxVec3(0.0f);
                PxVec3 point_accumulator = PxVec3(0.0f);
                float load_sum = 0.0f;
                float deepest = 0.0f;
                for (int r = 0; r < row_count; r++)
                {
                    tire_probe_row& row = rows[r];
                    if (!row.hit)
                    {
                        continue;
                    }

                    PxVec3 probe_velocity = wheel_actor ? actor_point_velocity(wheel_actor, row.point) : actor_point_velocity(body, row.point);
                    if (const PxRigidDynamic* ground_actor = row.actor ? row.actor->is<PxRigidDynamic>() : nullptr)
                    {
                        probe_velocity -= ground_actor->getLinearVelocity() + ground_actor->getAngularVelocity().cross(row.point - ground_actor->getGlobalPose().p);
                    }
                    // damping only exists while the tread is actually squashed, applying it on a grazing
                    // hit invents a force before the tire has touched anything
                    float closing = probe_velocity.dot(row.normal);
                    row.load = PxClamp(row.penetration * row_stiffness - closing * row_damping, 0.0f, row_force_limit);
                    if (row.load <= 0.0f)
                    {
                        continue;
                    }

                    total_force += row.normal * row.load;
                    point_accumulator += row.point * row.load;
                    load_sum += row.load;
                    w.row_load[r] = row.load;
                    if (row.penetration > deepest)
                    {
                        deepest = row.penetration;
                        w.contact_actor = row.actor;
                    }
                }

                if (load_sum <= 0.0f)
                {
                    w.contact_actor = nullptr;
                    continue;
                }

                PxVec3 aggregate_normal = total_force;
                if (aggregate_normal.normalize() < 1e-4f)
                {
                    aggregate_normal = local_up;
                }
                // normal filtering prevents mesh seams from converting forward speed into launch force
                if (is_finite_vec(previous_normal) && previous_normal.magnitudeSquared() > 1e-6f)
                {
                    previous_normal.normalize();
                    float normal_blend = exp_decay(30.0f, dt);
                    aggregate_normal = previous_normal * (1.0f - normal_blend) + aggregate_normal * normal_blend;
                    if (aggregate_normal.normalize() < 1e-4f)
                    {
                        aggregate_normal = local_up;
                    }
                }
                PxVec3 aggregate_point = point_accumulator / load_sum;
                if (!is_finite_vec(aggregate_point))
                {
                    w.contact_actor = nullptr;
                    continue;
                }

                w.grounded = true;
                w.contact_point = aggregate_point;
                w.contact_normal = aggregate_normal;
                w.tire_load = PxMax(total_force.dot(aggregate_normal), 0.0f);
                sweep_distance[i] = -deepest;
                debug_sweep[i].hit = true;
                debug_sweep[i].hit_point = aggregate_point;

                for (int r = 0; r < row_count; r++)
                {
                    const tire_probe_row& row = rows[r];
                    if (!row.hit || row.load <= 0.0f)
                    {
                        continue;
                    }
                    safe_add_force_at_pos(wheel_actor ? wheel_actor : body, row.normal * row.load, row.point);
                    if (PxRigidDynamic* ground_actor = row.actor ? row.actor->is<PxRigidDynamic>() : nullptr)
                    {
                        safe_add_force_at_pos(ground_actor, -row.normal * row.load, row.point);
                    }
                    int slot = debug_sweep[i].row_count++;
                    debug_sweep[i].row_point[slot] = row.point;
                    debug_sweep[i].row_normal[slot] = row.normal;
                    debug_sweep[i].row_load[slot] = row.load;
                }
            }
    }


    float Simulation::angular_velocity_to_rpm(float angular_velocity)
    {
            return angular_velocity * 60.0f / (2.0f * PxPi);
    }


    float Simulation::get_driven_wheel_radius()
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


    float Simulation::get_average_driven_angular_velocity(bool absolute, int* count )
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


    void Simulation::update_boost(float throttle, float rpm, float dt)
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


    float Simulation::get_engine_torque(float rpm)
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
            if (redline <= peak)
            {
                redline = peak + 1.0f;
            }

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


    float Simulation::get_electric_motor_torque(float rpm, float throttle)
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


    float Simulation::wheel_rpm_to_engine_rpm(float wheel_rpm, int gear)
    {
            if (gear < 0 || gear >= spec.gear_count || gear == 1)
            {
                return spec.engine_idle_rpm;
            }
            return fabsf(wheel_rpm * spec.gear_ratios[gear] * spec.final_drive);
    }


    float Simulation::get_upshift_speed(int from_gear, float throttle)
    {
            if (from_gear < 2 || from_gear >= spec.gear_count - 1)
            {
                return 999.0f;
            }
            float t = PxClamp((throttle - 0.3f) / 0.5f, 0.0f, 1.0f);
            return spec.upshift_speed_base[from_gear] + t * (spec.upshift_speed_sport[from_gear] - spec.upshift_speed_base[from_gear]);
    }


    float Simulation::get_downshift_speed(int gear)
    {
            return (gear >= 2 && gear < spec.gear_count) ? spec.downshift_speeds[gear] : 0.0f;
    }


    void Simulation::set_active_gear(int gear)
    {
            gear = PxClamp(gear, 0, spec.gear_count - 1);
            if (gear == current_gear)
            {
                return;
            }

            float next_ratio = gear == 1 ? 0.0f : spec.gear_ratios[gear] * spec.final_drive;
            // always kill stored twist, ratio sign flips (e.g. into reverse) were detonating the shaft
            driveshaft_twist = 0.0f;
            driveshaft_torque = 0.0f;
            if (fabsf(next_ratio) > 0.001f)
            {
                // resync from the wheels, remapping through the old ratio keeps spin-state errors
                gearbox_input_angular_velocity = get_average_driven_angular_velocity(false) * next_ratio;
            }
            else
            {
                gearbox_input_angular_velocity = 0.0f;
            }
            current_gear = gear;
    }


    void Simulation::update_automatic_gearbox(float dt, float throttle, float forward_speed)
    {
            bool kickdown_requested = throttle > 0.9f && previous_automatic_throttle <= 0.75f;
            previous_automatic_throttle = throttle;

            if (shift_cooldown > 0.0f)
            {
                shift_cooldown = PxMax(shift_cooldown - dt, 0.0f);
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
            float body_speed_ms = body ? body->getLinearVelocity().magnitude() : fabsf(forward_speed);

            // reverse only when actually rolling backward slowly, not while yaw-sliding with near-zero forward
            if (forward_speed < -0.5f && body_speed_ms < 2.0f && input.brake > 0.1f && throttle < 0.1f && current_gear != 0)
            {
                set_active_gear(0);
                is_shifting = true;
                shift_timer = spec.shift_time * 2.0f;
                last_shift_direction = -1;
                return;
            }

            // neutral to first: clutch engagement, no shift delay
            if (current_gear == 1 && throttle > 0.1f && forward_speed >= -0.5f)
            {
                set_active_gear(2);
                last_shift_direction = 1;
                return;
            }

            // reverse to first
            if (current_gear == 0)
            {
                if (throttle > 0.1f || forward_speed > 0.5f)
                {
                    set_active_gear(2);
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

                // hold the gear through a line lock, the driven wheels are spinning so both triggers are
                // armed even though the car has not moved and the box would run out of gears standing still
                if (can_shift && !burnout_active && (speed_trigger || rpm_trigger) && current_gear < spec.gear_count - 1 && throttle > 0.1f)
                {
                    set_active_gear(current_gear + 1);
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

                // hold gear on pure lift, coast downshifts only when braking for engine brake
                bool brake_requested = input.brake > spec.input_deadzone;
                if (can_shift && brake_requested && speed_kmh < downshift_threshold && current_gear > 2)
                {
                    set_active_gear(current_gear - 1);
                    is_shifting = true;
                    shift_timer = spec.shift_time;
                    last_shift_direction = -1;
                    downshift_blip_timer = spec.downshift_blip_duration;
                    return;
                }

                // lugging protection only under throttle, never on a closed pedal coast
                if (can_shift && throttle > 0.1f && current_gear > 2 && engine_rpm < spec.shift_down_rpm)
                {
                    float ratio          = fabsf(spec.gear_ratios[current_gear - 1]) * spec.final_drive;
                    float potential_rpm  = angular_velocity_to_rpm(fabsf(forward_speed) / get_driven_wheel_radius()) * ratio;
                    if (potential_rpm < spec.shift_up_rpm * 0.85f)
                    {
                        set_active_gear(current_gear - 1);
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
                            set_active_gear(target);
                            is_shifting = true;
                            shift_timer = spec.shift_time;
                            last_shift_direction = -1;
                            downshift_blip_timer = spec.downshift_blip_duration;
                        }
                    }
                }
            }
    }


    const char* Simulation::get_gear_string()
    {
            static constexpr const char* names[] = { "R", "N", "1", "2", "3", "4", "5", "6", "7", "8", "9" };
            constexpr int name_count = static_cast<int>(sizeof(names) / sizeof(names[0]));
            return (current_gear >= 0 && current_gear < spec.gear_count && current_gear < name_count) ? names[current_gear] : "?";
    }


    void Simulation::apply_axle_diff(int left, int right, float axle_torque, float dt)
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
                float left_inertia = PxMax(wheel_moi[left], 0.1f);
                float right_inertia = PxMax(wheel_moi[right], 0.1f);
                float inverse_inertia_sum = 1.0f / left_inertia + 1.0f / right_inertia;
                float non_reversing_torque = fabsf(delta_w) / (PxMax(dt, 0.001f) * inverse_inertia_sum);
                constexpr float maximum_speed_error_correction = 0.8f;
                lock_torque = PxMin(lock_torque, non_reversing_torque * maximum_speed_error_correction);
                float bias_sign = (delta_w > 0.0f) ? -1.0f : 1.0f;

                left_torque += bias_sign * lock_torque;
                right_torque -= bias_sign * lock_torque;
            }
            wheels[left].net_torque += left_torque;
            wheels[right].net_torque += right_torque;
            wheels[left].drive_torque += left_torque;
            wheels[right].drive_torque += right_torque;
    }


    void Simulation::apply_drive_torque(float total_torque, float dt)
    {
            if (spec.drivetrain_type == 2)
            {
                // awd - center diff torque split
                float front_torque = total_torque * spec.torque_split_front;
                float rear_torque  = total_torque * (1.0f - spec.torque_split_front);
                apply_axle_diff(front_left, front_right, front_torque, dt);
                apply_axle_diff(rear_left,  rear_right,  rear_torque, dt);
            }
            else if (spec.drivetrain_type == 1)
            {
                // fwd
                apply_axle_diff(front_left, front_right, total_torque, dt);
            }
            else
            {
                // rwd
                apply_axle_diff(rear_left, rear_right, total_torque, dt);
            }
    }


    void Simulation::integrate_powertrain(float dt)
    {
            float ratio = is_in_neutral() ? 0.0f : spec.gear_ratios[current_gear] * spec.final_drive;
            float wheel_angular_velocity = get_average_driven_angular_velocity(false);
            const bool physical_driveline = has_physical_driveline();
            if (physical_driveline && fabsf(ratio) > 0.001f)
            {
                // flanges live in axle space, reflected gearbox inertia changes with gear
                float reflected = PxMax(spec.driveline_inertia, 0.001f) * ratio * ratio;
                multibody.driveline.gearbox_output->setMassSpaceInertiaTensor(
                    PxVec3(reflected, reflected * 0.35f, reflected * 0.35f));
                gearbox_input_angular_velocity = read_driveline_gearbox_speed() * ratio;
            }
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
            // pedal closed means coast, blip may still raise revs for a matched downshift
            bool coasting = drive_input <= spec.input_deadzone;
            float idle_angular_velocity = spec.engine_idle_rpm * PxPi * 2.0f / 60.0f;
            float max_angular_velocity = PxMax(spec.engine_max_rpm * PxPi * 2.0f / 60.0f, idle_angular_velocity);
            float clutch_capacity = PxMax(spec.clutch_max_torque, 10.0f);
            float clutch_damping = clutch_capacity / 12.0f;
            float electric_target = is_in_forward_gear() ? get_electric_motor_torque(engine_rpm, input.throttle) * assisted_actuators.engine_torque_scale : 0.0f;
            float electric_rate = spec.electric_torque_response > 0.0f ? spec.electric_torque_response : 50.0f;
            // snap motor off on lift, lerp decay was still shoving the axle after throttle closed
            if (coasting)
            {
                motor_torque = 0.0f;
            }
            else
            {
                motor_torque = lerp(motor_torque, electric_target, exp_decay(electric_rate, dt));
            }
            axle_drive_torque = 0.0f;
            engine_brake_torque = 0.0f;

            // the clutch damper couples two small inertias, so the substep has to resolve it or the slip
            // rings, damping times substep times the coupling compliance must stay at or under one
            float coupling_compliance = 1.0f / engine_inertia + 1.0f / driveline_inertia;
            float clutch_substep = 1.0f / PxMax(clutch_damping * coupling_compliance, 1e-6f);
            float target_substep = PxMin(0.0025f, clutch_substep);
            int substep_count = PxClamp(static_cast<int>(ceilf(dt / target_substep)), 1, 32);
            float substep = dt / static_cast<float>(substep_count);
            float accumulated_axle_torque = 0.0f;
            float accumulated_powertrain_reaction = 0.0f;
            float accumulated_engine_torque = 0.0f;
            for (int step = 0; step < substep_count; step++)
            {
                if (physical_driveline && fabsf(ratio) > 0.001f)
                {
                    gearbox_input_angular_velocity = read_driveline_gearbox_speed() * ratio;
                }
                // torque follows rpm inside the loop, a value frozen for the whole step overshoots
                // idle control and the torque curve whenever the engine accelerates hard
                float substep_rpm = engine_angular_velocity * 60.0f / (PxPi * 2.0f);
                float boosted_torque = get_engine_torque(substep_rpm) * (1.0f + boost_pressure * spec.boost_torque_mult);
                float combustion_torque = rev_limiter_active ? 0.0f : boosted_torque * effective_throttle * assisted_actuators.engine_torque_scale;
                float idle_torque = PxClamp((idle_angular_velocity - engine_angular_velocity) * engine_inertia * spec.engine_rpm_smoothing, 0.0f, spec.engine_peak_torque * 0.35f);
                accumulated_engine_torque += combustion_torque + idle_torque;
                float clutch_slip = engine_angular_velocity - gearbox_input_angular_velocity;
                float clutch_torque = PxClamp(clutch_slip * clutch_damping, -clutch_capacity * clutch, clutch_capacity * clutch);
                // a damper cannot pull the two speeds past each other in one substep, without this cap the
                // explicit integration flipped the slip sign every substep and dragged the engine to a stall
                float non_overshoot_torque = fabsf(clutch_slip) / PxMax(substep * coupling_compliance, 1e-6f);
                clutch_torque = PxClamp(clutch_torque, -non_overshoot_torque, non_overshoot_torque);
                float shaft_torque = 0.0f;
                float shaft_speed_difference = 0.0f;
                // open driveline during shifts or with clutch out, shaft must not keep shoving the axle
                bool driveline_open = is_shifting || fabsf(ratio) <= 0.001f || clutch <= 0.05f;
                if (physical_driveline)
                {
                    set_driveline_torsion_enabled(!driveline_open && fabsf(ratio) > 0.001f);
                }
                if (driveline_open)
                {
                    driveshaft_twist = 0.0f;
                    clutch_torque = 0.0f;
                    shaft_torque = 0.0f;
                    if (fabsf(ratio) > 0.001f)
                    {
                        gearbox_input_angular_velocity = wheel_angular_velocity * ratio;
                        if (physical_driveline)
                        {
                            sync_driveline_axle_speed(wheel_angular_velocity);
                            PxVec3 axis = multibody.driveline.gearbox_output->getGlobalPose().q.rotate(PxVec3(1.0f, 0.0f, 0.0f));
                            multibody.driveline.gearbox_output->setAngularVelocity(
                                body->getAngularVelocity() + axis * wheel_angular_velocity);
                        }
                    }
                }
                else if (fabsf(ratio) > 0.001f)
                {
                    // clutch capacity caps shaft windup so a locked spinning wheel cannot store fake energy
                    float max_shaft_torque = clutch_capacity * fabsf(ratio) * 1.25f;
                    if (physical_driveline)
                    {
                        sync_driveline_axle_speed(wheel_angular_velocity);
                        gearbox_input_angular_velocity = read_driveline_gearbox_speed() * ratio;
                    }
                    shaft_speed_difference = gearbox_input_angular_velocity / ratio - wheel_angular_velocity;
                    driveshaft_twist += shaft_speed_difference * substep;
                    if (stiffness > 0.0f)
                    {
                        float max_twist = max_shaft_torque / stiffness;
                        driveshaft_twist = PxClamp(driveshaft_twist, -max_twist, max_twist);
                        shaft_torque = driveshaft_twist * stiffness + shaft_speed_difference * shaft_damping;
                    }
                    else
                    {
                        driveshaft_twist = 0.0f;
                        shaft_torque = clutch_torque * ratio * spec.drivetrain_efficiency;
                    }
                    shaft_torque = PxClamp(shaft_torque, -max_shaft_torque, max_shaft_torque);
                    // closed pedal cannot propel in the selected gear direction
                    if (coasting)
                    {
                        if (ratio >= 0.0f)
                        {
                            if (driveshaft_twist > 0.0f)
                            {
                                driveshaft_twist = 0.0f;
                            }
                            if (shaft_torque > 0.0f)
                            {
                                shaft_torque = 0.0f;
                            }
                        }
                        else
                        {
                            if (driveshaft_twist < 0.0f)
                            {
                                driveshaft_twist = 0.0f;
                            }
                            if (shaft_torque < 0.0f)
                            {
                                shaft_torque = 0.0f;
                            }
                        }
                    }
                }

                // closed throttle pumping losses, weak viscous friction alone leaves flywheel dump for seconds
                float coast_factor = 1.0f - PxClamp(effective_throttle, 0.0f, 1.0f);
                float rpm_above_idle = PxMax(engine_angular_velocity - idle_angular_velocity, 0.0f);
                float pumping_torque = coast_factor * rpm_above_idle * spec.engine_peak_torque * 0.0004f;
                float friction_torque = spec.engine_friction * engine_angular_velocity + pumping_torque;
                float engine_acceleration = (combustion_torque + idle_torque - friction_torque - clutch_torque) / engine_inertia;
                float previous_engine_angular_velocity = engine_angular_velocity;
                // idle is the floor, the torque curve is only defined at or above it and there is no
                // stall or restart model, so a dragged down engine could never come back
                engine_angular_velocity = PxClamp(engine_angular_velocity + engine_acceleration * substep, idle_angular_velocity, max_angular_velocity);
                engine_acceleration = (engine_angular_velocity - previous_engine_angular_velocity) / substep;

                float efficiency = PxClamp(spec.drivetrain_efficiency, 0.1f, 1.0f);
                float gearbox_output_angular_velocity = fabsf(ratio) > 0.001f ? gearbox_input_angular_velocity / ratio : 0.0f;
                float efficiency_factor = shaft_torque * gearbox_output_angular_velocity >= 0.0f ? 1.0f / efficiency : efficiency;
                float shaft_reaction = fabsf(ratio) > 0.001f ? shaft_torque * efficiency_factor / ratio : 0.0f;
                float driveline_drag = gearbox_input_angular_velocity * spec.bearing_friction * driveline_inertia;
                float gearbox_acceleration = 0.0f;
                if (!driveline_open)
                {
                    if (physical_driveline && fabsf(ratio) > 0.001f)
                    {
                        // flange carries reflected inertia, shaft reaction is applied here
                        float reflected = driveline_inertia * ratio * ratio;
                        float axle_speed = read_driveline_gearbox_speed();
                        float axle_clutch_torque = clutch_torque * ratio;
                        float axle_drag = axle_speed * spec.bearing_friction * reflected;
                        float axle_torque = axle_clutch_torque - shaft_torque * efficiency_factor - axle_drag;
                        gearbox_acceleration = axle_torque / PxMax(reflected, 0.001f) / ratio;
                        axle_speed += axle_torque / PxMax(reflected, 0.001f) * substep;
                        PxVec3 axis = multibody.driveline.gearbox_output->getGlobalPose().q.rotate(PxVec3(1.0f, 0.0f, 0.0f));
                        multibody.driveline.gearbox_output->setAngularVelocity(body->getAngularVelocity() + axis * axle_speed);
                        gearbox_input_angular_velocity = axle_speed * ratio;
                    }
                    else
                    {
                        gearbox_acceleration = (clutch_torque - shaft_reaction - driveline_drag) / driveline_inertia;
                        gearbox_input_angular_velocity += gearbox_acceleration * substep;
                    }
                }
                accumulated_axle_torque += shaft_torque;
                accumulated_powertrain_reaction -= engine_inertia * engine_acceleration + driveline_inertia * gearbox_acceleration;
            }

            engine_output_torque = accumulated_engine_torque / static_cast<float>(substep_count);
            float mechanical_axle_torque = accumulated_axle_torque / static_cast<float>(substep_count);
            float electric_axle_torque = motor_torque * spec.final_drive * spec.drivetrain_efficiency;
            driveshaft_torque = mechanical_axle_torque;
            axle_drive_torque = mechanical_axle_torque + electric_axle_torque;
            // clutch out means engine is not loaded into the chassis, idle must not shake the body awake
            if (clutch > 0.05f && !is_in_neutral())
            {
                PxVec3 crank_axis_local(spec.engine_crank_axis_x, spec.engine_crank_axis_y, spec.engine_crank_axis_z);
                PxVec3 crank_axis = body->getGlobalPose().q.rotate(crank_axis_local.getNormalized());
                safe_add_torque(body, crank_axis * (accumulated_powertrain_reaction / static_cast<float>(substep_count)));
            }
            if (input.throttle <= spec.input_deadzone && mechanical_axle_torque * wheel_angular_velocity < 0.0f)
            {
                engine_brake_torque = fabsf(mechanical_axle_torque);
            }
            // the final drive housing is bolted to the chassis so the axle torque reacts into the body,
            // without it angular momentum is created from nothing and squat and lift are under predicted
            if (fabsf(axle_drive_torque) > 0.0f)
            {
                PxVec3 axle_axis = body->getGlobalPose().q.rotate(PxVec3(1.0f, 0.0f, 0.0f));
                safe_add_torque(body, axle_axis * -axle_drive_torque);
            }
            apply_drive_torque(axle_drive_torque, dt);
            engine_rpm = engine_angular_velocity * 60.0f / (PxPi * 2.0f);
    }


    float Simulation::brake_torque_sign(int wheel_index)
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


    void Simulation::apply_service_brakes(float forward_speed_ms, float dt)
    {
            if (input.brake <= spec.input_deadzone)
            {
                clear_abs_state();
                reverse_request_timer = 0.0f;
                return;
            }

            // in reverse the brake pedal is the reverse throttle, apply_drivetrain already routed the drive torque
            if (is_in_reverse())
            {
                clear_abs_state();
                reverse_request_timer = 0.0f;
                return;
            }

            // require true near-stop, forward-only gate was engaging reverse mid-spin
            float body_speed_ms = body ? body->getLinearVelocity().magnitude() : fabsf(forward_speed_ms);
            bool reverse_conditions_met = !spec.manual_transmission
                && body_speed_ms < 1.0f
                && fabsf(forward_speed_ms) < 0.5f
                && input.brake > 0.8f
                && input.throttle < spec.input_deadzone
                && is_in_forward_gear()
                && !is_shifting;
            // dwell on the request, reaching for the throttle to hold a line lock lands the brake first
            // and a single frame of that used to drop the car straight into reverse
            reverse_request_timer = reverse_conditions_met ? reverse_request_timer + dt : 0.0f;
            bool reverse_requested = reverse_request_timer >= 0.25f;
            if (reverse_requested)
            {
                clear_abs_state();
                set_active_gear(0);
                is_shifting  = true;
                shift_timer  = spec.shift_time * 2.0f;
                return;
            }

            // line lock sends the whole circuit to the front so the held axle can out grip the driven one
            float bias_front = burnout_active ? 1.0f : spec.brake_bias_front;
            float front_t = spec.brake_force * cfg.front_wheel_radius * input.brake * bias_front * 0.5f;
            float rear_t  = burnout_active ? 0.0f : spec.brake_force * cfg.rear_wheel_radius * input.brake * (1.0f - spec.brake_bias_front) * 0.5f;

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


    void Simulation::apply_drivetrain(float forward_speed_kmh, float dt)
    {
            float forward_speed_ms = forward_speed_kmh / 3.6f;

            update_burnout(forward_speed_ms);
            update_automatic_gearbox(dt, input.throttle, forward_speed_ms);
            bool traction_requested = input.throttle > spec.input_deadzone && is_in_forward_gear();
            // abs must not pulse the held front wheels out of a line lock
            bool braking_requested = input.brake > spec.input_deadzone && !is_in_reverse() && !burnout_active && fabsf(forward_speed_kmh) > spec.braking_speed_threshold;
            update_assist_controller(traction_requested, braking_requested, dt);

            if (downshift_blip_timer > 0.0f)
            {
                downshift_blip_timer = PxMax(downshift_blip_timer - dt, 0.0f);
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


    float Simulation::get_effective_wheel_radius(int wheel_index, float tire_load)
    {
            float raw_radius = cfg.wheel_radius_for(wheel_index);
            float radius = std::isfinite(raw_radius) && raw_radius > 0.0f ? PxMax(raw_radius, 0.05f) : 0.34f;
            float deflection = spec.tire_vertical_stiffness > 1000.0f ? PxClamp(tire_load / spec.tire_vertical_stiffness, 0.0f, 0.05f) : 0.0f;
            return PxMax(radius - deflection * 0.55f, 0.05f);
    }


    void Simulation::update_handbrake()
    {
            float brake_torque = spec.handbrake_torque * input.handbrake;
            bool enabled = brake_torque > 0.0f;
            for (int i : { rear_left, rear_right })
            {
                if (PxRevoluteJoint* wheel_joint = multibody.corners[i].wheel_joint)
                {
                    wheel_joint->setDriveVelocity(0.0f);
                    wheel_joint->setDriveForceLimit(brake_torque);
                    wheel_joint->setRevoluteJointFlag(PxRevoluteJointFlag::eDRIVE_ENABLED, enabled);
                }
            }
    }


    void Simulation::update_tire_condition()
    {
            for (int i = 0; i < wheel_count; i++)
            {
                wheel& w = wheels[i];
                tire_condition_modifiers condition = get_tire_condition_modifiers(w.thermal.avg_surface(), w.thermal.core, w.wear, w.tire_load);
                w.condition_grip = condition.peak_grip;
                w.condition_stiffness = condition.stiffness;
                w.condition_relaxation = condition.relaxation;
                w.temperature_grip = condition.temperature_grip;
                w.wear_grip = condition.wear_grip;
                if (!multibody.corners[i].wheel_body || !w.grounded || w.tire_load <= 0.0f)
                {
                    w.slip_ratio = 0.0f;
                    w.slip_angle = 0.0f;
                    w.tire_saturation = 0.0f;
                }
            }
    }


    void Simulation::relax_tire_slip(wheel& w, float raw_slip_ratio, float raw_slip_angle, float ground_speed, float surface_speed, float dt)
    {
            float relaxation_length = PxMax(spec.tire_relaxation_length * w.condition_relaxation, 0.05f);
            float longitudinal_length = relaxation_length * (fabsf(raw_slip_ratio) > fabsf(w.slip_ratio) ? 0.85f : 0.65f);
            float lateral_length = relaxation_length * (fabsf(raw_slip_angle) > fabsf(w.slip_angle) ? 1.10f : 0.85f);
            float absolute_surface_speed = fabsf(surface_speed);
            float rolling_speed = PxMax(ground_speed, absolute_surface_speed);
            float longitudinal_blend = 1.0f - expf(-rolling_speed * dt / longitudinal_length);
            float lateral_blend = 1.0f - expf(-ground_speed * dt / lateral_length);
            w.slip_ratio = lerp(w.slip_ratio, raw_slip_ratio, longitudinal_blend);
            w.slip_angle = lerp(w.slip_angle, raw_slip_angle, lateral_blend);

            if (rolling_speed < spec.min_slip_speed && input.throttle <= spec.input_deadzone)
            {
                float rest_factor = 1.0f - rolling_speed / PxMax(spec.min_slip_speed, 0.01f);
                float rest_decay = exp_decay(24.0f * rest_factor, dt);
                w.slip_ratio = lerp(w.slip_ratio, 0.0f, rest_decay);
                w.slip_angle = lerp(w.slip_angle, 0.0f, rest_decay);
                if (rolling_speed < 0.03f)
                {
                    w.slip_ratio = 0.0f;
                    w.slip_angle = 0.0f;
                }
            }
    }


    void Simulation::apply_tire_forces(float dt)
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
                float wmoi    = (std::isfinite(wheel_moi[i]) && wheel_moi[i] > 0.0f) ? wheel_moi[i] : 1.0f;
                float wr_eff = get_effective_wheel_radius(i, w.tire_load);

                float dyn_camb = is_front(i) ? spec.front_camber : spec.rear_camber;
                if (wheel_actor)
                {
                    PxVec3 chassis_up = pose.q.rotate(PxVec3(0.0f, 1.0f, 0.0f));
                    // camber is an angle to the road, not to the body, measuring it against the chassis
                    // hid body roll from the tire so grip never faded as the car leaned over
                    PxVec3 camber_reference = w.grounded ? w.contact_normal : chassis_up;
                    float measured_camber = asinf(PxClamp(wheel_axis.dot(camber_reference), -1.0f, 1.0f));
                    dyn_camb = i == front_left || i == rear_left ? measured_camber : -measured_camber;
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
                    float spin_retain = powf(PxClamp(spec.airborne_wheel_decay, 0.0f, 1.0f), dt * 200.0f);
                    if (dt > 1e-5f)
                    {
                        w.net_torque += w.angular_velocity * (spin_retain - 1.0f) * wmoi / dt;
                    }
                    if (wheel_actor)
                    {
                        // irs: only the brake caliper reacts on the upright, drive is internal to the chassis
                        float brake_signed = brake_torque_sign(i) * w.brake_torque;
                        safe_add_torque(wheel_actor, wheel_axis * w.net_torque);
                        if (multibody.corners[i].upright)
                        {
                            safe_add_torque(multibody.corners[i].upright, wheel_axis * (-brake_signed));
                        }
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
                float ground_speed = sqrtf(vx * vx + vy * vy);

                if (log_pacejka)
                {
                    SP_LOG_INFO("[%s] vx=%.3f, vy=%.3f, ws=%.3f", wheel_name, vx, vy, w.angular_velocity * wr_eff);
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

                // slip relaxation and a wheel locking are both far faster than one physics step, so the tire
                // and the spin run against a frozen chassis and physx is handed the time average
                const int substep_count = PxClamp(spec.tire_substeps, 1, 8);
                const float substep = dt / static_cast<float>(substep_count);
                const float substep_inverse = 1.0f / static_cast<float>(substep_count);
                const float tread_width = PxMax(cfg.wheel_width_for(i), 0.05f);
                const bool use_brush = spec.tire_model_type == static_cast<int>(tire_model::brush);
                const float camber_thrust_sign = (i == front_left || i == rear_left) ? -1.0f : 1.0f;
                // the chassis cannot react inside the loop, so the rest state model still has to arrest a
                // whole step of residual motion and not a fraction of one
                const float inverse_dt = 1.0f / PxMax(dt, 0.001f);
                const float corner_mass = PxMax(w.tire_load / 9.81f, 1.0f);
                const float effective_longitudinal_mass = 1.0f / (1.0f / corner_mass + wr_eff * wr_eff / wmoi);
                const float blend_lo = 0.5f;
                const float blend_hi = PxMax(spec.min_slip_speed * 2.0f, 1.0f);
                float omega = w.angular_velocity;
                // only the drive, the caliper in net_torque is signed against the step start spin and the
                // loop resigns it every substep instead
                const float drive_torque = w.drive_torque;
                // used where the spin is too slow to have a meaningful direction
                const float brake_fallback = brake_torque_sign(i);
                float sum_long = 0.0f;
                float sum_lat = 0.0f;
                float sum_slip_speed = 0.0f;
                float sum_weight = 0.0f;
                float sum_saturation = 0.0f;
                float sum_brake = 0.0f;
                float patch_half_length = 0.0f;

                for (int step = 0; step < substep_count; step++)
                {
                    float wheel_speed = omega * wr_eff;
                    float slip_v_long = fabsf(wheel_speed - vx);
                    // include lateral motion, otherwise a sideways slide reads as at rest and
                    // falls back to the static friction model instead of the tire curve
                    float substep_slip_v = sqrtf(slip_v_long * slip_v_long + vy * vy);
                    float max_v = PxMax(fabsf(wheel_speed), ground_speed);

                    float slip_denominator = PxMax(PxMax(fabsf(vx), fabsf(wheel_speed)), 0.01f);
                    float raw_slip_ratio = PxClamp((wheel_speed - vx) / slip_denominator, -1.0f, 1.0f);
                    float raw_slip_angle = atan2f(vy, PxMax(fabsf(vx), 0.5f));
                    relax_tire_slip(w, raw_slip_ratio, raw_slip_angle, ground_speed, wheel_speed, substep);

                    // smoothstep blend: 0 at rest, 1 at full speed, smooth transition in between
                    float blend_t = PxClamp((max_v - blend_lo) / PxMax(blend_hi - blend_lo, 0.01f), 0.0f, 1.0f);
                    float substep_weight = blend_t * blend_t * (3.0f - 2.0f * blend_t);

                    // static friction model (dominant at rest / very low speed)
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
                    float curve_slip_angle = PxClamp(effective_slip_angle, -spec.max_slip_angle, spec.max_slip_angle);

                    tire_force_result dynamic_force;
                    float saturation = 0.0f;
                    if (use_brush)
                    {
                        brush_tire_params brush = evaluate_brush_params(spec, wr_eff, tread_width, w.tire_load, w.condition_stiffness);
                        dynamic_force = evaluate_brush_model(spec, brush, w.slip_ratio, curve_slip_angle, dyn_camb, w.tire_load, peak_force_long, peak_force_lat, camber_thrust_sign, saturation);
                        patch_half_length = brush.patch_half_length;
                    }
                    else
                    {
                        dynamic_force = evaluate_magic_formula(spec, w.slip_ratio, curve_slip_angle, dyn_camb, w.tire_load, peak_force_long, peak_force_lat, w.condition_stiffness, camber_thrust_sign);
                    }

                    // blend between static friction and the tire curve
                    float substep_lat = static_lat_f * (1.0f - substep_weight) + dynamic_force.lateral * substep_weight;
                    float substep_long = static_long_f * (1.0f - substep_weight) + dynamic_force.longitudinal * substep_weight;

                    // reclamp after blend, static and curve peaks can otherwise stack past mu n
                    float ellipse_x = substep_long / PxMax(peak_force_long, 1.0f);
                    float ellipse_y = substep_lat / PxMax(peak_force_lat, 1.0f);
                    float ellipse = sqrtf(ellipse_x * ellipse_x + ellipse_y * ellipse_y);
                    if (ellipse > 1.0f)
                    {
                        substep_long /= ellipse;
                        substep_lat /= ellipse;
                    }

                    sum_long += substep_long;
                    sum_lat += substep_lat;
                    sum_slip_speed += substep_slip_v;
                    sum_weight += substep_weight;
                    sum_saturation += saturation;

                    // everything acting on the spin except the caliper, the patch torque included because
                    // the next substep reads its slip from this integration
                    float free_torque = drive_torque - substep_long * wr_eff - omega * spec.bearing_friction * wmoi;

                    // a caliper stops a wheel, it never drives one backwards, so it is capped at the torque
                    // that brings the spin to exactly zero. this is what removes lockup chatter
                    float brake_signed = 0.0f;
                    if (w.brake_torque > 0.0f)
                    {
                        float direction = fabsf(omega) > 0.1f ? (omega > 0.0f ? -1.0f : 1.0f) : brake_fallback;
                        float stopping = -(omega * wmoi / substep + free_torque);
                        if (direction > 0.0f)
                        {
                            brake_signed = PxMin(w.brake_torque, PxMax(stopping, 0.0f));
                        }
                        else if (direction < 0.0f)
                        {
                            brake_signed = -PxMin(w.brake_torque, PxMax(-stopping, 0.0f));
                        }
                    }
                    sum_brake += brake_signed;

                    omega += (free_torque + brake_signed) / wmoi * substep;
                    w.rotation += omega * substep;
                }

                float long_f = sum_long * substep_inverse;
                float lat_f = sum_lat * substep_inverse;
                float slip_v = sum_slip_speed * substep_inverse;
                float pacejka_weight = sum_weight * substep_inverse;
                float mean_brake = sum_brake * substep_inverse;
                float wheel_speed = omega * wr_eff;
                w.tire_saturation = sum_saturation * substep_inverse;
                w.contact_patch_length = patch_half_length * 2.0f;
                // physx gets what the caliper actually did, not what it was asked for
                w.net_torque = drive_torque + mean_brake;

                if (log_pacejka)
                {
                    SP_LOG_INFO("[%s] blend=%.2f, lat_f=%.1f, long_f=%.1f", wheel_name, pacejka_weight, lat_f, long_f);
                }

                // carcass flex peaks below optimal pressure, clamp so over-inflation cannot make the friction-work term negative
                float pressure_heat_mult = PxMax(1.0f + (1.0f - pressure_ratio) * 1.5f, 0.2f);
                float rolling_heat = fabsf(wheel_speed) * spec.tire_heat_from_rolling * pressure_heat_mult;
                float cooling_air = spec.tire_cooling_rate + ground_speed * spec.tire_cooling_airflow;
                float force_magnitude = sqrtf(long_f * long_f + lat_f * lat_f);
                float normalized_force = force_magnitude / PxMax(spec.load_reference, 1.0f);
                float slip_ratio_eff = PxClamp(slip_v / PxMax(ground_speed, 0.5f), 0.0f, 2.0f);
                float speed_heat_scale = PxClamp(ground_speed / 2.0f, 0.0f, 1.0f);
                float friction_work = normalized_force * slip_ratio_eff * pacejka_weight * speed_heat_scale;
                float base_heat = friction_work * spec.tire_heat_from_slip * pressure_heat_mult + rolling_heat;

                // zone load comes from the tread rows the contact probes actually loaded, so where a tire
                // cooks follows the measured patch rather than an estimate made from camber alone
                float zone_load[3] = { 0.0f, 0.0f, 0.0f };
                int zone_rows[3] = { 0, 0, 0 };
                float zone_heat[3];
                {
                    int probe_rows = PxClamp(w.row_count, 1, max_tire_probe_rows);
                    // row zero sits at negative offset along the spin axis, which is the outboard shoulder
                    // on the left of the car and the inboard one on the right
                    float side_sign = (i == front_left || i == rear_left) ? 1.0f : -1.0f;
                    for (int r = 0; r < probe_rows; r++)
                    {
                        float offset = ((static_cast<float>(r) + 0.5f) / static_cast<float>(probe_rows) - 0.5f) * side_sign;
                        int zone = PxClamp(static_cast<int>((0.5f - offset) * 3.0f), 0, 2);
                        zone_load[zone] += w.row_load[r];
                        zone_rows[zone]++;
                    }

                    float load_total = 0.0f;
                    int counted = 0;
                    for (int z = 0; z < 3; z++)
                    {
                        if (zone_rows[z] > 0)
                        {
                            zone_load[z] /= static_cast<float>(zone_rows[z]);
                            load_total += zone_load[z];
                            counted++;
                        }
                    }
                    // fewer rows than zones cannot resolve a shoulder, so the gaps take the mean
                    float mean_load = counted > 0 ? load_total / static_cast<float>(counted) : 0.0f;
                    for (int z = 0; z < 3; z++)
                    {
                        if (zone_rows[z] == 0)
                        {
                            zone_load[z] = mean_load;
                        }
                    }
                    float share_total = zone_load[0] + zone_load[1] + zone_load[2];
                    for (int z = 0; z < 3; z++)
                    {
                        // shares average to one so the existing heat calibration is untouched when the
                        // patch loads evenly
                        float share = share_total > 1e-6f ? zone_load[z] * 3.0f / share_total : 1.0f;
                        zone_heat[z] = base_heat * share;
                    }
                }

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
                    float zone_excess = PxMax(w.thermal.surface[z] - spec.tire_optimal_temp, 0.0f) / PxMax(spec.tire_temp_range, 1.0f);
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
                // same body as the normal force (wheel), so the suspension carries the wrench
                // chassis-at-patch skipped anti-squat and invented wheelie pitch / yaw couples
                PxRigidDynamic* force_body = wheel_actor ? wheel_actor : body;
                safe_add_force_at_pos(force_body, tire_force, fpos);
                if (w.contact_actor)
                {
                    if (const PxRigidDynamic* ground_actor = w.contact_actor->is<PxRigidDynamic>())
                    {
                        safe_add_force_at_pos(const_cast<PxRigidDynamic*>(ground_actor), -tire_force, fpos);
                    }
                }

                w.net_torque -= w.angular_velocity * spec.bearing_friction * wmoi; // bearing drag

                if (wheel_actor)
                {
                    // drive, brake and bearing only, the patch force already spins the wheel down
                    // through its contact offset so adding long_f times radius here counted it twice
                    safe_add_torque(wheel_actor, wheel_axis * w.net_torque);
                    if (multibody.corners[i].upright)
                    {
                        // caliper on upright only, irs drive reaction must not pitch the knuckle
                        safe_add_torque(multibody.corners[i].upright, wheel_axis * (-mean_brake));
                    }
                }

                // no rotation integration here, the substep loop already did it against the evolving spin

                if (log_pacejka)
                {
                    SP_LOG_INFO("[%s] ang_vel=%.4f, lat_f=%.1f, long_f=%.1f", wheel_name, w.angular_velocity, lat_f, long_f);
                }
            }
            float front_steering_angle = (wheels[front_left].dynamic_toe + wheels[front_right].dynamic_toe) * 0.5f;
            wheels[front_left].bump_steer = wheels[front_left].dynamic_toe - front_steering_angle - spec.front_toe;
            wheels[front_right].bump_steer = wheels[front_right].dynamic_toe - front_steering_angle + spec.front_toe;
            wheels[rear_left].bump_steer = wheels[rear_left].dynamic_toe - spec.rear_toe;
            wheels[rear_right].bump_steer = wheels[rear_right].dynamic_toe + spec.rear_toe;
            if (log_pacejka)
            {
                SP_LOG_INFO("=== pacejka tick end ===\n");
            }
    }


    void Simulation::apply_self_aligning_torque()
    {
            for (int i = 0; i < wheel_count; i++)
            {
                if (!wheels[i].grounded)
                {
                    continue;
                }

                float trail = get_wheel_pneumatic_trail(i);
                PxVec3 torque = wheels[i].contact_normal * (-wheels[i].lateral_force * trail * spec.self_align_gain);
                safe_add_torque(multibody.corners[i].upright, torque);
                if (const PxRigidDynamic* ground_actor = wheels[i].contact_actor ? wheels[i].contact_actor->is<PxRigidDynamic>() : nullptr)
                {
                    safe_add_torque(const_cast<PxRigidDynamic*>(ground_actor), -torque);
                }
            }
    }


    void Simulation::set_validation_speed(float speed)
    {
            PxTransform pose = body->getGlobalPose();
            PxVec3 forward = pose.q.rotate(PxVec3(0.0f, 0.0f, 1.0f));
            body->setLinearVelocity(forward * speed);
            body->setAngularVelocity(PxVec3(0.0f));
            for (int i = 0; i < multibody.actor_count; i++)
            {
                PxRigidDynamic* actor = multibody.actors[i];
                if (!actor || actor->getRigidBodyFlags().isSet(PxRigidBodyFlag::eKINEMATIC))
                {
                    continue;
                }
                actor->setLinearVelocity(forward * speed);
                actor->setAngularVelocity(PxVec3(0.0f));
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


    void Simulation::refresh_geometry_cache()
    {
            cfg.wheelbase   = wheel_offsets[front_left].z - wheel_offsets[rear_left].z;
            cfg.track_front = wheel_offsets[front_right].x - wheel_offsets[front_left].x;
            cfg.track_rear  = wheel_offsets[rear_right].x  - wheel_offsets[rear_left].x;
    }


    void Simulation::apply_positive_override(float& target, float value)
    {
            if (value > 0.0f)
            {
                target = value;
            }
    }


    void Simulation::apply_preset_geometry(const car_preset& spec)
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


    void Simulation::compute_constants()
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
            // ride rates must track spring supported mass, not body only mass after
            // subtracting arb coilover and diff actors that still hang on the chassis
            float spring_mass = sprung_mass();
            float axle_mass[2] = { spring_mass * wdf * 0.5f, spring_mass * (1.0f - wdf) * 0.5f };
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


    void Simulation::destroy()
    {
            destroy_multibody();
            if (body)             { body->release();             body = nullptr; }
            if (material)         { material->release();         material = nullptr; }
    }


    bool Simulation::setup(const setup_params& params)
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
            // spawn near spring equilibrium with sweep clearance to avoid initial overlap
            float front_mass_per_wheel = sprung_mass() * get_weight_distribution_front() * 0.5f;
            float front_omega = 2.0f * PxPi * spec.front_spring_freq;
            float front_stiffness = front_mass_per_wheel * front_omega * front_omega;
            float expected_sag = PxClamp(
                (front_mass_per_wheel * 9.81f) / front_stiffness,
                0.0f,
                cfg.suspension_travel * 0.8f);
            float avg_wheel_r = (cfg.front_wheel_radius + cfg.rear_wheel_radius) * 0.5f;
            float spawn_y = avg_wheel_r + cfg.suspension_height - expected_sag + 0.02f;

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

            PxVec3 com(spec.center_of_mass_x, spec.center_of_mass_y, spec.center_of_mass_z);
            apply_chassis_mass_properties(chassis_mass(), com);
            body->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, false);
            body->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
            body->setLinearDamping(spec.linear_damping);
            body->setAngularDamping(spec.angular_damping);

            params.scene->addActor(*body);

            if (!params.vertices.empty())
            {
                compute_aero_from_shape(params.vertices);
            }

            if (params.create_mechanisms)
            {
                if (!create_multibody(params.physics, params.scene))
                {
                    SP_LOG_ERROR("failed to create car suspension assembly");
                    destroy();
                    return false;
                }
            }

            SP_LOG_INFO(
                "car setup complete: mass=%.0f kg mechanisms=%d",
                cfg.mass,
                params.create_mechanisms ? 1 : 0
            );
            return true;
    }


    bool Simulation::set_chassis(
        const std::vector<PxConvexMesh*>& meshes,
        const std::vector<PxVec3>& vertices,
        PxPhysics* physics
    )
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
                {
                    body->detachShape(*shape);
                }
            }

            int shapes_attached = 0;
            if (material)
            {
                for (PxConvexMesh* mesh : meshes)
                {
                    if (!mesh)
                    {
                        continue;
                    }

                    PxConvexMeshGeometry geometry(mesh);
                    PxShape* shape = physics->createShape(geometry, *material);
                    if (!shape)
                    {
                        continue;
                    }

                    shape->setFlag(PxShapeFlag::eVISUALIZATION, true);
                    body->attachShape(*shape);
                    shape->release();
                    shapes_attached++;
                }
            }

            if (PxScene* scene = body->getScene())
            {
                scene->flushQueryUpdates();
            }

            PxVec3 com(spec.center_of_mass_x, spec.center_of_mass_y, spec.center_of_mass_z);
            apply_chassis_mass_properties(chassis_mass(), com);
            if (multibody.initialized)
            {
                update_assembled_center_of_mass();
            }

            if (!vertices.empty())
            {
                compute_aero_from_shape(vertices);
            }

            return shapes_attached > 0;
    }


    void Simulation::update_mass_properties()
    {
            if (!body)
            {
                return;
            }

            PxVec3 com(spec.center_of_mass_x, spec.center_of_mass_y, spec.center_of_mass_z);
            apply_chassis_mass_properties(chassis_mass(), com);
            if (multibody.initialized)
            {
                update_assembled_center_of_mass();
            }

            SP_LOG_INFO("car center of mass set to (%.2f, %.2f, %.2f)", com.x, com.y, com.z);
    }


    void Simulation::apply_car_spec(const car_preset& preset, bool set_as_base)
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


    bool Simulation::rebuild_vehicle_geometry()
    {
            compute_constants();
            update_mass_properties();
            return !multibody.initialized || rebuild_multibody();
    }


    void Simulation::reset_drivetrain_transients()
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
            burnout_active = false;
            reverse_request_timer = 0.0f;
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


    void Simulation::reset_wheel_thermals()
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


    void Simulation::load_car(const car_preset& new_spec)
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


    car_preset Simulation::make_upgraded_spec(const car_preset& base, const active_upgrades& ups)
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
            if (ups.exhaust > 0 && base.exhaust_stage_max > 0)
            {
                // each stage frees flow and strips muffling, the last stage is close to a straight pipe
                p.engine_peak_torque *= 1.0f + 0.02f * ups.exhaust;
                float openness = static_cast<float>(ups.exhaust) / static_cast<float>(PxMax(base.exhaust_stage_max, 1));
                p.exhaust_muffler_level = base.exhaust_muffler_level * (1.0f - 0.9f * openness);
            }
            if (ups.intake > 0 && base.intake_stage_max > 0)
            {
                p.engine_peak_torque *= 1.0f + 0.015f * ups.intake;
            }
            if (ups.turbo > 0 && base.turbo_stage_max > 0)
            {
                // a kit on a naturally aspirated car starts from a mild street setup
                if (!base.turbo_enabled || base.boost_max_pressure <= 0.0f)
                {
                    p.turbo_enabled = true;
                    p.boost_max_pressure = 0.6f;
                    p.boost_spool_rate = 3.0f;
                    p.boost_wastegate_rpm = PxMax(base.engine_redline_rpm - 800.0f, base.engine_idle_rpm + 1000.0f);
                    p.boost_torque_mult = 0.25f;
                    p.boost_min_rpm = base.engine_idle_rpm + 1500.0f;
                }
                float bm = 1.0f + 0.15f * ups.turbo;
                p.boost_max_pressure *= bm;
                p.boost_spool_rate *= 1.0f + 0.10f * ups.turbo;
            }

            p.mass = PxMax(p.mass, 200.0f);
            p.engine_peak_torque = PxMax(p.engine_peak_torque, 10.0f);
            p.engine_redline_rpm = PxMax(p.engine_redline_rpm, p.engine_idle_rpm + 100.0f);
            p.tire_friction = PxMax(p.tire_friction, 0.1f);

            return p;
    }


    void Simulation::clamp_upgrade_stage(int& stage, int max_stage)
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


    void Simulation::reapply_upgrades()
    {
            car_preset previous_spec = spec;
            config previous_config = cfg;
            clamp_upgrade_stage(upgrades.engine, base_spec.engine_stage_max);
            clamp_upgrade_stage(upgrades.suspension, base_spec.suspension_stage_max);
            clamp_upgrade_stage(upgrades.tires, base_spec.tires_stage_max);
            clamp_upgrade_stage(upgrades.brakes, base_spec.brakes_stage_max);
            clamp_upgrade_stage(upgrades.aero, base_spec.aero_stage_max);
            clamp_upgrade_stage(upgrades.weight, base_spec.weight_stage_max);
            clamp_upgrade_stage(upgrades.exhaust, base_spec.exhaust_stage_max);
            clamp_upgrade_stage(upgrades.intake, base_spec.intake_stage_max);
            clamp_upgrade_stage(upgrades.turbo, base_spec.turbo_stage_max);

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

            // a fitted turbo kit owns the boost setup, otherwise runtime hud edits survive the rebuild
            bool turbo_kit = upgrades.turbo > 0 && base_spec.turbo_stage_max > 0;
            if (turbo_kit)
            {
                spec.turbo_enabled = true;
            }
            else
            {
                spec.boost_max_pressure = save_bst_max;
                spec.boost_spool_rate = save_bst_spool;
                spec.boost_wastegate_rpm = save_bst_waste;
                spec.boost_torque_mult = save_bst_torq;
                spec.boost_min_rpm = save_bst_min;
            }
            if (!rebuild_vehicle_geometry())
            {
                spec = previous_spec;
                cfg = previous_config;
                compute_constants();
                update_mass_properties();
                SP_LOG_ERROR("failed to rebuild vehicle upgrades");
            }
    }


    void Simulation::reset_upgrades()
    {
            upgrades = active_upgrades{};
            reapply_upgrades();
    }


    void Simulation::set_center_of_mass(float x, float y, float z)
    {
            spec.center_of_mass_x = x;
            spec.center_of_mass_y = y;
            spec.center_of_mass_z = z;
            compute_constants();
            update_mass_properties();
    }


    void Simulation::set_center_of_mass_x(float x)
    { set_center_of_mass(x, spec.center_of_mass_y, spec.center_of_mass_z); }


    void Simulation::set_center_of_mass_y(float y)
    { set_center_of_mass(spec.center_of_mass_x, y, spec.center_of_mass_z); }


    void Simulation::set_center_of_mass_z(float z)
    { set_center_of_mass(spec.center_of_mass_x, spec.center_of_mass_y, z); }


    float Simulation::get_center_of_mass_x()
    { return spec.center_of_mass_x; }


    float Simulation::get_center_of_mass_y()
    { return spec.center_of_mass_y; }


    float Simulation::get_center_of_mass_z()
    { return spec.center_of_mass_z; }


    float Simulation::get_frontal_area()
    { return spec.frontal_area; }


    float Simulation::get_side_area()
    { return spec.side_area; }


    float Simulation::get_drag_coeff()
    { return spec.drag_coeff; }


    float Simulation::get_lift_coeff_front()
    { return spec.lift_coeff_front; }


    float Simulation::get_lift_coeff_rear()
    { return spec.lift_coeff_rear; }


    void Simulation::set_frontal_area(float area)
    { spec.frontal_area = area; }


    void Simulation::set_side_area(float area)
    { spec.side_area = area; }


    void Simulation::set_drag_coeff(float cd)
    { spec.drag_coeff = cd; }


    void Simulation::set_lift_coeff_front(float cl)
    { spec.lift_coeff_front = cl; }


    void Simulation::set_lift_coeff_rear(float cl)
    { spec.lift_coeff_rear = cl; }


    void Simulation::set_ground_effect_enabled(bool enabled)
    { spec.ground_effect_enabled = enabled; }


    bool Simulation::get_ground_effect_enabled()
    { return spec.ground_effect_enabled; }


    void Simulation::set_ground_effect_multiplier(float mult)
    { spec.ground_effect_multiplier = mult; }


    float Simulation::get_ground_effect_multiplier()
    { return spec.ground_effect_multiplier; }


    void Simulation::set_throttle(float v)
    { input_target.throttle  = PxClamp(v, 0.0f, 1.0f); }


    void Simulation::set_brake(float v)
    { input_target.brake     = PxClamp(v, 0.0f, 1.0f); }


    void Simulation::set_steering(float v)
    { input_target.steering  = PxClamp(v, -1.0f, 1.0f); }


    void Simulation::set_handbrake(float v)
    { input_target.handbrake = PxClamp(v, 0.0f, 1.0f); }


    void Simulation::update_input(float dt)
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


    void Simulation::tick(float dt)
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
            update_input(dt);
            update_handbrake();
            PxScene* scene = body->getScene();
            if (!scene)
            {
                return;
            }

            bool steering_adjusting = fabsf(input_target.steering - input.steering) > 0.001f;
            bool motion_requested = input.throttle > spec.input_deadzone || (is_in_reverse() && input.brake > spec.input_deadzone) || steering_adjusting;
            if (vehicle_sleeping)
            {
                // only driver input wakes, contact solver must not clear park sleep
                if (motion_requested)
                {
                    wake_vehicle_assembly();
                }
                else
                {
                    sleep_vehicle_assembly();
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
                    float temp_drop = (cooling_power / PxMax(spec.brake_thermal_mass, 0.1f)) * dt;
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
            update_tire_condition();
            apply_drivetrain(forward_speed * 3.6f, dt);
            engine_rotation = fmodf(engine_rotation + engine_rpm * PxPi * 2.0f / 60.0f * dt, PxPi * 2.0f);
            if (!std::isfinite(engine_rotation))
            {
                engine_rotation = 0.0f;
            }

            apply_tire_forces(dt);
            apply_self_aligning_torque();

            apply_aero_and_resistance();
            // park when the driver is idle and body crawl is tiny, do not wait forever on multibody jitter
            bool driver_idle = !motion_requested
                && input.brake <= spec.input_deadzone
                && fabsf(input.steering) <= spec.input_deadzone;
            float park_speed = vel.magnitude();
            if (driver_idle && (park_speed < 0.08f || vehicle_assembly_is_settled()))
            {
                vehicle_sleep_timer += dt;
                if (vehicle_sleep_timer >= 0.35f)
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


    float Simulation::get_speed_kmh()
    { return body ? body->getLinearVelocity().magnitude() * 3.6f : 0.0f; }


    float Simulation::get_throttle()
    { return input.throttle; }


    float Simulation::get_brake()
    { return input.brake; }


    float Simulation::get_steering()
    { return input.steering; }


    float Simulation::get_wheel_pneumatic_trail(int i)
    {
            if (!is_valid_wheel(i))
            {
                return 0.0f;
            }
            const wheel& w = wheels[i];
            return spec.tire_model_type == static_cast<int>(tire_model::brush)
                ? evaluate_brush_trail(w.contact_patch_length * 0.5f, w.tire_saturation)
                : evaluate_pneumatic_trail(spec, w.slip_angle, w.tire_load, w.condition_stiffness);
    }


    float Simulation::get_wheel_self_aligning_torque(int i)
    {
            if (!is_valid_wheel(i) || !wheels[i].grounded)
            {
                return 0.0f;
            }
            return -wheels[i].lateral_force * get_wheel_pneumatic_trail(i) * spec.self_align_gain;
    }


    void Simulation::set_wheel_rotation(int i, float v)
    {
            if (is_valid_wheel(i))
            {
                wheels[i].rotation = std::isfinite(v) ? v : 0.0f;
            }
    }


    void Simulation::set_wheel_angular_velocity(int i, float v)
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


    float Simulation::get_handbrake()
    { return input.handbrake; }


    float Simulation::get_suspension_travel()
    { return cfg.suspension_travel; }


    float Simulation::get_wheel_compression(int i)
    { return is_valid_wheel(i) ? wheels[i].compression : 0.0f; }


    float Simulation::get_wheel_slip_angle(int i)
    { return is_valid_wheel(i) ? wheels[i].slip_angle : 0.0f; }


    float Simulation::get_wheel_slip_ratio(int i)
    { return is_valid_wheel(i) ? wheels[i].slip_ratio : 0.0f; }


    float Simulation::get_wheel_tire_load(int i)
    { return is_valid_wheel(i) ? wheels[i].tire_load : 0.0f; }


    float Simulation::get_wheel_lateral_force(int i)
    { return is_valid_wheel(i) ? wheels[i].lateral_force : 0.0f; }


    float Simulation::get_wheel_longitudinal_force(int i)
    { return is_valid_wheel(i) ? wheels[i].longitudinal_force : 0.0f; }


    float Simulation::get_wheel_angular_velocity(int i)
    { return is_valid_wheel(i) ? wheels[i].angular_velocity : 0.0f; }


    float Simulation::get_wheel_rotation(int i)
    { return is_valid_wheel(i) ? wheels[i].rotation : 0.0f; }


    float Simulation::get_wheel_effective_radius(int i)
    { return is_valid_wheel(i) ? wheels[i].effective_radius : 0.0f; }


    float Simulation::get_wheel_dynamic_camber(int i)
    { return is_valid_wheel(i) ? wheels[i].dynamic_camber : 0.0f; }


    float Simulation::get_wheel_dynamic_toe(int i)
    { return is_valid_wheel(i) ? wheels[i].dynamic_toe : 0.0f; }


    float Simulation::get_wheel_bump_steer(int i)
    { return is_valid_wheel(i) ? wheels[i].bump_steer : 0.0f; }


    float Simulation::get_wheel_motion_ratio(int i)
    { return is_valid_wheel(i) ? wheels[i].motion_ratio : 0.0f; }


    float Simulation::get_wheel_temperature(int i)
    { return is_valid_wheel(i) ? wheels[i].thermal.avg_surface() : 0.0f; }


    bool Simulation::is_wheel_grounded(int i)
    { return is_valid_wheel(i) && wheels[i].grounded; }


    float Simulation::get_wheel_suspension_force(int i)
    {
            return is_valid_wheel(i) ? spring_force[i] : 0.0f;
    }


    float Simulation::get_axle_roll_stiffness(bool front)
    {
            int left = front ? front_left : rear_left;
            int right = front ? front_right : rear_right;
            float track = front ? cfg.track_front : cfg.track_rear;
            float anti_roll = front ? spec.front_arb_stiffness : spec.rear_arb_stiffness;
            float left_wheel_rate = spring_stiffness[left] * wheels[left].motion_ratio * wheels[left].motion_ratio;
            float right_wheel_rate = spring_stiffness[right] * wheels[right].motion_ratio * wheels[right].motion_ratio;
            return ((left_wheel_rate + right_wheel_rate) * 0.5f + anti_roll) * track * track * 0.5f;
    }


    float Simulation::get_wheel_temp_grip_factor(int i)
    {
            return is_valid_wheel(i) ? wheels[i].condition_grip : 1.0f;
    }


    float Simulation::get_wheel_surface_temp(int i, int zone)
    {
            return (is_valid_wheel(i) && zone >= 0 && zone < 3) ? wheels[i].thermal.surface[zone] : 0.0f;
    }


    float Simulation::get_wheel_core_temp(int i)
    {
            return is_valid_wheel(i) ? wheels[i].thermal.core : 0.0f;
    }


    float Simulation::get_tire_pressure()
    { return spec.tire_pressure; }


    float Simulation::get_tire_pressure_optimal()
    { return spec.tire_pressure_optimal; }


    float Simulation::get_chassis_visual_offset_y()
    {
            const float offset = 0.1f;
            return -(cfg.height * 0.5f + cfg.suspension_height) + offset;
    }


    void Simulation::set_abs_enabled(bool enabled)
    { spec.abs_enabled = enabled; }


    bool Simulation::get_abs_enabled()
    { return spec.abs_enabled; }


    bool Simulation::is_abs_active(int i)
    { return is_valid_wheel(i) && abs_active[i]; }


    bool Simulation::is_abs_active_any()
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


    float Simulation::get_abs_phase()
    { return abs_phase; }


    void Simulation::set_tc_enabled(bool enabled)
    { spec.tc_enabled = enabled; }


    bool Simulation::get_tc_enabled()
    { return spec.tc_enabled; }


    bool Simulation::is_tc_active()
    { return tc_active; }


    float Simulation::get_tc_reduction()
    { return tc_reduction; }


    bool Simulation::is_burnout_active()
    { return burnout_active; }


    void Simulation::set_manual_transmission(bool enabled)
    { spec.manual_transmission = enabled; }


    bool Simulation::get_manual_transmission()
    { return spec.manual_transmission; }


    void Simulation::begin_shift(int direction)
    {
            is_shifting = true;
            shift_timer = spec.shift_time;
            last_shift_direction = direction;
    }


    void Simulation::shift_up()
    {
            if (!spec.manual_transmission || is_shifting || current_gear >= spec.gear_count - 1)
            {
                return;
            }
            set_active_gear((current_gear == 0) ? 1 : current_gear + 1);
            begin_shift(1);
    }


    void Simulation::shift_down()
    {
            if (!spec.manual_transmission || is_shifting || current_gear <= 0)
            {
                return;
            }
            if (current_gear > 2)
            {
                downshift_blip_timer = spec.downshift_blip_duration;
            }
            set_active_gear((current_gear == 1) ? 0 : current_gear - 1);
            begin_shift(-1);
    }


    void Simulation::shift_to_neutral()
    {
            if (!spec.manual_transmission || is_shifting)
            {
                return;
            }
            set_active_gear(1);
            begin_shift(0);
    }


    int Simulation::get_current_gear()
    { return current_gear; }


    float Simulation::get_current_engine_rpm()
    { return engine_rpm; }


    bool Simulation::get_is_shifting()
    { return is_shifting; }


    float Simulation::get_clutch()
    { return clutch; }


    float Simulation::get_engine_torque_current()
    { return get_engine_torque(engine_rpm) * (1.0f + boost_pressure * spec.boost_torque_mult); }


    float Simulation::get_engine_output_torque()
    { return engine_output_torque; }


    float Simulation::get_motor_torque()
    { return motor_torque; }


    float Simulation::get_driveshaft_twist()
    { return driveshaft_twist; }


    float Simulation::get_driveshaft_torque()
    { return driveshaft_torque; }


    float Simulation::get_axle_drive_torque()
    { return axle_drive_torque; }


    float Simulation::get_longitudinal_accel()
    { return longitudinal_accel; }


    float Simulation::get_lateral_accel()
    { return lateral_accel; }


    bool Simulation::get_vehicle_sleeping()
    { return vehicle_sleeping; }


    float Simulation::get_gearbox_input_angular_velocity()
    { return gearbox_input_angular_velocity; }


    float Simulation::get_motor_power_kw()
    { float w = motor_torque * engine_rpm * (2.0f * 3.14159265f / 60.0f); return w / 1000.0f; }


    float Simulation::get_redline_rpm()
    { return spec.engine_redline_rpm; }


    float Simulation::get_max_rpm()
    { return spec.engine_max_rpm; }


    float Simulation::get_idle_rpm()
    { return spec.engine_idle_rpm; }


    void Simulation::set_turbo_enabled(bool enabled)
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


    bool Simulation::get_turbo_enabled()
    { return spec.turbo_enabled; }


    float Simulation::get_boost_pressure()
    { return boost_pressure; }


    float Simulation::get_boost_max_pressure()
    { return spec.boost_max_pressure; }


    void Simulation::set_drs_enabled(bool enabled)
    { spec.drs_enabled = enabled; }


    bool Simulation::get_drs_enabled()
    { return spec.drs_enabled; }


    void Simulation::set_drs_active(bool active)
    { drs_active = active; }


    bool Simulation::get_drs_active()
    { return drs_active; }


    void Simulation::set_diff_type(int type)
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


    int Simulation::get_diff_type()
    { return spec.diff_type; }


    const char* Simulation::get_diff_type_name()
    {
            static constexpr const char* names[] = { "Open", "Locked", "LSD" };
            return (spec.diff_type >= 0 && spec.diff_type <= 2) ? names[spec.diff_type] : "?";
    }


    float Simulation::get_wheel_wear(int i)
    { return is_valid_wheel(i) ? wheels[i].wear : 0.0f; }


    void Simulation::reset_tire_wear()
    {
            for (int i = 0; i < wheel_count; i++)
            {
                wheels[i].wear = 0.0f;
            }
    }


    float Simulation::get_wheel_wear_grip_factor(int i)
    { return is_valid_wheel(i) ? wheels[i].wear_grip : 1.0f; }


    float Simulation::get_wheel_brake_temp(int i)
    { return is_valid_wheel(i) ? wheels[i].brake_temp : 0.0f; }


    float Simulation::get_wheel_brake_efficiency(int i)
    { return is_valid_wheel(i) ? wheels[i].brake_efficiency : 1.0f; }


    void Simulation::set_wheel_surface(int i, surface_type surface)
    {
            if (is_valid_wheel(i))
            {
                wheels[i].contact_surface = surface;
            }
    }


    surface_type Simulation::get_wheel_surface(int i)
    { return is_valid_wheel(i) ? wheels[i].contact_surface : surface_asphalt; }


    const char* Simulation::get_surface_name(surface_type surface)
    {
            static constexpr const char* names[] = { "Asphalt", "Concrete", "Wet", "Gravel", "Grass", "Ice" };
            return (surface >= 0 && surface < surface_count) ? names[surface] : "Unknown";
    }


    float Simulation::get_front_camber()
    { return spec.front_camber; }


    float Simulation::get_rear_camber()
    { return spec.rear_camber; }


    float Simulation::get_front_toe()
    { return spec.front_toe; }


    float Simulation::get_rear_toe()
    { return spec.rear_toe; }


    void Simulation::set_wheel_offset(int wheel, float x, float z)
    {
            if (wheel >= 0 && wheel < wheel_count)
            {
                wheel_offsets[wheel].x = x;
                wheel_offsets[wheel].z = z;
                refresh_geometry_cache();
            }
    }


    PxVec3 Simulation::get_wheel_offset(int wheel)
    {
            if (wheel >= 0 && wheel < wheel_count)
            {
                return wheel_offsets[wheel];
            }
            return PxVec3(0);
    }


    const aero_debug_data& Simulation::get_aero_debug()
    { return aero_debug; }


    const shape_2d& Simulation::get_shape_data()
    { return shape_data_ref(); }


    void Simulation::get_debug_sweep(int wheel, PxVec3& origin, PxVec3& hit_point, bool& hit)
    {
            if (wheel >= 0 && wheel < wheel_count)
            {
                origin    = debug_sweep[wheel].origin;
                hit_point = debug_sweep[wheel].hit_point;
                hit       = debug_sweep[wheel].hit;
            }
    }


    int Simulation::get_debug_contact_rows(int wheel) const
    {
            return (wheel >= 0 && wheel < wheel_count) ? debug_sweep[wheel].row_count : 0;
    }


    void Simulation::get_debug_contact_row(int wheel, int row, PxVec3& point, PxVec3& normal, float& load) const
    {
            if (wheel >= 0 && wheel < wheel_count && row >= 0 && row < debug_sweep[wheel].row_count)
            {
                point  = debug_sweep[wheel].row_point[row];
                normal = debug_sweep[wheel].row_normal[row];
                load   = debug_sweep[wheel].row_load[row];
            }
    }


    void Simulation::get_debug_suspension(int wheel, PxVec3& top, PxVec3& bottom)
    {
            if (wheel >= 0 && wheel < wheel_count)
            {
                top    = debug_suspension_top[wheel];
                bottom = debug_suspension_bottom[wheel];
            }
    }


    float Simulation::get_wheel_radius()
    { return (cfg.front_wheel_radius + cfg.rear_wheel_radius) * 0.5f; }


    float Simulation::get_wheel_width()
    { return (cfg.front_wheel_width + cfg.rear_wheel_width) * 0.5f; }


    PxTransform Simulation::get_body_pose()
    { return body ? body->getGlobalPose() : PxTransform(PxIdentity); }
}
