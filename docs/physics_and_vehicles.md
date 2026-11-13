# Physics and vehicles

## Physics

Spartan uses **PhysX** ([`source/physics/PhysicsWorld.cpp`](../source/physics/PhysicsWorld.cpp)) with the TGS solver and continuous collision detection.

### Fixed timestep

Physics steps at **200 Hz** (5 ms), independent of frame rate, while the engine is playing and not paused. Each frame `PhysicsWorld::Tick` accumulates time and schedules up to 8 steps; slow motion (`Timer::SetTimeScale`) shrinks the step. The steps run on a worker thread while the frame renders:

```
PhysicsWorld::Tick         decide step count
World::Tick                components read and write bodies
PhysicsWorld::Simulate     start the steps on a worker
Renderer::Tick             render in parallel
PhysicsWorld::WaitForSimulation
```

Code that must run once per physics step (vehicles, buoyancy, kinematic bodies) registers with `PhysicsWorld::RegisterStepCallback(owner, callback)`, which runs before each PhysX step. Rendered transforms are interpolated between the previous and current step.

`physics.threaded` (default on) controls the worker; `physics.assist` lets the waiting thread help PhysX.

### The Physics component

[`Physics`](../source/world/components/Physics.h) turns an entity into a body. `BodyType` selects the shape:

| Body type | Use |
| --------- | --- |
| `Box`, `Sphere`, `Capsule`, `Plane` | Primitive shapes |
| `Mesh`, `MeshConvex` | Triangle mesh (static) or convex hull from the entity's `Render` mesh |
| `Heightfield` | Terrain |
| `Controller` | Kinematic character controller with grounding, crouch and jump |
| `Custom` | A body provided by a factory; this is how vehicles are created (`BodyType.Vehicle` in Lua) |
| `Cloth` | A deformable sheet made from the entity's `Render` mesh; see [Cloth](#cloth) |

Settings include mass, friction, rolling friction, restitution, static or kinematic, center of mass and per-axis position and rotation locks. Forces and velocities are set through `ApplyForce`, `SetLinearVelocity` and `SetAngularVelocity`. Static colliders far from the camera can be streamed out (`distance_streaming`).

`friction` is the static coefficient, which holds a resting body in place. `friction_rolling` is passed to PhysX as the dynamic coefficient, which applies once the body slides. Set both to the same value for a single coefficient; a body with high `friction` and low `friction_rolling` holds on a slope until something nudges it and then keeps sliding.

A `Sphere` body takes its radius from the `Render` mesh's bounds, so it matches what is drawn (`standard_sphere` has radius 1 at scale 1).

### Example world

[`worlds/physics_example.world`](../worlds/physics_example.world) is a playground with one station per feature. It opens on a plaza with a sign post and a floor map, a coloured path leads to each station, and floating `Text3D` labels say what each exhibit does. Press play and walk around. Every experiment waits in its start pose behind a podium with a red button: look at the button from within 4 m, the podium gets an outline, and a left click plays the experiment. Clicking again resets it and plays it again.

