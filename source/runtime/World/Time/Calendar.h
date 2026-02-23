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

#pragma once

//= INCLUDES ====================
#include <chrono>
#include <memory>
#include <string>
//===============================

namespace spartan
{
    class Calendar
    {
    public:	
        typedef std::chrono::steady_clock Clock;
        typedef Clock::time_point TimePoint;

        virtual ~Calendar() = default;

        virtual void AdvanceTime(std::chrono::milliseconds delta);
        [[nodiscard]] virtual TimePoint GetCurrentTime() const;

        static Calendar& Instance();
        static void SetInstance(std::shared_ptr<Calendar> calendar);
        static void DestroyInstance();
        static void ResetInstance();

        static bool IsLeapYear(int year);
        static int  GetDaysInMonth(int year, int month);
        static int  GetDaysInYear(int year);
        static int  GetCurrentYear();
        static int  GetCurrentMonth();
        static int  GetCurrentDay();

        static double GetTimeInDays(int year, int month, int day, int hour, int minute, int second);
        [[nodiscard]] virtual double GetTimeInSeconds(int year, int month, int day, int hour, int minute, int second) const;
        [[nodiscard]] virtual double GetTimeInHours(int year, int month, int day, int hour, int minute, int second) const;

        static bool IsValidDate(int year, int month, int day);

        static unsigned int GetLengthOfMonth(int month, bool leap);

        static std::string GetMonthName(int month);
        static std::string GetDayName(int day);
        static std::string GetSeasonName(int month);
        static std::string GetDayCycleName(float timeOfDay);

    protected:
        [[nodiscard]] virtual bool IsLeapYearImpl(int year) const;
        [[nodiscard]] virtual int GetDaysInMonthImpl(int year, int month) const;
        [[nodiscard]] virtual int GetDaysInYearImpl(int year) const;
        [[nodiscard]] virtual int GetCurrentYearImpl() const;
        [[nodiscard]] virtual int GetCurrentMonthImpl() const;
        [[nodiscard]] virtual int GetCurrentDayImpl() const;
        [[nodiscard]] virtual double GetTimeInDaysImpl(int year, int month, int day, int hour, int minute, int second) const;
        [[nodiscard]] virtual bool IsValidDateImpl(int year, int month, int day) const;
        [[nodiscard]] virtual unsigned int GetLengthOfMonthImpl(int month, bool leap) const;
        [[nodiscard]] virtual std::string GetMonthNameImpl(int month) const;
        [[nodiscard]] virtual std::string GetDayNameImpl(int day) const;
        [[nodiscard]] virtual std::string GetSeasonNameImpl(int month) const;
        [[nodiscard]] virtual std::string GetDayCycleNameImpl(float timeOfDay) const;

        [[nodiscard]] double GetDateInDays(int year, int month, int day) const;

    private:
	TimePoint currentTime;
    };

}  // namespace spartan
