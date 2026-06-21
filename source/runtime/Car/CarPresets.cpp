#include "pch.h"
#include "CarPresets.h"
#include "../FileSystem/FileSystem.h"
#include "../IO/pugixml.hpp"
#include <algorithm>
#include <sstream>
#include <utility>

using namespace std;

namespace car
{
    namespace
    {
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

            READ_FLOAT(engine_idle_rpm);
            READ_FLOAT(engine_redline_rpm);
            READ_FLOAT(engine_max_rpm);
            READ_FLOAT(engine_peak_torque);
            READ_FLOAT(engine_peak_torque_rpm);
            READ_FLOAT(engine_inertia);
            READ_FLOAT(engine_friction);
            READ_FLOAT(engine_rpm_smoothing);
            READ_FLOAT(downshift_blip_amount);
            READ_FLOAT(downshift_blip_duration);

            READ_FLOAT_ARRAY(gear_ratios);
            READ_INT(gear_count);
            READ_FLOAT(final_drive);
            READ_FLOAT(shift_up_rpm);
            READ_FLOAT(shift_down_rpm);
            READ_FLOAT(shift_time);
            READ_FLOAT(clutch_engagement_rate);
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
            READ_FLOAT(load_B_scale_min);
            READ_FLOAT(pneumatic_trail_max);
            READ_FLOAT(pneumatic_trail_peak);

            READ_FLOAT(tire_friction);
            READ_FLOAT(min_slip_speed);
            READ_FLOAT(load_sensitivity);
            READ_FLOAT(load_reference);
            READ_FLOAT(rear_grip_ratio);
            READ_FLOAT(slip_angle_deadband);
            READ_FLOAT(min_lateral_grip);
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
            READ_FLOAT(front_arb_stiffness);
            READ_FLOAT(rear_arb_stiffness);
            READ_FLOAT(max_susp_force);
            READ_FLOAT(max_damper_velocity);
            READ_FLOAT(bump_stop_stiffness);
            READ_FLOAT(bump_stop_threshold);
            READ_FLOAT(front_roll_center_height);
            READ_FLOAT(rear_roll_center_height);

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
            READ_FLOAT(high_speed_steer_reduction);
            READ_FLOAT(steering_rate);
            READ_FLOAT(self_align_gain);
            READ_FLOAT(steering_linearity);
            READ_FLOAT(front_camber);
            READ_FLOAT(rear_camber);
            READ_FLOAT(front_toe);
            READ_FLOAT(rear_toe);
            READ_FLOAT(front_bump_steer);
            READ_FLOAT(rear_bump_steer);

            READ_FLOAT(airborne_wheel_decay);
            READ_FLOAT(bearing_friction);
            READ_FLOAT(ground_match_rate);
            READ_FLOAT(handbrake_sliding_factor);
            READ_FLOAT(handbrake_torque);

            READ_INT(drivetrain_type);
            READ_FLOAT(torque_split_front);
            READ_FLOAT(lsd_preload);
            READ_FLOAT(lsd_lock_ratio_accel);
            READ_FLOAT(lsd_lock_ratio_decel);
            READ_INT(diff_type);
            READ_FLOAT(driveshaft_stiffness);

            READ_FLOAT(input_deadzone);
            READ_FLOAT(steering_deadzone);
            READ_FLOAT(braking_speed_threshold);
            READ_FLOAT(linear_damping);
            READ_FLOAT(angular_damping);
            READ_FLOAT(yaw_damping);

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

            #undef READ_FLOAT
            #undef READ_INT
            #undef READ_BOOL
            #undef READ_FLOAT_ARRAY
        }
    }

    bool load_presets_from_xml(const char* file_path)
    {
        if (!file_path || file_path[0] == '\0')
        {
            return false;
        }

        if (!spartan::FileSystem::Exists(file_path))
        {
            SP_LOG_WARNING("car preset file not found: %s", file_path);
            return false;
        }

        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_file(file_path);
        if (!result)
        {
            SP_LOG_ERROR("failed to parse car preset file: %s, %s", file_path, result.description());
            return false;
        }

        pugi::xml_node root = doc.child("car_presets");
        if (!root)
        {
            root = doc.child("CarPresets");
        }

        if (!root)
        {
            SP_LOG_ERROR("car preset file missing car_presets root: %s", file_path);
            return false;
        }

        vector<car_preset> loaded_presets;
        vector<string> loaded_names;

        for (pugi::xml_node node = root.child("preset"); node; node = node.next_sibling("preset"))
        {
            string name = node.attribute("name").as_string();
            if (name.empty())
            {
                SP_LOG_WARNING("skipping unnamed car preset in: %s", file_path);
                continue;
            }

            car_preset preset;
            load_preset(node, preset);
            loaded_names.push_back(name);
            loaded_presets.push_back(preset);
        }

        if (loaded_presets.empty())
        {
            SP_LOG_ERROR("car preset file has no presets: %s", file_path);
            return false;
        }

        external_presets     = std::move(loaded_presets);
        external_preset_names = std::move(loaded_names);
        preset_registry.clear();
        preset_registry.reserve(external_presets.size());

        for (size_t i = 0; i < external_presets.size(); i++)
        {
            external_presets[i].name = external_preset_names[i].c_str();
            preset_registry.push_back({ external_presets[i].name, &external_presets[i] });
        }

        preset_count        = static_cast<int>(preset_registry.size());
        active_preset_index = std::clamp(active_preset_index, 0, preset_count - 1);

        SP_LOG_INFO("loaded %d car presets from: %s", preset_count, file_path);
        return true;
    }
}