| Station | What it shows |
| ------- | ------------- |
| Cloth | Flags, velvet curtains to walk through, a trampoline, a hammock, a tablecloth over a vase and a dust sheet over a statue; see [Cloth](#cloth). The buttons drop crates on the trampoline, a ball in the hammock and the two sheets again, and one unpins the flags, then hangs them back |
| Bounce and friction | Five balls dropped in front of a height board with `restitution` from 0 to 0.95, and four boxes on 25 degree ramps with friction from 0.02 (ice) to 0.9 (rubber) |
| Stacks and dominoes | A crate pyramid, a plank tower, and a domino chain that a ball rolling down a ramp sets off |
| Machines | Kinematic bodies moved by script that push whatever they touch: a rotating sweeper arm and a lift to ride up to a deck, each with a button that starts and stops it. A seesaw launches a light box when a heavy one lands on the other end |
| Ragdolls | Animated dummies with a `Ragdoll` component on a stage. Each press of the cannon button fires one ball at the next standing dummy, and once all have fallen the next press stands them back up |

In play mode, a plain **left click** presses the button you are looking at, **Alt + left click** grabs a body or a cloth, and holding the right mouse button with **Alt** and left clicking shoots a cube.

The companion script `project/physics_example_resources/scripts/physics_example.lua` (see [World companion scripts](scripting.md#world-companion-scripts)) spawns the dummies, drives the sweeper, the lift and the cannon, and runs the buttons. Its `experiments` table maps each podium under the `buttons` root to the bodies and cloths it owns, matched by name prefix. At the start of play it records the pose of those bodies and makes the ones that would otherwise fall or roll static, so nothing has already happened by the time you arrive. A press puts every body back, makes the held ones dynamic again and calls `ResetCloth` on the sheets. The script finds the button with `World.Pick` along the cursor ray, or the camera centre while the right mouse button is held, and outlines the podium with `World.SetHighlight`. To add an experiment, clone a podium, rename it and its `<podium>_button` child, and add a row to the table.

### Cloth

A `Cloth` body turns the vertices of the entity's `Render` mesh into particles, so any mesh works; a subdivided grid such as the 33 x 33 `cloth_grid_33` used by the example world gives smooth folds. `mass` is the mass of the whole sheet, spread over its particles. The cloth station of the [example world](#example-world) shows every use.

| Property | Meaning |
| -------- | ------- |
| `cloth_pin_mode` | Which vertices hold still and follow the entity. `Edge` pins the edge furthest along the pin direction (flag, curtain), `OppositeEdges` that edge and the one opposite (hammock), `Border` the whole outline (trampoline), `None` pins nothing so the cloth falls and drapes (tablecloth, dust sheet) |
| `cloth_pin_direction` | World direction that picks the pinned edge, `0 1 0` pins the top |
| `cloth_stiffness` | Resistance to stretching. It also sets the slack of the tethers that hold each free particle to its nearest pin, about 2% at 1 and 12% at 0 |
| `cloth_bending` | Resistance to folding. Around 0.3 for a flag: below about 0.1 a sheet in wind folds over itself |
| `cloth_damping` | Velocity lost per step |
| `cloth_iterations` | Solver passes per 1/120 s, scaled to the 200 Hz step |
| `cloth_wind_enabled` | Whether the world's wind blows it; air drag applies either way |
| `restitution` | How much of an impact the sheet gives back: 0.75 makes a trampoline, 0.05 a hammock |

Changing the pin mode or direction rebuilds the cloth. `ReleaseCloth` drops the pins and `ResetCloth` returns the sheet to its rest pose; the MCP exposes them as the `cloth_release` and `cloth_reset` component actions.

**Simulation.** `Physics::StepClothAll` runs inside the 200 Hz physics step, before PhysX integrates it, with all cloths in parallel. Each step applies wind and drag per triangle, integrates the particles (Verlet), and then solves stretch, bending and tether constraints and collisions together for several iterations. The main thread only uploads the new vertices. Do not move cloth back to a per-frame tick: bodies would run a frame ahead of the sheet and tunnel through it.

**Collisions.** A cloth collides with every PhysX shape that overlaps its bounds.
- Static and kinematic shapes push the cloth out.
- Each dynamic body that touches the cloth gets a proxy that falls inside the step and is pushed by the sheet, weighted by mass and by how taut the sheet is at the contact. The body then receives the velocity change that lands it where its proxy ended up, plus the cloth's `restitution` for impacts faster than 1 m/s.
- The player's character controller is treated as an 80 kg body (or its own `mass` when that is above 1 kg). It sinks into a trampoline, is thrown back out, counts as grounded while the cloth holds it (so it can jump), and takes the sheet's motion directly instead of adding pushes to its own gravity. Only cloth under its feet pushes back; a curtain gives way when the player walks into it.

**Grabbing.** In play mode, **Alt + left click** grabs the cloth at the point under the cursor, unless a dynamic body is nearer, which is grabbed instead. The nearest free particle follows the cursor and keeps its velocity on release. Pinned cloth cannot be pulled further than its tethers allow.

**Rendering.** The deformed vertices are written into the mesh in place every frame. Meshlet bounds, the LOD bounding box and the entity's bounding box are recomputed from the particles, so culling follows the cloth wherever it moves. The ray tracing acceleration structure is refit rather than rebuilt. A cloth renders with any material; for see-through fabric that bends what is behind it, use a glass material (see [Rendering](rendering.md#materials)).

From Lua, `Physics` has `Get/Set` pairs for `ClothStiffness`, `ClothBending`, `ClothDamping`, `ClothIterations`, `ClothWindEnabled`, `ClothPinDirection` and `ClothPinMode` (`ClothPinMode.Edge`, `OppositeEdges`, `Border`, `None`), plus `ReleaseCloth` and `ResetCloth`.

### Queries and contacts

- `PhysicsWorld::RaycastStatic`, `BoxCastStatic` and `SphereCast` for C++; `World.Raycast` for Lua.
- Contacts from the last step are available through `PhysicsWorld::GetFrameContacts()`. Vehicles install a contact observer for chassis impacts.
- [`PhysicsCollisionPolicy.cpp`](../source/physics/PhysicsCollisionPolicy.cpp) defines which groups (characters, vehicles, pedestrians, ragdolls) collide and report contacts.
- Large worlds rebase the PhysX origin around the camera ([`PhysicsSceneOrigin.h`](../source/physics/PhysicsSceneOrigin.h)).

## Vehicles

A vehicle is an entity with a `Physics` component of type `Custom`, backed by [`CarPhysics`](../source/car/CarPhysics.h), plus the [`Car`](../source/car/Car.h) object that owns the entity hierarchy, player control, cameras, audio and effects. The model runs inside the 200 Hz physics step.

### Simulation step

[`car::Simulation::tick`](../source/car/CarSimulation.cpp) runs once per physics step:

1. **Input**: smoothed throttle, brake, steering and handbrake; assists.
2. **Suspension**: tread contact by convex sweeps, spring-damper, anti-roll bars, bump stops, bump steer, camber and toe.
3. **Drivetrain**: engine torque curve, turbo, clutch, gearbox (manual or automatic with rev-match), differentials (open, locked, limited-slip, AWD split).
4. **Tires**: Pacejka MF 5.2 with combined slip ([`CarPacejka.h`](../source/car/CarPacejka.h)), optionally sub-stepped.
5. **Chassis**: self-aligning torque, aerodynamic drag and downforce, ground effect, DRS, rolling resistance.
6. **Assists**: ABS, traction control, stability control.
7. **Condition**: tire and brake temperatures, tire wear and pressure, brake fade, oil and chassis condition.
8. **Telemetry** recording.

PhysX then integrates the chassis. Traffic uses a cheaper mode (`VehicleSimMode::Cheap`) with kinematic wheels.

### Defining a car

Cars are `.car` XML files, loaded by `car::load_car_file` in [`CarPresets.cpp`](../source/car/CarPresets.cpp) and validated by `validate_preset`. Every car owns a folder, `binaries/project/game_resources/cars/<car>/`, holding `<car>.car` and, when the car has one, its body model (part of the downloaded project, not in Git). A `.car` file describes:

- `body`: the model, scale, orientation and parts to hide.
- `wheels`: wheel meshes and materials.
- Tuning sections for the engine, gearbox, differentials, suspension, tires, brakes, aerodynamics and mass.
- `packaging`: powertrain layout. When missing, [`data/cars/packaging.xml`](../data/cars/packaging.xml) supplies estimates per car file.

The drivable car, with its paint, lights, exhaust and tire effects, is wrapped in a prefab that lives in the car's own folder next to its `.car` preset and model, `project/game_resources/cars/ferrari_laferrari/ferrari_laferrari.prefab`. Every car in the bundled worlds is an instance of it with per-scene overrides.

To add a car:

1. Write `project/game_resources/cars/<car>/<car>.car` with its tuning, and put its body model in the same folder.
2. Add a `packaging` section, or an entry in `data/cars/packaging.xml`.
3. Make a prefab for it (copy the existing one), or reference the `.car` from `RaceDriver`, `RouteDriver` or `Traffic`.
4. Validate it through the MCP bridge with `vehicle_validate` and `vehicle_dyno`, then drive it.

### Driving and cameras

The player enters and exits cars in play mode; `CarReset` returns the player car to its spawn point. Cameras (`CarView`): chase (GT7-inspired, speed-dependent), hood and wheel. The driving HUD and telemetry panels are in [`CarHud.cpp`](../source/car/CarHud.cpp); **F3** (or the gamepad touchpad) toggles the telemetry HUD.

Engine sound is synthesized procedurally ([`CarEngineSoundSynthesis.cpp`](../source/car/CarEngineSoundSynthesis.cpp)), as is tire squeal. Rain beads on the paint, dirt and scratches accumulate on the body, and the camera lens gets wet ([`CarRain.cpp`](../source/car/CarRain.cpp), [`CarDirt.cpp`](../source/car/CarDirt.cpp), [`CarLens.cpp`](../source/car/CarLens.cpp)). Wet roads feed tire grip.

### AI

| Component | Behavior |
| --------- | -------- |
| `RaceDriver` | Spawns a grid of AI cars that race a closed spline circuit using a sampled racing line ([`RacingLine.cpp`](../source/car/RacingLine.cpp)) |
| `RouteDriver` | Drives from a start to an end point over the road network |
| `Traffic` | Populates the road network with ambient traffic |

AI driving is [`AiDriver`](../source/car/AiDriver.cpp): racing line following, skill, overtaking and following.

### Tuning from Lua

The `CarTuning` table and the vehicle methods on `Physics` expose parts, setup, service, wear and telemetry; see [Lua scripting](scripting.md#cartuning). The Plan tune shop (`project/game_resources/scripts/plan_tune_shop.lua`) is the reference.

### Tools

- MCP: `vehicle_list`, `vehicle_get`, `vehicle_enter`, `vehicle_exit`, `vehicle_set_input`, `vehicle_telemetry`, `vehicle_validate`, `vehicle_dyno`, `engine_sound_capture`.
- [`tools/engine_sound`](../tools/engine_sound): offline renderer and spectrogram comparison for the engine sound synthesizer.
- [`tools/ai_driver/analyze_trace.py`](../tools/ai_driver/analyze_trace.py): summarizes AI lap traces.

## Input

[`Input`](../source/input/Input.h) unifies keyboard, mouse, gamepads and steering wheels:

- `GetKey`, `GetKeyDown`, `GetKeyUp` with `KeyCode` covering keyboard, mouse buttons and gamepad buttons.
- Mouse position, delta and wheel; gamepad sticks and triggers; steering wheel steering, throttle and brake axes.
- Gamepad rumble and DualSense adaptive triggers, driven by the car (`GamepadDrivingFeedback`).
- `InjectKey` and `InjectMouseMotion` for automation (used by the MCP `input_inject` command).
