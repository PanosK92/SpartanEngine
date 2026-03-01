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
#include "World/Time/Calendar.h"
//=================================

//= NAMESPACES ===============
using namespace std;
using namespace std::chrono;
using namespace spartan::math;
//============================

namespace spartan
{

    namespace
    {
        std::shared_ptr<Calendar> calendar_instance;
    }

    void Calendar::AdvanceTime(const milliseconds delta)
    {
        m_current_time += delta;
    }

    Calendar::TimePoint Calendar::GetCurrentTime() const
    {
        return m_current_time;
    }

    Calendar& Calendar::Instance()
    {
        if (!calendar_instance)
        {
            calendar_instance = std::make_shared<Calendar>();
        }

        return *calendar_instance;
    }

    void Calendar::SetInstance(std::shared_ptr<Calendar> calendar)
    {
        if (calendar)
        {
            calendar_instance = std::move(calendar);
        }
    }

    void Calendar::DestroyInstance()
    {
        calendar_instance.reset();
    }

    void Calendar::ResetInstance()
    {
        calendar_instance = std::make_shared<Calendar>();
    }

    void Calendar::SetCurrentTimeOffset(const seconds offset)
    {
        Instance().m_current_time = TimePoint(offset);
    }

    bool Calendar::IsLeapYear(const int year)
    {
        return Instance().IsLeapYearImpl(year);
    }

    int Calendar::GetDaysInMonth(const int year, const int month)
    {
        return Instance().GetDaysInMonthImpl(year, month);
    }

    int Calendar::GetDaysInYear(const int year)
    {
        return Instance().GetDaysInYearImpl(year);
    }

    int Calendar::GetCurrentYear()
    {
        return Instance().GetCurrentYearImpl();
    }

    int Calendar::GetCurrentMonth()
    {
        return Instance().GetCurrentMonthImpl();
    }

    int Calendar::GetCurrentDay()
    {
        return Instance().GetCurrentDayImpl();
    }

    double Calendar::GetTimeInDays(const int year, const int month, const int day, const int hour, const int minute, const int second)
    {
        return Instance().GetTimeInDaysImpl(year, month, day, hour, minute, second);
    }

    double Calendar::GetTimeInSeconds(const int year, const int month, const int day, const int hour, const int minute, const int second) const
    {
        return GetTimeInDaysImpl(year, month, day, hour, minute, second) * 86400.0;
    }

    double Calendar::GetTimeInHours(const int year, const int month, const int day, const int hour, const int minute, const int second) const
    {
        return GetTimeInDaysImpl(year, month, day, hour, minute, second) * 24.0;
    }

    bool Calendar::IsValidDate(const int year, const int month, const int day)
    {
        return Instance().IsValidDateImpl(year, month, day);
    }

    unsigned int Calendar::GetLengthOfMonth(const int month, const bool leap)
    {
        return Instance().GetLengthOfMonthImpl(month, leap);
    }

    std::string Calendar::GetMonthName(const int month)
    {
        return Instance().GetMonthNameImpl(month);
    }

    std::string Calendar::GetDayName(const int day)
    {
        return Instance().GetDayNameImpl(day);
    }

    std::string Calendar::GetSeasonName(const int month)
    {
        return Instance().GetSeasonNameImpl(month);
    }

    std::string Calendar::GetDayCycleName(const float timeOfDay)
    {
        return Instance().GetDayCycleNameImpl(timeOfDay);
    }

    bool Calendar::IsLeapYearImpl(const int year) const
    {
        if (year % 4 == 0)
        {
            if (year % 100 == 0)
            {
                return year % 400 == 0;
            }

            return true;
        }

        return false;
    }

    int Calendar::GetDaysInMonthImpl(const int year, const int month) const
    {
        return GetLengthOfMonthImpl(month, IsLeapYearImpl(year));
    }

    int Calendar::GetDaysInYearImpl(const int year) const
    {
        return IsLeapYearImpl(year) ? 366 : 365;
    }

    int Calendar::GetCurrentYearImpl() const
    {
        auto now            = GetCurrentTime();
        auto elapsed        = now.time_since_epoch();
        auto elapsedSeconds = std::chrono::duration_cast<seconds>(elapsed);
        auto systemNow      = system_clock::now();
        auto tp             = system_clock::to_time_t(systemNow + elapsedSeconds);
        tm localTm;
        localtime_s(&localTm, &tp);
        return localTm.tm_year + 1900;
    }

    int Calendar::GetCurrentMonthImpl() const
    {
        auto now            = GetCurrentTime();
        auto elapsed        = now.time_since_epoch();
        auto elapsedSeconds = std::chrono::duration_cast<seconds>(elapsed);
        auto systemNow      = system_clock::now();
        auto tp             = system_clock::to_time_t(systemNow + elapsedSeconds);
        tm localTm;
        localtime_s(&localTm, &tp);
        return localTm.tm_mon + 1;
    }

