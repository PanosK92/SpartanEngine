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
location (37.78 N, 20.90 E), the existing map's +Z north orientation, and a
frozen summer daylight baseline (18 June 2026, 10:00 UTC). Other worlds can
configure their own location. World XML stores an `Environment` element,
including wind velocity in world m/s.
Older files receive default environment settings without requiring conversion.

The island's clear-weather look is authored in `plan.world`: cloud coverage
0.28, fog density 0.22, ground mist 0.12, haze altitude scale 600 m, bloom 0.25,
and camera exposure compensation -0.7 stops. These are look-development values,
not a weather observation. Keep the GT7 display transform fixed while comparing
materials; validate SDR captures and the HDR display separately. The directional
light's effective colour comes from atmospheric transmission, not the saved
temperature field. Grass honours its material tint and tree variation multiplies
the authored leaf reflectance, so species textures remain the colour reference.
Ray-traced reflections/shadows and SSAO are enabled for this baseline. ReSTIR
indirect lighting remains off: the island's large emissive set exceeds its NEE
pool and the tested garage view shows conspicuous coloured noise. Enabling it
is not a substitute for a stable indirect-lighting solution.

Vegetation in `plan.world` uses twelve independent scatter rules: mature pine and
olive canopy, maquis, a separate woodland understory, young pines, wildflowers,
grass, three scales of rock detail, fallen trunks and broken branches. Canopy
groups are tighter, with a wider
size range; grass pockets expose shorter fringes and bare ground. The pebble
rule uses the complementary coverage of the same patch field. Understory and
sapling draw distances are shorter than the mature canopy to bound nearby
geometry cost. These rules retain the terrain masks and road exclusions.

`tools/map/build_woodland_assets.py` reproduces the new woodland assets with
Blender in background factory-startup mode. It downloads checksum-verified 2K
textures and geometry from https://polyhaven.com/a/pine_sapling_small,
https://polyhaven.com/a/dead_tree_trunk and
https://polyhaven.com/a/dry_branches_medium_01 (all CC0). It separates the plants
and branches, seats each asset at zero, and reduces saplings to 24,000 triangles,
trunks to 6,000 and branches to 3,000 before engine LOD generation. Outputs and
the download
manifest live in the existing external `binaries/project/models/island_biomes`
asset tree. Run this builder when provisioning a fresh project asset directory.

Thin foliage receives sky transmission from its reverse side, sharing the
reflection energy budget. Direct subsurface lighting remains demodulated until
composition so leaf albedo is applied once. These are lighting corrections,
not a replacement for detailed mature-tree assets or indirect light transport.
Ray-traced leaf blockers now sample thin-sheet transmission stochastically,
preserving SIGMA's binary hit/miss contract; solid blockers stay opaque. Contact
shadow attenuation on leaf receivers retains that transmitted fraction.

The legacy pine atlas contains extremely dark, brown-biased needles (sampled
green texels average about 0.0081/0.0094/0.0036 linear RGB). A leaf-only scene
multiplier of 3.5/6.4/3.4 brings that sample to approximately 0.028/0.060/0.012.
This is an authored reflectance correction, not measured botanical data.
Runtime material clones isolate it from bark, other worlds and imported files.
The optional scatter `foliage_tint_*` and `foliage_scattering` settings persist
with the world; omitted tint values preserve the source material.

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
