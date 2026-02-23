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
//=================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace world_time
    {
        float get_time_of_day(bool use_real_world_time);
        extern float time_of_day;
    }

    float DateTime::GetTimeOfDay(bool use_real_world_time)
    {
        return world_time::get_time_of_day(use_real_world_time);
    }

    void DateTime::SetTimeOfDay(float time_of_day)
    {
        if (time_of_day < 0.0f)
            time_of_day = 0.0f;
        else if (time_of_day > 1.0f)
            time_of_day = 1.0f;
        world_time::time_of_day = time_of_day;
    }

    bool DateTime::IsValidTime(const int hour, const int minute, const int second)
    {
        SP_ASSERT_MSG(hour >= 0 && hour <= 23, "Hour must be in range 0-23")
        SP_ASSERT_MSG(minute >= 0 && minute <= 59, "Minute must be in range 0-59")
        SP_ASSERT_MSG(second >= 0 && second <= 59, "Second must be in range 0-59")

        if (hour < 0 || hour > 23)
            return false;

        if (minute < 0 || minute > 59)
            return false;
        
        if (second < 0 || second > 59)
            return false;

        return true;
    }

}

