# Earth environment

Select the world's directional light and open Day/Night in Properties.
The date is Gregorian UTC (1800–2200); press Enter after editing year/month/day.
The time slider is UTC too, independent of the computer's timezone and DST.
Day/Night Cycle advances the saved calendar during play. Clock speed 1 means
one simulated second per real second, 0 freezes it, and negative values rewind.
Real Time uses the operating system's current UTC date and time. Dawn/dusk
presets find actual solar rise/set events, day/night select upper/lower solar
transit, and the cinematic preset finds a descending 4-degree Sun. Horizon-event
presets leave the date unchanged when there is no crossing during polar day/night.
Stopping play restores the authored date/location/climate settings.

Latitude and longitude are degrees north/east; negative values mean south/west.
Elevation is metres above sea level. North heading rotates geographic north
clockwise from world +Z; at zero, +X is east. `plan.world` uses the Zakynthos
location (37.78 N, 20.90 E), the existing map's +Z north orientation, a summer
evening, and 200x time. Other worlds can configure their own location. World
XML stores an `Environment` element, including wind velocity in world m/s.
Older files receive default environment settings without requiring conversion.

Sun and Moon positions come from the vendored Astronomy Engine, with lunar
parallax, phase and apparent angular sizes. Stars use a horizon-to-J2000
transform, so latitude, sidereal rotation and precession affect the catalogue.
The Moon can appear in daytime; sky scattering and clouds use its own direction
and illuminated fraction. Manually oriented directional lights remain available
when the cycle is off; enable the cycle to keep direct sunlight tied to the sky.

Weather is a configurable climate approximation, not historical weather data
or a forecast. Annual mean, seasonal amplitude, daily amplitude, cloud coverage,
elevation and wind determine air temperature, pressure, density and exposed
asphalt temperature. Asphalt approaches a solar/convection equilibrium with a
30-minute thermal response; large date jumps reinitialize that estimate.
Wind shares the existing spatial gust field used by vegetation.

Full vehicle simulation receives these boundary conditions each physics step.
They affect tire/road heat exchange, cold-soak temperature, absolute-pressure
gas-law tire pressure, existing temperature/pressure grip modifiers, brake and
battery cooling, aerodynamic drag, downforce and crosswind forces. Preset tire
fill/reference temperatures remain unchanged. Tire thermal time follows physics
seconds, even when the world clock is accelerated. Cheap traffic simulation
retains its simplified handling.

Limits: one world-wide climate and exposed-road estimate; no local shade, road
material thermal map, precipitation, ice, humidity or weather-observation feed.
The lunar surface and Milky Way remain procedural, and stars do not have
per-star proper-motion updates. Moonlight uses the existing sky/environment
lighting path rather than a separate shadow-casting lunar light.

Validation performed with temporary harnesses (removed after use): development
engine build; live twilight and full-Moon/stars renders; world XML save/load;
Vulkan sky/cloud and Direct3D sky shader variants; NREL SPA reference for
2003-10-17 19:30:30 UTC at 39.742476 N, 105.1786 W (azimuth error 0.00012 degrees,
zenith error 0.00376 degrees, including different refraction assumptions);
1900/2000 leap years, historical year rollover and reverse time, polar seasons,
2024 new/full moons, angular sizes, star-frame orthogonality, gas-law density,
subzero tire soak, hot-road heating and gauge pressure. Production vehicle
checks cover headwind/tailwind/crosswind forces and density scaling, plus the
existing full vehicle regression/handling suite at 200 Hz.

Reference: https://docs.nlr.gov/docs/fy08osti/34302.pdf
