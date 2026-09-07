# Terrain and mountain rocks

For the current island habitat palette, preparation scripts and editor controls,
see [Island habitats](BIOMES.md). The island now uses smaller 48 × 25 × 14 m
formations and a varied model palette; the original formation defaults described
below remain the generic starting values for manually configured layers.

Run `tools\terrain_tests\run.cmd` from the repository root with Visual Studio C++ tools installed. It compiles the production terrain and instance algorithms with C++20, AVX2 (matching the engine), and `/W3 /WX`. Only logging, texture access (unused by these fixtures), and task dispatch use headless adapters. Assertions remain enabled.

The fixtures cover the rendered heightfield diagonal, boundary normals, repeatable seeds, independently generated tile boundaries, footprint exclusions, density/cap limits, neighbouring slabs, a broad size hierarchy, and cliff dimensions independent of asset units. Instance regressions check CPU/shader layout, identity, independent scales (including scales above 100), and rotation/placement precision. They do not certify an entire world's appearance or GPU performance.

In Terrain Editor → Props, **Mountain Formations** builds up to three overlapping bedrock slabs, three fractured shoulders, then smaller fragments at the downhill toe. Formation Length, Width and Slab Thickness specify metres independently of the imported asset; relief and a shared random factor vary the entire formation. Ordinary mesh scale/size settings control the fragments. Spacing and density control occupied formations. The generic formation parameters start at a 300 m span, 130 m depth and 90 m full thickness before burial. Other worlds without `mountain_rocks` retain ordinary scatter.

Embed Fraction seats the mesh bounds against the terrain. Thirty-seven footprint probes check rounded underside support, water and exclusion masks; slope/altitude preferences choose anchors rather than rejecting flatter support at the cliff toe. Formation placement remains deterministic across tiles. Caps retain contiguous formations before trimming the final group's fragment members. Sampling does not guarantee detection of exclusions narrower than the probe spacing.

**Surface Coating** deposits the local terrain's eligible cover materials on upward-facing rock ledges, with patch noise and normal detail breaking up coverage. Each ground layer's **Rock Cover** toggle controls eligibility. Standard soil, moss, grass, forest floor, sand and snow are eligible; bedrock and gravel are not. This transfers material appearance, not grass-blade geometry. Coating is separate from contact blending and defaults off for ordinary props.

The experimental contact collars have been removed: independent triangle strips could leave gaps or overlap on irregular rock contours. Contact material/normal blending and upward-facing surface coating remain. Old `contact_height` and `contact_width` XML attributes are ignored and omitted on save.

Mesh instances now use 32 bytes: full float position/XYZ scale and a signed 16-bit quaternion. The previous 16-byte format averaged all three scales, capped scale at 100, and used coarse rotation; it could render a different rock from the shape used for placement. The new decoder is shared by geometry, depth and culling. Normals use the inverse transpose for nonuniform scale; meshlet cone rejection is skipped for distorted cones, leaving the exact triangle test. Procedural grass retains its separate 16-byte format. Saved worlds still store instance matrices, so their XML representation is unchanged.

The terrain corrections include triangle-consistent CPU height sampling, correct edge-normal baselines, matching analysis-map coordinates for prop masks, snow respecting layer priority, removal of missing scatter strips at tile boundaries, and rejection of failed point-placement attempts. The default and island rock material transition starts earlier, at 28°, with grass yielding around 38°.

For shader validation with Windows SDK DXC:

```
dxc -E main_ps -T ps_6_8 -Wno-ignored-attributes -I data/shaders data/shaders/g_buffer.hlsl -Fo binaries/terrain_tests/gbuffer.dxil
```

The development project does not copy shader sources into `binaries/data/shaders`; synchronize changed shaders there before runtime validation. The CPU/GPU material layout shares `shared_buffers.h`, and coating uses existing padding slots without changing its stride.
