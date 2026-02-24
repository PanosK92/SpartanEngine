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
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
//===============================

namespace spartan
{
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

}
