# Astronomy Engine

Upstream: https://github.com/cosinekitty/astronomy

Vendored unmodified C source and header from commit
`865d3da7d8112bbc7911238052c6af4aaf877181` (MIT license retained in both files).
Compiled as C without the engine precompiled header by `tools/premake.lua`.

The world environment uses topocentric Sun/Moon coordinates, light-time and
aberration corrections, precession/nutation, the library's delta-T model,
standard atmospheric refraction, and lunar illumination. Upstream targets
approximately one arcminute positional accuracy; these are numerical
ephemerides, not exact measurements. UTC approximates UT1. No runtime network
connection is required.
