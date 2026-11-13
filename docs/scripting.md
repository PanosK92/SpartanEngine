# Lua scripting

Gameplay in Spartan is written in **Lua 5.4**, bound with sol2. There is one Lua state for the whole engine. All bindings are registered in [`source/scripting`](../source/scripting), which is the authoritative reference for anything not listed here.

There are two ways to run Lua:

- A **Script component** on an entity, for behavior that belongs to that entity.
- A **world companion script**, `project/<world>_resources/scripts/<world>.lua`, for world-wide setup and game flow.

Scripts are project assets: they live in the `scripts/` folder of the world that owns them, `binaries/project/<world>_resources/scripts/`, and ship in the project package, not in Git.

Script components can also run [visual scripts](visual_scripting.md): node graphs built in the editor that compile to Lua.

## Script components

Add a **Script** component to an entity and point its `file_path` at a `.lua` file (paths are relative to `binaries/`, e.g. `project/my_world_resources/scripts/my_world_door.lua`). Right-clicking in the **Assets** panel and choosing **New Lua script** creates this template:

```lua
MyScript = {
    -- Values here will be exposed to component details.
    -- bMyValue = true,
    -- MyString = "Hello, World!",
}

-- Called once when the simulation starts.
function MyScript:Start(Entity)
end

-- Called once when the simulation stops.
function MyScript:Stop(Entity)
end

-- Called when the script component is removed from the entity.
function MyScript:Remove(Entity)
end

-- Called every frame before Tick. Useful to reset temporary states.
function MyScript:PreTick(Entity)
end

-- Called every frame. Main update function.
function MyScript:Tick(Entity)
end

-- Called when the entity is being saved.
function MyScript:Save(Entity)
    -- Return a table with any custom data to save
end

-- Called when the entity is being loaded.
function MyScript:Load(Entity, data)
    -- Restore data from the table returned by Save
end

return MyScript
```

The file **must return a table**. Every callback receives that table as `self` and the owning `Entity`.

| Callback | When |
| -------- | ---- |
| `Initialize(entity)` | When the component is added, and after a world load once saved values are applied |
| `Start(entity)` | When play starts |
| `PreTick(entity)` | Every frame while playing and not paused, before `Tick` |
| `Tick(entity)` | Every frame while playing and not paused |
| `Stop(entity)` | When play stops |
| `Remove(entity)` | When the component is removed |
| `Save(entity)` | On world save. Values in the returned table are stored on the component |
| `Load(entity, data)` | On world load, with the saved values |

Numbers, booleans and strings in the script table are shown in the inspector, saved as attributes on the `<script>` element, and restored on load. Use them as tunable parameters.

There are no collision callbacks. Query state from `Physics` or use `World.Raycast`.

### Reloading

**Reload** in the inspector or the Script Editor re-executes the file and replaces the table. It does not run `Start` again and does not re-apply saved values; stop and restart play to get a clean start. Companion scripts reload with the world.

### Errors

Every callback runs in protected mode. A Lua error is logged to the console with the callback name and entity id, and the engine carries on. `print(...)` writes to the console with a `[Lua]` prefix.

## World companion scripts

The companion script is the one named after the world inside its resource folder: `project/game_resources/scripts/game.lua` runs whenever `worlds/game.world` loads, before the entity scripts and before roads are generated. Top-level code runs at load; if the file returns a table, its callbacks run during play:

| Callback | When |
| -------- | ---- |
| `Start()` | First frame of play |
| `Tick(dt)` | Every frame of play, `dt` in seconds |
| `Stop()` | When play stops |

```lua
local plan = {}

WorldSystems.SetAmbientWildlife(true)
WorldSystems.SetAmbientAtmosphere(true)
WorldSystems.SetRoadDetails(true)
WorldSystems.SetLayeredRoads("roads", "project/materials/island_roads/", "island")

function plan:Start()
    main_menu = dofile("project/game_resources/scripts/plan_main_menu.lua")
    main_menu.Start()
end

function plan:Tick(dt)
    main_menu.Tick(dt)
end

return plan
```

Split large worlds into `<world>_<part>.lua` files loaded with `dofile`, as `game.lua` does with `plan_ui.lua`, `plan_main_menu.lua` and the shop scripts. If an entity in the world already uses the companion file as a Script component, it is not loaded a second time.

## API reference

The standard libraries `base`, `package`, `coroutine`, `string`, `math`, `table` and `io` are available.

### Math

