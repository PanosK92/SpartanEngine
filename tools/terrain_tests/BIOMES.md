# Island habitats and model palette

`biome_layers.json` is the shared tuning source for the new terrain defaults and
the five mesh scatter layers in `worlds/plan.world`. The existing grass, flowers,
GPU pebbles, terrain materials, sculpt, roads, buildings and soundscapes are kept.

- **Pine woodland:** three branch/crown shapes, clustered on eligible hillsides
  from 35–520 m, with broad clearings and irregular edges.
- **Olive groves:** three tree shapes on gentle lower ground from 3–180 m,
  favouring openings outside the pine woodland.
- **Maquis scrub:** two assembled scanned shrub forms, in patches and woodland
  fringes. These have no collision.
- **Limestone outcrops:** two rock silhouettes used in buried hillside formations;
  nominal formations are 48 × 25 × 14 m before relief and burial vary them.
- **Loose stones:** three shapes shared between small rocks and larger fragments,
  with mineral masks, scree/deposition preferences and smaller clusters.

These are procedural habitat rules, not surveyed land-cover boundaries. Shared
continuous fields in `TerrainHabitat.h` define habitat patches across tile edges.
Existing slope, elevation, terrain material and exclusion masks constrain them.
Pines, scrub and rocks now use Any habitat: their local clumps and surface masks
already provide variation, without an extra noise field removing whole regions.
Olives retain their open-country preference. The coverage revision increases
densities and altitude/slope ranges while retaining road/water exclusion masks.
The smaller clumps create local spacing within those larger patches. Mesh variants
divide one placement budget; adding a model does not multiply the density.
Seeds reproduce placements and model choices when a tile is regenerated.

In **Terrain Editor → Props**, select a layer to adjust its Habitat, Mesh Variants
(semicolon-separated paths), density, height/slope limits and clumping. Habitat
selection applies to mesh layers; grass and pebble rings retain their GPU rules.
Old serialized layers without a habitat use Any and without variants use one mesh.
Edited existing layers keep their saved values; newly created terrain uses the new
palette defaults once its assets have been prepared.

## Assets and regeneration

Game-ready files are in `binaries/project/models/island_biomes/`, which is ignored
by Git like the rest of the project assets. Preserve this directory with the game
asset backup. The tracked scripts and `biome_sources.json` retain the preparation
recipe, download URLs, CC0 source pages, sizes and SHA-256 hashes.

The twelve exports comprise three procedural pines, three procedural olives, two
assembled shrubs and four scanned rock forms. Tree geometry is authored by the
generator; bark/needle textures and rock/shrub scans are from
[Poly Haven](https://polyhaven.com), whose assets are [CC0](https://polyhaven.com/license).
Source names and URLs for every download are in `biome_sources.json`. The trees
are art-directed approximations, not botanical scans. The source shrub files are
branch libraries, so the generator assembles their branches into bushes.

From the repository root, with Python 3 and Blender 5.2 installed:

```powershell
python tools/terrain_tests/fetch_biome_assets.py
& 'C:/Program Files/Blender Foundation/Blender 5.2/blender.exe' --background --python-exit-code 1 --python tools/terrain_tests/build_biome_models.py
python tools/terrain_tests/author_island_biomes.py --apply
```

The last step regenerates `IslandScatterDefaults.h` and updates only five scatter
slots in the island. It creates `binaries/project/backups/plan_before_biomes.world`
on the first application. Without `--apply`, it only regenerates the header.
Rebuild the engine after changing the generated defaults or scatter code.

Exports use metres, grounded origins and baked identity node transforms because
the scatter renderer consumes mesh-local vertices. Leaves carry the `_foliage`
material tag, including solid olive leaf geometry; this enables foliage shading
and prevents canopy collision. Rocks and shrubs are simplified to roughly
1,400–5,500 triangles; each tree is roughly 18,000 triangles. Instancing, visibility
limits and shorter shadow ranges bound rendering cost. Instanced collision uses
bounded convex hulls with an input-quantization retry for polygon-limit failures.
The model importer now respects glTF texture roles before filename guesses, so
packed leaf colour/alpha images keep their colour and cutout channels intact.
The g-buffer lights the visible side of two-sided foliage, including solid leaves,
instead of leaving their back faces almost black. Pine UVs stay inside the needle
cutout, clear of opaque cone/branch padding elsewhere in the atlas.
Terrain prop imports set `PostProcessPreserveLod0`, retaining the authored geometry
at LOD 0 with only lossless welding/reordering. Other imports keep their existing
density reduction unless they opt into this flag.
Distance LODs still simplify, but UV-preserving reduction no longer falls back to
an algorithm that joins separate leaf cards. The regression suite covers this
with a canopy of overlapping textured cards.

## Verification

`tools/terrain_tests/run.cmd` checks the production terrain placement algorithms,
grounding, exclusions, tile seams and deterministic habitat/variant helpers.
`python tools/terrain_tests/validate_biome_assets.py` checks the prepared palette,
node transforms, dependencies, triangle budgets, and unchanged world data.

For a disposable empty engine launched with `--mcp-control --mcp-port=47785`, run
`node tools/terrain_tests/live_biomes.mjs`. It loads the island and checks that all
five layers spawn. `--preview` uses a temporary copy with a free camera and no
editor location text/audio volumes; it never saves over the real world.
It also exports a diagnostic native pine mesh and checks that LOD 0 retained the
source triangle count, and that packed leaf RGBA bound to the colour slot. Do not
save a world from this disposable instance: the diagnostic export changes that
mesh's in-memory resource path.
Allow the engine bridge to finish starting before running the script.

`preview_biome_models.py` can be run through Blender with the same invocation as
the builder to produce a neutral-light palette image in `binaries/terrain_tests`.
Visual island checks complement these tests; the tests do not certify every tree
placement, collision shape or camera view across the entire island.
