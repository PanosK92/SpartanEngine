# Engine systems

## Animation

[`source/animation`](../source/animation) and the [`Animator`](../source/world/components/Animator.h) component.

- **Data**: `Skeleton` (joint hierarchy and bind pose) and `AnimationClip` (per-joint position, rotation and scale tracks). Both come from model import (FBX, glTF and the other Assimp formats) and are stored on the `Mesh`, or loaded from native clip files ([`source/resource/animation`](../source/resource/animation)).
- **Playback**: `Animator::Play(clip)` with speed, looping and crossfade blending. Transitions use an inertializer (`PoseInertializer` in [`AnimationEvaluate.h`](../source/animation/AnimationEvaluate.h)).
- **IK**: two-bone IK and ground-aware foot planting ([`AnimationIk.h`](../source/animation/AnimationIk.h)).
- **Skinning**: four bones per vertex, batched across all animators each frame.
- **Ragdolls**: the `Ragdoll` component drives the skeleton from physics.

[`worlds/character_animation_example.world`](../worlds/character_animation_example.world) is a working example. The ragdoll station of [`worlds/physics_example.world`](../worlds/physics_example.world) shows animated characters turning into ragdolls when a cannon ball hits them.

## Audio

Audio runs on SDL3. Playback is the [`AudioSource`](../source/world/components/AudioSource.h) component:

- Clip playback with streaming, looping, pitch and volume; 3D attenuation and Doppler.
- **Synthesis mode**: a callback generates samples on the audio thread. Car engines, tire squeal and rain ([`RainSoundSynthesis.h`](../source/world/RainSoundSynthesis.h)) are fully procedural.
- Reverb with room size, decay and wet mix; a `Volume` component applies regional reverb.
- Mixer groups ([`AudioMixer.h`](../source/audio/AudioMixer.h)): vehicle, ambience, SFX and music, each with gain, muffle and pitch, which the sequencer can animate.

`audio.ambience_volume` scales ambient sources.

[`worlds/audio_example.world`](../worlds/audio_example.world) is a sound garden: a looping 3D fountain, a drone that flies past with Doppler, the same bell outside and inside a reverb `Volume`, and a 2D birdsong bed.

## Navigation

A streamed Recast/Detour navigation mesh around the camera: 64 m tiles in an 11×11 window, built from terrain and static geometry, with DetourCrowd for pedestrian avoidance. Add a `Navigation` component; `Pedestrians` can use it. `Refresh()` rebuilds installed tiles in place after static geometry moves, such as a door opening. Lua scripts can query paths and drive crowd agents, see [`navigation_example.world`](../worlds/navigation_example.world). Details in [`source/navigation/README.md`](../source/navigation/README.md).

## Networking

[`source/network`](../source/network) is a small UDP layer with no dependencies: one server and its clients (8 by default), on a LAN or the same machine. Sessions exist only while playing and close when play stops.

- **Sessions**: `Network.Host(port, max_clients)` and `Network.Connect(address, port)` from Lua. The server is peer 0 and clients get ids from 1. Peers that go quiet for 5 seconds time out.
- **Messages**: strings of up to 1024 bytes, reliable and ordered by default, or unreliable. `Send` targets one peer and `Broadcast` targets everyone. `Network.Poll()` returns connect, disconnect, failure and message events.
- **Replication**: a [`NetworkSync`](../source/world/components/NetworkSync.h) component replicates its entity's transform. Its owner (0 for the server, or a client id) sends 20 snapshots a second. The server relays client-owned entities to the other clients, and receivers interpolate 100 ms behind. Each peer creates its own entities, so matching objects need the same network id on every peer.

There is no lobby, NAT traversal, encryption, prediction or physics replication. [`worlds/multiplayer_example.world`](../worlds/multiplayer_example.world) is a working server and client: run two instances. Details in [`source/network/README.md`](../source/network/README.md).

## Procedural geometry

[`source/geometry`](../source/geometry):

- [`GeometryGeneration.h`](../source/geometry/GeometryGeneration.h): cubes, quads, grids, spheres, cylinders, cones, tori, capsules, extruded, revolved and swept profiles, pipes along paths, the ocean clipmap and foliage geometry.
- **Trees**: `TreeGenerator` and `TreeSpecies` produce trees from species definitions in [`data/trees/species`](../data/trees/species); the editor's **Tree Creator** edits them.
- **Meshes** are processed with meshoptimizer: meshlets, LODs and vertex cache optimization. Foliage gets impostors as its last LOD.
- **Generated cache**: expensive generated data (road meshes, terrain products) is cached per world in `generated_cache/`, LZ4-compressed and checksummed, with a 2 GiB LRU budget ([`GeneratedCache.h`](../source/geometry/GeneratedCache.h)).

## Roads and splines

