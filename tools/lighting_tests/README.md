# Lighting and color audit — 8 September 2026

The core camera formula and most CPU light conversions were correct. The audit found and fixed errors in sky integration, spotlight normalization, reflection/fog attenuation, emissive color transport, exposure timing, and display conversion. These errors could compound or partly conceal one another. Existing scenes may consequently need their artistic exposure or light settings revisited.

## Unit contract

World distances are metres. Light authoring uses lux for directional lights and lumens for point, spot, and area lights. Color selects chromaticity; local-light colors have unit Rec.709 luminance so they do not silently change the authored lumen value. Black disables the light.

The shared `data/shaders/shared_lighting.h` defines the existing RGB storage scale, 683. Scene RGB is linear Rec.709 photometric light divided by 683. This is internally consistent, but calling every GPU light value “watts” is misleading: 683 is the peak photopic luminous efficacy, not a white lamp's spectral or electrical efficacy. The legacy `GetIntensityWatt()` API remains an alias, with this limitation documented. Watt-labelled bulb presets select lumen values; they do not model electrical power.

For a white source with authored illuminance E, flux Phi, and rectangle area A:

- Directional GPU intensity: E / 683. This is scaled irradiance; no distance falloff applies.
- Point GPU intensity: Phi / (4 pi 683). This is scaled intensity; inverse-square attenuation produces irradiance at a receiver.
- Spot GPU intensity: Phi / (Omega_effective 683). Omega_effective integrates the complete soft cone, including its squared cosine-space penumbra. The stored angle is the outer half-angle; the inner half-angle is 90% of it.
- One-sided Lambertian area GPU intensity: Phi / (pi A 683). This is scaled radiance. Rectangle geometry must then be integrated or approximated at the receiver.
- A Lambertian surface returns albedo times irradiance / pi. Under 1,000 lux, an 18% gray card emits approximately 57.296 nits toward the camera before specular and indirect contributions.
- White emissive textures at value 1 represent 10,000 nits. Emissive-from-albedo at strength 1 and white albedo represents 100,000 nits. Colored emission retains its RGB spectrum approximation and has the corresponding RGB luminance; these are existing authored calibrations, now shared by the relevant paths.

