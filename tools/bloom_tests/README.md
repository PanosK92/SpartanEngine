# Bloom replacement and verification

The old render pass never dispatched its dedicated bloom downsample shader. It used generic box mip generation, inconsistent reconstruction footprints, a luminance threshold and unnormalized additive mip blending. Small moving highlights could therefore change halo energy with their position on the downsampling grid, and coarse levels showed through as blocks.

The replacement uses a half-resolution FP16 pyramid with normalized 13-tap overlapping-box filtering at every reduction. Four bilinear taps evaluate a positive cubic B-spline when reconstructing every level and compositing at full resolution. Level weights preserve constant-image brightness independently of mip count. Every mip has explicit, disjoint SRV/UAV bindings; tiny and odd dimensions are covered by bounds-checked dispatches.

Bloom scatters scene-linear light before exposure and tone mapping, without a brightness threshold or luminance-dependent firefly weighting. This avoids coverage-dependent gain and exposure-threshold pumping. There is no separate bloom history to trail behind motion. The prefilter sanitizes invalid samples and caps values to FP16's representable range while retaining hue.

`r.bloom` controls scattering strength. The default 1 redistributes about 4.07% of light into the halo; 0 disables the pass. `r.bloom_scatter`, exposed as Bloom spread in the editor, defaults to 0.7 and is clamped to 0.05–0.95. Higher spread weights broader levels. Existing scenes may need bloom strength retuned because the old threshold/additive calibration has been replaced.

The filtering follows [Jorge Jimenez's SIGGRAPH 2014 post-processing work](https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/). Positive cubic reconstruction and normalized scale blending are also used in [Unity HDRP's bloom reconstruction](https://github.com/Unity-Technologies/Graphics/blob/master/Packages/com.unity.render-pipelines.high-definition/Runtime/PostProcessing/Shaders/BloomUpsample.compute).

## GPU checks

Run from the repository root on Windows with Node.js and Visual Studio C++ tools:

```powershell
cmd /c tools\bloom_tests\run.cmd
node tools/lighting_tests/compile_shaders.mjs
```

`generate.mjs` extracts the production bloom HLSL. `gpu.cpp` executes it on a hardware D3D11 device, using an FP16 pyramid and actual per-mip views of one texture. Vulkan/SPIR-V compilation separately checks all four full production variants. Generated sources, executables and images are kept in ignored `binaries/bloom_tests`.

`generate_pass.mjs` also extracts the production `Pass_Bloom` and RHI pass/pipeline selection functions. `pass.cpp` records their dispatch sequence after manual and automatic exposure, with one and seven mip levels. This integration check catches stale shader dispatch that isolated HLSL tests cannot detect.

Recorded on RTX 5070 Ti:

- Across 129 subpixel highlight positions, halo energy variation fell from **11.863774%** to **0.094026%** of its mean. The regression limit is 0.5%. The legacy comparison uses the saved previous shader and a 2x2 average equivalent to its generic mip generation on this even-sized fixture.
- Subpixel linearity error is **0.003347%** with FP16 storage.
- Constant RGB scenes preserve brightness within 0.03% from black through 60,000 scene units. Alpha preservation, bloom-off identity and black immediately after a bright frame pass.
- Dimensions 1x1, 3x1, 31x19, 127x73, 256x256 and 801x451 pass, including rounded-up thread groups and one-level pyramids.
- Average D3D11 harness timings over 32 chains were **0.1813 ms at 1920x1080** and **0.7788 ms at 3840x2160**. These are isolated shader timings, not a guarantee for other GPUs or integrated Vulkan scheduling.
- All **28** lighting/post-processing Vulkan shader variants and the development/x64 engine build pass. Existing third-party linker warnings remain.

The suite writes `after.bmp`. To repeat the before/after comparison, first place the previous production bloom shader at `binaries/bloom_tests/bloom_before.hlsl`; the test then also writes `before.bmp`. A missing baseline does not prevent the production regression checks from running.

## Live renderer check

`live.mjs` builds a temporary world containing an amber panel, a thin white emissive tube and a small blue highlight, then loads it into the separately launched audit engine on port 47779. It uses manual camera exposure, GT7 tone mapping and TAAU. Runtime assets are placed under `binaries/lighting_tests/bloom_tests`.

```powershell
node tools/bloom_tests/live.mjs load
node tools/bloom_tests/live.mjs capture bloom_live.png
node tools/bloom_tests/live.mjs move 0.001
node tools/bloom_tests/live.mjs capture bloom_live_moved.png
node tools/bloom_tests/live.mjs profile
```

Allow rendered frames after loading or moving before capturing. The fixture requires the isolated audit executable/runtime described in [the lighting audit](../lighting_tests/README.md); it must not target the user's active world. Screenshots appear in the runtime's `project/mcp/blockout/thumbnails` directory.

Live Vulkan captures at 3180x1547 output show smooth colored halos around the panel, thin tube and pinpoint before and after a small camera translation. The bloom time block sampled approximately **0.16 ms** in this fixture. Capture is SDR even when the monitor uses HDR. Quantitative motion stability comes from the 129-position GPU test above, not two screenshots.

Bloom cannot recover light that rasterization or an upstream temporal upscaler has already lost. Extremely thin geometry, unstable specular shading and path-tracing noise still need appropriate antialiasing or denoising. The filter itself has no temporal history and does not suppress such source noise through nonlinear weighting.

## Showroom performance regression

The first bloom integration used `BeginTimeblock` without establishing a named RHI pass. Automatic exposure calls `EndPass`, which clears the pending pipeline's name. The following unnamed bloom `SetShader` calls then failed the RHI's pipeline-readiness check, leaving the exposure compute shader bound. Bloom's image-sized dispatches repeatedly executed that expensive exposure shader. The manual-exposure live fixture did not exercise this transition.

Bloom now uses `BeginPass`/`EndPass`, with explicit names for all four shader stages. The new production pass-sequence test fails against the previous implementation at the first bloom dispatch after automatic exposure and passes with the correction. The image filtering shader is unchanged.

The actual `worlds/ferrari_showcase.world` reproduced the fault at roughly 1 FPS, with approximately 2,220 ms CPU waits for GPU completion. With the fix, the same camera, 1920x934 render size, 3180x1547 output, automatic exposure, TAAU, reflections and bloom enabled ran at approximately 217 FPS in the development build on RTX 5070 Ti. Existing GPU image/motion checks still pass. Performance depends on scene settings and hardware.

## Showroom emitter colors

The three colored analytic lights originally shared the white `ceiling_light` emissive material. Their visible cylinders therefore emitted white into primary rendering, reflections and bloom, independently of the light components' warm, red and cyan colors. The showroom now references three separate emitter materials whose linear RGB matches the corresponding lights. Emission strength and the bloom/tone-mapping shaders are unchanged.

Run `node tools/bloom_tests/showroom_emitters.mjs` after downloading an older project asset package, or to regenerate these materials from the saved showroom light colors. It copies the existing ceiling material to `binaries/project/ferrari_showcase_resources/tube_*_emitter.xml` and assigns the matching RGB. The shared white material is retained for other users. These project assets follow the repository's normal exclusion from Git and must accompany the world when publishing a project asset package.

Live showroom captures confirm cyan, warm and red/pink halos with the normal GT7 output. Very bright tube centers still approach display white, as expected from the highlight shoulder. The GPU suite additionally checks the three source chromaticities throughout the bloom pyramid at dim and HDR intensities, allowing FP16 rounding and subnormal precision. Worst measured HDR channel-ratio error was **0.031257%**; the bloom filter does not turn the colored input white.
