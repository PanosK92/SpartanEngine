# Dreamcore island generator

Load `worlds/dreamcore.world` to build the island with `worlds/dreamcore.lua`.
No engine rebuild is needed. Its nine varied tile platforms connect through twelve
4.4 metre causeways with parapets and open entrances. The spaces include arrival
gates, a roofless bathhouse, an arcade, theatre terraces, a suspended ziggurat,
a climbable observation stair, a hollow tower, a pavilion and a doorway garden.
The original FFT ocean, sun, ocean ambience and meditative music remain in the world.

The script's `seed` attribute defaults to `17`. Change it to vary platform sizes,
spacing, pastel colors and architectural proportions; `0` chooses a seed on load.
Reload the world after changing it. Geometry is generated synchronously before play,
uses static box colliders, and is transient so saving does not bake hundreds of boxes
into the world. Repeated initialization is guarded; replacing the script removes its
previous generated root. World reload regenerates any manually edited generated parts.

The five materials in `worlds/dreamcore_materials` reuse the installed
`project/materials/tile_white` textures with world-space UVs, pastel tints and a light
ceramic clearcoat. They do not require new assets or modify the original tile material.

## Checks

From the repository root:

```powershell
binaries/backrooms_tests/lua.exe tools/dreamcore_tests/generator.lua
```

Use Lua 5.4 if the bundled interpreter has not been built. Alternatively,
`tools/backrooms_tests/build.cmd` links it from the existing `lua.lib` with Visual
Studio tools, then runs the Backrooms checks.

The Dreamcore suite checks thirteen seeds for stable, finite geometry, separate
platform footprints, graph connectivity, standing-player clearance and continuous
floor support along three lanes of every center-to-center bridge route (including
entrances), spawn support, stair risers and headroom, bounded entity counts, material
loading, static collider setup, transient ownership, duplicate initialization and
script replacement. The clearance model uses a 0.3 m radius and 1.8 m standing height.
These are geometry and Lua integration checks, not a PhysX controller walkthrough.

The default seed was also loaded in the engine: 298 geometry instances, no Lua load
errors, an overhead and arrival-view render, and a play-mode spawn check with the
camera settling to 4.182 m above sea level. This confirms the starting collider;
the full bridge network is covered by the headless clearance checks above.
