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
| `Cloth` | Cloth with stiffness, damping and wind |

Settings include mass, friction, rolling friction, restitution, static or kinematic, center of mass and per-axis position and rotation locks. Forces and velocities are set through `ApplyForce`, `SetLinearVelocity` and `SetAngularVelocity`. Static colliders far from the camera can be streamed out (`distance_streaming`).

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