| Type | Members |
| ---- | ------- |
| `Vector2`, `Vector3`, `Vector4` | Fields `x`, `y`, `z`, `w`. Operators `+ - * /`, unary `-`, `==`, `tostring`. Methods `Length`, `LengthSquared`, `Normalize`, `Normalized`, `Distance`, `DistanceSquared` |
| `Quaternion` | Fields `x`, `y`, `z`, `w`. `Quaternion.FromEulerAngles(pitch, yaw, roll)` in degrees, `Quaternion.FromLookRotation(direction, up)`, `Quaternion.Lerp(a, b, t)`, `Quaternion.Identity` |
| `BoundingBox` | `BoundingBox(min, max)`, `Intersects`, `Contains`, `Merge`, `GetClosestPoint`, `GetCenter`, `GetSize`, `GetExtents`, `GetVolume`, `GetMin`, `GetMax` |

### Global tables

| Table | Functions |
| ----- | --------- |
| `World` | `GetName`, `GetFilePath`, `GetBoundingBox`, `GetEntities`, `GetEntitiesLights`, `CreateEntity`, `RemoveEntity`, `GetEntityByName`, `GetEntityById`, `GetCameraEntity`, `SetActiveCamera`, `GetActiveCameraOverride`, `GetDirectionalLight`, `GetDaylightIlluminance`, `GetLightCount`, `GetAudioSourceCount`, `Raycast(origin, direction, max_distance)` returning `{ entity, position }` or `nil` |
| `World` (environment) | `Get/SetTimeOfDay`, `Get/SetWind`, `Get/SetPuddliness`, `Get/SetRain`, `Get/SetCloudCoverage`, `SetDateUtc`, `GetAirTemperature`, `GetRoadTemperature` |
| `WorldSystems` | `SetAmbientWildlife(bool)`, `SetAmbientAtmosphere(bool)`, `SetRoadDetails(bool)`, `SetLayeredRoads(parent, material_dir, prefix)` |
| `Input` | `GetKey(key)`, `GetKeyDown(key)`, `GetKeyUp(key)`, `GetMouseDelta`, `GetMouseWheelDelta`, `GetGamepadThumbStickLeft` |
| `Timer` | `GetDeltaTimeSec`, `GetDeltaTimeMs`, `GetDeltaTimeRealSec`, `GetDeltaTimeSmoothedSec`, `GetTimeSec`, `GetTimeMs`, `GetSimTimeSec`, `Get/SetTimeScale`, `Get/SetFPSLimit` |
| `Engine` | `IsEditorVisible`, `IsPlaying`, `IsPaused`, `Quit` |
| `Console` | `Set(name, value)` with the value as a string, `Get(name)`, for [console variables](console_variables.md) |
| `ResourceCache` | `LoadMesh(path, flags)` |
| `Sequencer` | `IsAvailable`, `Play(name, entity)`, `Stop`, `IsPlaying`, `GetTime`, `GetDuration` (editor builds) |
| `Hud` | Screen-space drawing, see below |
| `CarTuning` | Player car, parts, setup, service, wear, save and load, see below |

### Entity

| Area | Methods |
| ---- | ------- |
| Identity | `GetName`, `SetName`, `GetObjectID`, `Clone`, `IsTransient`, `SetTransient` |
| Transform | `Get/SetPosition`, `Get/SetRotation`, `Get/SetScale`, the `...Local` versions, `Translate`, `Rotate`, `GetForward`, `GetBackward`, `GetRight`, `GetLeft`, `GetUp`, `GetDown` |
| Hierarchy | `GetParent`, `SetParent`, `GetChildren`, `HasChildren`, `GetChildrenCount`, `GetChildByName`, `GetChildByIndex`, `GetDescendantByName`, `GetDescendants`, `IsDescendantOf`, `ForEachChild(fn)`, `ForEachDescendant(fn)` |
| State | `IsActive`, `GetActive`, `SetActive`, `IsDynamic` |
| Components | `GetComponent(ComponentType.X)`, `AddComponent(ComponentType.X)`, `RemoveComponent(ComponentType.X)`, `GetAllComponents`, `GetComponentCount` |

`ComponentType` values reachable from Lua: `AudioSource`, `Camera`, `Light`, `Physics`, `Render`, `Terrain`, `Volume`, `Script`, `ParticleSystem`, `Spline`, `Text3D`, `Animator`, `Ragdoll`. `GetComponent` returns the typed component or `nil`.

### Components

