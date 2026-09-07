# Vegetation wind

Run `node tools/wind_tests/compile_shaders.mjs` from the repository root with Vulkan SDK DXC available. This compiles the field, vegetation vertex paths, shadow depth, mesh shaders and GPU culling. It does not certify appearance or frame time.

Tree wind uses a shared root gust sample and slow, irregular sway, smooth spatial branch modes, and small leaf-only flutter. Physical height keeps bark and canopy submeshes consistent without relying on their different material bounds. Terrain layers with Wind enabled animate both wood and cutouts; only cutouts receive leaf flutter and subsurface scattering. Rocks in layers without Wind remain static. Grass keeps its existing gust response; the shared micro turbulence now moves more slowly (including the CPU wind sampler).

Roots remain anchored and all tree movement vanishes at zero horizontal wind. Fine leaf movement fades between 25 and 90 metres using the corresponding current/previous camera. Geometry and shadows use the same vertex function. GPU instance/meshlet bounds include a conservative rotation envelope, while LOD selection retains the original radius. Static cone and triangle rejection are bypassed for wind-deformed meshes; meshlet/instance culling remains active.

Limitations: branch structure is inferred from position, not an authored branch skeleton; physics collision remains undeformed. Existing previous-field sampling reconstructs history through advection and is approximate for evolving gusts. This is procedural animation, not a fluid or structural simulation.

For visual verification, load Plasma's island after rebuilding and synchronizing modified shaders into `binaries/data/shaders`. Observe near/far trees, roots, leaf/bark attachment, shadows and screen-edge motion at zero, light and strong wind; drive through the forest to assess temporal stability and GPU cost. No world or vegetation assets need conversion.
