# Zakynthos Town

The town replaces the old city grid and port church blockouts in `worlds/plan.world`, under `Zakynthos Town / Zakynthos Town - city`. The existing ZANTE label identifies it on the island. The city centre is approximately `(8300, 8, -400)`.

The layout contains 586 buildings using 24 reusable architectural variants, two civic spaces, 11 new streets, 57 connected road junctions, 392 street olives and 131 lamp assemblies. Shops, shutters, balconies, awnings, rooftop details, a market, arcaded civic building, church and bell tower establish a Zakynthos-inspired town. This is an original gameplay layout, not a surveyed reconstruction. Solomos Square was a reference: https://culture.rezante.gr/en/history/poi/dionisios_solomos_square

Terrain is flattened to world Y=8 within X=7710..8920 and Z=-1120..300, with a 160 m smooth transition. Existing approach roads are graded to the plateau. The island sculpt file is `binaries/project/plan_resources/terrain_sculpt.bin`; distribute it with the world and `binaries/project/zakynthos_town` assets. The project asset directory is ignored by Git, so committing the world alone does not distribute the city.

## Rebuild

Run from the repository root. Python requires NumPy, Shapely and Pillow; mesh authoring requires Blender. The original road survey and sparse terrain baseline are retained under the asset package's `sources` directory for repeatable installation.

1. `python tools/map/plan_zakynthos_town.py`
2. `blender --background --python tools/map/build_zakynthos_town.py`
3. With an engine control bridge listening on port 47791: `node tools/map/import_zakynthos_town.mjs`
4. Close the world in the editor, then run `python tools/map/install_zakynthos_town.py`.
5. `python tools/map/verify_zakynthos_town.py`

The editable, instanced Blender scene is `binaries/project/zakynthos_town/sources/zakynthos_town.blend`. Generate authoring previews with `blender --background --python tools/map/preview_zakynthos_town.py -- overview` (also `street` or `square`). These previews use simplified ground and roads and are not engine screenshots.

The verifier checks IDs, asset and material references, building clearance, shared junction alignment and sculpt heights. Native rendering and arrival-triggered town road collision were verified after the September 14 loading fixes. A development-build run exited the loading state in about 23 seconds; background biome population completed around 39 seconds. These are local measurements, not a first-install or gameplay performance guarantee.

The engine now skips the duplicate pre-terrain generation of conforming roads and prepares distant static mesh collision when its bounds approach the camera's existing collision range. Editor cooking budgets advance with engine ticks even without rendered frames. The renderer's CPU/GPU bounding-box capacity now covers both the prepass and indirect draw budgets; the previous smaller buffer could overflow on the first island frame. Road and terrain preparation have separate loading labels and timings.
