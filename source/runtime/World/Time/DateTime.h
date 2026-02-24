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

//===============================

namespace spartan
{
    class DateTime
    {
    public:
        /**
         * @brief Updates the in-game time based on the elapsed time and timescale
         */
        static void Tick();

        /**
         * @brief Sets the current time based on the system's real-world time.
         */
        static void SetCurrentTime();

        /**
         * @brief Gets the current time of day as a normalized value between 0.0 and 1.0, where 0.0 represents midnight, 0.5
         * represents noon, and 1.0 represents the next midnight. The method can return either real-world time or in-game time
         * based on the provided parameter.
         * @param use_real_world_time Whether to use real-world time or in-game time.
         * @return The current time of day as a normalized float value between 0.0 and 1.0.
         */
        static float GetTimeOfDay(bool use_real_world_time = false);

        /**
         * @brief Sets the current time of day as a normalized value between 0.0 and 1.0, where 0.0 represents midnight, 0.5
         * represents noon, and 1.0 represents the next midnight.
         * @param time_of_day The time of day as a normalized
         * float value between 0.0 and 1.0.
         */
        static void SetTimeOfDay(float time_of_day);

        void GetTimeScale(float& time_scale) { time_scale = m_time_scale; }
        void SetTimeScale(float time_scale) { m_time_scale = time_scale; }

        static int SetCurrentYear(int year);
        static int SetCurrentMonth(int month);
        static int SetCurrentDay(int day);
        static void SetCurrentDate(int year, int month, int day);
        static void SetCurrentTime(int year, int month, int day, int hour, int minute, int second);

        /**
         * @brief Validates whether the provided hour, minute, and second values represent a valid time. The method checks that
         * the hour is between 0 and 23, the minute is between 0 and 59, and the second is between 0 and 59.
         * @param hour The hour value to validate.
         * @param minute The minute value to validate.
         * @param second The
         * second value to validate.
         * @return True if the provided values represent a valid time, false otherwise.
         */
        static bool IsValidTime(int hour, int minute, int second);

    private:
        static DateTime& Instance();
        float m_time_scale    = 200.0f;  // 200x real time
        float m_time_of_day   = 0.25f;   // normalized 0.0 to 1.0, where 0.0 is midnight, 0.5 is noon, and 1.0 is the next midnight
        float m_current_month = 0.0f;
        float m_current_hour  = 0.0f;
        int m_current_year    = 0;
        int m_current_day     = 0;
        int m_current_minute  = 0;
        int m_current_second  = 0;
    };

}  // namespace spartan
