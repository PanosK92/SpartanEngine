#include "pch.h"
#include "Environment.h"
#include "../../third_party/astronomy/astronomy.h"
#include <chrono>
#include <algorithm>
#include <cmath>
#include <mutex>

namespace spartan
{
    namespace
    {
        std::recursive_mutex environment_mutex;
        EnvironmentSettings settings;
        EnvironmentState state;
        uint64_t revision = 0, evaluated_revision = ~uint64_t(0);
        double evaluated_days = -1e20;
        float evaluated_clouds = -1, evaluated_wind = -1;
        constexpr double radians = 0.017453292519943295;
        math::Vector3 horizontal(double azimuth, double altitude)
        {
            const double a = (azimuth + settings.north_degrees) * radians, h = altitude * radians;
            return math::Vector3(float(sin(a)*cos(h)), float(sin(h)), float(cos(a)*cos(h)));
        }
        double finite(double value, double fallback, double low, double high)
        { return std::isfinite(value) ? std::clamp(value, low, high) : fallback; }
    }
    EnvironmentSettings Environment::GetSettings() { std::lock_guard lock(environment_mutex); return settings; }
    void Environment::SetSettings(EnvironmentSettings value)
    {
        std::lock_guard lock(environment_mutex);
        value.utc_days = finite(value.utc_days, settings.utc_days, -73048.5, 73413.49999999999); // 1800..2200
        value.latitude = finite(value.latitude, settings.latitude, -90, 90);
        value.longitude = finite(value.longitude, settings.longitude, -180, 180);
        value.elevation = finite(value.elevation, settings.elevation, -400, 10000);
        value.time_scale = finite(value.time_scale, settings.time_scale, -86400, 86400);
        value.north_degrees = float(finite(value.north_degrees, 0, -360, 360));
        value.annual_temperature = float(finite(value.annual_temperature, 18, -70, 50));
        value.seasonal_amplitude = float(finite(value.seasonal_amplitude, 9, 0, 40));
        value.daily_amplitude = float(finite(value.daily_amplitude, 4, 0, 20));
        value.sea_level_pressure = float(finite(value.sea_level_pressure, 101325, 87000, 108500));
        settings = value;
        ++revision;
    }
    double Environment::GetDays(bool real_time)
    {
        std::lock_guard lock(environment_mutex);
        if (!real_time) return settings.utc_days;
        return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count() / 86400.0 - 10957.5;
    }
    bool Environment::SetDate(int year, int month, int day, int hour, int minute, double second)
    {
        std::lock_guard lock(environment_mutex);
        if (year < 1800 || year > 2200 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || !std::isfinite(second) || second < 0 || second >= 60) return false;
        auto time = Astronomy_MakeTime(year, month, day, hour, minute, second);
        auto date = Astronomy_UtcFromTime(time);
        if (date.year != year || date.month != month || date.day != day) return false;
        auto next = settings; next.utc_days = time.ut; SetSettings(next); return true;
    }
    void Environment::GetDate(int& year, int& month, int& day, int& hour, int& minute, double& second, bool real_time)
    {
        std::lock_guard lock(environment_mutex);
        auto date = Astronomy_UtcFromTime(Astronomy_TimeFromDays(GetDays(real_time)));
        year = date.year; month = date.month; day = date.day; hour = date.hour; minute = date.minute; second = date.second;
    }
    float Environment::GetTimeOfDay(bool real_time)
    {
        std::lock_guard lock(environment_mutex);
        double day = GetDays(real_time) + 0.5;
        return float(day - floor(day));
    }
    void Environment::SetTimeOfDay(float fraction)
    {
        std::lock_guard lock(environment_mutex);
        if (!std::isfinite(fraction)) return;
        auto next = settings;
        next.utc_days = floor(next.utc_days + 0.5) - 0.5 + std::clamp(double(fraction), 0.0, 0.999999);
        SetSettings(next);
    }
    void Environment::Tick(double seconds)
    {
        std::lock_guard lock(environment_mutex);
        if (!std::isfinite(seconds) || seconds <= 0 || settings.time_scale == 0) return;
        auto next = settings; next.utc_days += seconds * settings.time_scale / 86400.0; SetSettings(next);
    }
    bool Environment::SetSolarEvent(SolarEvent event)
    {
        std::lock_guard lock(environment_mutex);
        const auto start = Astronomy_TimeFromDays(floor(settings.utc_days + 0.5) - 0.5);
        const auto observer = Astronomy_MakeObserver(settings.latitude, settings.longitude, settings.elevation);
        astro_time_t event_time;
        if (event == SolarEvent::Noon || event == SolarEvent::Midnight)
        {
            auto result = Astronomy_SearchHourAngleEx(BODY_SUN, observer, event == SolarEvent::Noon ? 0 : 12, start, 1);
            if (result.status != ASTRO_SUCCESS) return false;
            event_time = result.time;
        }
        else
        {
            auto result = event == SolarEvent::GoldenHour
                ? Astronomy_SearchAltitude(BODY_SUN, observer, DIRECTION_SET, start, 1, 4)
                : Astronomy_SearchRiseSetEx(BODY_SUN, observer, event == SolarEvent::Dawn ? DIRECTION_RISE : DIRECTION_SET, start, 1, 0);
            // Polar day/night can have no horizon crossing on this date; never fabricate one.
            if (result.status != ASTRO_SUCCESS) return false;
            event_time = result.time;
        }
        auto next = settings; next.utc_days = event_time.ut; SetSettings(next);
        return true;
    }
    EnvironmentState Environment::Evaluate(bool real_time, float clouds, float wind_speed)
    {
        std::lock_guard lock(environment_mutex);
        const double days = GetDays(real_time);
        clouds = float(finite(clouds, 0, 0, 1)); wind_speed = float(finite(wind_speed, 0, 0, 100));
        if (revision == evaluated_revision && fabs(days - evaluated_days) < 0.25 / 86400.0 && clouds == evaluated_clouds && wind_speed == evaluated_wind) return state;
        auto time = Astronomy_TimeFromDays(days);
        auto observer = Astronomy_MakeObserver(settings.latitude, settings.longitude, settings.elevation);
        auto sun = Astronomy_Equator(BODY_SUN, &time, observer, EQUATOR_OF_DATE, ABERRATION);
        auto moon = Astronomy_Equator(BODY_MOON, &time, observer, EQUATOR_OF_DATE, ABERRATION);
        if (sun.status != ASTRO_SUCCESS || moon.status != ASTRO_SUCCESS) return state;
        auto sh = Astronomy_Horizon(&time, observer, sun.ra, sun.dec, REFRACTION_NORMAL);
        auto mh = Astronomy_Horizon(&time, observer, moon.ra, moon.dec, REFRACTION_NORMAL);
        state.sun = horizontal(sh.azimuth, sh.altitude);
        state.moon = horizontal(mh.azimuth, mh.altitude);
        state.sun_radius = float(asin(SUN_RADIUS_KM / (sun.dist * KM_PER_AU)));
        state.moon_radius = float(asin(1737.4 / (moon.dist * KM_PER_AU)));
        auto illumination = Astronomy_Illumination(BODY_MOON, time);
        if (illumination.status == ASTRO_SUCCESS) state.moon_fraction = float(illumination.phase_fraction);
        // Transform the horizon into the J2000 frame of the star catalogue, including precession/nutation.
        auto rotation = Astronomy_Rotation_HOR_EQJ(&time, observer);
        const double yaw = settings.north_degrees * radians;
        auto transform = [&](double x, double y, double z)
        {
            astro_vector_t v = { ASTRO_SUCCESS, x*sin(yaw)+z*cos(yaw), -x*cos(yaw)+z*sin(yaw), y, time };
            auto q = Astronomy_RotateVector(rotation, v);
            // Star catalogue uses +Y for celestial north and atan2(z,x) for right ascension.
            return math::Vector3(float(q.x), float(q.z), float(q.y));
        };
        state.equatorial_x = transform(1,0,0); state.equatorial_y = transform(0,1,0); state.equatorial_z = transform(0,0,1);
        // Configurable climate approximation, not historical meteorological observations.
        // Seasonal peak lags the summer solstice by ~30 days; daily peak is at local solar 15:00.
        double season = cos((days - 202.0) * 6.283185307179586 / 365.2422) * (settings.latitude >= 0 ? 1 : -1);
        double solar_hour = GetTimeOfDay(real_time) * 24 + settings.longitude / 15;
        double daily = cos((solar_hour - 15) * 6.283185307179586 / 24) * (1 - 0.65 * clouds);
        state.air_temperature = float(std::clamp(settings.annual_temperature + settings.seasonal_amplitude * season + settings.daily_amplitude * daily - 0.0065 * settings.elevation, -90.0, 60.0));
        state.pressure = settings.sea_level_pressure * float(pow(1 - 2.25577e-5 * settings.elevation, 5.25588));
        state.air_density = state.pressure / (287.05f * (state.air_temperature + 273.15f));
        // Exposed asphalt equilibrium: absorbed shortwave, longwave loss and wind convection.
        float solar = 900 * std::max(state.sun.y, 0.0f) * (1 - 0.75f * clouds * clouds * clouds);
        float target = state.air_temperature + (0.85f * solar - 55 * (1 - clouds)) / (18 + 4 * wind_speed);
        double elapsed = (days - evaluated_days) * 86400;
        bool discontinuity = evaluated_revision == ~uint64_t(0) || elapsed < 0 || elapsed > 600 || (revision != evaluated_revision && elapsed == 0);
        float blend = discontinuity ? 1.0f : float(-expm1(-elapsed / 1800));
        state.road_temperature += (target - state.road_temperature) * blend;
        evaluated_days = days; evaluated_revision = revision; evaluated_clouds = clouds; evaluated_wind = wind_speed;
        return state;
    }
}
