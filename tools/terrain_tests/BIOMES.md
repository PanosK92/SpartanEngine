# Island habitats and model palette

`biome_layers.json` is the shared tuning source for the new terrain defaults and
the five mesh scatter layers in `worlds/plan.world`. The existing grass, flowers,
GPU pebbles, sculpt, roads and buildings are kept. Meadow and forest-floor
materials share their habitat boundary with the vegetation rules.
The summer island disables snow coverage. An inactive snow layer no longer
imposes a treeline or removes grass; species altitude limits still apply.

- **Pine woodland:** four branch/crown shapes, including younger trees, clustered on eligible hillsides
  from 4–700 m, with a soft coastal exclusion, broad clearings and irregular edges.
- **Olive groves:** four tree shapes on gentle lower ground from 3–280 m (fading above 180 m),
  favouring openings outside the pine woodland.
- **Maquis scrub:** five assembled scanned shrub forms, in patches and woodland
  fringes. These have no collision.
- **Limestone outcrops:** two rock silhouettes used in buried hillside formations;
  nominal formations are 60 × 30 × 16 m before relief and burial vary them.
- **Loose stones:** three shapes shared between small rocks and larger fragments,
  with mineral masks, scree/deposition preferences and smaller clusters.

These are procedural habitat rules, not surveyed land-cover boundaries. Shared
functions in `data/shaders/terrain_habitat.h` are compiled by both C++ and HLSL.
Slope, elevation, insolation and deposition shape woodland patches. Forest-floor
cover follows those patches and meadow cover recedes inside them. Pines use this
woodland weight, olives favour gentle openings, and scrub fills suitable soil
and woodland fringes. Road, building and water exclusions still apply.
Peak densities are 360 pines, 160 olives and 720 shrubs per hectare of fully
accepted ground; canopy dimensions and clump spacing preserve open gaps. Mesh variants
divide one placement budget; adding a model does not multiply the density.
Seeds reproduce placements and model choices when a tile is regenerated.

Tree Canopy and Shrub Cover scatter flags identify acoustic vegetation independently
of the placement habitat. The largest horizontal mesh part represents each plant
once; its live instances feed the soundscape after road and building exclusions.
Changing a tree layer to Any habitat does not make its canopy acoustically disappear.

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

The seventeen exports comprise four procedural pines, four procedural olives, five
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

The last step regenerates `IslandScatterDefaults.h` and updates five scatter slots,
the meadow/forest-floor habitat flags, and summer snow coverage in the island.
It creates `binaries/project/backups/plan_before_biomes.world`
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

## Island wildlife

`source/world/IslandWildlife.h` adds a transient population only while playing
`plan`: 12 songbirds, 12 coastal gulls, 12 squirrel placeholders, 12 martens,
12 lizards and 12 butterflies. Animals are assembled from coloured cubes with
animated wings, heads, tails and legs; squirrels are an artistic placeholder,
not a claim about the island's species distribution. Martens, lizards and
butterflies are documented by [NECCA](https://necca.gov.gr/mdpp/m-d-ethnikon-parkon-zakynthou-ainou-kai-prostatevomenon-periochon-ionion-nison/).

The pool reuses animals within 180 m of the camera, creates at most two per frame,
and retries unsuitable ground at bounded intervals. Gulls prefer the coast;
ground animals reject steep slopes, water, roads, building masks and physical
obstacles. Animals forage around their home patches, rest, and flee nearby
players. Ground movement follows the terrain slope and checks escape routes before
turning. Turns are rate-limited; blocked animals wait between route probes and
do not play walking animation without actual travel. Squirrels and martens cycle
through roaming, foraging, sniffing and resting. Birds share patches in groups of
four, approach checked landing spots, fold their wings and peck on the ground,
then take off again. A close approach scatters nearby flockmates with staggered
reaction times. Running increases the alarm radius; a quiet approach lets ground
animals pause and watch for 2-4 seconds, with a cooldown before watching again.
Squirrels and martens leave alternating paw prints on gentle, sparse ground.
The transient print pool is capped at 96, renders within 35 m, and fades after
75 seconds before expiring at 90 seconds. Night lengthens perching and resting except for martens.
The population is illustrative ambient life, not a navigation or ecology simulation.
It has no colliders, no ray-tracing instances, and nearby-only shadows. Stop and
world unload remove all runtime animals without serializing them into the island.

New shrub exports use Poly Haven's CC0 shrub_01, shrub_02 and shrub_03 sources,
with URLs and content hashes retained in biome_sources.json. Runtime files stay
in the existing ignored asset directory; reproduce them with the commands above.

The island UFO uses the tracked `data/scripts/ufo_hover.lua`. It observes from
roughly 1.7 km away and 650 m above the player, with a 1.1 km inner distance
threshold. It hovers with subtle pitch, roll and vertical wobble, then repositions
in a roughly 0.16-second burst after 12-24 seconds or when the player moves away.
Being watched for 0.7 seconds triggers a farther, roughly 2.8 km vantage. Jumps
follow an arc rather than crossing directly over the player and sample terrain
for clearance. This is cinematic behaviour, not a physical simulation. Play start
acquires a high position outside the camera's forward view; edit mode keeps the
authored transform. All settings are exposed on the island's UFO script.

During island play, a four-minute cloud-cover cycle smoothly builds a cloud bank
and clears back to the authored coverage. Existing cloud advection and shadows
carry its movement; this pass does not add precipitation. Pausing freezes the
cycle, and stopping restores the authored coverage.

The shared wind field uses advected, domain-warped gradient noise at three
scales instead of elliptical gust stamps. Independently drifting detail changes
the fronts' outlines as they travel, while periodic sampling preserves tile seams.
