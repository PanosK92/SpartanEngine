# Backrooms generator checks

`worlds/liminal_space.world` loads `binaries/project/scripts/backrooms.lua`. Reload the world to rebuild
its streamed geometry after editing the script. No engine rebuild is required. The project
materials in `binaries/project/liminal_space_materials` reuse the existing Backrooms textures.

The generator now groups chunks into architectural districts, with quiet maze areas
between six room motifs:

- Low rooms inside tall halls, with a thick entrance and an observation window.
- Regular column fields beneath unusually low or high ceilings, sometimes with a close pair.
- Repeated doorways and waiting arrangements, ending in a taller room; rare offset/narrow openings.
- Curved, converging passages with wedge-shaped spaces alongside them.
- Reception openings facing blank walls, empty stepped platforms, and waiting chairs.
- Observation windows into a neighbouring room with an elevated service gallery.

Ceiling rectangles and wall runs merge to reduce entity count. Ceiling transitions are
closed, lights follow their actual ceiling heights, and the low nested room has a second
roof above it to close the surrounding tall hall. District ambience blends gradually.
Generation depends on coordinates and seed, never on the order chunks are visited.

The polish pass adds slim 1.22 x 0.62 metre fluorescent troffers (square in one district),
visible dead diffusers, and at most two wall maintenance details per chunk: a removed
noticeboard patch or a louvered vent. Occasional replacement ceiling tiles interrupt the
repetition. A separate carpet material corrects the original metallic surface and assigns
the roughness map to its proper slot, with reduced normal strength.

Eight percent of ballasts can briefly falter during occasional 67-second cycles. Faults
are coordinate-stable and update both the visible diffuser and its pooled point light.
Disabling `flicker` restores full output. Lighting still uses the existing six point lights.
A quiet spatial hum fades out before moving to a nearer fixture; footsteps blend from dry
low-room acoustics to longer reflections beneath tall ceilings. This uses one extra audio
source, with no new audio files. It does not simulate sound occlusion through walls.

The script attributes `district_chunks`, `feature_chance`, `anomaly_chance`,
`low_ceiling`, and `high_ceiling` control the new behaviour. The defaults are three
chunks per district side, 72% feature probability, 24% anomaly probability, and
2.6/6.4 metre ceilings. The origin contains a feature so it is possible to encounter
the new architecture near the start. Feature layouts require at least ten cells per
chunk and 3.3 metre cells; smaller configurations retain the ordinary maze.

## Run

From the repository root, run `tools\backrooms_tests\build.cmd`. This links the
standard Lua interpreter already included in the engine's bundled `lua.lib` and
runs `generator.lua`. It uses Visual Studio's x64 tools and requires no downloads.
Alternatively, run `lua tools/backrooms_tests/generator.lua` with Lua 5.4.

The suite invokes the production Lua functions through debug upvalues, without
adding test exports to the serialized script table. It checks:

- Full cell connectivity, treating windows as barriers, and guaranteed border exits.
- Ceiling coverage, matching heights at chunk boundaries, and finite positive box sizes.
- Light positions relative to the correct ceiling.
- Complete fixture frames, dead/live diffuser associations, rare deterministic faults,
  visible/pooled light synchronization, and bounded acoustic profiles.
- Standing-player routes through the actual rotated box footprints and approximate
  chair/table footprints in 507 seeded chunks and 63 custom size configurations.
- Stable geometry and furniture when revisiting all six motifs, with and without anomalies.

The physical checks use a 1.8 metre standing height, a conservative 0.29 metre
horizontal radius, and a roughly 0.4 metre raster. The three 0.12 metre dais steps
are treated as walkable. A failure writes a route diagnostic SVG under
`binaries/backrooms_tests`. These are headless geometry checks, not a PhysX walk-through
or a rendered lighting/performance assessment.
