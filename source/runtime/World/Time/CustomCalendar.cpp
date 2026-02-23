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
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES ======================
#include "pch.h"
#include "World/Time/CustomCalendar.h"
//=================================

namespace spartan
{
    void CustomCalendar::SetMonthNames(const std::vector<std::string>& names)
    {
        m_month_names = names;
    }

    void CustomCalendar::SetDayNames(const std::vector<std::string>& names)
    {
        m_day_names = names;
    }

    void CustomCalendar::SetSeasonNames(const std::vector<std::string>& names)
    {
        m_season_names = names;
    }

    void CustomCalendar::SetDayCycleNames(const std::vector<std::string>& names)
    {
        m_day_cycle_names = names;
    }

    void CustomCalendar::SetMonthLengths(const std::vector<int>& lengths)
    {
        m_month_lengths = lengths;
    }

    void CustomCalendar::SetLeapMonthLengths(const std::vector<int>& lengths)
    {
        m_month_lengths_leap = lengths;
    }

    void CustomCalendar::SetLeapYearRule(std::function<bool(int)> rule)
    {
        m_is_leap_year = std::move(rule);
    }

    bool CustomCalendar::IsLeapYearImpl(const int year) const
    {
        if (m_is_leap_year)
        {
            return m_is_leap_year(year);
        }

        return Calendar::IsLeapYearImpl(year);
    }

    int CustomCalendar::GetDaysInMonthImpl(const int year, const int month) const
    {
        const bool leap = IsLeapYearImpl(year);
        return static_cast<int>(GetLengthOfMonthImpl(month, leap));
    }

    int CustomCalendar::GetDaysInYearImpl(const int year) const
    {
        if (!m_month_lengths.empty())
        {
            const bool leap = IsLeapYearImpl(year);
            const auto& lengths = (leap && !m_month_lengths_leap.empty()) ? m_month_lengths_leap : m_month_lengths;
            int total = 0;
            for (int length : lengths)
            {
                total += length;
            }
            return total;
        }

        return Calendar::GetDaysInYearImpl(year);
    }

    unsigned int CustomCalendar::GetLengthOfMonthImpl(const int month, const bool leap) const
    {
        if (!m_month_lengths.empty())
        {
            const size_t index = static_cast<size_t>(month - 1);
            const auto& lengths = (leap && !m_month_lengths_leap.empty()) ? m_month_lengths_leap : m_month_lengths;
            if (index < lengths.size())
            {
                return static_cast<unsigned int>(lengths[index]);
            }
        }

        return Calendar::GetLengthOfMonthImpl(month, leap);
    }

    std::string CustomCalendar::GetMonthNameImpl(const int month) const
    {
        if (!m_month_names.empty())
        {
            const size_t index = static_cast<size_t>(month - 1);
            if (index < m_month_names.size())
            {
                return m_month_names[index];
            }
        }

        return Calendar::GetMonthNameImpl(month);
    }

    std::string CustomCalendar::GetDayNameImpl(const int day) const
    {
        if (!m_day_names.empty())
        {
            const size_t index = static_cast<size_t>(day - 1);
            if (index < m_day_names.size())
            {
                return m_day_names[index];
            }
        }

        return Calendar::GetDayNameImpl(day);
    }

    std::string CustomCalendar::GetSeasonNameImpl(const int month) const
    {
        if (m_season_names.size() == 4)
        {
            if (month == 12 || month == 1 || month == 2)
                return m_season_names[0];
            if (month == 3 || month == 4 || month == 5)
                return m_season_names[1];
            if (month == 6 || month == 7 || month == 8)
                return m_season_names[2];
            if (month == 9 || month == 10 || month == 11)
                return m_season_names[3];
        }

        return Calendar::GetSeasonNameImpl(month);
    }

    std::string CustomCalendar::GetDayCycleNameImpl(const float timeOfDay) const
    {
        if (m_day_cycle_names.size() == 4)
        {
            if (timeOfDay >= 0.0f && timeOfDay < 6.0f)
                return m_day_cycle_names[0];
            if (timeOfDay >= 6.0f && timeOfDay < 12.0f)
                return m_day_cycle_names[1];
            if (timeOfDay >= 12.0f && timeOfDay < 18.0f)
                return m_day_cycle_names[2];
            if (timeOfDay >= 18.0f && timeOfDay < 24.0f)
                return m_day_cycle_names[3];
        }

        return Calendar::GetDayCycleNameImpl(timeOfDay);
    }
}