| Component | Highlights |
| --------- | ---------- |
| `Render` | `SetMesh(mesh or MeshType)`, `SetMaterial(material or path)`, `SetDefaultMaterial`, `GetMaterial`, `GetBoundingBox`, `SetFlag(RenderFlags, bool)`, `SetMaxRenderDistance`, `SetMaxShadowDistance`, `AddDecal(...)`, `ClearDecals` |
| `Physics` | Mass, friction, restitution, `Get/SetLinearVelocity`, `Get/SetAngularVelocity`, center of mass, `SetBodyType(BodyType)`, `SetStatic`, `SetBodyTransform(position, rotation)`, `IsGrounded`, cloth parameters. Vehicles add throttle, brake, steering, gears, wheel telemetry and assists |
| `Light` | Type, color, `SetTemperature` (kelvin), intensity in lux or lumens, range, angle, IES profile, area size, shadow and volumetric distances, `LightFlags`, `LightPreset` |
| `Camera` | FOV, near and far planes, exposure mode and auto-exposure settings, `CameraFlags` |
| `AudioSource` | `SetAudioClip(path)`, `PlayClip`, `StopClip`, `IsPlaying`, volume, pitch, loop, `SetIs3d`, `SetPlayOnStart`, mute, reverb settings |
| `Animator` | `Play(clip)`, `Stop`, `Pause`, `Resume`, `IsPlaying`, speed, loop, blend time, foot IK |
| `Ragdoll` | `Activate`, `Wake`, `IsDead`, `IsFrozen` |
| `ParticleSystem` | Emission rate, lifetime, size, gravity, direction cone, colors, texture, bursts, effect load and save |
| `Spline` | `GetPoint(t)`, `GetTangent(t)`, `GetLength`, `GetTAtDistance(d)`, control points, road mesh generation |
| `Text3D` | Text, font, size, alignment, `GenerateMesh` |

`Material.New()` creates a material: `SetProperty(MaterialProperty.X, value)`, `SetColor(r, g, b, a)`, `SetTexture(MaterialTextureType.X, path)`.

### Enums

`KeyCode` (keyboard `A`–`Z`, `Alpha0`–`Alpha9`, `F1`–`F12`, modifiers, arrows, `Click_Left/Middle/Right`, gamepad `Button_South/East/West/North`, `DPad_*`, shoulders, sticks), `ComponentType`, `BodyType` (plus `BodyType.Vehicle`), `WheelIndex`, `LightType`, `LightFlags`, `LightIntensity`, `LightIntensityUnit`, `LightPreset`, `CameraExposureMode`, `CameraFlags`, `MaterialProperty`, `MaterialTextureType`, `MeshType`, `MeshFlags`, `RenderFlags`, `Text3DAlignment`, `Intersection`.

### HUD

`Hud` draws in game-view pixels and is meant to be called from `Tick`:

| Function | |
| -------- | - |
| `GetSize()`, `GetMousePosition()` | Game view size and cursor, two return values each |
| `LoadFont(name, path)` | Registers a font for `Text` |
| `Rect`, `RectOutline`, `RectGradient`, `Line`, `Circle`, `Triangle` | Primitives |
| `Text(text, x, y, size, color, font, align, spacing)` | Returns the drawn width |
| `TextWidth(text, size, font, spacing)` | Measures text |

`project/game_resources/scripts/plan_ui.lua` is a full example.

### CarTuning

Operates on the player car, or on the entity passed as the first argument: `GetPlayerCar`, `GetName`, `IsOccupied`, `SetHeld`, `PlaceAt`, `GetStage`/`SetStage`/`GetStageMax` per part, `GetSetting`/`SetSetting`/`GetSettings`, `Service`, `GetDirt`, `Wash`, `GetTireWear`, `ReplaceTires`, `SetNitrous`, `GetStats`, `PreviewStats`, `Save`, `Load`, `ResetAll`. See `project/game_resources/scripts/plan_tune_shop.lua`.

## Example: a spinning, bobbing pickup

```lua
Pickup = {
    spin_speed = 90.0,
    bob_height = 0.25,
}

function Pickup:Start(entity)
    self.origin = entity:GetPosition()
    self.time   = 0.0
end

function Pickup:Tick(entity)
    local dt  = Timer.GetDeltaTimeSec()
    self.time = self.time + dt

    entity:Rotate(Quaternion.FromEulerAngles(0.0, self.spin_speed * dt, 0.0))

    local position = Vector3(self.origin.x, self.origin.y + math.sin(self.time * 2.0) * self.bob_height, self.origin.z)
    entity:SetPosition(position)

    if Input.GetKeyDown(KeyCode.E) then
        print("picked up " .. entity:GetName())
        entity:SetActive(false)
    end
end

return Pickup
```

`spin_speed` and `bob_height` appear in the inspector and are saved with the world.

## Visual scripting

Scripts can also be built as node graphs in the editor's **Visual Script** window. A visual script is an ordinary `.lua` file: the graph is stored as XML in a comment block at the top, and the Lua generated from it follows. The Script component runs it like any other script, with no interpreter at runtime, and its variables show up in the inspector like the values in the example above. Every function in the API reference is also a node.

See **[Visual scripting](visual_scripting.md)** for the editor, the node library, variables and the tutorial world, `worlds/visual_scripting_example.world`.

## Running Lua from tools

The [MCP bridge](mcp.md) command `execute_lua` runs a snippet in the engine's Lua state (edit mode only), which is handy for testing bindings without writing a file.
