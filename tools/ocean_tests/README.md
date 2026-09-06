# Water validation

From the repository root, run `tools\ocean_tests\run.cmd`. Requires Visual Studio C++ tools, Node.js and the Vulkan SDK (`VULKAN_SDK`). The build uses `binaries/ocean_tests/obj` and an isolated executable, so it does not share compiler outputs with normal engine builds.

The suite links the production Water component and checks its three editable settings, old-scene migration, save/load, cloning, bounds and non-finite inputs. It also compiles twelve Vulkan shader variants covering the spectrum, FFT, assembly, surface shading, foam composition and underwater optics. These checks do not replace visual validation.

The Water inspector provides:

- **Wave Size:** 0 is flat; 1 uses the wind-driven spectrum at its default height; 3 triples that height. Wind direction and speed remain world settings.
- **Water Clarity:** increases transmission as suspended-particle density decreases. Clear water still absorbs light.
- **Sea Level:** still-water height in world metres.

Old `amplitude * displacement_scale` values migrate to Wave Size, subject to its 0–3 range. Turbidity maps to Clarity. Detail count, crest sharpening, normal strength and caustic intensity use automatic defaults when loading; old technical C++ setters remain available for existing engine callers. New saves contain only `wave_size`, `clarity` and `sea_level`.

For visual checks, load `worlds/dreamcore.world`, view the platform legs from outside, and compare Wave Size 0, 1 and 2. Flat water should have no foam. Waves should compress at crests, leave fading whitewater, and produce broken contact foam at the moving waterline. Check close and distant views, clear and cloudy water, and a terrain coastline. Arbitrary-object contact uses visible depth geometry and remains a screen-space approximation; this change does not simulate wakes, spray, or full shallow-water fluid dynamics.

The displacement sign and directional compression criterion follow [Tessendorf et al., Visualization of Simulated Ocean](https://people.computing.clemson.edu/~jtessen/reports/papers_files/Os2022_tessendorf_reinhardt_gao.pdf).
