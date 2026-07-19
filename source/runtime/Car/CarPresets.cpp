#include "pch.h"
#include "CarPresets.h"
#include "../FileSystem/FileSystem.h"
#include "../IO/pugixml.hpp"
#include <algorithm>
#include <sstream>
#include <utility>
#include <mutex>

using namespace std;

namespace car
{
    namespace
    {
        // world loading creates car prefabs from multiple threads, definitions and
        // preset_registry are shared globals so their mutation must be serialized
        mutex load_mutex;

        void read_float(pugi::xml_node node, const char* name, float& value)
        {
            if (pugi::xml_attribute attribute = node.attribute(name))
            {
                value = attribute.as_float(value);
            }
        }

        void read_int(pugi::xml_node node, const char* name, int& value)
        {
            if (pugi::xml_attribute attribute = node.attribute(name))
            {
                value = attribute.as_int(value);
            }
        }

        void read_bool(pugi::xml_node node, const char* name, bool& value)
        {
            if (pugi::xml_attribute attribute = node.attribute(name))
            {
                value = attribute.as_bool(value);
            }
        }

        void read_float_array(pugi::xml_node node, const char* name, float* values, int count)
        {
            pugi::xml_attribute attribute = node.attribute(name);
            if (!attribute)
            {
                return;
            }

            string data = attribute.as_string();
            for (char& character : data)
            {
                if (character == ',')
                {
                    character = ' ';
                }
            }

            stringstream stream(data);
            for (int i = 0; i < count; i++)
            {
                float value = 0.0f;
                if (!(stream >> value))
                {
                    break;
                }

                values[i] = value;
            }
        }

        suspension_mechanism parse_suspension_mechanism(const char* value)
        {
            const string mechanism = value ? value : "";
            if (mechanism == "macpherson")
            {
                return suspension_mechanism::macpherson;
            }
            if (mechanism == "multi_link")
            {
                return suspension_mechanism::multi_link;
            }
            return suspension_mechanism::double_wishbone;
        }

        void load_suspension_geometry(pugi::xml_node node, suspension_geometry& geometry)
        {
            if (!node)
            {
                return;
            }

            geometry.mechanism = parse_suspension_mechanism(node.attribute("mechanism").as_string("double_wishbone"));
            read_float(node, "chassis_inset", geometry.chassis_inset);
            read_float(node, "upper_inner_y", geometry.upper_inner_y);
            read_float(node, "lower_inner_y", geometry.lower_inner_y);
            read_float(node, "upper_upright_y", geometry.upper_upright_y);
            read_float(node, "lower_upright_y", geometry.lower_upright_y);
            read_float(node, "arm_span", geometry.arm_span);
            read_float(node, "strut_top_y", geometry.strut_top_y);
            read_float(node, "strut_top_inset", geometry.strut_top_inset);
            read_float(node, "tie_rod_y", geometry.tie_rod_y);
            read_float(node, "tie_rod_z", geometry.tie_rod_z);
            read_float(node, "link_spread_y", geometry.link_spread_y);
            read_float(node, "link_spread_z", geometry.link_spread_z);
        }

        void load_assists(pugi::xml_node node, assist_settings& assists)
        {
            if (!node)
            {
                return;
            }
            read_float(node, "steering_speed_reduction", assists.steering_speed_reduction);
            read_float(node, "steering_speed_reference", assists.steering_speed_reference);
            read_float(node, "abs_level", assists.abs_level);
            read_float(node, "traction_control_level", assists.traction_control_level);
        }

        void load_validation_targets(pugi::xml_node node, validation_targets& validation)
        {
            if (!node)
            {
                return;
            }
            read_float(node, "settle_speed_max", validation.settle_speed_max);
            read_float(node, "zero_to_100_min", validation.zero_to_100_min);
            read_float(node, "zero_to_100_max", validation.zero_to_100_max);
            read_float(node, "braking_distance_min", validation.braking_distance_min);
            read_float(node, "braking_distance_max", validation.braking_distance_max);
            read_float(node, "skidpad_g_min", validation.skidpad_g_min);
            read_float(node, "skidpad_g_max", validation.skidpad_g_max);
        }

