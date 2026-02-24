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

//= INCLUDES ====================
#include "pch.h"
#include "TimeZone.h"
#include "TimeZone_db.cpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
//===============================

namespace spartan
{
    namespace
    {
        TimeZoneInfo ToTimeZoneInfo(const TimeZoneDbEntry& entry)
        {
            TimeZoneInfo info;
            info.offset_hours = entry.offset_hours;
            info.tz_id = entry.tz_id;
            info.latitude = entry.latitude;
            info.longitude = entry.longitude;
            info.country_code = entry.country_code;
            info.tz_code = entry.tz_code;
            info.tz_name = entry.tz_name;
            return info;
        }

        std::string StripWhitespace(std::string value)
        {
            value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c)
            {
                return std::isspace(c) != 0;
            }), value.end());
            return value;
        }

        std::string ToUpper(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
            {
                return static_cast<char>(std::toupper(c));
            });
            return value;
        }

        bool TryParseOffsetToken(const std::string& value, float& offset_hours)
        {
            if (value.empty())
                return false;

            char sign = '+';
            size_t start = 0;
            if (value[0] == '+' || value[0] == '-')
            {
                sign = value[0];
                start = 1;
            }

            if (start >= value.size())
                return false;

            std::string token = value.substr(start);
            size_t colon_index = token.find(':');
            if (colon_index != std::string::npos)
            {
                std::string hours_text = token.substr(0, colon_index);
                std::string minutes_text = token.substr(colon_index + 1);
                if (hours_text.empty() || minutes_text.empty())
                    return false;

                char* hours_end = nullptr;
                char* minutes_end = nullptr;
                long hours = std::strtol(hours_text.c_str(), &hours_end, 10);
                long minutes = std::strtol(minutes_text.c_str(), &minutes_end, 10);
                if (hours_end == hours_text.c_str() || *hours_end != '\0')
                    return false;
                if (minutes_end == minutes_text.c_str() || *minutes_end != '\0')
                    return false;
                if (minutes < 0 || minutes >= 60)
                    return false;

                offset_hours = static_cast<float>(hours) + static_cast<float>(minutes) / 60.0f;
            }
            else
            {
                char* end = nullptr;
                float parsed = std::strtof(token.c_str(), &end);
                if (end == token.c_str() || *end != '\0')
                    return false;

                offset_hours = parsed;
            }

            if (sign == '-')
            {
                offset_hours = -offset_hours;
            }

            return true;
        }
    }

    TimeZone& TimeZone::Instance()
    {
        static TimeZone instance;
        return instance;
    }

    void TimeZone::SetTimeZoneOffsetHours(float offset_hours)
    {
        offset_hours = std::clamp(offset_hours, -24.0f, 24.0f);
        Instance().m_time_offset = offset_hours;
    }

    float TimeZone::GetTimeZoneOffsetHours()
    {
        return Instance().m_time_offset;
    }

    std::string TimeZone::GetTimeZoneName(const float offset_hours)
    {
        // use the compiled-in database to find a matching timezone name
        auto matches = timezone_db_find_by_offset(offset_hours);
        if (!matches.empty())
        {
            return matches[0]->tz_code;
        }

        const float absolute_offset = std::fabs(offset_hours);
        int hours = static_cast<int>(std::floor(absolute_offset));
        int minutes = static_cast<int>(std::round((absolute_offset - static_cast<float>(hours)) * 60.0f));
        if (minutes == 60)
        {
            hours += 1;
            minutes = 0;
        }

        std::ostringstream stream;
        stream << "UTC" << (offset_hours >= 0.0f ? "+" : "-")
               << std::setw(2) << std::setfill('0') << hours
               << ":" << std::setw(2) << std::setfill('0') << minutes;
        return stream.str();
    }

    std::string TimeZone::GetTimeZoneName()
    {
        return GetTimeZoneName(GetTimeZoneOffsetHours());
    }

    void TimeZone::SetLocation(const float latitude, const float longitude)
    {
        TimeZone& instance = Instance();
        instance.m_latitude = latitude;
        instance.m_longitude = longitude;
        instance.m_has_location = true;
    }

    bool TimeZone::HasLocation()
    {
        return Instance().m_has_location;
    }

    void TimeZone::GetLocation(float& latitude, float& longitude)
    {
        const TimeZone& instance = Instance();
        latitude = instance.m_latitude;
        longitude = instance.m_longitude;
    }

    bool TimeZone::GetTimeZoneInfoByZoneCode(const std::string& zone_code, TimeZoneInfo& info)
    {
        if (zone_code.empty())
        {
            return false;
        }

        const TimeZoneDbEntry* entry = timezone_db_find_by_zone_code(zone_code.c_str());
        if (!entry)
        {
            return false;
        }

        info = ToTimeZoneInfo(*entry);
        return true;
    }

    bool TimeZone::GetTimeZoneInfoByCountryCode(const std::string& country_code, TimeZoneInfo& info)
    {
        if (country_code.empty())
        {
            return false;
        }

        const TimeZoneDbEntry* entry = timezone_db_find_by_country_code(country_code.c_str());
        if (!entry)
        {
            return false;
        }

        info = ToTimeZoneInfo(*entry);
        return true;
    }

    std::vector<TimeZoneInfo> TimeZone::GetTimeZonesByCountryCode(const std::string& country_code)
    {
        std::vector<TimeZoneInfo> results;
        if (country_code.empty())
        {
            return results;
        }

        const std::vector<const TimeZoneDbEntry*> matches = timezone_db_find_all_by_country(country_code.c_str());
        results.reserve(matches.size());
        for (const TimeZoneDbEntry* entry : matches)
        {
            results.push_back(ToTimeZoneInfo(*entry));
        }

        return results;
    }

    std::vector<TimeZoneInfo> TimeZone::GetTimeZonesByOffsetHours(const float offset_hours, const float epsilon)
    {
        std::vector<TimeZoneInfo> results;
        const std::vector<const TimeZoneDbEntry*> matches = timezone_db_find_by_offset(offset_hours, epsilon);
        results.reserve(matches.size());
        for (const TimeZoneDbEntry* entry : matches)
        {
            results.push_back(ToTimeZoneInfo(*entry));
        }

        return results;
    }

    bool TimeZone::GetTimeZoneInfoByLocation(const float latitude, const float longitude, TimeZoneInfo& info)
    {
        const TimeZoneDbEntry* entry = timezone_db_find_nearest(latitude, longitude);
        if (!entry)
        {
            return false;
        }

        info = ToTimeZoneInfo(*entry);
        return true;
    }

    bool TimeZone::ParseTimeZoneOffsetHours(const std::string& text, float& offset_hours)
    {
        std::string normalized = StripWhitespace(text);
        if (normalized.empty())
        {
            return false;
        }

        std::string upper = ToUpper(normalized);
        if (upper == "UTC" || upper == "GMT")
        {
            offset_hours = 0.0f;
            return true;
        }

        if (upper.starts_with("UTC") || upper.starts_with("GMT"))
        {
            normalized = normalized.substr(3);
            if (normalized.empty())
            {
                offset_hours = 0.0f;
                return true;
            }
        }

        if (TryParseOffsetToken(normalized, offset_hours))
        {
            return true;
        }

        const TimeZoneDbEntry* entry = timezone_db_find_by_zone_code(normalized.c_str());
        if (!entry)
        {
            return false;
        }

        offset_hours = entry->offset_hours;
        return true;
    }

}