    int Calendar::GetCurrentDayImpl() const
    {
        auto now            = GetCurrentTime();
        auto elapsed        = now.time_since_epoch();
        auto elapsedSeconds = std::chrono::duration_cast<seconds>(elapsed);
        auto systemNow      = system_clock::now();
        auto tp             = system_clock::to_time_t(systemNow + elapsedSeconds);
        tm local_tm;
        localtime_s(&local_tm, &tp);
        return local_tm.tm_mday;
    }

    double Calendar::GetTimeInDaysImpl(const int year, const int month, const int day, const int hour, const int minute, const int second) const
    {
        return GetDateInDays(year, month, day) + (hour * 3600 + minute * 60 + second) / 86400.0;
    }

    bool Calendar::IsValidDateImpl(const int year, const int month, const int day) const
    {
        SP_ASSERT_MSG(year > 0, "Year must be greater than 0");
        SP_ASSERT_MSG(month >= 1 && month <= 12, "Month must be in range 1..12");
        SP_ASSERT_MSG(day >= 1 && day <= GetDaysInMonthImpl(year, month), "Day must be in range 1..31");

        if (year <= 0)
            return false;

        if (month < 1 || month > 12)
            return false;

        if (int daysInMonth = GetDaysInMonthImpl(year, month); day < 1 || day > daysInMonth)
        {
            return false;
        }

        return true;
    }

    unsigned int Calendar::GetLengthOfMonthImpl(const int month, const bool leap) const
    {
        SP_ASSERT_MSG(month >= 1 && month <= 12, "Month must be in range 1..12");
        switch (month)
        {
            case 2: return leap ? 29 : 28;
            case 4: 
            case 6:
            case 9:
            case 11:return 30;
            case 1:
            case 3:
            case 5:
            case 7:
            case 8:
            case 10:
            case 12: return 31;
        }
        return 0;
    }

    std::string Calendar::GetMonthNameImpl(const int month) const
    {
        SP_ASSERT_MSG(month >= 1 && month <= 12, "Month must be in range 1..12");
        static const char* monthNames[12] = {
            "January", 
            "February", 
            "March",     
            "April",   
            "May",      
            "June",
            "July",    
            "August",   
            "September", 
            "October", 
            "November", 
            "December"
        };
        if (month < 1 || month > 12)
        {
            SP_ASSERT_MSG(false, "Month must be in range 1..12");
        }
        return monthNames[month - 1];
    }

    std::string Calendar::GetDayNameImpl(const int day) const
    {
        SP_ASSERT_MSG(day >= 1 && day <= 7, "Day must be in range 1..7");
        static const char* dayNames[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        if (day < 1 || day > 7) { throw std::invalid_argument("Day must be in range 1..7"); }
        return dayNames[day - 1];
    }

    std::string Calendar::GetSeasonNameImpl(const int month) const
    {
        SP_ASSERT_MSG(month >= 1 && month <= 12, "Month must be in range 1..12");
        static const char* seasonNames[4] = {"Winter", "Spring", "Summer", "Fall"};
        const char* season                = nullptr;
        switch (month)
        {
            case 12:
            case 1:
            case 2: season = seasonNames[0]; break;
            case 3:
            case 4:
            case 5: season = seasonNames[1]; break;
            case 6:
            case 7:
            case 8: season = seasonNames[2]; break;
            case 9:
            case 10:
            case 11: season = seasonNames[3]; break;
            default: SP_LOG_ERROR("Month must be in range 1..12");
        }

        return season;
    }

    std::string Calendar::GetDayCycleNameImpl(const float timeOfDay) const
    {
        SP_ASSERT_MSG(timeOfDay >= 0.0f && timeOfDay < 24.0f, "Time of day must be in range 0..24");

        static const char* dayCycleNames[4] = {
            "Night", 
            "Morning", 
            "Afternoon", 
            "Evening"
        };

        const char* dayCycle                = nullptr;

        if (timeOfDay >= 0.0f && timeOfDay < 6.0f)
        {
            dayCycle = dayCycleNames[0];
        }
        else if (timeOfDay >= 6.0f && timeOfDay < 12.0f)
        {
            dayCycle = dayCycleNames[1];
        }
        else if (timeOfDay >= 12.0f && timeOfDay < 18.0f)
        {
            dayCycle = dayCycleNames[2];
        }
        else if (timeOfDay >= 18.0f && timeOfDay < 24.0f)
        {
            dayCycle = dayCycleNames[3];
        }
        else
        {
            SP_LOG_ERROR("Time of day must be in range 0..24");
        }

        return dayCycle;
    }

    double Calendar::GetDateInDays(const int year, const int month, const int day) const
    {
        SP_ASSERT_MSG(year > 0, "Year must be greater than 0");
        SP_ASSERT_MSG(month >= 1 && month <= 12, "Month must be in range 1..12");
        SP_ASSERT_MSG(day >= 1 && day <= GetDaysInMonthImpl(year, month), "Day must be in valid range");

        int totalDays = 0;
        for (int y = 1; y < year; ++y)
        {
            totalDays += IsLeapYearImpl(y) ? 366 : 365;
        }

        for (int m = 1; m < month; ++m)
        {
            totalDays += GetDaysInMonthImpl(year, m);
        }

        totalDays += day - 1;

        return totalDays;
    }

}

