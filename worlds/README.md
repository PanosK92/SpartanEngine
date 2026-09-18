# World files and project assets

Keep only `.world` files (XML scene definitions) in this directory. This README is
the only exception. Do not put supporting assets or generated output here.

All project assets belong in `binaries/project/`, which is excluded from Git and
distributed through the Dropbox `project.7z` download. This includes materials,
textures, models, Lua scripts, `.car` definitions, particle effects, audio,
previews, sequencer data, map exports and backups.

The engine runs from `binaries/`, so references in worlds and scripts use paths
such as `project/scripts/sun.lua`, `project/cars/ferrari_laferrari.car`, or
`project/dreamcore_materials/porcelain.xml`. Tools run from the repository root
must use `binaries/project/...` to access the same files. Keep `.world` references
in the root `worlds/` directory.

When adding or moving an asset, update its references and any tools that generate
or load it. Keep new assets out of Git, and include them in the Dropbox project
package when publishing an asset update. A local move does not update that
download automatically. Do not force-add assets or create resource subdirectories
here; the Git ignore rules intentionally allow only `.world` files and this note.

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
