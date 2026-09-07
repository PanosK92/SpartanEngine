# Compact ray payload checks

Run from the repository root with Node and the Vulkan SDK installed:

```sh
node tools/payload_tests/compile_shaders.mjs
node tools/restir_tests/compile_shaders.mjs
```

The payload check compiles GI, reflections (normal and debug modes), and shadows
to both Vulkan SPIR-V and DirectX DXIL. It validates SPIR-V and checks the compiled
four-word hit record, geometry-buffer stride, material index offset, and transform
offsets. The CPU upload also asserts the geometry record size during compilation.
Generated files go under `binaries/payload_tests`.

Payload-access warnings are treated as errors. Surface reconstruction receives
individual payload fields so DXC can see the reads at each `TraceRay` call site.
The reflection debug variant carries only the 4-byte distance.

GI now passes 16 bytes instead of 72 through `TraceRay`; reflections pass 16
instead of 48 (debug mode uses 4 instead of 52). The normal record contains hit distance, TLAS instance
index, primitive index, and two half-float barycentrics packed into one uint.
Closest-hit only records the intersection. The caller reconstructs the surface
using the existing material evaluation and ray origin/direction. Miss distance is
negative; reflection output still uses zero distance and the sky ray direction.
Shadows retain their existing 8-byte record. DirectX's maximum payload is 16 bytes.

The geometry record grows from 40 to 144 bytes to supply the material index and
linear object/world transforms outside closest-hit. CPU uploads explicit matrix
rows to avoid HLSL matrix-storage ambiguity, including inverse transforms for
nonuniform scale. Rebuild the executable along with the shaders: an old executable
uploads the old geometry layout and cannot run the new shaders correctly.

Half-float rounding introduces small interpolation differences. Decoding clamps
and renormalizes barycentrics so edge samples stay inside the triangle. Hit
positions still use full-precision ray distance rather than rounded barycentrics.

These checks establish compilation and layout consistency, not visual equivalence
or a speedup. Compare fixed cameras with identical settings, including terrain,
normal maps, mirrored/nonuniform transforms, triangle edges, and sky misses. Measure
GPU pass times for GI and reflections before claiming a performance improvement;
the smaller payload trades traversal state for caller shading and extra instance
data. The screenshot's 32% result is not a measured Spartan result.

Payload access qualifiers follow the [DXR specification](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#payload-access-qualifiers):
the caller reads the hit record, and closest-hit/miss write it unconditionally.
