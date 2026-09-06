# ReSTIR correctness checks

The September 2026 dark-room investigation found several independent lighting defects:

- An empty **visible** light list inserted an 85,000-lux sun without shadows, including in loaded worlds whose point lights were culled. The fallback now applies only to an empty editor without authored lights. Zero-light frames also upload a cleared GPU slot 0, with valid directions, so sky shaders cannot retain an earlier sun.
- The emissive-triangle sampler computed tiny solid angles by subtracting nearly equal angles. Float cancellation generated rays outside the actual emitter and incorrect PDFs. It now uses a stable determinant formula, switches to uniform-area sampling for very small/large projected triangles with the corresponding solid-angle PDF, and bounds spherical-sampling roundoff to the triangle.
- Fog injected unoccluded sky probes into enclosed rooms. With ray-traced shadows enabled, both ambient probes now test visibility; the sun probe also checks that slot 0 really is directional. Fog shadow queries now finish traversal and commit triangle candidates.
- Reflection and path-tracing hits interpreted the missing BC5 blue channel as negative tangent-space Z. They now reconstruct positive Z and apply normal strength consistently with the G-buffer. Reflection hits also use inverse-transpose normals, face orientation and an orthogonalized tangent basis.

The preceding ReSTIR corrections remain in place:

- Spatial and temporal reuse selected techniques based on whether their sampled contribution was positive. Valid zero-weight and occluded draws now retain their technique in the MIS denominators. Duplicate seeds no longer select the spatial technique set; the existing duplication map controls their temporal confidence.
- Temporal backward density evaluation was conditional on forward success and omitted visibility. Both directions now evaluate independently with visibility.
- Three inline ray queries read committed status after a single `Proceed`. They now finish traversal and commit non-opaque triangle candidates, matching the current surface-only `TraceRay` path (which has no transmission/any-hit shader). This is not an implementation of transparent light transport.
- The primary footprint divided by `cos * 4*pi` instead of `cos / (4*pi)`. This made the reconnection threshold about 157.9 times too small.
- Paired reuse applied depth/distance gates relative to whichever pixel was the caller. Both directions now use the same pair depth and agree on eligibility.

Run from the repository root:

```powershell
node tools/restir_tests/resampling.mjs
node tools/restir_tests/temporal.mjs
node tools/restir_tests/footprint.mjs
node tools/restir_tests/compatibility.mjs
node tools/restir_tests/visibility.mjs
node tools/restir_tests/default_light.mjs
node tools/restir_tests/compile_shaders.mjs
# In a Visual Studio developer shell (requires cl.exe):
node tools/restir_tests/emitter_sampling.mjs
```

The Node checks execute extracted production blocks with controlled CPU fixtures. They cover scalar energy conservation for rare light samples, null histories, visibility in both directions, reciprocal geometry gates, the paper's footprint equation, four RayQuery traversal contracts, default-sun eligibility and GPU light-slot clearing. The original five checks fail on the pre-fix shaders and pass after correction. The compiler check builds 12 Vulkan SPIR-V variants with DXC: seven ReSTIR entry points, reflection tracing, and fog injection/integration with and without ray tracing. Generated binaries go under `binaries/restir_tests`.

The emitter test compiles the extracted HLSL function as C++ float arithmetic and compares 140,000 samples against independent double-precision quadrature. All corrected samples lie on the triangle; maximum relative solid-angle error was 0.000103 and diffuse-integral error was 0.000200. At distances 120 and 240, all 20,000 samples per distance from the old function missed the triangle. The test validates this fixture's sampling/PDF consistency, not every triangle geometry.

For example, with light-hit probability 0.001 and equal confidence, the old one-neighbor spatial block estimates 0.00124975 and the old temporal block estimates 0.0014995, against an expected 0.001. Both corrected blocks estimate 0.001. These are synthetic estimator results, not measured scene brightness or GPU performance.

## Live verification

A development/x64 Vulkan engine build succeeded, followed by live tests through a separate MCP instance on port 47779. A sealed six-cube room with no authored lights was tested with ReSTIR, ray-traced reflections, ray-traced shadows, fog density 1.4 and automatic exposure. The old build visibly illuminated its walls and floor. The corrected build produced identical RGB pixels in two separated 3180x1555 captures, with values only 0 or 1 out of 255. The optional VHS overlay was disabled for this measurement; its deliberate signal noise must not be counted as path-tracing instability.

