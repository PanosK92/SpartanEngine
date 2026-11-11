# Worlds and their resources

This directory holds one thing: the `<world>.world` XML files (and this README). It is
version controlled and flat; the Git ignore rules allow only `*.world` and `README.md`.

Everything else a world needs lives in `binaries/project/<world>_resources/`, the folder
named after the world file plus `_resources` (`plan.world` -> `plan_resources/`):
its Lua scripts in `scripts/`, engine baked `meshes/`, `materials/` and `textures/`,
audio, authored areas with their own pipelines, the offline generators under `sources/`
and the disposable `generated_cache/`. `binaries/` is excluded from Git and distributed
through the Dropbox `project.7z` download.

`binaries/project/README.md` is the layout contract: where every kind of file goes, how
cars are organised (`plan_resources/cars/<car>/` holds the car's preset, model and
prefab), which folders are shared, how to move or delete assets safely and how to
publish the package. Read it before touching assets.

The short version:

- World scripts are `project/<world>_resources/scripts/<world>.lua` (the companion script,
  run by the engine on every load) and `<world>_<part>.lua` for the rest, for example
  `liminal_space.lua`, `liminal_space_stalker.lua` and `liminal_space_soundscape.lua`.
  Do not put Lua in this directory.
- Do not put one world's content in shared folders, in another world's folder, in
  `project/mcp/blockout/`, in new top-level `project/` folders or in `tools/`. `tools/` is
  for engine and project tooling; generators of a single world's assets go in
  `<world>_resources/sources/`.
- MCP tools write to `project/mcp/blockout/` by default. When something made there becomes
  part of a world, move it into that world's folder and update its references. The folder
  a world uses is reported by `world_resource_directory_get`.
- The engine runs from `binaries/`, so references in worlds and scripts use paths such as
  `project/liminal_space_resources/audio/door_slam.wav` and
  `project/liminal_space_resources/scripts/liminal_space.lua`, in script components and in
  `dofile`. Tools run from the repository root use `binaries/project/...` for the same files.
- When adding or moving an asset, update its references and the tools that generate or load
  it, then load the world in the engine and check the log. A local change does not reach
  anyone else until the project package is published again.

Generated world caches use lossless LZ4 compression automatically. Existing raw
caches are accepted and converted on a successful read; incompressible payloads
stay raw inside the versioned format. Each file is size-bounded and checksummed,
and a missing or invalid cache is rebuilt from its inputs. No asset reimport or
world-file conversion is required.

After world preparation and successful saves, the engine trims each world's
`generated_cache/` directory to a 2 GiB budget, evicting the least recently used
entries first. This is a budget for disposable build data, not for the world or
its assets. Evicted entries are regenerated when needed; a working set larger
than the budget can therefore cost additional build time. Authored meshes,
textures, materials, world XML and terrain sculpt layers are outside this
cleanup. Cache files are replaced only after the new file is fully written.

The Ferrari showroom's warm, red and cyan tubes use separate emissive materials.
After downloading an older project package, run
`node tools/worlds/showroom_emitters.mjs` from the repository root to generate
them from the saved light colors and the existing `ceiling_light.xml` material.
Include the resulting `tube_*_emitter.xml` assets in the next project package.
The showcase explicitly sets bloom and mist density in its `ConsoleVariables`.

Atmospheric air is always present. All lights scatter through that air; nearby
light scattering retains a distance budget under each light's Performance settings.
The Atmosphere controls add mist to the baseline air: `r.atmosphere.mist_density`
is the amount, `mist_height` is its altitude scale, `ground_mist` sets its relative
concentration near terrain, and `mist_variation` controls its wind-driven breakup.
Zero mist density means clear air, with distant atmospheric haze still present.
The island uses zero extra mist; the showroom uses one. These values belong to
each world and are saved with it, rather than inherited from the previous world.
Old `r.fog` settings load as mist density with their original values. The legacy
per-light Volumetric flag is ignored; shadows control occlusion, not scattering.


## Plan garage and maintenance

Open the tune shop from the main menu's developer quick jump (Tab, Enter).
Q/E selects a department, up/down selects a part, left/right selects a stage,
and Enter fits it. Long tire lists scroll with the selected stage. Z/C selects
a tuning control; Shift+left/right adjusts it. Parts unlock controls at the
stages shown in the work order, and changes require a stationary car.

Suspension controls change the physical coilovers, alignment and anti-roll
forces. Transmission ratios also update the automatic shift-speed thresholds.
Adjustable LSD settings control power/coast locking and preload; the AWD
controller changes the actual axle torque split. Road, sport, racing, dirt and
snow tires differ in surface grip, working temperature and wear.

Hold N while driving to inject nitrous above 2,500 RPM at full throttle in a
forward gear. The metered flow adds crankshaft power and consumes the bottle;
installing the kit does not include gas. SERVICE refills it, changes oil or
restores chassis rigidity. Oil ages with distance, running time and heat,
increasing engine losses. Chassis fatigue reduces effective suspension and
anti-roll stiffness. These are bounded gameplay models, not measured oil or
chassis-life calibrations.

Plan saves installed parts, setup, oil/chassis condition, mileage and remaining
nitrous automatically, plus credits and owned parts. Saves are kept outside
the project assets in SDL's per-user Spartan/Plan directory (on Windows,
%APPDATA%/Spartan/Plan). The shop saves after successful changes, and Plan
autosaves every 30 simulation seconds and when play stops. Do not package
these personal saves with the project.

A car's prefab lives in its own folder next to its preset and model, `project/plan_resources/cars/<car>/<car>.prefab`. Every car placed in a world today is `project/plan_resources/cars/ferrari_laferrari/ferrari_laferrari.prefab`: the player cars, the AI racers (`race_driver car_prefab`), the route drivers, the showcase and dyno cars, and the traffic (which skips the lights and effects). The prefab owns the drivable car definition, the metallic paint defaults, the body fit, the headlights, brake lights, exhaust and tire effects. Scene instances only override camera/HUD/dyno behavior and optional paint colors through prefab attributes, and add scene specific parts such as the playground's `car_reset` and spawn point.
