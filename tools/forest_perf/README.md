# Dense island performance work

Run `node tools/forest_perf/capture.mjs <label>` against a disposable loaded
engine on MCP port 47785. It records 30 profiler snapshots at 400 ms intervals,
the camera/world context, and a screenshot. Do not save the disposable world.
`node tools/terrain_tests/live_biomes.mjs --preview` can prepare the test island
in an empty instance. Let loading, shader compilation and BLAS builds settle.

Initial measurement: development Vulkan build, RTX 5070 Ti, render 1920 x 938,
output 3180 x 1555, camera (6100, 140, -2600) looking +Z, edit mode. This is one
fixed overview, not a driving benchmark or a guarantee for other machines.

The first optimization pass retained the same scatter counts: 320829 pines,
122015 olives, 52930 outcrop rocks, 250425 loose stones, 325196 scrub plants,
and 49799 flowers. Baseline frame/GPU time was approximately 71 ms. The first
optimized capture measured 64.8/64.5 ms, with world update around 7.5 ms.
Instance culling went from approximately 0.12 to 0.017 ms. Samples and screenshots
are in binaries/forest_perf and binaries/project/mcp/blockout/thumbnails.

Changes:
- Upload one culling range per 64 instances per LOD. GPU lanes expand the range;
  distance, frustum, LOD, wind bounds and Hi-Z tests stay the same. Two-dimensional
  dispatch handles more than 65535 batches. Partial final batches do not emit
  extra instances.
- Mesh shaders evaluate root gust/phase/axis/sway once per meshlet and reuse it
  across vertices. The g-buffer caches current and previous samples; depth only
  needs current state. Per-vertex branch/leaf motion remains unchanged.
- Static-actor distance queries transform packed positions directly instead of
  unpacking rotations and composing full matrices.
- Compact CPU/upload buffers preserve the old logical instance budget, allowing
  an extra partial batch for every possible draw. Survivor buffers keep their
  original capacity; reducing a work-list buffer must not reduce visible output.
- World profiler subregions distinguish pre-tick, render, post-render, logic and
  change scanning so subsequent CPU work can be based on measured costs.

The vertex-path diagnostic is INVALID as a performance comparison: switching off
mesh shaders caused missing terrain in that existing fallback. Keep mesh shaders
on. A low frame time with missing geometry is not an optimization.

Validation: development build, 14 Vulkan shader compilation variants through
`node tools/wind_tests/compile_shaders.mjs`, live island palette checks and
before/after screenshots. Further moving-camera, close-forest and collision
checks are needed before calling the whole optimization goal complete.

Latest verified overview (`optimized_pad_lookup.json`): median GPU 63.396 ms,
frame 63.585 ms, World::Tick 7.094 ms, ProduceFrame 2.608 ms. Baseline medians:
GPU 71.228 ms, World::Tick 7.701 ms, ProduceFrame 2.719 ms. World XML SHA-256
remains FF76072027D7B1927FC5A491AB7CE0015971EACC413F2068A3AA8330891657F3.
Pad occupants now resolve together in one entity scan. Runtime figures do not
isolate the effect of each change, and the GPU timings include ordinary variance.

The experiment sharing full instance/world/normal matrices (`optimized_shared_transforms`)
REGRESSED GPU time to 75.6 ms and was reverted. Only root wind state remains shared.

Earlier investigation (resolved below): `world_post_render_tick` still consumes roughly 5.8 ms; CPU
render culling itself is only about 0.2 ms. Identify the costly component types
before changing scheduling. `world_logic_tick` fell from roughly 1.4 to 0.8 ms
after batched pad lookup. G-buffer (~40.7 ms) and depth (~14.4 ms) still dominate
GPU cost. The existing meshlet culler supports opaque/alpha list splitting, but
current mesh shaders launch both variants across the entire shared survivor list.
If enabling splitting, preserve capacity for each category and verify offsets in
both depth and g-buffer; simply halving the budget would discard geometry.
A closer view at (6100,65,-2250) looking toward (6100,45,-1900) was captured as
`close_forest`; it has no matched baseline, so its timings are not a speedup claim.
The goal remains active: further CPU investigation and moving/near-collider
verification remain outstanding.


## Verified final pass

