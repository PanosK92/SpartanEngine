/*
    Astronomy Engine for C/C++.
    https://github.com/cosinekitty/astronomy

    MIT License

    Copyright (c) 2019-2025 Don Cross <cosinekitty@gmail.com>

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/

#ifndef __ASTRONOMY_H
#define __ASTRONOMY_H

#include <stddef.h>     /* for size_t */

#ifdef __cplusplus
extern "C" {
#endif

/*---------- numeric constants ----------*/

/**
 * \def C_AUDAY
 * @brief The speed of light in AU/day.
 */
#define C_AUDAY     173.1446326846693

/**
 * \def KM_PER_AU
 * @brief The number of kilometers in one astronomical unit (AU).
 */
#define KM_PER_AU   1.4959787069098932e+8

/**
 * \def AU_PER_LY
 * @brief The number of astronomical units per light-year.
 */
#define AU_PER_LY   63241.07708807546

/**
 * \def DEG2RAD
 * @brief The factor to convert degrees to radians = pi/180.
 */
#define DEG2RAD     0.017453292519943296

/**
 * \def HOUR2RAD
 * @brief The factor to convert sidereal hours to radians = pi/12.
 */
#define HOUR2RAD    0.2617993877991494365

/**
 * \def RAD2DEG
 * @brief The factor to convert radians to degrees = 180/pi.
 */
#define RAD2DEG     57.295779513082321

/**
 * \def RAD2HOUR
 * @brief The factor to convert radians to sidereal hours = 12/pi.
 */
#define RAD2HOUR    3.819718634205488

/**
 * \def SUN_RADIUS_KM
 * @brief The mean radius of the Sun's photosphere, expressed in kilometers. The Sun is nearly spherical.
 */
#define SUN_RADIUS_KM               695700.0

/**
 * \def MERCURY_EQUATORIAL_RADIUS_KM
 * @brief The equatorial radius of Mercury, expressed in kilometers.
 */
#define MERCURY_EQUATORIAL_RADIUS_KM    2440.5

/**
 * \def MERCURY_POLAR_RADIUS_KM
 * @brief The polar radius of Mercury, expressed in kilometers.
 */
#define MERCURY_POLAR_RADIUS_KM         2438.3

/**
 * \def VENUS_RADIUS_KM
 * @brief The mean radius of Venus, expressed in kilometers. Venus is nearly spherical.
 */
#define VENUS_RADIUS_KM     6051.8

/**
 * \def EARTH_EQUATORIAL_RADIUS_KM
 * @brief The equatorial radius of the Earth, expressed in kilometers.
 */
#define EARTH_EQUATORIAL_RADIUS_KM  6378.1366

/**
 * \def EARTH_FLATTENING
 * @brief The Earth's polar radius divided by its equatorial radius.
 */
#define EARTH_FLATTENING            0.996647180302104

/**
 * \def EARTH_POLAR_RADIUS_KM
 * @brief The polar radius of the Earth, expressed in kilometers.
 */
#define EARTH_POLAR_RADIUS_KM       (EARTH_EQUATORIAL_RADIUS_KM * EARTH_FLATTENING)

/**
 * \def MOON_EQUATORIAL_RADIUS_KM
 * @brief The equatorial radius of the Moon, expressed in kilometers.
 */
#define MOON_EQUATORIAL_RADIUS_KM   1738.1

/**
 * \def MOON_POLAR_RADIUS_KM
 * @brief The polar radius of the Moon, expressed in kilometers.
 */
#define MOON_POLAR_RADIUS_KM        1736.0

/**
 * \def MARS_EQUATORIAL_RADIUS_KM
 * @brief The equatorial radius of Mars, expressed in kilometers.
 */
#define MARS_EQUATORIAL_RADIUS_KM   3396.2

/**
 * \def MARS_POLAR_RADIUS_KM
 * @brief The polar radius of Mars, expressed in kilometers.
 */
#define MARS_POLAR_RADIUS_KM        3376.2

/*
    Jupiter radius data are nominal values obtained from:
    https://www.iau.org/static/resolutions/IAU2015_English.pdf
    https://nssdc.gsfc.nasa.gov/planetary/factsheet/jupiterfact.html
*/

/**
 * \def JUPITER_EQUATORIAL_RADIUS_KM
 * @brief The equatorial radius of Jupiter, expressed in kilometers.
 */
#define JUPITER_EQUATORIAL_RADIUS_KM    71492.0

/**
 * \def JUPITER_POLAR_RADIUS_KM
 * @brief The polar radius of Jupiter, expressed in kilometers.
 */
#define JUPITER_POLAR_RADIUS_KM         66854.0

/**
 * \def JUPITER_MEAN_RADIUS_KM
 * @brief The volumetric mean radius of Jupiter, expressed in kilometers.
 */
#define JUPITER_MEAN_RADIUS_KM          69911.0

/*
    The radii of Jupiter's four major moons are obtained from:
    https://ssd.jpl.nasa.gov/?sat_phys_par
*/

/**
 * \def IO_RADIUS_KM
 * @brief The mean radius of Jupiter's moon Io, expressed in kilometers.
 */
#define IO_RADIUS_KM            1821.6

/**
 * \def EUROPA_RADIUS_KM
 * @brief The mean radius of Jupiter's moon Europa, expressed in kilometers.
 */
#define EUROPA_RADIUS_KM        1560.8

/**
 * \def GANYMEDE_RADIUS_KM
 * @brief The mean radius of Jupiter's moon Ganymede, expressed in kilometers.
 */
#define GANYMEDE_RADIUS_KM      2631.2

/**
 * \def CALLISTO_RADIUS_KM
 * @brief The mean radius of Jupiter's moon Callisto, expressed in kilometers.
 */
#define CALLISTO_RADIUS_KM      2410.3

/**
 * \def SATURN_EQUATORIAL_RADIUS_KM
 * @brief The equatorial radius of Saturn, expressed in kilometers.
 */
#define SATURN_EQUATORIAL_RADIUS_KM     60268.0

/**
 * \def SATURN_POLAR_RADIUS_KM
 * @brief The polar radius of Saturn, expressed in kilometers.
 */
#define SATURN_POLAR_RADIUS_KM          54364.0

/**
 * \def URANUS_EQUATORIAL_RADIUS_KM
 * @brief The equatorial radius of Uranus, expressed in kilometers.
 */
#define URANUS_EQUATORIAL_RADIUS_KM     25559.0

/**
 * \def URANUS_POLAR_RADIUS_KM
 * @brief The polar radius of Uranus, expressed in kilometers.
 */
#define URANUS_POLAR_RADIUS_KM          24973.0

/**
 * \def NEPTUNE_EQUATORIAL_RADIUS_KM
 * @brief The equatorial radius of Neptune, expressed in kilometers.
 */
#define NEPTUNE_EQUATORIAL_RADIUS_KM    24764.0

/**
 * \def NEPTUNE_POLAR_RADIUS_KM
 * @brief The polar radius of Neptune, expressed in kilometers.
 */
#define NEPTUNE_POLAR_RADIUS_KM         24341.0

/**
 * \def PLUTO_RADIUS_KM
 * @brief The mean radius of Pluto, expressed in kilometers. Pluto is nearly spherical.
 */
#define PLUTO_RADIUS_KM             1188.3


/*---------- types ----------*/

/**
 * @brief Indicates success/failure of an Astronomy Engine function call.
 */
typedef enum
{
    ASTRO_SUCCESS,                  /**< The operation was successful. */
    ASTRO_NOT_INITIALIZED,          /**< A placeholder that can be used for data that is not yet initialized. */
    ASTRO_INVALID_BODY,             /**< The celestial body was not valid. Different sets of bodies are supported depending on the function. */
    ASTRO_NO_CONVERGE,              /**< A numeric solver failed to converge. This should not happen unless there is a bug in Astronomy Engine. */
    ASTRO_BAD_TIME,                 /**< The provided date/time is outside the range allowed by this function. */
    ASTRO_BAD_VECTOR,               /**< Vector magnitude is too small to be normalized into a unit vector. */
    ASTRO_SEARCH_FAILURE,           /**< Search was not able to find an ascending root crossing of the function in the specified time interval. */
    ASTRO_EARTH_NOT_ALLOWED,        /**< The Earth cannot be treated as a celestial body seen from an observer on the Earth itself. */
    ASTRO_NO_MOON_QUARTER,          /**< No lunar quarter occurs inside the specified time range. */
    ASTRO_WRONG_MOON_QUARTER,       /**< Internal error: Astronomy_NextMoonQuarter found the wrong moon quarter. */
    ASTRO_INTERNAL_ERROR,           /**< A self-check failed inside the code somewhere, indicating a bug needs to be fixed. */
    ASTRO_INVALID_PARAMETER,        /**< A parameter value passed to a function was not valid. */
    ASTRO_FAIL_APSIS,               /**< Special-case logic for finding Neptune/Pluto apsis failed. */
    ASTRO_BUFFER_TOO_SMALL,         /**< A provided buffer's size is too small to receive the requested data. */
    ASTRO_OUT_OF_MEMORY,            /**< An attempt to allocate memory failed. */
    ASTRO_INCONSISTENT_TIMES        /**< The provided initial state vectors did not have matching times. */
}
astro_status_t;

/**
 * @brief A date and time used for astronomical calculations.
 *
 * This type is of fundamental importance to Astronomy Engine.
 * It is used to represent dates and times for all astronomical calculations.
 * It is also included in the values returned by many Astronomy Engine functions.
 *
 * To create a valid astro_time_t value from scratch, call #Astronomy_MakeTime
 * (for a given calendar date and time) or #Astronomy_CurrentTime (for the system's
 * current date and time).
 *
 * To adjust an existing astro_time_t by a certain real number of days,
 * call #Astronomy_AddDays.
 *
 * The astro_time_t type contains `ut` to represent Universal Time (UT1/UTC) and
 * `tt` to represent Terrestrial Time (TT, also known as *ephemeris time*).
 * The difference `tt-ut` is known as *&Delta;T*, using a best-fit piecewise model devised by
 * [Espenak and Meeus](https://eclipse.gsfc.nasa.gov/SEhelp/deltatpoly2004.html).
 *
 * Both `tt` and `ut` are necessary for performing different astronomical calculations.
 * Indeed, certain calculations (such as rise/set times) require both time scales.
 * See the documentation for the `ut` and `tt` fields for more detailed information.
 *
 * In cases where astro_time_t is included in a structure returned by
 * a function that can fail, the #astro_status_t field `status` will contain a value
 * other than `ASTRO_SUCCESS`; in that case the `ut` and `tt` will hold `NAN` (not a number).
 * In general, when there is an error code stored in a struct field `status`, the
 * caller should ignore all other values in that structure, including the `ut` and `tt`
 * inside `astro_time_t`.
 */
typedef struct
{
    /**
     * @brief   UT1/UTC number of days since noon on January 1, 2000.
     *
     * The floating point number of days of Universal Time since noon UTC January 1, 2000.
     * Astronomy Engine approximates UTC and UT1 as being the same thing, although they are
     * not exactly equivalent; UTC and UT1 can disagree by up to &plusmn;0.9 seconds.
     * This approximation is sufficient for the accuracy requirements of Astronomy Engine.
     *
     * Universal Time Coordinate (UTC) is the international standard for legal and civil
     * timekeeping and replaces the older Greenwich Mean Time (GMT) standard.
     * UTC is kept in sync with unpredictable observed changes in the Earth's rotation
     * by occasionally adding leap seconds as needed.
     *
     * UT1 is an idealized time scale based on observed rotation of the Earth, which
     * gradually slows down in an unpredictable way over time, due to tidal drag by the Moon and Sun,
     * large scale weather events like hurricanes, and internal seismic and convection effects.
     * Conceptually, UT1 drifts from atomic time continuously and erratically, whereas UTC
     * is adjusted by a scheduled whole number of leap seconds as needed.
     *
     * The value in `ut` is appropriate for any calculation involving the Earth's rotation,
     * such as calculating rise/set times, culumination, and anything involving apparent
     * sidereal time.
     *
     * Before the era of atomic timekeeping, days based on the Earth's rotation
     * were often known as *mean solar days*.
     */
    double ut;

    /**
     * @brief   Terrestrial Time days since noon on January 1, 2000.
     *
     * Terrestrial Time is an atomic time scale defined as a number of days since noon on January 1, 2000.
     * In this system, days are not based on Earth rotations, but instead by
     * the number of elapsed [SI seconds](https://physics.nist.gov/cuu/Units/second.html)
     * divided by 86400. Unlike `ut`, `tt` increases uniformly without adjustments
     * for changes in the Earth's rotation.
     *
     * The value in `tt` is used for calculations of movements not involving the Earth's rotation,
     * such as the orbits of planets around the Sun, or the Moon around the Earth.
     *
     * Historically, Terrestrial Time has also been known by the term *Ephemeris Time* (ET).
     */
    double tt;

    /**
     * @brief   For internal use only. Used to optimize Earth tilt calculations.
     */
    double psi;

    /**
     * @brief   For internal use only.  Used to optimize Earth tilt calculations.
     */
    double eps;

    /**
     * @brief   For internal use only.  Lazy-caches sidereal time (Earth rotation).
     */
    double st;
}
astro_time_t;

/**
 * @brief A calendar date and time expressed in UTC.
 */
typedef struct
{
    int     year;       /**< The year value, e.g. 2019. */
    int     month;      /**< The month value: 1=January, 2=February, ..., 12=December. */
    int     day;        /**< The day of the month in the range 1..31. */
    int     hour;       /**< The hour of the day in the range 0..23. */
    int     minute;     /**< The minute of the hour in the range 0..59. */
    double  second;     /**< The floating point number of seconds in the range [0,60). */
}
astro_utc_t;

/**
 * @brief A 3D Cartesian vector whose components are expressed in Astronomical Units (AU).
 */
typedef struct
{
    astro_status_t status;  /**< `ASTRO_SUCCESS` if this struct is valid; otherwise an error code. */
    double x;               /**< The Cartesian x-coordinate of the vector in AU. */
    double y;               /**< The Cartesian y-coordinate of the vector in AU. */
    double z;               /**< The Cartesian z-coordinate of the vector in AU. */
    astro_time_t t;         /**< The date and time at which this vector is valid. */
}
astro_vector_t;

/**
 * @brief A state vector that contains a position (AU) and velocity (AU/day).
 */
typedef struct
{
    astro_status_t status;  /**< `ASTRO_SUCCESS` if this struct is valid; otherwise an error code. */
    double x;               /**< The Cartesian position x-coordinate of the vector in AU. */
    double y;               /**< The Cartesian position y-coordinate of the vector in AU. */
    double z;               /**< The Cartesian position z-coordinate of the vector in AU. */
    double vx;              /**< The Cartesian velocity x-coordinate of the vector in AU/day. */
    double vy;              /**< The Cartesian velocity y-coordinate of the vector in AU/day. */
    double vz;              /**< The Cartesian velocity z-coordinate of the vector in AU/day. */
    astro_time_t t;         /**< The date and time at which this state vector is valid. */
}
astro_state_vector_t;

/**
 * @brief Spherical coordinates: latitude, longitude, distance.
 */
typedef struct
{
    astro_status_t status;  /**< `ASTRO_SUCCESS` if this struct is valid; otherwise an error code. */
    double lat;             /**< The latitude angle: -90..+90 degrees. */
    double lon;             /**< The longitude angle: 0..360 degrees. */
    double dist;            /**< Distance in AU. */
}
astro_spherical_t;

/**
 * @brief An angular value expressed in degrees.
 */
typedef struct
{
    astro_status_t status;  /**< `ASTRO_SUCCESS` if this struct is valid; otherwise an error code. */
    double angle;           /**< An angle expressed in degrees. */
}
astro_angle_result_t;

/**
 * @brief A celestial body.
 */
typedef enum
{
    BODY_INVALID = -1,      /**< An invalid or undefined celestial body. */
    BODY_MERCURY,           /**< Mercury */
    BODY_VENUS,             /**< Venus */
    BODY_EARTH,             /**< Earth */
    BODY_MARS,              /**< Mars */
    BODY_JUPITER,           /**< Jupiter */
    BODY_SATURN,            /**< Saturn */
    BODY_URANUS,            /**< Uranus */
    BODY_NEPTUNE,           /**< Neptune */
    BODY_PLUTO,             /**< Pluto */
    BODY_SUN,               /**< Sun */
    BODY_MOON,              /**< Moon */
    BODY_EMB,               /**< Earth/Moon Barycenter */
    BODY_SSB,               /**< Solar System Barycenter */
    BODY_STAR1 = 101,       /**< user-defined star #1 */
    BODY_STAR2,             /**< user-defined star #2 */
    BODY_STAR3,             /**< user-defined star #3 */
    BODY_STAR4,             /**< user-defined star #4 */
    BODY_STAR5,             /**< user-defined star #5 */
    BODY_STAR6,             /**< user-defined star #6 */
    BODY_STAR7,             /**< user-defined star #7 */
    BODY_STAR8,             /**< user-defined star #8 */
}
astro_body_t;

/**
 * @brief The location of an observer on (or near) the surface of the Earth.
 *
 * This structure is passed to functions that calculate phenomena as observed
 * from a particular place on the Earth.
 *
 * You can create this structure directly, or you can call the convenience function
 * #Astronomy_MakeObserver to create one for you.
 */
typedef struct
{
    double latitude;        /**< Geographic latitude in degrees north (positive) or south (negative) of the equator. */
    double longitude;       /**< Geographic longitude in degrees east (positive) or west (negative) of the prime meridian at Greenwich, England. */
    double height;          /**< The height above (positive) or below (negative) sea level, expressed in meters. */
}
astro_observer_t;

/**
 * @brief Equatorial angular and cartesian coordinates.
 *
 * Coordinates of a celestial body as seen from the Earth (geocentric or topocentric, depending on context),
 * oriented with respect to the projection of the Earth's equator onto the sky.
 */
typedef struct
{
    astro_status_t status;  /**< `ASTRO_SUCCESS` if this struct is valid; otherwise an error code. */
    double ra;              /**< right ascension in sidereal hours. */
    double dec;             /**< declination in degrees */
    double dist;            /**< distance to the celestial body in AU. */
    astro_vector_t vec;     /**< equatorial coordinates in cartesian vector form: x = March equinox, y = June solstice, z = north. */
}
astro_equatorial_t;

/**
 * @brief Ecliptic angular and Cartesian coordinates.
 *
 * Coordinates of a celestial body as seen from the center of the Sun (heliocentric),
 * oriented with respect to the plane of the Earth's orbit around the Sun (the ecliptic).
 */
typedef struct
{
    astro_status_t status;  /**< `ASTRO_SUCCESS` if this struct is valid; otherwise an error code. */
    astro_vector_t vec;     /**< Cartesian ecliptic vector: x=equinox, y=90 degrees prograde in ecliptic plane, z=northward perpendicular to ecliptic. */
    double elat;            /**< Latitude in degrees north (positive) or south (negative) of the ecliptic plane. */
    double elon;            /**< Longitude in degrees around the ecliptic plane prograde from the equinox. */
}
astro_ecliptic_t;

/**
 * @brief Coordinates of a celestial body as seen by a topocentric observer.
 *
 * Contains horizontal and equatorial coordinates seen by an observer on or near
 * the surface of the Earth (a topocentric observer).
 * Optionally corrected for atmospheric refraction.
 */
typedef struct
{
    double azimuth;     /**< Compass direction around the horizon in degrees. 0=North, 90=East, 180=South, 270=West. */
    double altitude;    /**< Angle in degrees above (positive) or below (negative) the observer's horizon. */
    double ra;          /**< Right ascension in sidereal hours. */
    double dec;         /**< Declination in degrees. */
}
astro_horizon_t;

/**
 * @brief Contains a rotation matrix that can be used to transform one coordinate system to another.
 */
typedef struct
{
    astro_status_t status;  /**< `ASTRO_SUCCESS` if this struct is valid; otherwise an error code. */
    double rot[3][3];       /**< A normalized 3x3 rotation matrix. */
}
astro_rotation_t;

/**
 * @brief Selects whether to correct for atmospheric refraction, and if so, how.
 */
typedef enum
{
    REFRACTION_NONE,    /**< No atmospheric refraction correction (airless). */
    REFRACTION_NORMAL,  /**< Recommended correction for standard atmospheric refraction. */
    REFRACTION_JPLHOR   /**< Used only for compatibility testing with JPL Horizons online tool. */
}
astro_refraction_t;

/**
 * @brief Information about idealized atmospheric variables at a given elevation.
 */
typedef struct
{
    astro_status_t status;  /**< `ASTRO_SUCCESS` if this struct is valid; otherwise an error code. */
    double pressure;        /**< Atmospheric pressure in pascals */
    double temperature;     /**< Atmospheric temperature in kelvins */
    double density;         /**< Atmospheric density relative to sea level */
}
astro_atmosphere_t;

/**
 * @brief The result of a search for an astronomical event.
 */
typedef struct
{
    astro_status_t  status;     /**< `ASTRO_SUCCESS` if this struct is valid; otherwise an error code. */
    astro_time_t    time;       /**< The time at which a searched-for event occurs. */
}
astro_search_result_t;

/**
 * @brief
 *      The dates and times of changes of season for a given calendar year.
 *      Call #Astronomy_Seasons to calculate this data structure for a given year.
 */
typedef struct
{
    astro_status_t  status;         /**< `ASTRO_SUCCESS` if this struct is valid; otherwise an error code. */
    astro_time_t    mar_equinox;    /**< The date and time of the March equinox for the specified year. */
    astro_time_t    jun_solstice;   /**< The date and time of the June soltice for the specified year. */
    astro_time_t    sep_equinox;    /**< The date and time of the September equinox for the specified year. */
    astro_time_t    dec_solstice;   /**< The date and time of the December solstice for the specified year. */
}
astro_seasons_t;

/**
 * @brief A lunar quarter event (new moon, first quarter, full moon, or third quarter) along with its date and time.
 */
typedef struct
{
    astro_status_t  status;     /**< `ASTRO_SUCCESS` if this struct is valid; otherwise an error code. */
    int             quarter;    /**< 0=new moon, 1=first quarter, 2=full moon, 3=third quarter. */
    astro_time_t    time;       /**< The date and time of the lunar quarter. */
}
astro_moon_quarter_t;

/**
 * @brief A real value returned by a function whose ascending root is to be found.
 *
 * When calling #Astronomy_Search, the caller must pass in a callback function
 * compatible with the function-pointer type #astro_search_func_t
 * whose ascending root is to be found. That callback function must return astro_func_result_t.
 * If the function call is successful, it will set `status` to `ASTRO_SUCCESS` and `value`
 * to the numeric value appropriate for the given date and time.
 * If the call fails for some reason, it should set `status` to an appropriate error value
 * other than `ASTRO_SUCCESS`; in the error case, to guard against any possible misuse of `value`,
 * it is recommended to set `value` to `NAN`, though this is not strictly necessary.
 */
typedef struct
{
    astro_status_t status;      /**< `ASTRO_SUCCESS` if this struct is valid; otherwise an error code. */
    double value;               /**< The value returned by a function whose ascending root is to be found. */
}
astro_func_result_t;

/**
 * @brief A pointer to a function that is to be passed as a callback to #Astronomy_Search.
 *
 * The function #Astronomy_Search numerically solves for the time that a given event occurs.
 * An event is defined as the time when an arbitrary function transitions between having
 * a negative value and a non-negative value. This transition is called an *ascending root*.
 *
 * The type #astro_search_func_t represents such a callback function that accepts a
 * custom `context` pointer and an astro_time_t representing the time to probe.
 * The function returns an astro_func_result_t that contains either a real
 * number in `value` or an error code in `status` that aborts the search.
 *
 * The `context` points to some data whose type varies depending on the callback function.
 * It can contain any auxiliary parameters (other than time) needed to evaluate the function.
 * For example, a function may pertain to a specific celestial body, in which case `context`
 * may point to a value of type #astro_body_t. The `context` parameter is supplied by
 * the caller of #Astronomy_Search, which passes it along to every call to the callback function.
 * If the caller of `Astronomy_Search` knows that the callback function does not need a context,
 * it is safe to pass `NULL` as the context pointer.
 */
typedef astro_func_result_t (* astro_search_func_t) (void *context, astro_time_t time);

/**
 * @brief A pointer to a function that calculates Delta T.
 *
 * Delta T is the discrepancy between times measured using an atomic clock
 * and times based on observations of the Earth's rotation, which is gradually
 * slowing down over time. Delta T = TT - UT, where
 * TT = Terrestrial Time, based on atomic time, and
 * UT = Universal Time, civil time based on the Earth's rotation.
 * Astronomy Engine defaults to using a Delta T function defined by
 * Espenak and Meeus in their "Five Millennium Canon of Solar Eclipses".
 * See: https://eclipse.gsfc.nasa.gov/SEhelp/deltatpoly2004.html
 */
typedef double (* astro_deltat_func) (double ut);

double Astronomy_DeltaT_EspenakMeeus(double ut);
double Astronomy_DeltaT_JplHorizons(double ut);

void Astronomy_SetDeltaTFunction(astro_deltat_func func);

/**
 * @brief Indicates whether a body (especially Mercury or Venus) is best seen in the morning or evening.
 */
typedef enum
{
    VISIBLE_MORNING,    /**< The body is best visible in the morning, before sunrise. */
    VISIBLE_EVENING     /**< The body is best visible in the evening, after sunset. */
}
astro_visibility_t;

/**
 * @brief
 *      Contains information about the visibility of a celestial body at a given date and time.
 *      See #Astronomy_Elongation for more detailed information about the members of this structure.
 *      See also #Astronomy_SearchMaxElongation for how to search for maximum elongation events.
 */
typedef struct
{
    astro_status_t      status;                 /**< `ASTRO_SUCCESS` if this struct is valid; otherwise an error code. */
    astro_time_t        time;                   /**< The date and time of the observation. */
    astro_visibility_t  visibility;             /**< Whether the body is best seen in the morning or the evening. */
    double              elongation;             /**< The angle in degrees between the body and the Sun, as seen from the Earth. */
    double              ecliptic_separation;    /**< The difference between the ecliptic longitudes of the body and the Sun, as seen from the Earth. */
}
astro_elongation_t;

/**
 * @brief Information about a celestial body crossing a specific hour angle.
 *
 * Returned by the function #Astronomy_SearchHourAngleEx to report information about
 * a celestial body crossing a certain hour angle as seen by a specified topocentric observer.
 */
typedef struct
{
    astro_status_t      status;     /**< `ASTRO_SUCCESS` if this struct is valid; otherwise an error code. */
    astro_time_t        time;       /**< The date and time when the body crosses the specified hour angle. */
    astro_horizon_t     hor;        /**< Apparent coordinates of the body at the time it crosses the specified hour angle. */
}
astro_hour_angle_t;

/**
 * @brief Information about the brightness and illuminated shape of a celestial body.
 *
 * Returned by the functions #Astronomy_Illumination and #Astronomy_SearchPeakMagnitude
 * to report the visual magnitude and illuminated fraction of a celestial body at a given date and time.
 */
typedef struct
{
    astro_status_t      status;         /**< `ASTRO_SUCCESS` if this struct is valid; otherwise an error code. */
    astro_time_t        time;           /**< The date and time of the observation. */
    double              mag;            /**< The visual magnitude of the body. Smaller values are brighter. */
    double              phase_angle;    /**< The angle in degrees between the Sun and the Earth, as seen from the body. Indicates the body's phase as seen from the Earth. */
    double              phase_fraction; /**< A value in the range [0.0, 1.0] indicating what fraction of the body's apparent disc is illuminated, as seen from the Earth. */
    double              helio_dist;     /**< The distance between the Sun and the body at the observation time. */
    double              ring_tilt;      /**< For Saturn, the tilt angle in degrees of its rings as seen from Earth. For all other bodies, 0. */
}
astro_illum_t;

/**
 * @brief The type of apsis: pericenter (closest approach) or apocenter (farthest distance).
 */
typedef enum
{
    APSIS_PERICENTER,   /**< The body is at its closest approach to the object it orbits. */
    APSIS_APOCENTER,    /**< The body is at its farthest distance from the object it orbits. */
    APSIS_INVALID       /**< Undefined or invalid apsis. */
}
astro_apsis_kind_t;

/**
 * @brief An apsis event: pericenter (closest approach) or apocenter (farthest distance).
 *
 * For the Moon orbiting the Earth, or a planet orbiting the Sun, an *apsis* is an
 * event where the orbiting body reaches its closest or farthest point from the primary body.
 * The closest approach is called *pericenter* and the farthest point is *apocenter*.
 *
 * More specific terminology is common for particular orbiting bodies.
 * The Moon's closest approach to the Earth is called *perigee* and its farthest
 * point is called *apogee*. The closest approach of a planet to the Sun is called
 * *perihelion* and the furthest point is called *aphelion*.
 *
 * This data structure is returned by #Astronomy_SearchLunarApsis and #Astronomy_NextLunarApsis
 * to iterate through consecutive alternating perigees and apogees.
 */
typedef struct
{
    astro_status_t      status;     /**< `ASTRO_SUCCESS` if this struct is valid; otherwise an error code. */
    astro_time_t        time;       /**< The date and time of the apsis. */
    astro_apsis_kind_t  kind;       /**< Whether this is a pericenter or apocenter event. */
    double              dist_au;    /**< The distance between the centers of the bodies in astronomical units. */
    double              dist_km;    /**< The distance between the centers of the bodies in kilometers. */
}
astro_apsis_t;

/**
 * @brief The different kinds of lunar/solar eclipses.
 */
typedef enum
{
    ECLIPSE_NONE,       /**< No eclipse found. */
    ECLIPSE_PENUMBRAL,  /**< A penumbral lunar eclipse. (Never used for a solar eclipse.) */
    ECLIPSE_PARTIAL,    /**< A partial lunar/solar eclipse. */
    ECLIPSE_ANNULAR,    /**< An annular solar eclipse. (Never used for a lunar eclipse.) */
    ECLIPSE_TOTAL       /**< A total lunar/solar eclipse. */
}
astro_eclipse_kind_t;

/**
 * @brief Information about a lunar eclipse.
 *
 * Returned by #Astronomy_SearchLunarEclipse or #Astronomy_NextLunarEclipse
 * to report information about a lunar eclipse event.
 * If a lunar eclipse is found, `status` holds `ASTRO_SUCCESS` and the other fields are set.
 * If `status` holds any other value, it is an error code and the other fields are undefined.
 *
 * When a lunar eclipse is found, it is classified as penumbral, partial, or total.
 * Penumbral eclipses are difficult to observe, because the Moon is only slightly dimmed
 * by the Earth's penumbra; no part of the Moon touches the Earth's umbra.
 * Partial eclipses occur when part, but not all, of the Moon touches the Earth's umbra.
 * Total eclipses occur when the entire Moon passes into the Earth's umbra.
 *
 * The `kind` field thus holds `ECLIPSE_PENUMBRAL`, `ECLIPSE_PARTIAL`, or `ECLIPSE_TOTAL`,
 * depending on the kind of lunar eclipse found.
 *
 * The `obscuration` field holds a value in the range [0, 1] that indicates what fraction
 * of the Moon's apparent disc area is covered by the Earth's umbra at the eclipse's peak.
 * This indicates how dark the peak eclipse appears. For penumbral eclipses, the obscuration
 * is 0, because the Moon does not pass through the Earth's umbra. For partial eclipses,
 * the obscuration is somewhere between 0 and 1. For total lunar eclipses, the obscuration is 1.
 *
 * Field `peak` holds the date and time of the center of the eclipse, when it is at its peak.
 *
 * Fields `sd_penum`, `sd_partial`, and `sd_total` hold the semi-duration of each phase
 * of the eclipse, which is half of the amount of time the eclipse spends in each
 * phase (expressed in minutes), or 0 if the eclipse never reaches that phase.
 * By converting from minutes to days, and subtracting/adding with `center`, the caller
 * may determine the date and time of the beginning/end of each eclipse phase.
 */
typedef struct
{
    astro_status_t          status;         /**< `ASTRO_SUCCESS` if this struct is valid; otherwise an error code. */
    astro_eclipse_kind_t    kind;           /**< The type of lunar eclipse found. */
    double                  obscuration;    /**< The peak fraction of the Moon's apparent disc that is covered by the Earth's umbra. */
    astro_time_t            peak;           /**< The time of the eclipse at its peak. */
    double                  sd_penum;       /**< The semi-duration of the penumbral phase in minutes. */
    double                  sd_partial;     /**< The semi-duration of the partial phase in minutes, or 0.0 if none. */
    double                  sd_total;       /**< The semi-duration of the total phase in minutes, or 0.0 if none. */
}
astro_lunar_eclipse_t;


/**
 * @brief Reports the time and geographic location of the peak of a solar eclipse.
 *
 * Returned by #Astronomy_SearchGlobalSolarEclipse or #Astronomy_NextGlobalSolarEclipse
 * to report information about a solar eclipse event.
 * If a solar eclipse is found, `status` holds `ASTRO_SUCCESS` and `kind`, `peak`, and `distance`
 * have valid values. The `latitude` and `longitude` are set only for total and annular eclipses
 * (see more below).
 * If `status` holds any value other than `ASTRO_SUCCESS`, it is an error code;
 * in that case, `kind` holds `ECLIPSE_NONE` and all the other fields are undefined.
 *
 * The eclipse is classified as partial, annular, or total, depending on the
 * maximum amount of the Sun's disc obscured, as seen at the peak location
 * on the surface of the Earth.
 *
 * The `kind` field thus holds `ECLIPSE_PARTIAL`, `ECLIPSE_ANNULAR`, or `ECLIPSE_TOTAL`.
 * A total eclipse is when the peak observer sees the Sun completely blocked by the Moon.
 * An annular eclipse is like a total eclipse, but the Moon is too far from the Earth's surface
 * to completely block the Sun; instead, the Sun takes on a ring-shaped appearance.
 * A partial eclipse is when the Moon blocks part of the Sun's disc, but nobody on the Earth
 * observes either a total or annular eclipse.
 *
 * If `kind` is `ECLIPSE_TOTAL` or `ECLIPSE_ANNULAR`, the `latitude` and `longitude`
 * fields give the geographic coordinates of the center of the Moon's shadow projected
 * onto the daytime side of the Earth at the instant of the eclipse's peak.
 * If `kind` has any other value, `latitude` and `longitude` are undefined and should
 * not be used.
 *
 * For total or annular eclipses, the `obscuration` field holds the fraction (0, 1]
 * of the Sun's apparent disc area that is blocked from view by the Moon's silhouette,
 * as seen by an observer located at the geographic coordinates `latitude`, `longitude`
 * at the darkest time `peak`. The value will always be 1 for total eclipses, and less than
 * 1 for annular eclipses.
 * For partial eclipses, `obscuration` is undefined and should not be used.
 * This is because there is little practical use for an obscuration value of
 * a partial eclipse without supplying a particular observation location.
 * Developers who wish to find an obscuration value for partial solar eclipses should therefore use
 * #Astronomy_SearchLocalSolarEclipse and provide the geographic coordinates of an observer.
 */
typedef struct
{
    astro_status_t          status;         /**< `ASTRO_SUCCESS` if this struct is valid; otherwise an error code. */
    astro_eclipse_kind_t    kind;           /**< The type of solar eclipse found. */
    double                  obscuration;    /**< The peak fraction of the Sun's apparent disc area obscured by the Moon (total and annular eclipses only). */
    astro_time_t            peak;           /**< The date and time when the solar eclipse is darkest. This is the instant when the axis of the Moon's shadow cone passes closest to the Earth's center. */
    double                  distance;       /**< The distance between the Sun/Moon shadow axis and the center of the Earth, in kilometers. */
    double                  latitude;       /**< The geographic latitude at the center of the peak eclipse shadow. */
    double                  longitude;      /**< The geographic longitude at the center of the peak eclipse shadow. */
}
astro_global_solar_eclipse_t;


/**
 * @brief Holds a time and the observed altitude of the Sun at that time.
 *
 * When reporting a solar eclipse observed at a specific location on the Earth
 * (a "local" solar eclipse), a series of events occur. In addition
 * to the time of each event, it is important to know the altitude of the Sun,
 * because each event may be invisible to the observer if the Sun is below
 * the horizon.
 *
 * If `altitude` is negative, the event is theoretical only; it would be
 * visible if the Earth were transparent, but the observer cannot actually see it.
 * If `altitude` is positive but less than a few degrees, visibility will be impaired by
 * atmospheric interference (sunrise or sunset conditions).
 */
typedef struct
{
    astro_time_t    time;       /**< The date and time of the event. */
    double          altitude;   /**< The angular altitude of the center of the Sun above/below the horizon, at `time`, corrected for atmospheric refraction and expressed in degrees. */
}
astro_eclipse_event_t;


/**
 * @brief Information about a solar eclipse as seen by an observer at a given time and geographic location.
 *
 * Returned by #Astronomy_SearchLocalSolarEclipse or #Astronomy_NextLocalSolarEclipse
 * to report information about a solar eclipse as seen at a given geographic location.
 * If a solar eclipse is found, `status` holds `ASTRO_SUCCESS` and the other fields are set.
 * If `status` holds any other value, it is an error code and the other fields are undefined.
 *
 * When a solar eclipse is found, it is classified as partial, annular, or total.
 * The `kind` field thus holds `ECLIPSE_PARTIAL`, `ECLIPSE_ANNULAR`, or `ECLIPSE_TOTAL`.
 * A partial solar eclipse is when the Moon does not line up directly enough with the Sun
 * to completely block the Sun's light from reaching the observer.
 * An annular eclipse occurs when the Moon's disc is completely visible against the Sun
 * but the Moon is too far away to completely block the Sun's light; this leaves the
 * Sun with a ring-like appearance.
 * A total eclipse occurs when the Moon is close enough to the Earth and aligned with the
 * Sun just right to completely block all sunlight from reaching the observer.
 *
 * The `obscuration` field reports what fraction of the Sun's disc appears blocked
 * by the Moon when viewed by the observer at the peak eclipse time.
 * This is a value that ranges from 0 (no blockage) to 1 (total eclipse).
 * The obscuration value will be between 0 and 1 for partial eclipses and annular eclipses.
 * The value will be exactly 1 for total eclipses. Obscuration gives an indication
 * of how dark the eclipse appears.
 *
 * There are 5 "event" fields, each of which contains a time and a solar altitude.
 * Field `peak` holds the date and time of the center of the eclipse, when it is at its peak.
 * The fields `partial_begin` and `partial_end` are always set, and indicate when
 * the eclipse begins/ends. If the eclipse reaches totality or becomes annular,
 * `total_begin` and `total_end` indicate when the total/annular phase begins/ends.
 * When an event field is valid, the caller must also check its `altitude` field to
 * see whether the Sun is above the horizon at that time. See #astro_eclipse_kind_t
 * for more information.
 */
typedef struct
{
    astro_status_t          status;         /**< `ASTRO_SUCCESS` if this struct is valid; otherwise an error code. */
    astro_eclipse_kind_t    kind;           /**< The type of solar eclipse found: `ECLIPSE_PARTIAL`, `ECLIPSE_ANNULAR`, or `ECLIPSE_TOTAL`. */
    double                  obscuration;    /**< The fraction of the Sun's apparent disc area obscured by the Moon at the eclipse peak. */
    astro_eclipse_event_t   partial_begin;  /**< The time and Sun altitude at the beginning of the eclipse. */
    astro_eclipse_event_t   total_begin;    /**< If this is an annular or a total eclipse, the time and Sun altitude when annular/total phase begins; otherwise invalid. */
    astro_eclipse_event_t   peak;           /**< The time and Sun altitude when the eclipse reaches its peak. */
    astro_eclipse_event_t   total_end;      /**< If this is an annular or a total eclipse, the time and Sun altitude when annular/total phase ends; otherwise invalid. */
    astro_eclipse_event_t   partial_end;    /**< The time and Sun altitude at the end of the eclipse. */
}
astro_local_solar_eclipse_t;


/**
 * @brief Information about a transit of Mercury or Venus, as seen from the Earth.
 *
 * Returned by #Astronomy_SearchTransit or #Astronomy_NextTransit to report
 * information about a transit of Mercury or Venus.
 * A transit is when Mercury or Venus passes between the Sun and Earth so that
 * the other planet is seen in silhouette against the Sun.
 *
 * The `start` field reports the moment in time when the planet first becomes
 * visible against the Sun in its background.
 * The `peak` field reports when the planet is most aligned with the Sun,
 * as seen from the Earth.
 * The `finish` field reports the last moment when the planet is visible
 * against the Sun in its background.
 *
 * The calculations are performed from the point of view of a geocentric observer.
 */
typedef struct
{
    astro_status_t  status;         /**< `ASTRO_SUCCESS` if this struct is valid; otherwise an error code. */
    astro_time_t    start;          /**< Date and time at the beginning of the transit. */
    astro_time_t    peak;           /**< Date and time of the peak of the transit. */
    astro_time_t    finish;         /**< Date and time at the end of the transit. */
    double          separation;     /**< Angular separation in arcminutes between the centers of the Sun and the planet at time `peak`. */
}
astro_transit_t;


/**
 * @brief   Aberration calculation options.
 *
 * [Aberration](https://en.wikipedia.org/wiki/Aberration_of_light) is an effect
 * causing the apparent direction of an observed body to be shifted due to transverse
 * movement of the Earth with respect to the rays of light coming from that body.
 * This angular correction can be anywhere from 0 to about 20 arcseconds,
 * depending on the position of the observed body relative to the instantaneous
 * velocity vector of the Earth.
 *
 * Some Astronomy Engine functions allow optional correction for aberration by
 * passing in a value of this enumerated type.
 *
 * Aberration correction is useful to improve accuracy of coordinates of
 * apparent locations of bodies seen from the Earth.
 * However, because aberration affects not only the observed body (such as a planet)
 * but the surrounding stars, aberration may be unhelpful (for example)
 * for determining exactly when a planet crosses from one constellation to another.
 */
typedef enum
{
    ABERRATION,     /**< Request correction for aberration. */
    NO_ABERRATION   /**< Do not correct for aberration. */
}
astro_aberration_t;

/**
 * @brief   Selects the date for which the Earth's equator is to be used for representing equatorial coordinates.
 *
 * The Earth's equator is not always in the same plane due to precession and nutation.
 *
 * Sometimes it is useful to have a fixed plane of reference for equatorial coordinates
 * across different calendar dates.  In these cases, a fixed *epoch*, or reference time,
 * is helpful. Astronomy Engine provides the J2000 epoch for such cases.  This refers
 * to the plane of the Earth's orbit as it was on noon UTC on 1 January 2000.
 *
 * For some other purposes, it is more helpful to represent coordinates using the Earth's
 * equator exactly as it is on that date. For example, when calculating rise/set times
 * or horizontal coordinates, it is most accurate to use the orientation of the Earth's
 * equator at that same date and time. For these uses, Astronomy Engine allows *of-date*
 * calculations.
 */
typedef enum
{
    EQUATOR_J2000,      /**< Represent equatorial coordinates in the J2000 epoch. */
    EQUATOR_OF_DATE     /**< Represent equatorial coordinates using the Earth's equator at the given date and time. */
}
astro_equator_date_t;

/**
 * @brief Selects whether to search for a rise time or a set time.
 *
 * The #Astronomy_SearchRiseSetEx function finds the rise or set time of a body
 * depending on the value of its `direction` parameter.
 */
typedef enum
{
    DIRECTION_RISE = +1,    /**< Search for the time a body begins to rise above the horizon. */
    DIRECTION_SET  = -1,    /**< Search for the time a body finishes sinking below the horizon. */
}
astro_direction_t;


/**
 * @brief Reports the constellation that a given celestial point lies within.
 *
 * The #Astronomy_Constellation function returns this struct
 * to report which constellation corresponds with a given point in the sky.
 * Constellations are defined with respect to the B1875 equatorial system
 * per IAU standard. Although `Astronomy.Constellation` requires J2000 equatorial
 * coordinates, the struct contains converted B1875 coordinates for reference.
 */
typedef struct
{
    astro_status_t status;      /**< `ASTRO_SUCCESS` if this struct is valid; otherwise an error code. */
    const char    *symbol;      /**< 3-character mnemonic symbol for the constellation, e.g. "Ori". */
    const char    *name;        /**< Full name of constellation, e.g. "Orion". */
    double         ra_1875;     /**< Right ascension expressed in B1875 coordinates. */
    double         dec_1875;    /**< Declination expressed in B1875 coordinates. */
}
astro_constellation_t;


/**
 * @brief Selects the output format of the function #Astronomy_FormatTime.
 */
typedef enum
{
    TIME_FORMAT_DAY,    /**< Truncate to UTC calendar date only, e.g. `2020-12-31`. Buffer size must be at least 11 characters. */
    TIME_FORMAT_MINUTE, /**< Round to nearest UTC minute, e.g. `2020-12-31T15:47Z`. Buffer size must be at least 18 characters. */
    TIME_FORMAT_SECOND, /**< Round to nearest UTC second, e.g. `2020-12-31T15:47:32Z`. Buffer size must be at least 21 characters. */
    TIME_FORMAT_MILLI   /**< Round to nearest UTC millisecond, e.g. `2020-12-31T15:47:32.397Z`. Buffer size must be at least 25 characters. */
}
astro_time_format_t;

/**
 * @brief Lunar libration angles, returned by #Astronomy_Libration.
 */
typedef struct
{
    double elat;                /**< Sub-Earth libration ecliptic latitude angle, in degrees. */
    double elon;                /**< Sub-Earth libration ecliptic longitude angle, in degrees. */
    double mlat;                /**< Moon's geocentric ecliptic latitude, in degrees. */
    double mlon;                /**< Moon's geocentric ecliptic longitude, in degrees. */
    double dist_km;             /**< Distance between the centers of the Earth and Moon in kilometers. */
    double diam_deg;            /**< The apparent angular diameter of the Moon, in degrees, as seen from the center of the Earth. */
}
astro_libration_t;

/**
 * @brief Information about a body's rotation axis at a given time.
 *
 * This structure is returned by #Astronomy_RotationAxis to report
 * the orientation of a body's rotation axis at a given moment in time.
 * The axis is specified by the direction in space that the body's north pole
 * points, using angular equatorial coordinates in the J2000 system (EQJ).
 *
 * Thus `ra` is the right ascension, and `dec` is the declination, of the
 * body's north pole vector at the given moment in time. The north pole
 * of a body is defined as the pole that lies on the north side of the
 * [Solar System's invariable plane](https://en.wikipedia.org/wiki/Invariable_plane),
 * regardless of the body's direction of rotation.
 *
 * The `spin` field indicates the angular position of a prime meridian
 * arbitrarily recommended for the body by the International Astronomical
 * Union (IAU).
 *
 * The fields `ra`, `dec`, and `spin` correspond to the variables
 * α0, δ0, and W, respectively, from
 * [Report of the IAU Working Group on Cartographic Coordinates and Rotational Elements: 2015](https://astropedia.astrogeology.usgs.gov/download/Docs/WGCCRE/WGCCRE2015reprint.pdf).
 *
 * The field `north` is a unit vector pointing in the direction of the body's north pole.
 * It is expressed in the equatorial J2000 system (EQJ).
 */
typedef struct
{
    astro_status_t  status;     /**< `ASTRO_SUCCESS` if this struct is valid; otherwise an error code. */
    double          ra;         /**< The J2000 right ascension of the body's north pole direction, in sidereal hours. */
    double          dec;        /**< The J2000 declination of the body's north pole direction, in degrees. */
    double          spin;       /**< Rotation angle of the body's prime meridian, in degrees. */
    astro_vector_t  north;      /**< A J2000 dimensionless unit vector pointing in the direction of the body's north pole. */
}
astro_axis_t;


/**
 * \def TIME_TEXT_BYTES
 * @brief The smallest number of characters that is always large enough for #Astronomy_FormatTime.
 */
#define TIME_TEXT_BYTES  28

/**
 * @brief Holds the positions and velocities of Jupiter's major 4 moons.
 *
 * The #Astronomy_JupiterMoons function returns this struct
 * to report position and velocity vectors for Jupiter's largest 4 moons
 * Io, Europa, Ganymede, and Callisto. Each position vector is relative
 * to the center of Jupiter. Both position and velocity are oriented in
 * the EQJ system (that is, using Earth's equator at the J2000 epoch.)
 * The positions are expressed in astronomical units (AU),
 * and the velocities in AU/day.
 */
typedef struct
{
    astro_state_vector_t io;        /**< Jovicentric position and velocity of Io. */
    astro_state_vector_t europa;    /**< Jovicentric position and velocity of Europa. */
    astro_state_vector_t ganymede;  /**< Jovicentric position and velocity of Ganymede. */
    astro_state_vector_t callisto;  /**< Jovicentric position and velocity of Callisto. */
}
astro_jupiter_moons_t;


/**
 * @brief  Indicates whether a crossing through the ecliptic plane is ascending or descending.
 */
typedef enum
{
    INVALID_NODE    =  0,   /**< Placeholder value for a missing or invalid node. */
    ASCENDING_NODE  = +1,   /**< The body passes through the ecliptic plane from south to north. */
    DESCENDING_NODE = -1    /**< The body passes through the ecliptic plane from north to south. */
}
astro_node_kind_t;

/**
 * @brief Information about an ascending or descending node of a body.
 *
 * This structure is returned by #Astronomy_SearchMoonNode and #Astronomy_NextMoonNode
 * to report information about the center of the Moon passing through the ecliptic plane.
 */
typedef struct
{
    astro_status_t      status;     /**< `ASTRO_SUCCESS` if this struct is valid; otherwise an error code. */
    astro_time_t        time;       /**< The time when the body passes through the ecliptic plane. */
    astro_node_kind_t   kind;       /**< Either `ASCENDING_NODE` or `DESCENDING_NODE`, depending on the direction of the ecliptic plane crossing. */
}
astro_node_event_t;


/**
 * @brief A data type used for managing simulation of the gravitational forces on a small body.
 *
 * This is an opaque data type used to hold the internal state of
 * a numeric integrator used to calculate the trajectory of a small
 * body moving through the Solar System.
 */
typedef struct astro_grav_sim_s astro_grav_sim_t;


/*---------- functions ----------*/

void Astronomy_Reset(void);
double Astronomy_VectorLength(astro_vector_t vector);
astro_angle_result_t Astronomy_AngleBetween(astro_vector_t a, astro_vector_t b);
const char *Astronomy_BodyName(astro_body_t body);
astro_body_t Astronomy_BodyCode(const char *name);
astro_observer_t Astronomy_MakeObserver(double latitude, double longitude, double height);
#if !defined(ASTRONOMY_ENGINE_NO_CURRENT_TIME)
astro_time_t Astronomy_CurrentTime(void);
#endif
astro_time_t Astronomy_MakeTime(int year, int month, int day, int hour, int minute, double second);
astro_time_t Astronomy_TimeFromUtc(astro_utc_t utc);
astro_utc_t  Astronomy_UtcFromTime(astro_time_t time);
astro_status_t Astronomy_FormatTime(astro_time_t time, astro_time_format_t format, char *text, size_t size);
astro_time_t Astronomy_TimeFromDays(double ut);
astro_time_t Astronomy_TerrestrialTime(double tt);
astro_time_t Astronomy_AddDays(astro_time_t time, double days);
double Astronomy_SiderealTime(astro_time_t *time);
astro_func_result_t Astronomy_HelioDistance(astro_body_t body, astro_time_t time);
astro_vector_t Astronomy_HelioVector(astro_body_t body, astro_time_t time);
astro_vector_t Astronomy_GeoVector(astro_body_t body, astro_time_t time, astro_aberration_t aberration);
astro_vector_t Astronomy_GeoMoon(astro_time_t time);
astro_spherical_t Astronomy_EclipticGeoMoon(astro_time_t time);
astro_state_vector_t Astronomy_GeoMoonState(astro_time_t time);
astro_state_vector_t Astronomy_GeoEmbState(astro_time_t time);
astro_libration_t Astronomy_Libration(astro_time_t time);
astro_state_vector_t Astronomy_BaryState(astro_body_t body, astro_time_t time);
astro_state_vector_t Astronomy_HelioState(astro_body_t body, astro_time_t time);

double Astronomy_MassProduct(astro_body_t body);
double Astronomy_PlanetOrbitalPeriod(astro_body_t body);

astro_state_vector_t Astronomy_LagrangePoint(
    int point,
    astro_time_t time,
    astro_body_t major_body,
    astro_body_t minor_body
);

astro_state_vector_t Astronomy_LagrangePointFast(
    int point,
    astro_state_vector_t major_state,
    double major_mass,
    astro_state_vector_t minor_state,
    double minor_mass
);

astro_jupiter_moons_t Astronomy_JupiterMoons(astro_time_t time);

astro_equatorial_t Astronomy_Equator(
    astro_body_t body,
    astro_time_t *time,
    astro_observer_t observer,
    astro_equator_date_t equdate,
    astro_aberration_t aberration
);

astro_vector_t Astronomy_ObserverVector(
    astro_time_t *time,
    astro_observer_t observer,
    astro_equator_date_t equdate
);

astro_state_vector_t Astronomy_ObserverState(
    astro_time_t *time,
    astro_observer_t observer,
    astro_equator_date_t equdate
);

astro_observer_t Astronomy_VectorObserver(astro_vector_t *vector, astro_equator_date_t equdate);

double Astronomy_ObserverGravity(double latitude, double height);

astro_ecliptic_t Astronomy_SunPosition(astro_time_t time);
astro_ecliptic_t Astronomy_Ecliptic(astro_vector_t eqj);
astro_angle_result_t Astronomy_EclipticLongitude(astro_body_t body, astro_time_t time);

astro_horizon_t Astronomy_Horizon(
    astro_time_t *time,
    astro_observer_t observer,
    double ra,
    double dec,
    astro_refraction_t refraction);

astro_angle_result_t Astronomy_AngleFromSun(astro_body_t body, astro_time_t time);
astro_elongation_t Astronomy_Elongation(astro_body_t body, astro_time_t time);
astro_elongation_t Astronomy_SearchMaxElongation(astro_body_t body, astro_time_t startTime);
astro_angle_result_t Astronomy_PairLongitude(astro_body_t body1, astro_body_t body2, astro_time_t time);

/** @cond DOXYGEN_SKIP */
/* Provided for backward compatibility. Newer code can use Astronomy_PairLongitude. */
#define Astronomy_LongitudeFromSun(body,time)   (Astronomy_PairLongitude((body), BODY_SUN, (time)))
/** @endcond */

astro_search_result_t Astronomy_SearchRelativeLongitude(astro_body_t body, double targetRelLon, astro_time_t startTime);
astro_angle_result_t Astronomy_MoonPhase(astro_time_t time);
astro_search_result_t Astronomy_SearchMoonPhase(double targetLon, astro_time_t startTime, double limitDays);
astro_moon_quarter_t Astronomy_SearchMoonQuarter(astro_time_t startTime);
astro_moon_quarter_t Astronomy_NextMoonQuarter(astro_moon_quarter_t mq);
astro_lunar_eclipse_t Astronomy_SearchLunarEclipse(astro_time_t startTime);
astro_lunar_eclipse_t Astronomy_NextLunarEclipse(astro_time_t prevEclipseTime);
astro_global_solar_eclipse_t Astronomy_SearchGlobalSolarEclipse(astro_time_t startTime);
astro_global_solar_eclipse_t Astronomy_NextGlobalSolarEclipse(astro_time_t prevEclipseTime);
astro_local_solar_eclipse_t Astronomy_SearchLocalSolarEclipse(astro_time_t startTime, astro_observer_t observer);
astro_local_solar_eclipse_t Astronomy_NextLocalSolarEclipse(astro_time_t prevEclipseTime, astro_observer_t observer);
astro_transit_t Astronomy_SearchTransit(astro_body_t body, astro_time_t startTime);
astro_transit_t Astronomy_NextTransit(astro_body_t body, astro_time_t prevTransitTime);
astro_node_event_t Astronomy_SearchMoonNode(astro_time_t startTime);
astro_node_event_t Astronomy_NextMoonNode(astro_node_event_t prevNode);

astro_search_result_t Astronomy_Search(
    astro_search_func_t func,
    void *context,
    astro_time_t t1,
    astro_time_t t2,
    double dt_tolerance_seconds);

astro_search_result_t Astronomy_SearchSunLongitude(
    double targetLon,
    astro_time_t startTime,
    double limitDays);

astro_hour_angle_t Astronomy_SearchHourAngleEx(
    astro_body_t body,
    astro_observer_t observer,
    double hourAngle,
    astro_time_t startTime,
    int direction);

astro_func_result_t Astronomy_HourAngle(
    astro_body_t body,
    astro_time_t *time,
    astro_observer_t observer);

/** @cond DOXYGEN_SKIP */

/* The following are provided for backward compatibility. */
#define Astronomy_SearchHourAngle(body,observer,hourAngle,startTime)    \
    Astronomy_SearchHourAngleEx((body), (observer), (hourAngle), (startTime), +1)

#define Astronomy_SearchRiseSet(body,observer,direction,startTime,limitDays) \
    Astronomy_SearchRiseSetEx((body), (observer), (direction), (startTime), (limitDays), 0.0)

/** @endcond */

astro_search_result_t Astronomy_SearchRiseSetEx(
    astro_body_t body,
    astro_observer_t observer,
    astro_direction_t direction,
    astro_time_t startTime,
    double limitDays,
    double metersAboveGround);

astro_search_result_t Astronomy_SearchAltitude(
    astro_body_t body,
    astro_observer_t observer,
    astro_direction_t direction,
    astro_time_t startTime,
    double limitDays,
    double altitude);

astro_atmosphere_t Astronomy_Atmosphere(double elevationMeters);

astro_axis_t Astronomy_RotationAxis(astro_body_t body, astro_time_t *time);

astro_seasons_t Astronomy_Seasons(int year);
astro_illum_t Astronomy_Illumination(astro_body_t body, astro_time_t time);
astro_illum_t Astronomy_SearchPeakMagnitude(astro_body_t body, astro_time_t startTime);
astro_apsis_t Astronomy_SearchLunarApsis(astro_time_t startTime);
astro_apsis_t Astronomy_NextLunarApsis(astro_apsis_t apsis);
astro_apsis_t Astronomy_SearchPlanetApsis(astro_body_t body, astro_time_t startTime);
astro_apsis_t Astronomy_NextPlanetApsis(astro_body_t body, astro_apsis_t apsis);

astro_rotation_t Astronomy_IdentityMatrix(void);
astro_rotation_t Astronomy_InverseRotation(astro_rotation_t rotation);
astro_rotation_t Astronomy_CombineRotation(astro_rotation_t a, astro_rotation_t b);
astro_rotation_t Astronomy_Pivot(astro_rotation_t rotation, int axis, double angle);
astro_vector_t Astronomy_VectorFromSphere(astro_spherical_t sphere, astro_time_t time);
astro_spherical_t Astronomy_SphereFromVector(astro_vector_t vector);
astro_equatorial_t Astronomy_EquatorFromVector(astro_vector_t vector);
astro_vector_t Astronomy_VectorFromHorizon(astro_spherical_t sphere, astro_time_t time, astro_refraction_t refraction);
astro_spherical_t Astronomy_HorizonFromVector(astro_vector_t vector, astro_refraction_t refraction);
astro_vector_t Astronomy_RotateVector(astro_rotation_t rotation, astro_vector_t vector);
astro_state_vector_t Astronomy_RotateState(astro_rotation_t rotation, astro_state_vector_t state);

astro_rotation_t Astronomy_Rotation_EQD_EQJ(astro_time_t *time);
astro_rotation_t Astronomy_Rotation_EQD_ECL(astro_time_t *time);
astro_rotation_t Astronomy_Rotation_EQD_ECT(astro_time_t *time);
astro_rotation_t Astronomy_Rotation_EQD_HOR(astro_time_t *time, astro_observer_t observer);
astro_rotation_t Astronomy_Rotation_EQJ_EQD(astro_time_t *time);
astro_rotation_t Astronomy_Rotation_EQJ_ECT(astro_time_t *time);
astro_rotation_t Astronomy_Rotation_EQJ_ECL(void);
astro_rotation_t Astronomy_Rotation_EQJ_HOR(astro_time_t *time, astro_observer_t observer);
astro_rotation_t Astronomy_Rotation_ECL_EQD(astro_time_t *time);
astro_rotation_t Astronomy_Rotation_ECL_EQJ(void);
astro_rotation_t Astronomy_Rotation_ECL_HOR(astro_time_t *time, astro_observer_t observer);
astro_rotation_t Astronomy_Rotation_ECT_EQJ(astro_time_t *time);
astro_rotation_t Astronomy_Rotation_ECT_EQD(astro_time_t *time);
astro_rotation_t Astronomy_Rotation_HOR_EQD(astro_time_t *time, astro_observer_t observer);
astro_rotation_t Astronomy_Rotation_HOR_EQJ(astro_time_t *time, astro_observer_t observer);
astro_rotation_t Astronomy_Rotation_HOR_ECL(astro_time_t *time, astro_observer_t observer);
astro_rotation_t Astronomy_Rotation_EQJ_GAL(void);
astro_rotation_t Astronomy_Rotation_GAL_EQJ(void);

double Astronomy_Refraction(astro_refraction_t refraction, double altitude);
double Astronomy_InverseRefraction(astro_refraction_t refraction, double bent_altitude);

astro_constellation_t Astronomy_Constellation(double ra, double dec);

astro_status_t Astronomy_GravSimInit(
    astro_grav_sim_t **simOut,
    astro_body_t originBody,
    astro_time_t time,
    int numBodies,
    const astro_state_vector_t *bodyStateArray
);

astro_status_t Astronomy_GravSimUpdate(
    astro_grav_sim_t *sim,
    astro_time_t time,
    int numBodies,
    astro_state_vector_t *bodyStateArray
);

astro_state_vector_t Astronomy_GravSimBodyState(
    astro_grav_sim_t *sim,
    astro_body_t body
);

astro_time_t Astronomy_GravSimTime(const astro_grav_sim_t *sim);
int Astronomy_GravSimNumBodies(const astro_grav_sim_t *sim);
astro_body_t Astronomy_GravSimOrigin(const astro_grav_sim_t *sim);
void Astronomy_GravSimSwap(astro_grav_sim_t *sim);
void Astronomy_GravSimFree(astro_grav_sim_t *sim);

/**
 * @brief A function for which to solve a light-travel time problem.
 *
 * The function #Astronomy_CorrectLightTravel solves a generalized
 * problem of deducing how far in the past light must have left
 * a target object to be seen by an observer at a specified time.
 * This function pointer type expresses an arbitrary position vector
 * as function of time. Such a function must be passed to
 * `Astronomy_CorrectLightTravel`.
 */
typedef astro_vector_t (* astro_position_func_t) (void *context, astro_time_t time);

astro_vector_t Astronomy_CorrectLightTravel(
    void *context,
    astro_position_func_t func,
    astro_time_t time
);

astro_vector_t Astronomy_BackdatePosition(
    astro_time_t time,
    astro_body_t observerBody,
    astro_body_t targetBody,
    astro_aberration_t aberration
);

astro_status_t Astronomy_DefineStar(
    astro_body_t body,
    double ra,
    double dec,
    double distanceLightYears
);

#ifdef __cplusplus
}
#endif

#endif  /* ifndef __ASTRONOMY_H */
