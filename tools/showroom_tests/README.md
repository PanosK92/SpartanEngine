# Local light shadow regression

The ray-generation shader cleared every local shadow slot to fully visible but
never traced the selected lights. The lighting pass still sampled those slots
instead of the shadow atlas. In `ferrari_showcase.world`, all three area lights
therefore lost their primary shadows when ray-traced shadows were enabled.

The fix restores one bounded opaque-only ray for point/spot lights and eight
fixed rectangle samples for area lights. Each selected light writes its own
slot. It retains the opaque instance mask and accept-first-hit traversal from
the car crash fix, without restoring the transparent layered traversal.
Local rays use a small receiver offset, and the SIGMA pack no longer discards
real blockers closer than 8 cm. Area lights retain their existing spatial filter.
The showroom's authored light placement, colors, and intensity are unchanged.

Run from the repository root:

```powershell
# In a Visual Studio developer shell (cl.exe):
node tools/showroom_tests/local_shadows.mjs
# Requires VULKAN_SDK or dxc on PATH:
node tools/showroom_tests/compile_shaders.mjs
```

The CPU regression executes the extracted production HLSL local-shadow helpers
and dispatcher as C++ against analytic blockers. It covers three simultaneous
area lights with shuffled slots and an inactive sun, full/partial/no occlusion,
near blockers, blockers beyond emitters, point/spot lights, invalid/unshadowed
slots, one-sided emission, rectangle-based range, and tube/panel sample bounds.
The compiler check builds four Vulkan SPIR-V variants: shadow ray library,
SIGMA packing, and lighting with ray tracing enabled and disabled.
Generated files go under `binaries/showroom_tests`.

Both checks passed. These are CPU geometry/dispatch and shader compilation
checks, not GPU visual or performance measurements. The running editor's MCP
bridge was unavailable, so live showroom verification remains outstanding.
Restart the engine to load the changed shaders, then inspect the tire contacts,
car interior, and the three overlapping soft shadows with RT shadows enabled.
