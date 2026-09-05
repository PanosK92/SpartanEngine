# Skid mark regression checks

Run `tools\skid_tests\run.cmd` from the repository root with the Visual Studio C++ tools installed. It compiles the production deposition/fade helpers with warnings as errors. Checks cover resting and unloaded wheels, normal rolling, wheelspin, locked braking, lateral slip, reverse symmetry, hysteresis, invalid telemetry, attack/release at 30/60/144 Hz, teleports, sharp reversals, contact-normal breaks, high-speed continuity, lifetime/capacity retirement, and half-float vertex fade packing.

For a visual driving check, launch a **disposable** development engine instance from `binaries` with `--mcp-control --mcp-port=47779`, load `worlds/plan.world`, then run:

```powershell
node tools/skid_tests/drive.mjs 47779
```

The script settles the player car, checks for idle trails, accelerates, performs a braking slide, and captures an overhead image. It leaves the instance paused in edit mode and does not save the world. The screenshot is written to `binaries/project/mcp/blockout/thumbnails/skid_regression.png`. After moving to edit mode, unpause briefly and pause again to finalize the strip end fade; move the editor camera closer to inspect the shoulders and endpoints.

## Rendering and lifetime

The skid pass uses ordinary source-alpha blending into opaque ground albedo before lighting. It preserves destination alpha and never writes depth, normals, motion vectors, or material IDs. Crossing marks therefore retain the underlying ground and previously deposited rubber. The RGBA8 tread mask is generated in memory with mipmaps; the former cached `materials/skid_marks/stain.png` is no longer used. Analytic shoulders retain feathering at coarse mips. Material opacity multiplies texture coverage and vertex fade.

Deposition requires tire load and measured contact-patch sliding velocity, with independent slip-ratio/angle scaling and a time-based intensity response. Unsupported wheels stop immediately. Moving receivers are skipped because these ribbons are stored in world space. Stationary burnouts accumulate into one feathered footprint. Geometry breaks on teleports, long frame stalls, reversals, large height steps, and abrupt normal changes; adjacent static road actors can share a continuous strip. Live vertex bounds replace the fixed box at the world origin.

The default and plan-world budget is 4096 quads per wheel. Marks fade over their final 12 seconds of a 60-second lifetime, and over the final 24 slots before capacity reuse. Segment spacing, opacity, UV repetition, and endpoint fade remain component settings. The material inherits the ground's normal and roughness; it does not deform grass or create physical ruts.

## Validation recorded for this change

- Development engine build passes; existing third-party linker/PDB warnings remain.
- Native regression checks pass, including vertex alpha round-tripping.
- In `plan.world`, the settling check created no skid entities. A driving run reached 120.8 km/h before braking and produced continuous trails at the player's location beyond 6 km from the origin.
- Overhead and close-up captures were visually inspected for ground detail through the marks, feathered shoulders, and strip-end fade. Captures are retained under `binaries/skid_tests` and the screenshot directory above.
- The runtime log contained no shader compilation or validation errors during that run.

This run validates the Vulkan path on the available machine. D3D12/XR and the full range of terrain transitions still need their own visual coverage; the numerical helper tests are not a substitute for those checks.