The camera uses `EV100 = log2(N*N / t * 100/ISO)` and `exposure = 1/(1.2 * 2^EV100)`, with shutter time t in seconds. These formulas were already correct. At f/16, 1/125 s, ISO 100, exposure is approximately 0.0000260417. An 18% card under 120,000 lux then reaches approximately 0.179 relative linear brightness before tone mapping. This calibration follows the [Filament camera and physical-lighting reference](https://google.github.io/filament/main/filament.html).

Automatic exposure replaces the manual multiplier. It does not multiply a second camera exposure into the scene. It meters scene luminance in nits; ISO, aperture and shutter retain their separate lens/noise/motion uses. The output path converts stored radiance back to photometric RGB, applies the selected exposure once, then tone maps and encodes for the display. The subsequent bloom replacement uses threshold-free scene-linear scattering, with exposure applied to scene and bloom together during output. See [bloom verification](../bloom_tests/README.md).

## Corrections

1. **Diffuse sky energy:** SH already returns irradiance. Its diffuse application omitted the Lambertian `1/pi`, making this contribution approximately 3.142 times too bright. A constant-environment white-furnace test now returns 0.18 for an 18% card.
2. **Spotlight lumens:** CPU intensity used the solid angle of a hard cone while shaders attenuated its outer band. The shared integral now preserves total authored lumens. Narrow-cone denominator limits, valid half-angles, fog and GI use the matching profile.
3. **Local light colors and presets:** manually authored RGB now preserves lumens, matching temperature colors. Selecting the custom intensity preset no longer sets intensity to zero. The existing directional atmosphere color model is retained.
4. **Reflected and volumetric analytic lights:** reflected lighting now uses the primary path's range, cone and area attenuation. The separate half-area factor in fog/reflections is removed. Range fading no longer becomes an unrelated hard cutoff in reflections.
5. **Area-light sampling:** rectangle samples preserve authored roll. A positive-only epsilon had destroyed the sign of `sin(au)`, biasing samples toward one side. The sign is preserved and `atan2` improves angular precision. The derivation is from [Urena, Fajardo and King](https://www.ugr.es/~curena/publ/2013-egsr/).
6. **Emission:** a dedicated RGB radiance buffer replaces a scalar emission channel. Emission textures no longer contaminate albedo or get multiplied by unrelated base color. Primary, reflected and path-traced emitters use the shared nits calibration. Reflection ray hits sample the actual emission texture, replacing hard-coded guesses.
7. **GI emission probes:** environment-probe ray hits now evaluate emission textures at the hit UV, including color-space decoding, instead of substituting material color. World-space UV inversion now agrees with the path-tracing hit evaluator. Textured-albedo, terrain and instanced authored emitters fall back from the constant-radiance triangle pool to BRDF/environment sampling, avoiding replacement by an incorrect flat emitter. This fallback can be noisier. Pool-cap fallback already existed and was retained.
8. **Exposure timing:** tone mapping explicitly binds the current metered exposure. Scene lighting keeps the previous exposure where needed before metering. This fixes the stale-exposure/reset fallback in display output. The initial audit also bound it in bloom; the subsequent threshold-free bloom replacement no longer needs that binding. The automatic cap now applies before stop compensation, so +1 stop doubles exposure even in dark scenes. Invalid history handling also rejects infinities.
9. **GT7 input scale:** the curve itself agrees with Polyphony's published implementation. Its framebuffer units are 100 nits and its SDR reference white is 250 nits; camera-normalized RGB now crosses this boundary with the required factor of 2.5. [Published GT7 implementation](https://blog.selfshadow.com/publications/s2025-shading-course/pdi/supplemental/gt7_tone_mapping.cpp).
10. **HDR output:** GT7's absolute output maps to HDR10 as nits/10,000 followed by PQ, and to scRGB as Rec.709 nits/80. Applying the OS SDR-white multiplier to GT7 scRGB had rescaled the already-mapped HDR peak. The other, SDR-oriented mappers continue to use OS SDR white. [Microsoft Advanced Color reference](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range).
11. **AgX and SDR transfer:** fixed inset/outset matrix orientation, their Rec.2020 working-space conversion, and missing sigmoid linearization before the final sRGB transfer. The existing saturation/contrast look remains an artistic extension. Negative power inputs are guarded. The ineffective Gamma slider is replaced by the actual fixed sRGB transfer label; the legacy cvar remains compatible. [Primary AgX implementation reference](https://github.com/mrdoob/three.js/blob/dev/src/renderers/shaders/ShaderChunk/tonemapping_pars_fragment.glsl.js).
12. **Vulkan HDR metadata:** maximum mastering luminance is passed in floating-point nits, without the incorrect 10,000 multiplier. Unmeasured black-level and content statistics are now zero, as specified for unknown values, rather than invented constants. [Vulkan metadata specification](https://docs.vulkan.org/refpages/latest/refpages/source/VkHdrMetadataEXT.html).

## Verification

Run from the repository root on Windows with Node.js, Visual Studio C++ tools, and the Vulkan SDK:

```powershell
cmd /c tools\lighting_tests\run.cmd
node tools/lighting_tests/compile_shaders.mjs
```

`generate.mjs` extracts the production C++/HLSL function bodies; the tests do not substitute a second implementation for the code under test. It downloads the MIT-licensed GT7 reference once into ignored build output and verifies its SHA-256. `gpu.cpp` executes the isolated production HLSL through D3D11 compute, while the shader compiler script separately checks full Vulkan/SPIR-V production variants.

Recorded results on RTX 5070 Ti:

- Point, area and directional unit checks pass. Numerical integration of spot emission over half-angles 0.6, 1, 5, 15, 30, 60 and 89 degrees has worst relative flux error **0.00642%**.
- ISO/shutter/aperture stop changes and temperature-color luminance checks pass.
- All six output modes preserve neutral colors and black in the tested range. GT7 maps exposed 0.18 to approximately **0.179378** linear SDR.
- **16,384** RGB/display-peak cases agree with the published GT7 reference within 0.1% of display peak; maximum observed difference is **7.559231 nits**, at the 10,000-nit end. CPU/GPU transcendental approximations are included in this tolerance.
- HDR10 and scRGB agree in decoded absolute luminance within 0.05% of peak; maximum observed difference is **1.311900 nits**. GT7 output stays within the target peak.
- Automatic +/-1-stop changes pass, including darkness. One second of logarithmic adaptation agrees at 30, 60 and 144 Hz. The sky white-furnace check passes.
- Rectangle sample bounds, solid angle, symmetry and second moments agree with analytic/numerically integrated references at distances 1, 3, 10, 30 and 100 metres. Coplanar receivers return a finite sample with zero projected solid angle.
- **28 full Vulkan shader variants compile**, including ray tracing, direct/indirect G-buffer paths, fog, output, all four replacement bloom stages and ReSTIR reuse passes.
- The development/x64 engine builds successfully. Existing third-party missing-PDB/import linker warnings remain. The isolated HLSL harness reports an existing histogram loop-variable shadow warning.

The default absolute intermediate-directory build failed before compilation with an MSVC PCH-path `Invalid argument` error. A fresh relative intermediate directory succeeded, without replacing the user's main executable:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' spartan.vcxproj /p:Configuration=development /p:Platform=x64 /p:PreferredToolArchitecture=x64 '/p:IntDir=binaries\lighting_tests\obj\' '/p:OutDir=binaries\lighting_tests\' /p:TargetName=spartan_lighting_audit /m /nologo /verbosity:minimal
```

### Live fixtures

`live.mjs` builds temporary materials/worlds under `binaries/lighting_tests` and controls a separately launched audit engine on port **47779**. It must be used with that isolated engine. Its runtime directory needs the engine DLLs, `data` resources and `project` resource directory, as usual. The audit used directory junctions for the latter two and copied the runtime DLLs; it did not replace an existing user world.

```powershell
node tools/lighting_tests/live.mjs load
node tools/lighting_tests/live.mjs capture lighting_emission_fixture.png
node tools/lighting_tests/live.mjs reflections
node tools/lighting_tests/live.mjs capture lighting_reflections_fixture.png
node tools/lighting_tests/live.mjs gi
node tools/lighting_tests/live.mjs capture lighting_gi_fixture.png
node tools/lighting_tests/live.mjs gi-black
node tools/lighting_tests/live.mjs capture lighting_gi_black_fixture.png
```

Allow rendered frames between loading and capture. Captures are written under the runtime's `project/mcp/blockout/thumbnails` directory. The first fixture disables lights, GI, reflections, bloom and other post effects, uses no tone mapper and a camera exposure of 0.00008. Red emission on black albedo and green emission on blue albedo render pure red and green. White emission texture and white albedo emission both represent 10,000 nits and match: sampled 8-bit SDR RGB was (231,231,230), consistent with linear 0.8 and R11G11B10 quantization. Direct red/green channels were 231. The mirror fixture retains these colors and samples 230 in reflected regions, versus 231 directly, with filtering/BRDF evaluation active.

The GI fixture contains only a green emission texture on a blue-base-color panel and an 18% gray floor, with no analytic lights. The floor receives green bounced light. A dense pixel check finds channel maxima (1,133,1), permitting at most one 8-bit code value in red/blue through the filtered capture path; the unrelated blue material does not produce blue GI. Replacing the emission texture with black leaves the floor black to within that same one-code-value tolerance, with maxima (1,1,1). `verify_capture.ps1 -Kind emission|reflections|gi|gi-black -Path <capture.png>` repeats the corresponding pixel assertions. Both isolated engine sessions were closed after verification.

## Deliberate approximations and limits

This is a physically based RGB renderer, not a spectral light meter. The 683 storage scale cannot recover an arbitrary lamp spectrum or electrical efficiency. Normal maps, finite light ranges, small-distance regularization, denoisers, reservoir clamps, rough-reflection compression and artist-controlled bounce boosts still affect measured energy. The audit is not a proof that every ReSTIR estimator or denoiser is unbiased.

Primary area lights use a representative/closest-point approximation and a near-field cap, not a full rectangular BRDF integral. ReSTIR samples the rectangle's solid angle. These paths can still differ in the near field despite sharing the correct source units.

The sun's atmospheric transmittance and directional authoring are retained. The visible sun disc/aureole and night-sky/fill values contain explicit artistic scaling, so the visible solar disc is not an absolute radiance reference. Restoring true solar-disc radiance would also require addressing the scene buffer's finite range and avoiding duplicate solar energy in sky lighting.

Automatic exposure intentionally meters a center-weighted percentile band and uses an artistic night key/cap. Its response need not equal the manual camera at every scene brightness. AgX's existing saturation/contrast look, bloom and other post effects also intentionally change appearance.

The RGB emission buffers add four bytes per render pixel per eye, plus four bytes per reflection pixel when reflections are enabled. GPU performance, stereo/XR, every imported material, physical monitor calibration and every weather/scene preset were not exhaustively validated. Live screenshots verify SDR capture and integrated rendering; the compute tests verify HDR encoding numerically, not the display's optical response.
