/*
Copyright(c) 2015-2026 Panos Karabelas & Thomas Ray

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

//= INCLUDES ======================
#include "pch.h"
#include "DateTime.h"
#include "Calendar.h"
#include "TimeZone.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
//=================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        time_t to_utc_time_t(const tm& local_tm, const float offset_hours)
        {
            tm utc_tm = local_tm;
        #ifdef _WIN32
            time_t utc_time = _mkgmtime(&utc_tm);
        #else
            time_t utc_time = timegm(&utc_tm);
        #endif
            if (utc_time == -1)
            {
                return -1;
            }

            const auto offset_seconds = static_cast<time_t>(std::llround(offset_hours * 3600.0f));
            return utc_time - offset_seconds;
        }

        tm get_calendar_local_time()
        {
            using namespace std::chrono;
            auto now      = Calendar::Instance().GetCurrentTime();
            auto elapsed             = now.time_since_epoch();
            auto elapsedSeconds = duration_cast<seconds>(elapsed);
            auto systemNow= system_clock::now();
            auto tp                   = system_clock::to_time_t(systemNow + elapsedSeconds);
            const float offset_hours  = TimeZone::GetTimeZoneOffsetHours();
            const auto offset_seconds = static_cast<time_t>(std::llround(offset_hours * 3600.0f));
            tp += offset_seconds;
            tm local_tm{};
        #ifdef _WIN32
            gmtime_s(&local_tm, &tp);
        #else
            gmtime_r(&tp, &local_tm);
        #endif
            return local_tm;
        }

    }

    void DateTime::Tick()
    {
        DateTime& instance        = Instance();
        const float delta_seconds = static_cast<float>(Timer::GetDeltaTimeSec()) * instance.m_time_scale;
        const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::duration<double>(delta_seconds));
        Calendar::Instance().AdvanceTime(delta);

        tm local_tm               = get_calendar_local_time();
        instance.m_current_year   = local_tm.tm_year + 1900;
        instance.m_current_month  = static_cast<float>(local_tm.tm_mon + 1);
        instance.m_current_day    = local_tm.tm_mday;
        instance.m_current_hour   = static_cast<float>(local_tm.tm_hour);
        instance.m_current_minute = local_tm.tm_min;
        instance.m_current_second = local_tm.tm_sec;
        instance.m_time_of_day = (static_cast<float>(local_tm.tm_hour) + local_tm.tm_min / 60.0f + local_tm.tm_sec / 3600.0f) / 24.0f;
    }

    void DateTime::SetCurrentTime()
    {
        using namespace std::chrono;
        auto now = system_clock::now();
        time_t t = system_clock::to_time_t(now);
        const float offset_hours  = TimeZone::GetTimeZoneOffsetHours();
        const auto offset_seconds = static_cast<time_t>(std::llround(offset_hours * 3600.0f));
        t += offset_seconds;
        tm local_time{};
    #ifdef _WIN32
        gmtime_s(&local_time, &t);
    #else
        gmtime_r(&t, &local_time);
    #endif
        SetCurrentTime(local_time.tm_year + 1900, local_time.tm_mon + 1, local_time.tm_mday,
            local_time.tm_hour, local_time.tm_min, local_time.tm_sec);
    }

    float DateTime::GetTimeOfDay(bool use_real_world_time)
    {
        if (use_real_world_time)
        {
            using namespace std::chrono;
            auto now = system_clock::now();
            time_t t = system_clock::to_time_t(now);
            const float offset_hours  = TimeZone::GetTimeZoneOffsetHours();
            const auto offset_seconds = static_cast<time_t>(std::llround(offset_hours * 3600.0f));
            t += offset_seconds;
            tm local_time{};
    #ifdef _WIN32
            gmtime_s(&local_time, &t);
    #else
            gmtime_r(&t, &local_time);
    #endif
            float hours   = static_cast<float>(local_time.tm_hour);
            float minutes = static_cast<float>(local_time.tm_min);
            float seconds = static_cast<float>(local_time.tm_sec);
            return (hours + minutes / 60.0f + seconds / 3600.0f) / 24.0f;
        }

        return Instance().m_time_of_day;
    }

    void DateTime::SetTimeOfDay(float time_of_day)
    {
        if (time_of_day < 0.0f)
        {
            time_of_day = 0.0f;
        }
        else if (time_of_day > 1.0f)
        {
            time_of_day = 1.0f;
        }

        DateTime& instance = Instance();
        if (instance.m_current_year <= 0 || instance.m_current_month <= 0.0f || instance.m_current_day <= 0)
        {
            SetCurrentTime();
        }

        int hour              = static_cast<int>(time_of_day * 24.0f);
        float minute_fraction = time_of_day * 24.0f - static_cast<float>(hour);
        int minute            = static_cast<int>(minute_fraction * 60.0f);
        float second_fraction = minute_fraction * 60.0f - static_cast<float>(minute);
        int second            = static_cast<int>(second_fraction * 60.0f);
        SetCurrentTime(instance.m_current_year, static_cast<int>(instance.m_current_month), instance.m_current_day, hour, minute, second);
    }

    int DateTime::SetCurrentYear(const int year)
    {
        DateTime& instance = Instance();
        if (year <= 0)
        {
            return instance.m_current_year;
        }

        int month = static_cast<int>(instance.m_current_month);
        int day   = instance.m_current_day;
        if (month <= 0)
        {
            month = Calendar::GetCurrentMonth();
        }
        if (day <= 0)
        {
            day = Calendar::GetCurrentDay();
        }

        int days_in_month = Calendar::GetDaysInMonth(year, month);
        day               = std::min(day, days_in_month);

        SetCurrentTime(year, month, day, static_cast<int>(instance.m_current_hour), instance.m_current_minute, instance.m_current_second);
        return instance.m_current_year;
    }

    int DateTime::SetCurrentMonth(const int month)
    {
        DateTime& instance = Instance();
        if (month < 1 || month > 12)
        {
            return static_cast<int>(instance.m_current_month);
        }

        int year          = instance.m_current_year > 0 ? instance.m_current_year : Calendar::GetCurrentYear();
        int day           = instance.m_current_day > 0 ? instance.m_current_day : Calendar::GetCurrentDay();
        int days_in_month = Calendar::GetDaysInMonth(year, month);
        day               = std::min(day, days_in_month);

        SetCurrentTime(year, month, day, static_cast<int>(instance.m_current_hour), instance.m_current_minute, instance.m_current_second);
        return static_cast<int>(instance.m_current_month);
    }

    int DateTime::SetCurrentDay(const int day)
    {
        DateTime& instance = Instance();
        int year           = instance.m_current_year > 0 ? instance.m_current_year : Calendar::GetCurrentYear();
        int month          = static_cast<int>(instance.m_current_month);
        if (month <= 0)
        {
            month = Calendar::GetCurrentMonth();
        }

        if (int days_in_month = Calendar::GetDaysInMonth(year, month); day < 1 || day > days_in_month)
        {
            return instance.m_current_day;
        }

        SetCurrentTime(year, month, day, static_cast<int>(instance.m_current_hour), instance.m_current_minute, instance.m_current_second);
        return instance.m_current_day;
    }

    void DateTime::SetCurrentDate(const int year, const int month, const int day)
    {
        DateTime& instance = Instance();
        SetCurrentTime(year, month, day, static_cast<int>(instance.m_current_hour), instance.m_current_minute, instance.m_current_second);
    }

    void DateTime::SetCurrentTime(const int year, const int month, const int day, const int hour, const int minute, const int second)
    {
        if (!Calendar::IsValidDate(year, month, day) || !IsValidTime(hour, minute, second))
        {
            return;
        }

        DateTime& instance        = Instance();
        instance.m_current_year   = year;
        instance.m_current_month  = static_cast<float>(month);
        instance.m_current_day    = day;
        instance.m_current_hour   = static_cast<float>(hour);
        instance.m_current_minute = minute;
        instance.m_current_second = second;
        instance.m_time_of_day    = (static_cast<float>(hour) + minute / 60.0f + second / 3600.0f) / 24.0f;

        tm local_tm{};
        local_tm.tm_year   = year - 1900;
        local_tm.tm_mon    = month - 1;
        local_tm.tm_mday   = day;
        local_tm.tm_hour   = hour;
        local_tm.tm_min    = minute;
        local_tm.tm_sec    = second;
        time_t target_time = to_utc_time_t(local_tm, TimeZone::GetTimeZoneOffsetHours());
        if (target_time == -1)
        {
            return;
        }

        auto target_tp = std::chrono::system_clock::from_time_t(target_time);
        auto offset    = std::chrono::duration_cast<std::chrono::seconds>(target_tp - std::chrono::system_clock::now());
        Calendar::SetCurrentTimeOffset(offset);
    }

    bool DateTime::IsValidTime(const int hour, const int minute, const int second)
    {
        SP_ASSERT_MSG(hour >= 0 && hour <= 23, "Hour must be in range 0-23");
        SP_ASSERT_MSG(minute >= 0 && minute <= 59, "Minute must be in range 0-59");
        SP_ASSERT_MSG(second >= 0 && second <= 59, "Second must be in range 0-59");

        if (hour < 0 || hour > 23)
        {
            return false;
        }

        if (minute < 0 || minute > 59)
        {
            return false;
        }

        if (second < 0 || second > 59)
        {
            return false;
        }

        return true;
    }

    DateTime& DateTime::Instance()
    {
        static DateTime instance;
        return instance;
    }

}

