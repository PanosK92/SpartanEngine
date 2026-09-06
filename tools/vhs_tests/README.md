# VHS playback

The old `vhs.hlsl` has been replaced by a pass wrapper and the shared
`data/shaders/vhs_signal.hlsl` signal model. `liminal_space.world` enables `r.vhs`
and disables the separate film-grain and chromatic-aberration effects so they do
not obscure the tape treatment.

## Reference and intended look

The target is a moderately worn NTSC recording viewed through a digital capture,
with the game's aspect ratio retained. This is a baseband approximation suitable
for a real-time shader, not a simulation of the magnetic coating or FM carrier.

- [Canadian Conservation Institute, The Digitization of VHS Videotapes](https://www.canada.ca/en/conservation-institute/services/conservation-preservation-publications/technical-bulletins/digitization-vhs-video-tapes.html): dropout, time-base errors, tracking failures, and the distinction between video signal problems and display artifacts.
- [AV Artifact Atlas, Head Switching Noise](https://www.avartifactatlas.com/artifacts/head_switching_noise.html): the few disturbed lines near the bottom of an uncropped capture. Normal overscan often hides these.
- [AV Artifact Atlas, Tracking Error](https://www.avartifactatlas.com/artifacts/tracking_error.html): noise/distortion when the heads fail to follow recorded tracks.
- [Analog Devices, Understanding Analog Video Signals](https://www.analog.com/en/resources/technical-articles/understanding-analog-video-signals.html): nonlinear luma/chroma, reduced colour bandwidth, differential delay, and field timing.

The implementation uses a virtual 720 x 480 raster and a 60000/1001 Hz artifact
clock. It combines small line-wise timing errors with occasional five-field
mistracking bursts, short dropout streaks with imperfect neighbouring-line
compensation, and four to six head-switch lines. It filters brightness and colour
separately, delays the colour signal, and adds different bandwidths of luma and
chroma noise. Noise and transport are held within each video field, regardless of
the rendering frame rate. There is no Perlin/sine picture warping, generic RGB
split, vignette, warm colour grade, or CRT scanline mask.

The source image continues to update at the game frame rate. The vertical aperture
and tiny alternating registration offset approximate reconstructed video; true
interlaced motion/combing would need previous-field storage and is not claimed.
The steady-state path uses 28 filtered source samples per output pixel; a dropout
can add one more. In-engine GPU cost has not been measured.

VHS is SDR. On HDR desktops, the pass decodes HDR10/PQ or scRGB to nonlinear video
before processing, then displays the result at the OS SDR paper-white level.
Highlights beyond that recording range are clipped intentionally. The renderer
explicitly identifies screenshots and stereo HMD output as already SDR, preventing
an accidental second HDR conversion. This integration requires an engine rebuild;
reload `liminal_space.world` afterwards to apply its saved settings.

## Validation

From the repository root:

```powershell
node tools/vhs_tests/compile.mjs
tools\vhs_tests\build.cmd
```

The first command uses the Vulkan SDK's DXC to compile the production pass to
Vulkan SPIR-V and DXIL. The second finds Visual Studio and runs the production
signal functions in a standalone D3D11 compute harness (hardware, with WARP as a
fallback). It does not start or modify the live editor.

Checks cover:

- Identical noise within a held field and different noise in the next field.
- Neutral-grey preservation, finite output, and SDR bounds.
- Strong suppression of an isoluminant colour pattern while retaining a brightness
  pattern at the same spatial frequency.
- 600 transport fields: head switching confined to the bottom, small healthy
  displacement, and brief rather than continuous mistracking.
- Matching transport at 720 x 480 and 1440 x 960, plus partial dispatch groups.
- HDR10 and scRGB transfer round trips, known 203 nit white values, and equivalence
  with SDR signal processing.

If `binaries/screenshot_0.png` exists, the harness also renders 90 consecutive
playback fields from that existing capture to `binaries/vhs_tests/sequence`, plus
full-colour PNG comparisons. These demonstrate tape behavior over a fixed image;
they are not footage of a live camera walk-through. All generated files are under
the ignored `binaries/vhs_tests` directory.
