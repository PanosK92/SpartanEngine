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

//= INCLUDES =====
#include <cstddef>
#include <vector>
//================

namespace spartan
{
    // Consolidated timezone entry - merged from https://www.iana.org/time-zones
    struct TimeZoneDbEntry
    {
        float       offset_hours;   // UTC offset in fractional hours (e.g. 5.5 for +05:30)
        const char* tz_id;          // IANA tz database identifier (e.g. "EST, CST")
        float       latitude;       // decimal degrees (south is negative)
        float       longitude;      // decimal degrees (west is negative)
        const char* country_code;   // ISO 3166-1 alpha-2
        const char* tz_code;        // IANA tz database identifier (e.g. "America/New_York")
        const char* tz_name;        // human-readable label
    };

    // database constants
    static constexpr size_t k_timezone_db_count = 418;
    extern const TimeZoneDbEntry k_timezone_db[];

    // find the first entry whose tz_code matches exactly (nullptr if not found)
    const TimeZoneDbEntry* timezone_db_find_by_zone_code(const char* zone_code);

    // find the first entry whose country_code matches exactly (nullptr if not found)
    const TimeZoneDbEntry* timezone_db_find_by_country_code(const char* cc);

    // return all entries that share the given UTC offset (within epsilon)
    std::vector<const TimeZoneDbEntry*> timezone_db_find_by_offset(float offset_hours, float epsilon = 0.01f);

    // return all entries for a given country code
    std::vector<const TimeZoneDbEntry*> timezone_db_find_all_by_country(const char* cc);

    // find the entry closest to a given lat/lon (Euclidean approximation)
    const TimeZoneDbEntry* TimezoneDbFindNearest(float latitude, float longitude);

} // namespace spartan