        bool validate_preset(car_preset& preset, const string& name)
        {
            auto finite_range = [](float value, float minimum, float maximum) { return std::isfinite(value) && value >= minimum && value <= maximum; };
            bool valid = true;
            auto require = [&](bool condition, const char* field)
            {
                if (!condition)
                {
                    SP_LOG_ERROR("car preset %s has invalid %s", name.c_str(), field);
                    valid = false;
                }
            };

            require(finite_range(preset.mass, 200.0f, 5000.0f), "mass");
            require(finite_range(preset.length, 2.0f, 8.0f), "length");
            require(finite_range(preset.width, 1.0f, 3.0f), "width");
            require(finite_range(preset.height, 0.7f, 3.0f), "height");
            require(finite_range(preset.wheelbase, 1.5f, 5.0f), "wheelbase");
            require(finite_range(preset.track_front, 1.0f, 2.5f) && finite_range(preset.track_rear, 1.0f, 2.5f), "track");
            require(finite_range(preset.front_wheel_radius, 0.2f, 0.7f) && finite_range(preset.rear_wheel_radius, 0.2f, 0.7f), "wheel_radius");
            require(finite_range(preset.front_wheel_width, 0.1f, 0.6f) && finite_range(preset.rear_wheel_width, 0.1f, 0.6f), "wheel_width");
            require(finite_range(preset.suspension_height, 0.05f, 1.5f), "suspension_height");
            require(finite_range(preset.suspension_travel, 0.04f, 0.7f), "suspension_travel");
            require(finite_range(preset.front_spring_freq, 0.5f, 5.0f) && finite_range(preset.rear_spring_freq, 0.5f, 5.0f), "spring_frequency");
            require(finite_range(preset.front_damping_ratio, 0.1f, 2.0f) && finite_range(preset.rear_damping_ratio, 0.1f, 2.0f), "damping_ratio");
            require(finite_range(preset.damping_bump_ratio, 0.05f, 3.0f) && finite_range(preset.damping_rebound_ratio, 0.05f, 3.0f), "low_speed_damping");
            require(finite_range(preset.damping_bump_high_speed_ratio, 0.05f, 3.0f) && finite_range(preset.damping_rebound_high_speed_ratio, 0.05f, 3.0f), "high_speed_damping");
            require(finite_range(preset.damper_knee_velocity, 0.01f, 10.0f), "damper_knee_velocity");
            require(finite_range(preset.max_steer_angle, 0.1f, 1.2f), "max_steer_angle");
            require(finite_range(preset.tire_friction, 0.3f, 2.5f), "tire_friction");
            require(preset.gear_count >= 3 && preset.gear_count <= max_gears, "gear_count");
            require(preset.drivetrain_type >= 0 && preset.drivetrain_type <= 2, "drivetrain_type");
            require(preset.diff_type >= 0 && preset.diff_type <= 2, "diff_type");
            require(finite_range(preset.engine_idle_rpm, 300.0f, 2000.0f) && finite_range(preset.engine_redline_rpm, preset.engine_idle_rpm + 500.0f, 20000.0f) && finite_range(preset.engine_max_rpm, preset.engine_redline_rpm, 22000.0f), "engine_speed_range");
            require(finite_range(preset.engine_peak_torque, 10.0f, 5000.0f) && finite_range(preset.engine_inertia, 0.01f, 10.0f) && finite_range(preset.engine_friction, 0.0f, 10.0f) && finite_range(preset.engine_rpm_smoothing, 0.1f, 100.0f), "engine_response");
            float crank_axis_length = sqrtf(preset.engine_crank_axis_x * preset.engine_crank_axis_x + preset.engine_crank_axis_y * preset.engine_crank_axis_y + preset.engine_crank_axis_z * preset.engine_crank_axis_z);
            require(finite_range(crank_axis_length, 0.99f, 1.01f), "engine_crank_axis");
            require(finite_range(preset.final_drive, 0.1f, 10.0f) && finite_range(preset.clutch_engagement_rate, 0.1f, 100.0f) && finite_range(preset.clutch_max_torque, 10.0f, 10000.0f) && finite_range(preset.driveline_inertia, 0.001f, 10.0f) && finite_range(preset.drivetrain_efficiency, 0.1f, 1.0f) && finite_range(preset.driveshaft_stiffness, 1.0f, 1000000.0f) && finite_range(preset.driveshaft_damping, 0.0f, 10000.0f), "drivetrain");
            require(std::isfinite(preset.gear_ratios[1]) && fabsf(preset.gear_ratios[1]) <= 1e-6f, "neutral_gear_ratio");
            for (int i = 0; i < std::clamp(preset.gear_count, 0, max_gears); i++)
            {
                if (i != 1)
                {
                    require(finite_range(fabsf(preset.gear_ratios[i]), 0.1f, 10.0f), "gear_ratio");
                }
            }
            require(finite_range(preset.brake_thermal_mass, 0.1f, 100.0f), "brake_thermal_mass");
            require(finite_range(preset.brake_ambient_temp, 0.0f, 100.0f) && finite_range(preset.brake_optimal_temp, preset.brake_ambient_temp, 1000.0f) && finite_range(preset.brake_fade_temp, preset.brake_optimal_temp, 1500.0f) && finite_range(preset.brake_max_temp, preset.brake_fade_temp, 2000.0f), "brake_temperature_range");
            require(finite_range(preset.load_reference, 100.0f, 20000.0f), "load_reference");
            require(finite_range(preset.tire_temp_range, 1.0f, 300.0f), "tire_temp_range");
            require(finite_range(preset.tire_vertical_stiffness, 10000.0f, 1000000.0f), "tire_vertical_stiffness");
            require(finite_range(preset.tire_relaxation_length, 0.01f, 5.0f), "tire_relaxation_length");
            require(finite_range(preset.tire_pressure, 0.5f, 6.0f) && finite_range(preset.tire_pressure_optimal, 0.5f, 6.0f), "tire_pressure");
            require(finite_range(preset.lat_B, 0.1f, 50.0f) && finite_range(preset.lat_C, 0.1f, 5.0f) && finite_range(preset.lat_D, 0.1f, 3.0f), "lateral_tire_coefficients");
            require(finite_range(preset.long_B, 0.1f, 50.0f) && finite_range(preset.long_C, 0.1f, 5.0f) && finite_range(preset.long_D, 0.1f, 3.0f), "longitudinal_tire_coefficients");
            require(finite_range(preset.combined_long_B, 0.1f, 50.0f) && finite_range(preset.combined_long_C, 0.1f, 5.0f) && finite_range(preset.combined_long_E, -2.0f, 2.0f), "combined_longitudinal_tire_coefficients");
            require(finite_range(preset.combined_lat_B, 0.1f, 50.0f) && finite_range(preset.combined_lat_C, 0.1f, 5.0f) && finite_range(preset.combined_lat_E, -2.0f, 2.0f), "combined_lateral_tire_coefficients");
            require(finite_range(preset.max_susp_force, 1000.0f, 500000.0f) && finite_range(preset.max_damper_velocity, 0.1f, 50.0f), "suspension_force_limits");
            require(finite_range(preset.bump_stop_stiffness, 1000.0f, 5000000.0f) && finite_range(preset.bump_stop_threshold, 0.5f, 1.2f), "bump_stop");
            require(finite_range(preset.bump_stop_progression, 0.0f, 20.0f), "bump_stop_progression");
            require(finite_range(preset.packer_threshold, preset.bump_stop_threshold, 1.5f) && finite_range(preset.packer_stiffness, 10000.0f, 10000000.0f), "packer");
            require(finite_range(preset.steering_rate, 0.1f, 20.0f) && finite_range(preset.steering_linearity, 0.1f, 5.0f), "steering_response");
            require(finite_range(preset.steering_deadzone, 0.0f, 0.5f) && finite_range(preset.input_deadzone, 0.0f, 0.5f), "input_deadzones");
            require(finite_range(preset.center_of_mass_x, -preset.width * 0.5f, preset.width * 0.5f), "center_of_mass_x");
            require(finite_range(preset.center_of_mass_y, -preset.height, preset.height), "center_of_mass_y");
            require(finite_range(preset.center_of_mass_z, -preset.length * 0.5f, preset.length * 0.5f), "center_of_mass_z");
            auto validate_geometry = [&](const suspension_geometry& geometry, const char* field)
            {
                bool geometry_valid = finite_range(geometry.chassis_inset, 0.1f, 0.9f) && finite_range(geometry.arm_span, 0.05f, 0.6f) && finite_range(geometry.strut_top_y, 0.1f, 1.2f) && finite_range(geometry.strut_top_inset, 0.05f, 0.8f) && geometry.upper_upright_y > geometry.lower_upright_y && geometry.upper_inner_y > geometry.lower_inner_y;
                require(geometry_valid, field);
            };
            validate_geometry(preset.front_geometry, "front_suspension_geometry");
            validate_geometry(preset.rear_geometry, "rear_suspension_geometry");

            require(finite_range(preset.assists.steering_speed_reduction, 0.0f, 0.9f), "steering_speed_reduction");
            require(finite_range(preset.assists.steering_speed_reference, 5.0f, 100.0f), "steering_speed_reference");
            require(finite_range(preset.assists.abs_level, 0.0f, 1.0f), "abs_level");
            require(finite_range(preset.assists.traction_control_level, 0.0f, 1.0f), "traction_control_level");
            require(finite_range(preset.validation.settle_speed_max, 0.001f, 1.0f), "settle_speed_max");
            require(finite_range(preset.validation.zero_to_100_min, 0.5f, 30.0f) && finite_range(preset.validation.zero_to_100_max, 0.5f, 30.0f) && preset.validation.zero_to_100_min < preset.validation.zero_to_100_max, "zero_to_100_range");
            require(finite_range(preset.validation.braking_distance_min, 5.0f, 200.0f) && finite_range(preset.validation.braking_distance_max, 5.0f, 200.0f) && preset.validation.braking_distance_min < preset.validation.braking_distance_max, "braking_distance_range");
            require(finite_range(preset.validation.skidpad_g_min, 0.1f, 3.0f) && finite_range(preset.validation.skidpad_g_max, 0.1f, 3.0f) && preset.validation.skidpad_g_min < preset.validation.skidpad_g_max, "skidpad_range");
            return valid;
        }