The split meshlet lists now dispatch opaque and alpha-tested geometry separately
in both depth and g-buffer passes. Each category retains the original 4M-entry
capacity (1M in capture mode); this adds 192 MiB of device memory across four
normal frame buffers. The fallback vertex path keeps its original logical limit.
This trades device memory for fewer mesh-shader launches, without changing LOD,
visibility rules, alpha testing, wind, draw distance, or population settings.

Spline control-point and road-node inspection snapshots the child list under one
lock per scan, instead of locking once or twice per child. Tick also reuses the
local point vector for its count. Snapshots are local to the call, so child edits,
reparenting, names and tags are still examined every frame without cache
invalidation requirements. Spline CPU time fell from about 4.8 to 2.7 ms.

Final matched overview (`spline_snapshot.json`), 30-sample medians:
- GPU: 47.128 ms versus baseline 71.228 ms (33.8% lower).
- World::Tick: 5.152 ms versus 7.701 ms (33.1% lower).
- Frame: 47.104 ms, approximately 21 FPS versus approximately 14 FPS originally.
- G-buffer: 29.535 ms; depth: 9.531 ms; instance cull: 0.015 ms.

The preceding GPU-only split capture measured 46.424 ms; ordinary runtime
variation accounts for some difference between runs. CPU frame totals include
GPU semaphore waiting and should not be presented as CPU execution time.
The matched closer camera improved from 67.35 ms (`close_forest`) to 45.776 ms
(`split_close`). These are development-build editor measurements on this machine,
not release-build or gameplay frame-rate guarantees.

Validation completed:
- Development Vulkan C++ build succeeds; existing external-library linker warnings remain.
- All 14 shader compile cases pass, including both alpha mesh variants.
- Terrain sampling, seams, formations, footprints, budgets, habitats, asset
  variants and foliage UV regression tests pass.
- Asset validation passes for 12 models and five habitats; authored pine LOD 0
  retains all 18,156 triangles. The population/world hash recorded above is unchanged.
- Overview and close screenshots preserve the same terrain, trunks and foliage.
- `node tools/forest_perf/movement.mjs` passes 25 near-ground camera positions,
  finds nearby tree collision, confirms distance deactivation and reactivation on
  return. Evidence is `movement_verified.json` and its screenshot. This is a
  camera/collision smoke test, not a vehicle-driving test or percentile benchmark.

The requested measured optimization pass is verified. The scene remains GPU
bound, principally in depth and g-buffer geometry. Further optimization is still
possible; no claim of optimal performance or a universal target frame rate is made.
The existing broken non-mesh fallback is outside this pass and was not used to
produce accepted performance numbers. No production world was saved by the tests.


## Follow-up: repair incomplete LOD chains

The earlier optimization pass did not address incomplete authored-prop LODs.
Inspecting the serialized pine exposed a three-level canopy and a one-level
branch/trunk submesh, despite Mesh.cpp already requesting five meshoptimizer LODs.

Distance LOD generation now explicitly permits meshoptimizer component pruning
for non-terrain meshes, while preserving UV attributes. Increasing the error
bound continues after an unsuccessful attempt: a low-error plateau is not proof
that later bounds cannot simplify. Pruning overshoot is refined with a bounded
binary search, so a target of roughly 850 canopy triangles does not accidentally
produce a nearly empty 48-triangle canopy. LOD 0 and terrain border protection
are unchanged; non-LOD callers retain their old pruning policy.

GPU instance culling now uses one reference draw's bounds for every LOD candidate
of a renderable. Using each simplified LOD's different bounds could select no
level or multiple levels near a threshold. The reference uses the previously
unused uint in CullTask and does not enlarge that buffer.

Verified pine_01 triangle chains:
- Canopy: 14192 -> 7096 -> 3406 -> 1703 -> 852.
- Branch/trunk: 3964 -> 1804 -> 980 -> 480 -> 204.
All three pines have five useful levels in both submeshes. Olive trunks now have
five levels; olive canopy chains remain three to five levels where topology
limits useful reduction. Inspect with `python tools/forest_perf/inspect_lods.py
<saved.mesh>`. Diagnostic exports live under binaries/project/mcp/blockout/meshes;
these are test artifacts, not production source assets.