A fixed-seed, non-flickering snapshot of the liminal scene was also tested at position (18, 1.97, 18), looking toward (30, 1.97, 30), with manual exposure (f/8, ISO 100, 1/60 s). The emitter correction alone reduced two-frame display-code RMS differences from 6.44 to 1.41 in a floor region and 51.30 to 8.30 on a wall. The final build was visually checked at that view with fog off and on. Some variation remains on lit surfaces. These are short fixed-view comparisons, not a converged HDR reference, a motion-sequence acceptance test, or a performance benchmark. Extra visibility queries have unmeasured GPU cost.

Local captures and fixtures are in `binaries/restir_tests` and `binaries/project/mcp/blockout/thumbnails` (ignored generated output). Generate the sealed-room fixture again with `node tools/restir_tests/make_dark_room.mjs`, then load the printed world path in the engine. It uses the existing liminal wallpaper material, six solid cubes, a locked camera, automatic exposure and no lights. Restart the engine after shader/C++ changes to reset histories.

## Comparison with ReSTIR PT Enhanced

Reference: [Lin, Kettunen and Wyman, 2026](https://research.nvidia.com/labs/rtr/publication/lin2026restirptenhanced/), particularly Eq. 5 and Sections 3, 5, 6 and 7. The complete paper was read, including visual verification of Eq. 5 on page 7. Query semantics were checked against [Microsoft's DXR specification](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html).

The implementation already has reciprocal pairing tables, a shift pre-pass, a 17x17 duplication map, RGB shading sums and dual-motion history lookup. These do not constitute the complete enhanced algorithm:

- `try_reconnection_shift` reconnects at the first secondary vertex; it has no hybrid prefix replay or search for a later reconnection vertex. Rejected paths only survive identity shifts. The paper's compacted replay and forced NEE reconnection are therefore absent.
- Initial tracing uses eight path candidates per pixel, plus emissive candidates; the paper evaluates one path tree per pixel. Each bounce also loops over all analytic lights. Its light-tile sampling and bounce-dependent NEE budget are absent. Reducing to one path without validating the missing reuse support could increase noise.
- Direct analytic illumination and primary reflections remain separate renderer passes. This is a hybrid renderer, not the paper's unified direct/indirect path-space estimator; copying its PDFs or Jacobian without matching the parameterization would be incorrect.
- Stored samples contain aggregated direct and suffix radiance rather than one selected terminal path from the entire tree. Refresh updates only the direct term at the reconnection vertex; suffix radiance and emissive/NEE samples are not fully revalidated. `backrooms.lua` moves six point lights between panels and flickers them, so lighting-history response still needs a dedicated moving-light test.
- The inverse-footprint test is skipped at roughness >= 0.2, whereas the paper's simplification is for diffuse/emissive vertices. A mixed glossy material is not guaranteed view-independent at that threshold. A consistent lobe/transport representation is needed before changing this in isolation.
- Duplication control uses a 16-to-64 confidence cap, minimum 2 and exponent 0.5. The paper tests a default cap of 20, minimum 1 and exponent 0.1. Current settings suppress correlation less aggressively; the paper explicitly notes that sample-dependent confidence introduces bias.
- Reservoirs consume 80 bytes rather than the paper's 64 bytes. Its storage and divergence optimizations remain performance opportunities.

Table 1 reports 35.73 ms for the baseline and 15.53 ms with all improvements across four scenes on an RTX 5880 Ada. The paired pass saves repeated shift work, not half the whole frame. These timings are not a speedup measured in Spartan.

A trial of the paper's duplication settings reduced variation but also markedly reduced scene brightness. It was reverted: the original cap and exponent remain. This investigation fixes demonstrated defects without claiming a complete port of the enhanced algorithm. The stable spherical-triangle sampling approach was also checked against [PBRT v4](https://www.pbr-book.org/4ed/Shapes/Triangle_Meshes).

Next visual acceptance should compare fixed cameras and identical motion through `liminal_space.world`, including a dark room, a panel-lit corridor and a doorway. Hold exposure constant; distinguish raw tracing, reuse and denoised output; test lights fixed, flickering and relocated separately. Compare average radiance against a converged reference as well as temporal variance and GPU pass time. A clean denoised image alone cannot establish estimator correctness.
