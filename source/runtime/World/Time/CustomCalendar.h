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
        void SetMonthNames(const std::vector<std::string>& names);
        void SetDayNames(const std::vector<std::string>& names);
        void SetSeasonNames(const std::vector<std::string>& names);
        void SetDayCycleNames(const std::vector<std::string>& names);
        void SetMonthLengths(const std::vector<int>& lengths);
        void SetLeapMonthLengths(const std::vector<int>& lengths);
        void SetLeapYearRule(std::function<bool(int)> rule);

    protected:
        [[nodiscard]] bool IsLeapYearImpl(int year) const override;
        [[nodiscard]] int GetDaysInMonthImpl(int year, int month) const override;
        [[nodiscard]] int GetDaysInYearImpl(int year) const override;
        [[nodiscard]] unsigned int GetLengthOfMonthImpl(int month, bool leap) const override;
        [[nodiscard]] std::string GetMonthNameImpl(int month) const override;
        [[nodiscard]] std::string GetDayNameImpl(int day) const override;
        [[nodiscard]] std::string GetSeasonNameImpl(int month) const override;
        [[nodiscard]] std::string GetDayCycleNameImpl(float timeOfDay) const override;

    private:
        std::vector<std::string> m_month_names;
        std::vector<std::string> m_day_names;
        std::vector<std::string> m_season_names;
        std::vector<std::string> m_day_cycle_names;
        std::vector<int> m_month_lengths;
        std::vector<int> m_month_lengths_leap;
        std::function<bool(int)> m_is_leap_year;
    };
}