        void load_preset(pugi::xml_node node, car_preset& preset)
        {
            #define READ_FLOAT(member) read_float(node, #member, preset.member)
            #define READ_INT(member) read_int(node, #member, preset.member)
            #define READ_BOOL(member) read_bool(node, #member, preset.member)
            #define READ_FLOAT_ARRAY(member) read_float_array(node, #member, preset.member, max_gears)

            READ_FLOAT(mass);
            READ_FLOAT(length);
            READ_FLOAT(width);
            READ_FLOAT(height);
            READ_FLOAT(wheelbase);
            READ_FLOAT(track_front);
            READ_FLOAT(track_rear);
            READ_FLOAT(suspension_height);
            READ_FLOAT(suspension_travel);
            READ_FLOAT(front_wheel_radius);
            READ_FLOAT(rear_wheel_radius);
            READ_FLOAT(front_wheel_width);
            READ_FLOAT(rear_wheel_width);
            READ_FLOAT(wheel_mass);
            READ_FLOAT(upright_mass);
            READ_FLOAT(suspension_link_mass);
            READ_FLOAT(steering_rack_mass);

            READ_FLOAT(engine_idle_rpm);
            READ_FLOAT(engine_redline_rpm);
            READ_FLOAT(engine_max_rpm);
            READ_FLOAT(engine_peak_torque);
            READ_FLOAT(engine_peak_torque_rpm);
            READ_FLOAT(engine_inertia);
            READ_FLOAT(engine_friction);
            READ_FLOAT(engine_rpm_smoothing);
            READ_FLOAT(engine_crank_axis_x);
            READ_FLOAT(engine_crank_axis_y);
            READ_FLOAT(engine_crank_axis_z);
            READ_FLOAT(downshift_blip_amount);
            READ_FLOAT(downshift_blip_duration);

            READ_FLOAT_ARRAY(gear_ratios);
            READ_INT(gear_count);
            READ_FLOAT(final_drive);
            READ_FLOAT(shift_up_rpm);
            READ_FLOAT(shift_down_rpm);
            READ_FLOAT(shift_time);
            READ_FLOAT(clutch_engagement_rate);
            READ_FLOAT(clutch_max_torque);
            READ_FLOAT(driveline_inertia);
            READ_FLOAT(drivetrain_efficiency);
            READ_BOOL(manual_transmission);
            READ_FLOAT_ARRAY(upshift_speed_base);
            READ_FLOAT_ARRAY(upshift_speed_sport);
            READ_FLOAT_ARRAY(downshift_speeds);

            READ_FLOAT(brake_force);
            READ_FLOAT(brake_bias_front);
            READ_FLOAT(reverse_power_ratio);
            READ_FLOAT(brake_ambient_temp);
            READ_FLOAT(brake_optimal_temp);
            READ_FLOAT(brake_fade_temp);
            READ_FLOAT(brake_max_temp);
            READ_FLOAT(brake_heat_coefficient);
            READ_FLOAT(brake_cooling_base);
            READ_FLOAT(brake_cooling_airflow);
            READ_FLOAT(brake_thermal_mass);

            READ_FLOAT(throttle_smoothing);
            READ_FLOAT(brake_smoothing);

            READ_FLOAT(lat_B);
            READ_FLOAT(lat_C);
            READ_FLOAT(lat_D);
            READ_FLOAT(lat_E);
            READ_FLOAT(long_B);
            READ_FLOAT(long_C);
            READ_FLOAT(long_D);
            READ_FLOAT(long_E);
            READ_FLOAT(combined_long_B);
            READ_FLOAT(combined_long_C);
            READ_FLOAT(combined_long_E);
            READ_FLOAT(combined_lat_B);
            READ_FLOAT(combined_lat_C);
            READ_FLOAT(combined_lat_E);
            READ_FLOAT(load_B_scale_min);
            READ_FLOAT(pneumatic_trail_max);
            READ_FLOAT(pneumatic_trail_peak);

            READ_FLOAT(tire_friction);
            READ_FLOAT(min_slip_speed);
            READ_FLOAT(load_sensitivity);
            READ_FLOAT(load_reference);
            READ_FLOAT(rear_grip_ratio);
            READ_FLOAT(slip_angle_deadband);
            READ_FLOAT(camber_thrust_coeff);
            READ_FLOAT(max_slip_angle);
            READ_FLOAT(tire_pressure);
            READ_FLOAT(tire_pressure_optimal);

            READ_FLOAT(tire_ambient_temp);
            READ_FLOAT(tire_optimal_temp);
            READ_FLOAT(tire_temp_range);
            READ_FLOAT(tire_heat_from_slip);
            READ_FLOAT(tire_heat_from_rolling);
            READ_FLOAT(tire_cooling_rate);
            READ_FLOAT(tire_cooling_airflow);
            READ_FLOAT(tire_grip_temp_factor);
            READ_FLOAT(tire_min_temp);
            READ_FLOAT(tire_max_temp);
            READ_FLOAT(tire_relaxation_length);
            READ_FLOAT(tire_wear_rate);
            READ_FLOAT(tire_wear_heat_mult);
            READ_FLOAT(tire_grip_wear_loss);
            READ_FLOAT(tire_core_transfer_rate);
            READ_FLOAT(tire_surface_response);

            READ_FLOAT(front_spring_freq);
            READ_FLOAT(rear_spring_freq);
            READ_FLOAT(front_damping_ratio);
            READ_FLOAT(rear_damping_ratio);
            READ_FLOAT(damping_bump_ratio);
            READ_FLOAT(damping_rebound_ratio);
            READ_FLOAT(damping_bump_high_speed_ratio);
            READ_FLOAT(damping_rebound_high_speed_ratio);
            READ_FLOAT(damper_knee_velocity);
            READ_FLOAT(front_arb_stiffness);
            READ_FLOAT(rear_arb_stiffness);
            READ_FLOAT(max_susp_force);
            READ_FLOAT(max_damper_velocity);
            READ_FLOAT(bump_stop_stiffness);
            READ_FLOAT(bump_stop_threshold);
            READ_FLOAT(bump_stop_progression);
            READ_FLOAT(packer_threshold);
            READ_FLOAT(packer_stiffness);

            READ_FLOAT(rolling_resistance);
            READ_FLOAT(drag_coeff);
            READ_FLOAT(frontal_area);
            READ_FLOAT(lift_coeff_front);
            READ_FLOAT(lift_coeff_rear);
            READ_BOOL(drs_enabled);
            READ_FLOAT(drs_rear_cl_factor);
            READ_FLOAT(side_area);
            READ_BOOL(ground_effect_enabled);
            READ_FLOAT(ground_effect_multiplier);
            READ_FLOAT(ground_effect_height_ref);
            READ_FLOAT(ground_effect_height_max);
            READ_BOOL(yaw_aero_enabled);
            READ_FLOAT(yaw_drag_multiplier);
            READ_FLOAT(yaw_side_force_coeff);
            READ_BOOL(pitch_aero_enabled);
            READ_FLOAT(pitch_sensitivity);
            READ_FLOAT(aero_center_height);
            READ_FLOAT(aero_center_front_z);
            READ_FLOAT(aero_center_rear_z);

            READ_FLOAT(center_of_mass_x);
            READ_FLOAT(center_of_mass_y);
            READ_FLOAT(center_of_mass_z);

            READ_FLOAT(max_steer_angle);
            READ_FLOAT(steering_rate);
            READ_FLOAT(self_align_gain);
            READ_FLOAT(steering_linearity);
            READ_FLOAT(front_camber);
            READ_FLOAT(rear_camber);
            READ_FLOAT(front_toe);
            READ_FLOAT(rear_toe);
            READ_FLOAT(tire_vertical_stiffness);
            READ_FLOAT(lsd_viscous);
            READ_FLOAT(abs_load_sensitivity);

            READ_FLOAT(airborne_wheel_decay);
            READ_FLOAT(bearing_friction);
            READ_FLOAT(handbrake_torque);

            READ_INT(drivetrain_type);
            READ_FLOAT(torque_split_front);
            READ_FLOAT(lsd_preload);
            READ_FLOAT(lsd_lock_ratio_accel);
            READ_FLOAT(lsd_lock_ratio_decel);
            READ_INT(diff_type);
            READ_INT(engine_stage_max);
            READ_INT(suspension_stage_max);
            READ_INT(tires_stage_max);
            READ_INT(brakes_stage_max);
            READ_INT(aero_stage_max);
            READ_INT(weight_stage_max);
            READ_FLOAT(driveshaft_stiffness);
            READ_FLOAT(driveshaft_damping);

            READ_FLOAT(input_deadzone);
            READ_FLOAT(steering_deadzone);
            READ_FLOAT(braking_speed_threshold);
            READ_FLOAT(linear_damping);
            READ_FLOAT(angular_damping);

            READ_BOOL(abs_enabled);
            READ_FLOAT(abs_slip_threshold);
            READ_FLOAT(abs_release_rate);
            READ_FLOAT(abs_pulse_frequency);
            READ_BOOL(tc_enabled);
            READ_FLOAT(tc_slip_threshold);
            READ_FLOAT(tc_power_reduction);
            READ_FLOAT(tc_response_rate);

            READ_BOOL(turbo_enabled);
            READ_FLOAT(boost_max_pressure);
            READ_FLOAT(boost_spool_rate);
            READ_FLOAT(boost_wastegate_rpm);
            READ_FLOAT(boost_torque_mult);
            READ_FLOAT(boost_min_rpm);

            READ_BOOL(electric_enabled);
            READ_FLOAT(electric_motor_torque);
            READ_FLOAT(electric_motor_power_kw);
            READ_FLOAT(electric_motor_max_rpm);
            READ_FLOAT(electric_torque_response);

            #undef READ_FLOAT
            #undef READ_INT
            #undef READ_BOOL
            #undef READ_FLOAT_ARRAY
        }
    }