The [`Spline`](../source/world/components/Spline.h) component is a Catmull-Rom spline that can extrude a profile into a road, wall or tube, conform to terrain, form junctions and place instanced props. `SplineFollower` moves an entity along a spline. Layered road materials and road details (markings, props) are optional world systems enabled from the companion script. The MCP bridge can author roads (`spline_create_road`, `spline_connect`, `spline_junction`, `spline_decorate`) and block out districts and cities.

[`worlds/roads_example.world`](../worlds/roads_example.world) is a ring road and four avenues meeting at a junction. Its companion script enables layered asphalt and markings with `WorldSystems.SetLayeredRoads("roads", "project/materials/island_roads/", "roads_example")`, which applies to every spline under the `roads` entity. Lua cars drive the ring with `Spline:GetTAtDistance`, `GetPoint` and `GetTangent` and keep their distance, and a bus runs on a `SplineFollower` with no script.

## Terrain and vegetation

The [`Terrain`](../source/world/components/Terrain.h) component builds a heightfield with sculpt layers, a physics heightfield, and GPU-scattered vegetation (grass, flowers, trees with impostors). The **Terrain** editor panel sculpts and paints. Tires deform soft terrain (`terrain.deformation*` cvars), and vehicles push vegetation aside.

[Terrain](terrain.md) is the full guide: creating a flat terrain, the brush, what to check when nothing seems to happen, and the Shape, Ground, Life and Analysis tabs. [`worlds/terrain_example.world`](../worlds/terrain_example.world) is an island made this way from a flat square: a raised hill, a pond dug below sea level, a flattened plateau and rough bumps next to smoothed ones, with signs that explain each step. Its dirt trail and stamped rock patch demonstrate per-layer painting, erasing and brush-image import.

## Environment and weather

The world's `Environment` holds date, latitude and longitude (which position the sun), time of day, clouds, rain, wind and puddles. Rain wets exposed surfaces, fills puddles, keeps covered areas dry and lowers tire grip. Atmospheric scattering, volumetric clouds and froxel fog render it; `r.atmosphere.mist_*` cvars add mist per world. The FFT ocean is part of the renderer and reacts to wind; boats and cars leave wakes (`ocean.wake*`). Ambient wildlife and atmosphere (birds, insects, dust) are optional world systems.

In [`worlds/weather_example.world`](../worlds/weather_example.world), follow the pier to the coastal observatory. Keys 1–4 ease between clear skies, overcast, rain and storm. Watch the anemometer as you adjust wind speed with Up and Down, or step under the shelter to compare its dry floor with the exposed paving. Left and Right scrub the time of day, and T toggles time lapse.

## Particles

The `ParticleSystem` component simulates and emits on the GPU, collides with the depth buffer, blends softly and supports flipbooks, bursts, turbulence and vortices. Effects can be saved and loaded as files.

[`worlds/particles_example.world`](../worlds/particles_example.world) is a gallery of the built-in presets, one per pedestal. The companion script fades emitters on and off with `SetEmissionRate` and fires bursts as a short pulse of a high rate.

## Cinematics

The editor's **Sequencer** ([`source/editor/widgets/Sequencer.h`](../source/editor/widgets/Sequencer.h)) authors cutscenes on a timeline:

- **Camera** cuts and moves (rig, orbit, dolly, path), entity tracking, focal length, aperture and shake.
- **Spline** events drive `SplineFollower`s; **drive** events run real cars along splines with speed and rev keys.
- **Channels** animate time scale, mixer gains, time of day, rain, clouds, wind, puddles, fades and letterboxing.
- **Capture** renders frame sequences offline at a fixed timestep.

Sequences are saved as `project/sequencer_<name>.xml` and played from Lua with `Sequencer.Play(name, entity)`; Plan uses this for shop entrances. The sequencer lives in the editor, so `Sequencer.IsAvailable()` is false in builds without it.

[`worlds/cinematic_example.world`](../worlds/cinematic_example.world) is a five-shot cutscene around a dusk monument: a crane in, an orbit, a chase behind a glider on a `SplineFollower`, a shallow depth of field tilt up and a crane out. Its companion script plays `sequencer_cinematic_example.xml` shortly after play starts.

## XR

OpenXR with single-pass multiview stereo on Vulkan and DirectX 12 ([`source/xr`](../source/xr)). It is a work in progress and not started automatically: **Ctrl+0** toggles it.

## Steam

[`source/steam/Steam.h`](../source/steam/Steam.h) wraps Steamworks: achievements, stats, rich presence (strings in [`tools/steam`](../tools/steam)) and cloud saves. It compiles to stubs when the SDK is absent and is skipped with `--no-steam` or when headless. Development builds use the Spacewar app id (480); packaged games set their app id in `game.xml` through the game builder.
