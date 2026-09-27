# Worlds, their scripts and their resources

Each world has two homes:

- `worlds/` (this directory, version controlled) holds what defines the world as text:
  the `<world>.world` XML and the world's own Lua scripts. Name the main script
  `<world>.lua` and the rest `<world>_<part>.lua`, for example `liminal_space.lua`,
  `liminal_space_stalker.lua` and `liminal_space_soundscape.lua`. Keep the directory
  flat; the Git ignore rules allow only `*.world`, `*.lua` and this README.
- `binaries/project/<world>_resources/` holds every asset the world owns: materials,
  textures, audio, meshes, prefabs, the offline scripts that generate them (under
  `sources/`) and the engine's `generated_cache/`. For example, `liminal_space`
  keeps its materials at the root of `liminal_space_resources/`, plus `textures/`,
  `audio/` and `sources/`. `binaries/` is excluded from Git and distributed through
  the Dropbox `project.7z` download.

Do not put one world's content in shared folders. That means no world assets in
`project/scripts/`, `project/music/`, `project/mcp/blockout/`, other worlds'
resource folders or `tools/`. `tools/` is for engine and project tooling, not for
generators of a single world's assets; those go in `<world>_resources/sources/`.
Only content that several worlds share lives outside a resource folder:
`project/materials/` (texture library), `project/models/`, `project/cars/` and
shared scripts in `project/scripts/`, such as `footsteps.lua` and `sun.lua`.

MCP tools write to `project/mcp/blockout/` by default (materials, textures,
screenshots). When something made there becomes part of a world, move it into
that world's resource folder and update its references. The folder a world uses
is reported by `world_resource_directory_get`.

The engine runs from `binaries/`, so references in worlds and scripts use paths
such as `project/liminal_space_resources/audio/door_slam.wav` for assets and
`../worlds/liminal_space.lua` for world scripts, both in script components and in
`dofile`. Tools run from the repository root use `binaries/project/...` for the
same files.

When adding or moving an asset, update its references and any tools that generate
or load it. Keep assets out of Git, and include them in the Dropbox project
package when publishing an asset update. A local move does not update that
download automatically.

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
