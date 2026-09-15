// Earth environment shared by world lighting and vehicle boundary conditions.
#pragma once
#include "../math/Vector3.h"

namespace spartan
{
    struct EnvironmentSettings
    {
        double utc_days = 9665.25; // days since J2000 noon; 2026-06-18 18:00 UTC
        double latitude = 37.78, longitude = 20.90, elevation = 0; // degrees north/east, metres
        double time_scale = 200;
        float north_degrees = 0; // world +Z is north, +X east at zero
        float annual_temperature = 18, seasonal_amplitude = 9, daily_amplitude = 4;
        float sea_level_pressure = 101325; // Pa
    };

    struct EnvironmentState
    {
        math::Vector3 sun, moon;
        math::Vector3 equatorial_x, equatorial_y, equatorial_z;
        float moon_fraction = 0, moon_radius = 0.0045f, sun_radius = 0.00465f;
        float air_temperature = 20, road_temperature = 20, air_density = 1.225f, pressure = 101325;
    };

    enum class SolarEvent { Dawn, Noon, Dusk, Midnight, GoldenHour };

    class Environment
    {
    public:
        static EnvironmentSettings GetSettings();
        static void SetSettings(EnvironmentSettings settings);
        static bool SetDate(int year, int month, int day, int hour, int minute, double second);
        static void GetDate(int& year, int& month, int& day, int& hour, int& minute, double& second, bool real_time = false);
        static double GetDays(bool real_time = false);
        static float GetTimeOfDay(bool real_time = false); // UTC
        static void SetTimeOfDay(float fraction);
        static void Tick(double seconds);
        static bool SetSolarEvent(SolarEvent event);
        static EnvironmentState Evaluate(bool real_time, float clouds, float wind_speed);
    };
}
