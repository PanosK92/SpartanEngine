/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "Environment.h"
#include "../io/pugixml.hpp"
using namespace spartan::math;
namespace spartan
{
    void Environment::Save(pugi::xml_node& environment_node)
    {
        const auto& environment = GetSettings();
        environment_node.append_attribute("utc_days") = environment.utc_days;
        environment_node.append_attribute("latitude") = environment.latitude;
        environment_node.append_attribute("longitude") = environment.longitude;
        environment_node.append_attribute("elevation") = environment.elevation;
        environment_node.append_attribute("time_scale") = environment.time_scale;
        environment_node.append_attribute("north_degrees") = environment.north_degrees;
        environment_node.append_attribute("annual_temperature") = environment.annual_temperature;
        environment_node.append_attribute("seasonal_amplitude") = environment.seasonal_amplitude;
        environment_node.append_attribute("daily_amplitude") = environment.daily_amplitude;
        environment_node.append_attribute("sea_level_pressure") = environment.sea_level_pressure;
        environment_node.append_attribute("wind_x") = GetWind().x;
        environment_node.append_attribute("wind_y") = GetWind().y;
        environment_node.append_attribute("wind_z") = GetWind().z;
        environment_node.append_attribute("puddliness") = GetPuddliness();
        environment_node.append_attribute("rain") = environment.rain;
        environment_node.append_attribute("cloud_coverage") = environment.cloud_coverage;
    }
    void Environment::Load(pugi::xml_node& world_node)
    {
        EnvironmentSettings environment;
        auto environment_node = world_node.child("Environment");
        environment.utc_days = environment_node.attribute("utc_days").as_double(environment.utc_days);
        environment.latitude = environment_node.attribute("latitude").as_double(environment.latitude);
        environment.longitude = environment_node.attribute("longitude").as_double(environment.longitude);
        environment.elevation = environment_node.attribute("elevation").as_double(environment.elevation);
        environment.time_scale = environment_node.attribute("time_scale").as_double(environment.time_scale);
        environment.north_degrees = environment_node.attribute("north_degrees").as_float(environment.north_degrees);
        environment.annual_temperature = environment_node.attribute("annual_temperature").as_float(environment.annual_temperature);
        environment.seasonal_amplitude = environment_node.attribute("seasonal_amplitude").as_float(environment.seasonal_amplitude);
        environment.daily_amplitude = environment_node.attribute("daily_amplitude").as_float(environment.daily_amplitude);
        environment.sea_level_pressure = environment_node.attribute("sea_level_pressure").as_float(environment.sea_level_pressure);
        // Older files stored world weather on the first directional light.
        pugi::xml_node legacy;
        const auto find_light = [&](const auto& self, pugi::xml_node node) -> void
        {
            for (auto child : node.children())
            {
                if (std::string_view(child.name()) == "light" && child.attribute("light_type").as_int(1) == 0) { legacy = child; return; }
                self(self, child);
                if (legacy) return;
            }
        };
        if (!environment_node.attribute("rain") || !environment_node.attribute("cloud_coverage")) find_light(find_light, world_node);
        environment.rain = environment_node.attribute("rain").as_float(legacy.attribute("rain").as_float(0.0f));
        environment.cloud_coverage = environment_node.attribute("cloud_coverage").as_float(legacy.attribute("cloud_coverage").as_float(environment.cloud_coverage));
        SetSettings(environment);
        // Older worlds have no saved wind. Restore the default instead of
        // flattening their FFT ocean or inheriting the previous world's wind.
        const Vector3 default_wind = GetWind();
        SetWind(Vector3(
            environment_node.attribute("wind_x").as_float(default_wind.x),
            environment_node.attribute("wind_y").as_float(default_wind.y),
            environment_node.attribute("wind_z").as_float(default_wind.z)
        ));
        SetPuddliness(environment_node.attribute("puddliness").as_float(0.0f));
    }
}
