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
#include <algorithm>
//=================================

namespace spartan
{
    /**
     * Calculates local days based on Earth hours.
     * @param total_time_hours The total time period being measured (e.g., a year) in Earth hours.
     * @param local_day_hours The length of one rotation (sidereal day) in Earth hours.
     * @return The number of full rotation periods.
     */
    static double CalculateBasicDays(double total_time_hours, double local_day_hours) 
    {
        if (local_day_hours == 0)
            return 0; // Prevent division by zero

        return total_time_hours / local_day_hours;
    }

    /**
     * Advanced: Calculates actual Solar Days (sunrise to sunrise).
     * This accounts for the planet's movement along its orbit.
     * @param year_hours Orbital period in Earth hours.
     * @param sidereal_day_hours Time for one 360-degree rotation in Earth hours.
     * @param is_prograde True if planet spins same direction as orbit (like Earth), False for retrograde (like Venus).
     * @return The number of Solar Days in one orbital year.
     */
    static double CalculateSolarDaysPerYear(double year_hours, double sidereal_day_hours, bool is_prograde) 
    {
        // Formula: SolarDay = (Year * Sidereal) / (Year +/- Sidereal)
        // Earth-like (Prograde) uses minus; Retrograde uses plus.
        double solarDayLength;
        if (is_prograde)
        {
            solarDayLength = (year_hours * sidereal_day_hours) / (year_hours - sidereal_day_hours);
        } else 
            {
            solarDayLength = (year_hours * sidereal_day_hours) / (year_hours + sidereal_day_hours);
        }
        
        return year_hours / solarDayLength;
    }

    /*
     * ==================================================================================================================================================
     * [!!!WARNING!!!]
     * Since the timeOfDay input defaults to Earth hours (0.0 to 24.0), but your world has a custom m_hours_in_day, you must
     * normalize the incoming time. If you don't, a 48-hour custom planet would only ever see the first half of its day cycles
     * before the engine's clock resets.
     * ==================================================================================================================================================
     */

    CustomCalendar::CustomCalendar(const int days_in_year, const int hours_in_day)
        : m_days_in_year(days_in_year),
          m_leap_days_in_year(days_in_year + 1),
          m_hours_in_day(hours_in_day)
    {
        // 1. Determine a default month count (e.g., 12 or a divisor of days_in_year)
        // For custom worlds, we'll aim for ~30 days per month as a baseline
        int target_days_per_month = 30;
        int month_count           = (m_days_in_year > 0) ? std::max(1, m_days_in_year / target_days_per_month) : 1;

        // 2. Calculate distribution
        int base_days_per_month = m_days_in_year / month_count;
        int remainder_days      = m_days_in_year % month_count;
        m_month_lengths.clear();
        m_month_names.clear();

        for (int i = 0; i < month_count; ++i)
        {
            // Distribute remainders to the first few months
            int days = base_days_per_month + (i < remainder_days ? 1 : 0);
            m_month_lengths.push_back(days);

            // Provide generic names so the UI/Engine doesn't crash or stay empty
            m_month_names.push_back("Month " + std::to_string(i + 1));
        }

        // 3. Default Season names (if empty)
        if (m_season_names.empty())
        { 
            m_season_names = {
                "Spring", 
                "Summer", 
                "Autumn", 
                "Winter"
            }; 
        }

        // 4. Default Day Cycle names (Morning, Afternoon, Evening, Night)
        if (m_day_cycle_names.empty())
        { 
            m_day_cycle_names = {
                "Night", 
                "Morning", 
                "Afternoon", 
                "Evening"
            }; 
        }
    }

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
        // If the user manually provided month lengths, sum them up
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

        // Otherwise, return the value passed in the constructor
        return IsLeapYearImpl(year) ? m_leap_days_in_year : m_days_in_year;
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

    // TODO: Allow developers to specify season lengths and names rather than just mapping them to the base calendar months.
    std::string CustomCalendar::GetSeasonNameImpl(const int month) const
    {
        if (!m_season_names.empty())
        {
            // Calculate season based on progress through the total months
            if (size_t month_count = m_month_lengths.size(); month_count > 0)
            {
                // Map month index (0 to N-1) to season index
                size_t season_index = (static_cast<size_t>(month - 1) * m_season_names.size()) / month_count;
                return m_season_names[std::min(season_index, m_season_names.size() - 1)];
            }
        }

        return Calendar::GetSeasonNameImpl(month);
    }

    std::string CustomCalendar::GetDayCycleNameImpl(const float timeOfDay) const
    {
        if (!m_day_cycle_names.empty())
        {
            // Map timeOfDay (0 to m_hours_in_day) to day cycle index
            float progress = timeOfDay / static_cast<float>(m_hours_in_day);
            size_t cycle_index = static_cast<size_t>(progress * m_day_cycle_names.size());
            
            return m_day_cycle_names[std::min(cycle_index, m_day_cycle_names.size() - 1)];
        }
        return Calendar::GetDayCycleNameImpl(timeOfDay);
    }

}
