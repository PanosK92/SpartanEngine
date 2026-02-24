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
#include <string>
#include <vector>
//===============================

namespace spartan
{
    /* TODO: The two structs should be refactored into a single struct with const char* members, and the TimeZoneInfo can be
     * constructed from the TimeZoneDbEntry when needed. This would reduce code duplication and memory usage. */
    struct TimeZoneInfo
    {
        float offset_hours = 0.0f;  // UTC offset in fractional hours (e.g. 5.5 for +05:30)
        std::string tz_id;          // Time zone database identifier (e.g. "EST, CST")
        float latitude  = 0.0f;     // Decimal degrees (south is negative)
        float longitude = 0.0f;     // Decimal degrees (west is negative)
        std::string country_code;   // ISO 3166-1 alpha-2 country code
        std::string tz_code;        // Time zone database identifier (e.g. "America/New_York")
        std::string tz_name;        // Human-readable label (e.g. "United States - Eastern Time")
    };

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

    class TimeZone
    {
    public:
        /**
         * @brief Set the current time zone offset for the application. This is a global setting that affects all time
         * calculations.
         * @param offset_hours UTC offset in fractional hours (e.g. 5.5 for +05:30)
         */
        static void SetTimeZoneOffsetHours(float offset_hours);

        /**
         * @brief Get the current time zone offset for the application.
         * @return UTC offset in fractional hours
         * (e.g. 5.5 for +05:30)
         */
        static float GetTimeZoneOffsetHours();

        /**
         * @brief Get the time zone name for a given UTC offset.
         * @param offset_hours UTC offset in fractional hours
         * (e.g. 5.5 for +05:30)
         * @return Time zone name
         */
        static std::string GetTimeZoneName(float offset_hours);

        /**
         * @brief Get the time zone name for the current UTC offset.
         * @return Time zone name
         */
        static std::string GetTimeZoneName();

        /**
         * @brief Set the current location for the player position. 
         * This is a global setting that affects all time calculations.
         * @param latitude Decimal degrees (south is negative)
         * @param longitude Decimal
         * degrees (west is negative)
         */
        static void SetLocation(float latitude, float longitude);

        /**
         * @brief Check if the current location is set for the player position.
         * @return True if the location is set, false
         * otherwise.
         */
        static bool HasLocation();

        /**
         * @brief Get the current location for the player position.
         * @param latitude Decimal degrees (south is
         * negative)
         * @param longitude Decimal degrees (west is negative)
         */
        static void GetLocation(float& latitude, float& longitude);

        /**
         * @brief Get the time zone information for a given zone code.
         * @param zone_code Time zone database identifier
         * (e.g. "America/New_York")
         * @param info Time zone information
         * @return True if the time zone
         * information was successfully retrieved, false otherwise
         */
        static bool GetTimeZoneInfoByZoneCode(const std::string& zone_code, TimeZoneInfo& info);

        /**
         * @brief Get the time zone information for a given country code.
         * @param country_code ISO 3166-1 alpha-2
         * country code for the United States ("US"), India ("IN"), etc.
         * @param info Time zone information
         * @return True if the time zone information was successfully retrieved, false otherwise
         */
        static bool GetTimeZoneInfoByCountryCode(const std::string& country_code, TimeZoneInfo& info);

        /**
         * @brief Get the time zones for a given country code.
         * @param country_code ISO 3166-1 alpha-2 country code,
         * (e.g. "US" for United States)
         * @return A vector of TimeZoneInfo structures for the specified country
         */
        static std::vector<TimeZoneInfo> GetTimeZonesByCountryCode(const std::string& country_code);

        /**
         * @brief Get the time zones for a given UTC offset.
         * @param offset_hours UTC offset in fractional hours
         * (e.g. 5.5 for +05:30)
         * @param epsilon Tolerance for matching the offset
         * @return A vector of
         * TimeZoneInfo structures for the specified offset
         */
        static std::vector<TimeZoneInfo> GetTimeZonesByOffsetHours(float offset_hours, float epsilon = 0.01f);

        /**
         * @brief Get the time zone information for a given location.
         * @param latitude Decimal degrees (south is
         * negative)
         * @param longitude Decimal degrees (west is negative)
         * @param info Time zone information

         * * @return True if the time zone information was successfully retrieved, false otherwise
         */
        static bool GetTimeZoneInfoByLocation(float latitude, float longitude, TimeZoneInfo& info);

        /**
         * @brief Parse a time zone offset from a string.
         * @param text The string containing the time zone offset (e.g.
         * "+05:30")
         * @param offset_hours The parsed UTC offset in fractional hours
         * @return True if the
         * parsing was successful, false otherwise
         */
        static bool ParseTimeZoneOffsetHours(const std::string& text, float& offset_hours);

        // find the first entry whose tz_code matches exactly (nullptr if not found)
        static const TimeZoneDbEntry* FindByZoneCode(const char* zone_code);

        // find the first entry whose country_code matches exactly (nullptr if not found)
        static const TimeZoneDbEntry* FindByCountryCode(const char* cc);

        // return all entries that share the given UTC offset (within epsilon)
        static std::vector<const TimeZoneDbEntry*> FindByOffset(float offset_hours, float epsilon = 0.01f);

        // return all entries for a given country code
        static std::vector<const TimeZoneDbEntry*> FindAllByCountry(const char* cc);

        // find the entry closest to a given lat/lon (Euclidean approximation)
        static const TimeZoneDbEntry* FindNearest(float latitude, float longitude);


    private:
        /**
         * @brief Get the singleton instance of the TimeZone class.
         * @return Reference to the TimeZone instance
 */
        static TimeZone& Instance();

        float m_time_offset = 0.0f;
        bool m_has_location = false;
        float m_latitude = 0.0f;
        float m_longitude = 0.0f;
    };

}  // namespace spartan
