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