    namespace
    {
        string normalize_path(const string& path)
        {
            string result = path;
            replace(result.begin(), result.end(), '\\', '/');
            return result;
        }

        void split_csv(const string& value, vector<string>& out)
        {
            stringstream stream(value);
            string token;
            while (getline(stream, token, ','))
            {
                const size_t first = token.find_first_not_of(' ');
                if (first == string::npos)
                {
                    continue;
                }
                const size_t last = token.find_last_not_of(' ');
                out.push_back(token.substr(first, last - first + 1));
            }
        }
    }

    const car_definition* load_car_file(const string& file_path)
    {
        lock_guard<mutex> lock(load_mutex);

        const string path = normalize_path(file_path);

        // cached by path
        for (const car_definition& definition : definitions)
        {
            if (definition.file_path == path)
            {
                return &definition;
            }
        }

        if (!spartan::FileSystem::Exists(path))
        {
            SP_LOG_WARNING("car file not found: %s", path.c_str());
            return nullptr;
        }

        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_file(path.c_str());
        if (!result)
        {
            SP_LOG_ERROR("failed to parse car file: %s, %s", path.c_str(), result.description());
            return nullptr;
        }

        pugi::xml_node root = doc.child("car");
        if (!root)
        {
            SP_LOG_ERROR("car file missing car root node: %s", path.c_str());
            return nullptr;
        }

        car_definition definition;
        definition.file_path = path;
        definition.name      = root.attribute("name").as_string("unnamed car");

        if (pugi::xml_node body = root.child("body"))
        {
            definition.body_model     = body.attribute("model").as_string("");
            definition.body_scale     = body.attribute("scale").as_float(1.0f);
            definition.body_forward_z = body.attribute("forward_z").as_float(1.0f);
            split_csv(body.attribute("hide_parts").as_string(""), definition.body_hide_parts);
        }

        if (pugi::xml_node wheels = root.child("wheels"))
        {
            definition.wheel_model     = wheels.attribute("model").as_string("");
            definition.wheel_albedo    = wheels.attribute("albedo").as_string("");
            definition.wheel_metalness = wheels.attribute("metalness").as_string("");
            definition.wheel_normal    = wheels.attribute("normal").as_string("");
            definition.wheel_roughness = wheels.attribute("roughness").as_string("");
            load_preset(wheels, definition.performance);
        }

        // thematic sections are free form and later attributes override earlier ones
        for (pugi::xml_node section : root.children())
        {
            const string section_name = section.name();
            if (section_name == "body" || section_name == "wheels")
            {
                continue;
            }
            load_preset(section, definition.performance);
        }

        load_suspension_geometry(root.child("front_suspension"), definition.performance.front_geometry);
        load_suspension_geometry(root.child("rear_suspension"), definition.performance.rear_geometry);
        load_assists(root.child("assists"), definition.performance.assists);
        load_validation_targets(root.child("validation"), definition.performance.validation);
        if (!validate_preset(definition.performance, definition.name))
        {
            return nullptr;
        }

        definitions.push_back(std::move(definition));
        car_definition& stored = definitions.back();

        // register for the hud preset selector
        stored.performance.name = stored.name.c_str();
        preset_registry.push_back({ stored.name.c_str(), &stored.performance, &stored });

        SP_LOG_INFO("loaded car: %s (%s)", stored.name.c_str(), path.c_str());
        return &stored;
    }

    void load_car_directory(const string& directory)
    {
        if (!spartan::FileSystem::IsDirectory(directory))
        {
            return;
        }

        for (const string& file : spartan::FileSystem::GetFilesInDirectory(directory))
        {
            if (spartan::FileSystem::GetExtensionFromFilePath(file) == ".car")
            {
                load_car_file(file);
            }
        }
    }
}
