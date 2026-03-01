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

#pragma once

//= INCLUDES ====================
#include "Calendar.h"
#include <functional>
#include <vector>
//===============================

namespace spartan
{
    class CustomCalendar : public Calendar
    {
    public:
        /**
         * @brief Constructs a CustomCalendar with specified days in a year and hours in a day. It calculates month lengths and names
         * based on these parameters.
         * @param days_in_year The total number of days in a year for this calendar.
         * @param hours_in_day The number of hours in a day for this calendar.
         */
        explicit CustomCalendar(int days_in_year = 365, int hours_in_day = 24);

        void SetMonthNames(const std::vector<std::string>& names);
        void SetDayNames(const std::vector<std::string>& names);
        void SetSeasonNames(const std::vector<std::string>& names);
        void SetDayCycleNames(const std::vector<std::string>& names);
        void SetMonthLengths(const std::vector<int>& lengths);
        void SetLeapMonthLengths(const std::vector<int>& lengths);

        /**
         * @brief Allows the user to define a custom leap year rule by providing a function that takes an integer year as input
         * and returns a boolean indicating whether the year is a leap year.
         * @param rule A function that determines if a given year is a leap year. The function should return true for leap years
         * and false for non-leap years.
         */
        void SetLeapYearRule(std::function<bool(int)> rule);

    protected:
        /**
         * @brief Determines if a given year is a leap year based on the custom leap year rule provided by the user. If no custom
         * rule is set, it falls back to the default leap year calculation (every 4 years, except every 100 years, but including
         * every 400 years).
         * @param year The year to check.
         * @return True if the year is a leap year, false otherwise.
         */
        [[nodiscard]] bool IsLeapYearImpl(int year) const override;

        [[nodiscard]] int GetDaysInMonthImpl(int year, int month) const override;
        [[nodiscard]] int GetDaysInYearImpl(int year) const override;
        [[nodiscard]] unsigned int GetLengthOfMonthImpl(int month, bool leap) const override;

        [[nodiscard]] std::string GetMonthNameImpl(int month) const override;
        [[nodiscard]] std::string GetDayNameImpl(int day) const override;
        [[nodiscard]] std::string GetSeasonNameImpl(int month) const override;
        [[nodiscard]] std::string GetDayCycleNameImpl(float timeOfDay) const override;

    private:
        int m_days_in_year      = 365;  // Default to 365, can be overridden by month lengths
        int m_leap_days_in_year = 366;  // Default to 366, can be overridden by month lengths
        int m_hours_in_day      = 24;   // Default to 24, can be overridden if needed

        std::vector<std::string> m_month_names;
        std::vector<std::string> m_day_names;
        std::vector<std::string> m_season_names;
        std::vector<std::string> m_day_cycle_names;
        std::vector<int> m_month_lengths;
        std::vector<int> m_month_lengths_leap;
        std::function<bool(int)> m_is_leap_year;
    };
}