Build, all 14 shader variants, the terrain regression suite, the new component
pruning/UV preservation regression, authored LOD 0 preservation and movement/
collision smoke tests pass. The production world SHA-256 remains unchanged.

Performance evidence caveat: `followup_baseline` is the temporary overview
(47.939 ms/frame). `lod_prune` is an intermediate build at that same overview
(29.461 ms/frame), before the pruning refinement and stable LOD-bound fix.
During final validation the running instance switched to the main world and a
different camera. `lod_verified` and `lod_close` therefore are NOT matched
before/after benchmarks. GPU timestamp blocks also reported zero depth time;
those per-pass GPU results must not be used as trustworthy speedup figures.
Do not claim the user's original 16 FPS case is solved based on these captures.

## Follow-up: G-buffer export and shared wind cost

The indirect G-buffer payload now carries one draw index instead of repeating
immutable UV transforms, flags and material index at each vertex. The pixel
shader reads those fields from the original draw record. This removes nine
32-bit components from the mesh output without quantizing positions, normals,
tangents, UVs or motion vectors. CPU-driven geometry keeps its existing payload.

The meshlet culler also evaluates current/previous root wind once per surviving
instance and shares it with its depth and G-buffer meshlets. Branch and leaf
motion still runs per vertex. The cache uses 4 MiB per in-flight frame, 16 MiB
total. Capacity exhaustion uses the same uncached evaluator; it never discards
an instance. `r.tree_wind_cache_entries` defaults to 65536; set 0 for an A/B
comparison or 1 to exercise the overflow path. Both culling phases populate
their own current survivor slots, and normal resource tracking orders the
compute writes and mesh reads.

Separate opaque/alpha timing scopes and the MCP `profiler_record` command make
these costs measurable. Ordinary live snapshots can associate stale GPU query
slots with current scope names and report zero or incorrect pass durations.
Use `node tools/forest_perf/record.mjs <label>` and
`python tools/forest_perf/summarize_recording.py binaries/forest_perf/<label>.csv`
for fresh timestamp samples. The recorder also rejects captures where rendering
stops (for example when the window is minimized). The default recording port is
47786; set SPARTAN_MCP_PORT explicitly when using other scripts.

Fixed overview, same development Vulkan build settings, resolution and population:
- Before compact exports (`exports_before.csv`, 26 fresh samples): indirect
  G-buffer 9.303 ms, opaque 4.819 ms, alpha 4.480 ms.
- After compact exports (`exports_after.csv`, 27 samples): indirect G-buffer
  8.028 ms, opaque 4.230 ms, alpha 3.796 ms.
- Same-binary cache off/on (`windcache_off_matched.csv` and
  `windcache_on_matched.csv`, 28/29 samples): G-buffer 7.851 -> 7.201 ms;
  depth 3.220 -> 2.891 ms. Summed meshlet-cull scopes add about 0.06 ms.
  Recorded total GPU busy time is 18.156 -> 17.228 ms (sum of top-level
  GPU scopes, not GPU critical-path wall time).
- Final normal overview capture (`windcache_overview_final.json`): median
  frame 17.516 ms, approximately 57 FPS, versus 19.373 ms before this pass.
  World::Tick is 4.674 ms and ProduceFrame 2.441 ms. GPU pass fields in normal
  snapshot JSON are not accepted evidence; use the CSV values above.

The combined indirect G-buffer reduction is about 23% in this overview.
Wind and clouds evolve between captures. These numbers do not establish the
result for the user's particular 12 ms camera or guarantee 60 FPS throughout
the island. `windcache_on.json` is INVALID: its window stopped rendering;
the earlier `windcache_on.csv` contains valid fresh samples, but the matched
off/on recordings above are preferred for isolating cache benefit.

Validation: development build and all 17 shader compile variants pass. The
island loads all five habitats and preserves authored pine LOD 0. Cache off,
one-entry fallback and full-cache close screenshots preserve vegetation;
overview and near-ground screenshots were inspected. The 25-position camera
and tree-collider activation/deactivation test passes. Production world hash
and population remain the values recorded above; no production world was saved.
`wind_cache_check.mjs` runs the bounded A/B and fallback checks on the disposable
preview world. Keep that window unminimized until the captures finish.
