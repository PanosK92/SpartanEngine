# Lua Scripting

Spartan uses **Lua 5.4** for gameplay scripting. Scripts are attached to entities via the **Script** component and run within the engine's entity-component lifecycle. The Lua API mirrors the C++ API — same function names, same return types.

---

## Getting Started

### Creating a Script

Right-click in the file browser and select **New Lua script**. This generates a `.lua` file with the template below. You can also open and edit scripts directly in the built-in script editor (syntax highlighting included).

### Attaching a Script

Add a **Script** component to any entity, then assign a `.lua` file to it. The script loads immediately and hooks into the entity's lifecycle.

### Script Structure

Every script must create a table and return it. The engine calls named functions on that table at the appropriate times. The first argument (`self`) is always the owning **Entity**.

```lua
MyScript = {}

function MyScript:Start()
    -- called once when simulation starts
end

function MyScript:Stop()
    -- called once when simulation stops
end

function MyScript:Remove()
    -- called when the script component is removed
end

function MyScript:PreTick()
    -- called every frame before Tick
end

function MyScript:Tick()
    -- called every frame (main update)
end

function MyScript:Save()
    -- called when the entity is saved
end

function MyScript:Load(data)
    -- called when the entity is loaded
end

return MyScript
```

All callbacks are optional — implement only what you need.

---

## Lifecycle

| Callback    | When it runs                              |
|-------------|-------------------------------------------|
| `Start()`   | Once, when the simulation begins          |
| `Stop()`    | Once, when the simulation ends            |
| `Remove()`  | When the Script component is removed      |
| `PreTick()` | Every frame, before `Tick`                |
| `Tick()`    | Every frame                               |
| `Save()`    | When the entity is serialized             |
| `Load()`    | When the entity is deserialized           |

Every callback receives `self` (the owning Entity) as the first argument when using colon syntax.

---

## API Reference

### print

The global `print` function is overridden to route output to the engine console, prefixed with `[Lua]`.

```lua
print("hello from lua")            -- [Lua] hello from lua
print("pos:", entity:GetPosition()) -- [Lua] pos: Vector3(...)
```

---

### Timer

| Function                          | Returns | Description                          |
|-----------------------------------|---------|--------------------------------------|
| `Timer.SetFPSLimit(fps)`         | —       | Set the FPS cap                      |
| `Timer.GetFPSLimit()`            | float   | Get the current FPS cap              |
| `Timer.GetTimeMs()`              | float   | Elapsed time in milliseconds         |
| `Timer.GetTimeSec()`             | float   | Elapsed time in seconds              |
| `Timer.GetDeltaTimeMs()`         | float   | Frame delta time in milliseconds     |
| `Timer.GetDeltaTimeSec()`        | float   | Frame delta time in seconds          |
| `Timer.GetDeltaTimeSmoothedMs()` | float   | Smoothed delta time in milliseconds  |
| `Timer.GetDeltaTimeSmoothedSec()`| float   | Smoothed delta time in seconds       |

---

### World

| Function                         | Returns        | Description                                     |
|----------------------------------|----------------|-------------------------------------------------|
| `World.GetName()`                | string         | Current world name                              |
| `World.GetFilePath()`            | string         | File path of the current world                  |
| `World.GetBoundingBox()`         | BoundingBox    | Bounding box enclosing all renderable entities   |
| `World.GetEntities()`            | Entity[]       | All entities in the world                       |
| `World.GetEntitiesLights()`      | Entity[]       | All entities that have a Light component        |
| `World.CreateEntity()`           | Entity         | Create a new empty entity                       |
| `World.RemoveEntity(entity)`     | —              | Remove an entity and its descendants            |
| `World.GetLightCount()`          | int            | Number of active lights                         |
| `World.GetAudioSourceCount()`    | int            | Number of active audio sources                  |
| `World.GetTimeOfDay(use_real)`   | float          | Time of day (0.0–1.0). Pass `true` for real-world time |
| `World.SetTimeOfDay(time)`       | —              | Set simulated time of day (0.0–1.0)             |
| `World.GetDirectionalLight()`    | Light          | The primary directional light                   |

---

### Entity

Passed as `self` in all script callbacks.

#### Component Management

| Function                              | Returns   | Description                          |
|---------------------------------------|-----------|--------------------------------------|
| `entity:GetComponent(ComponentType)`  | Component | Get a component by type, or `nil`    |
| `entity:AddComponent(ComponentType)`  | Component | Add a component, returns it          |
| `entity:RemoveComponent(ComponentType)` | —       | Remove a component by type           |
| `entity:GetAllComponents()`           | table     | All components on this entity        |
| `entity:GetComponentCount()`          | int       | Number of attached components        |

#### Identity & State

| Function                | Returns | Description                    |
|-------------------------|---------|--------------------------------|
| `entity:GetName()`      | string  | Entity name                    |
| `entity:GetObjectID()`  | int     | Unique object ID               |
| `entity:GetObjectSize()`| int     | Object memory size             |
| `entity:IsActive()`     | bool    | Whether the entity is active   |
| `entity:IsTransient()`  | bool    | Whether the entity is transient|

#### Hierarchy

| Function                            | Returns   | Description                         |
|-------------------------------------|-----------|-------------------------------------|
| `entity:GetParent()`                | Entity    | Parent entity, or `nil`             |
| `entity:GetChildren()`             | Entity[]  | Direct children                     |
| `entity:HasChildren()`             | bool      | Whether children exist              |
| `entity:GetChildrenCount()`        | int       | Number of direct children           |
| `entity:GetChildByName(name)`      | Entity    | Find child by name                  |
| `entity:GetChildByIndex(index)`    | Entity    | Find child by index                 |
| `entity:IsDescendantOf(entity)`    | bool      | Whether this is a descendant        |
| `entity:ForEachChild(function)`    | —         | Iterate children with a callback    |

#### Transform

| Function                            | Returns    | Description                        |
|-------------------------------------|------------|------------------------------------|
| `entity:GetPosition()`             | Vector3    | World position                     |
| `entity:GetPositionLocal()`        | Vector3    | Local position                     |
| `entity:SetPosition(vec3)`         | —          | Set world position                 |
| `entity:SetPositionLocal(vec3)`    | —          | Set local position                 |
| `entity:GetRotation()`             | Quaternion | World rotation                     |
| `entity:GetRotationLocal()`        | Quaternion | Local rotation                     |
| `entity:SetRotation(quat)`         | —          | Set world rotation                 |
| `entity:SetRotationLocal(quat)`    | —          | Set local rotation                 |
| `entity:GetScale()`                | Vector3    | World scale                        |
| `entity:GetScaleLocal()`           | Vector3    | Local scale                        |
| `entity:SetScale(vec3)`            | —          | Set world scale                    |
| `entity:SetScaleLocal(vec3)`       | —          | Set local scale                    |
| `entity:Translate(vec3)`           | —          | Translate by offset                |
| `entity:Rotate(quat)`             | —          | Rotate by quaternion               |

#### Direction Vectors

| Function              | Returns | Description |
|-----------------------|---------|-------------|
| `entity:GetUp()`      | Vector3 | Local up    |
| `entity:GetDown()`    | Vector3 | Local down  |
| `entity:GetForward()` | Vector3 | Local forward |
| `entity:GetBackward()`| Vector3 | Local backward |
| `entity:GetRight()`   | Vector3 | Local right |
| `entity:GetLeft()`    | Vector3 | Local left  |

---

### Math Types

#### Vector2

```lua
local v = Vector2(1.0, 2.0)
```

| Field / Method          | Description                           |
|-------------------------|---------------------------------------|
| `v.x`, `v.y`           | Component access                      |
| `v[1]`, `v[2]`         | Index access (1-based)                |
| `+`, `-`, `*`, `/`     | Arithmetic (vector or scalar)         |
| `-v`                    | Unary negation                        |
| `==`                    | Equality comparison                   |
| `tostring(v)`           | String representation                 |
| `v:Length()`            | Magnitude                             |
| `v:LengthSquared()`    | Squared magnitude                     |
| `v:Normalize()`        | Normalize in place                    |
| `v:Normalized()`       | Returns normalized copy               |
| `v:Distance(other)`    | Distance to another Vector2           |
| `v:DistanceSquared(other)` | Squared distance                  |

#### Vector3

```lua
local v = Vector3(1.0, 2.0, 3.0)
```

| Field / Method          | Description                           |
|-------------------------|---------------------------------------|
| `v.x`, `v.y`, `v.z`   | Component access                      |
| `v[1]`, `v[2]`, `v[3]`| Index access (1-based)                |
| `+`, `-`, `*`, `/`     | Arithmetic (vector or scalar)         |
| `-v`                    | Unary negation                        |
| `==`                    | Equality comparison                   |
| `tostring(v)`           | String representation                 |
| `v:Length()`            | Magnitude                             |
| `v:LengthSquared()`    | Squared magnitude                     |
| `v:Normalize()`        | Normalize in place                    |
| `v:Normalized()`       | Returns normalized copy               |
| `v:Distance(other)`    | Distance to another Vector3           |
| `v:DistanceSquared(other)` | Squared distance                  |

#### Vector4

```lua
local v = Vector4(1.0, 2.0, 3.0, 4.0)
```

| Field / Method                  | Description                           |
|---------------------------------|---------------------------------------|
| `v.x`, `v.y`, `v.z`, `v.w`    | Component access                      |
| `v[1]`, `v[2]`, `v[3]`, `v[4]`| Index access (1-based)                |
| `+`, `-`, `*`, `/`             | Arithmetic (vector or scalar)         |
| `==`                            | Equality comparison                   |
| `tostring(v)`                   | String representation                 |
| `v:Length()`                    | Magnitude                             |
| `v:LengthSquared()`            | Squared magnitude                     |
| `v:Normalize()`                | Normalize in place                    |
| `v:Normalized()`               | Returns normalized copy               |
| `v:Distance(other)`            | Distance to another Vector4           |
| `v:DistanceSquared(other)`     | Squared distance                      |

#### Quaternion

```lua
local q = Quaternion()
```

| Field        | Description        |
|--------------|--------------------|
| `q.x`, `q.y`, `q.z`, `q.w` | Component access |

#### BoundingBox

```lua
local bb = BoundingBox()                              -- default
local bb = BoundingBox(Vector3(0,0,0), Vector3(1,1,1)) -- min/max
```

| Method                    | Returns     | Description                                 |
|---------------------------|-------------|---------------------------------------------|
| `bb:Intersects(point)`    | Intersection| Test intersection with a Vector3 point      |
| `bb:Intersects(other_bb)` | Intersection| Test intersection with another BoundingBox  |
| `bb:Contains(point)`      | Intersection| Test if a point is contained                |
| `bb:Merge(other_bb)`      | —           | Expand to include another bounding box      |
| `bb:GetClosestPoint(point)`| Vector3    | Closest point on the box to a given point   |
| `bb:GetCenter()`          | Vector3     | Center of the box                           |
| `bb:GetSize()`            | Vector3     | Full size (max - min)                       |
| `bb:GetExtents()`         | Vector3     | Half-size extents                           |
| `bb:GetVolume()`          | float       | Volume of the box                           |
| `bb:GetMin()`             | Vector3     | Minimum corner                              |
| `bb:GetMax()`             | Vector3     | Maximum corner                              |

---

### Enums

#### ComponentType

Used with `entity:GetComponent()`, `entity:AddComponent()`, and `entity:RemoveComponent()`.

| Value                        |
|------------------------------|
| `ComponentType.AudioSource`  |
| `ComponentType.Camera`       |
| `ComponentType.Light`        |
| `ComponentType.Physics`      |
| `ComponentType.Renderable`   |
| `ComponentType.Terrain`      |
| `ComponentType.Volume`       |
| `ComponentType.Script`       |

#### Intersection

Returned by `BoundingBox` intersection tests.

| Value                    |
|--------------------------|
| `Intersection.Outside`   |
| `Intersection.Inside`    |
| `Intersection.Intersects`|

#### BodyType

Used with the Physics component.

| Value                |
|----------------------|
| `BodyType.Box`       |
| `BodyType.Sphere`    |
| `BodyType.Plane`     |
| `BodyType.Capsule`   |
| `BodyType.Mesh`      |
| `BodyType.MeshConvex`|
| `BodyType.Controller`|
| `BodyType.Vehicle`   |
| `BodyType.Max`       |

#### WheelIndex

| Value                  |
|------------------------|
| `WheelIndex.FrontLeft` |
| `WheelIndex.FrontRight`|
| `WheelIndex.RearLeft`  |
| `WheelIndex.RearRight` |
| `WheelIndex.Count`     |

#### LightType

| Value                   |
|-------------------------|
| `LightType.Directional` |
| `LightType.Point`       |
| `LightType.Spot`        |
| `LightType.Area`        |
| `LightType.Max`         |

#### LightIntensity

| Value                          |
|--------------------------------|
| `LightIntensity.bulb_stadium`  |
| `LightIntensity.bulb_500_watt` |
| `LightIntensity.bulb_150_watt` |
| `LightIntensity.bulb_100_watt` |
| `LightIntensity.bulb_60_watt`  |
| `LightIntensity.bulb_25_watt`  |
| `LightIntensity.bulb_flashlight`|
| `LightIntensity.black_hole`    |
| `LightIntensity.custom`        |

---

### Components

All components inherit from `Component`. Retrieve them with `entity:GetComponent(ComponentType.X)`.

#### Light

```lua
local light = entity:GetComponent(ComponentType.Light)
```

| Function                              | Returns     | Description                               |
|---------------------------------------|-------------|-------------------------------------------|
| `light:SetTemperature(kelvin)`        | —           | Set color temperature in Kelvin           |
| `light:GetTemperature()`              | float       | Get color temperature                     |
| `light:SetColor(vec4)`                | —           | Set light color (RGBA)                    |
| `light:GetColor()`                    | Vector4     | Get light color                           |
| `light:GetIntensityLumens()`          | float       | Intensity in lumens                       |
| `light:GetIntensityWatt()`            | float       | Intensity in watts                        |
| `light:SetIntensity(lumens)`          | —           | Set intensity by lumens (float)           |
| `light:SetIntensity(LightIntensity)`  | —           | Set intensity by preset                   |
| `light:SetAngle(radians)`             | —           | Set spot angle                            |
| `light:GetAngle()`                    | float       | Get spot angle                            |
| `light:SetAreaWidth(width)`           | —           | Set area light width                      |
| `light:GetAreaWidth()`                | float       | Get area light width                      |
| `light:SetAreaHeight(height)`         | —           | Set area light height                     |
| `light:GetAreaHeight()`               | float       | Get area light height                     |
| `light:SetDrawDistance(distance)`     | —           | Set shadow draw distance                  |
| `light:GetDrawDistance()`             | float       | Get shadow draw distance                  |
| `light:GetLightType()`               | LightType   | Get the light type                        |
| `light:SetLightType(LightType)`      | —           | Set the light type                        |
| `light:IsInViewFrustrum()`           | bool        | Whether the light is in the view frustum  |
| `light:GetBoundingBox()`             | BoundingBox | Light's bounding box                      |

#### AudioSource

```lua
local audio = entity:GetComponent(ComponentType.AudioSource)
```

| Function                          | Returns | Description                          |
|-----------------------------------|---------|--------------------------------------|
| `audio:SetAudioClip(path)`       | —       | Set the audio clip file path         |
| `audio:GetAudioClipName()`       | string  | Get the clip name                    |
| `audio:PlayClip()`               | —       | Start playback                       |
| `audio:StopClip()`               | —       | Stop playback                        |
| `audio:IsPlaying()`              | bool    | Whether the clip is playing          |
| `audio:GetMute()`                | bool    | Whether the source is muted          |
| `audio:SetMute(bool)`            | —       | Mute or unmute                       |
| `audio:GetPitch()`               | float   | Get pitch multiplier                 |
| `audio:SetPitch(float)`          | —       | Set pitch multiplier                 |
| `audio:IsSynthesisMode()`        | bool    | Whether synthesis mode is active     |
| `audio:SetSynthesisMode(bool)`   | —       | Enable or disable synthesis mode     |
| `audio:StartSynthesis()`         | —       | Start synthesis playback             |
| `audio:StopSynthesis()`          | —       | Stop synthesis playback              |

#### Physics

```lua
local physics = entity:GetComponent(ComponentType.Physics)
```

**General**

| Function                              | Returns | Description                          |
|---------------------------------------|---------|--------------------------------------|
| `physics:GetMass()`                   | float   | Get mass in kg                       |
| `physics:SetMass(float)`              | —       | Set mass in kg                       |
| `physics:GetFriction()`               | float   | Get friction coefficient             |
| `physics:SetFriction(float)`          | —       | Set friction coefficient             |
| `physics:GetFrictionRolling()`        | float   | Get rolling friction                 |
| `physics:SetFrictionRolling(float)`   | —       | Set rolling friction                 |
| `physics:GetRestitution()`            | float   | Get restitution (bounciness)         |
| `physics:SetRestitution(float)`       | —       | Set restitution                      |
| `physics:SetLinearVelocity(vec3)`     | —       | Set linear velocity                  |
| `physics:GetLinearVelocity()`         | Vector3 | Get linear velocity                  |
| `physics:SetAngularVelocity(vec3)`    | —       | Set angular velocity                 |
| `physics:SetCenterOfMass(vec3)`       | —       | Set center of mass offset            |
| `physics:GetCenterOfMass()`           | Vector3 | Get center of mass offset            |
| `physics:GetCapsuleVolume()`          | float   | Get capsule volume                   |
| `physics:GetCapsuleRadius()`          | float   | Get capsule radius                   |

**Vehicle**

| Function                                      | Returns | Description                              |
|-----------------------------------------------|---------|------------------------------------------|
| `physics:SetVehicleThrottle(float)`            | —       | Set throttle input (0–1)                 |
| `physics:SetVehicleBrake(float)`               | —       | Set brake input (0–1)                    |
| `physics:SetVehicleSteering(float)`            | —       | Set steering input (-1 to 1)             |
| `physics:SetVehicleHandbrake(float)`           | —       | Set handbrake input (0–1)                |
| `physics:GetVehicleThrottle()`                 | float   | Get throttle input                       |
| `physics:GetVehicleBrake()`                    | float   | Get brake input                          |
| `physics:GetVehicleSteering()`                 | float   | Get steering input                       |
| `physics:GetVehicleHandbrake()`                | float   | Get handbrake input                      |
| `physics:SetWheelEntity(WheelIndex, entity)`   | —       | Assign an entity to a wheel              |
| `physics:GetWheelEntity(WheelIndex)`           | Entity  | Get the entity assigned to a wheel       |
| `physics:SetChassisEntity(entity)`             | —       | Set chassis entity                       |
| `physics:GetChassisEntity()`                   | Entity  | Get chassis entity                       |
| `physics:SetWheelRadius(WheelIndex, float)`    | —       | Set wheel radius                         |
| `physics:GetWheelRadius(WheelIndex)`           | float   | Get wheel radius                         |
| `physics:GetSuspensionHeight(WheelIndex)`      | float   | Get suspension height                    |
| `physics:ComputeWheelRadiusFromEntity(WheelIndex)` | —  | Auto-compute radius from wheel entity    |

**Wheel Telemetry**

| Function                                        | Returns | Description                          |
|-------------------------------------------------|---------|--------------------------------------|
| `physics:IsWheelGrounded(WheelIndex)`            | bool    | Whether the wheel has ground contact |
| `physics:GetWheelCompression(WheelIndex)`        | float   | Suspension compression               |
| `physics:GetWheelSuspensionForce(WheelIndex)`    | float   | Suspension force                     |
| `physics:GetWheelSlipAngle(WheelIndex)`          | float   | Tire slip angle                      |
| `physics:GetWheelSlipRatio(WheelIndex)`          | float   | Tire slip ratio                      |
| `physics:GetWheelTireLoad(WheelIndex)`           | float   | Tire load                            |
| `physics:GetWheelLateralForce(WheelIndex)`       | float   | Lateral force                        |
| `physics:GetWheelLongitudinalForce(WheelIndex)`  | float   | Longitudinal force                   |
| `physics:GetWheelAngularVelocity(WheelIndex)`    | float   | Wheel angular velocity               |
| `physics:GetWheelRPM(WheelIndex)`                | float   | Wheel RPM                            |
| `physics:GetWheelTemperature(WheelIndex)`        | float   | Tire temperature                     |
| `physics:GetWheelTempGripFactor(WheelIndex)`     | float   | Temperature-based grip factor        |
| `physics:GetWheelBrakeTemp(WheelIndex)`          | float   | Brake temperature                    |
| `physics:GetWheelBrakeEfficiency(WheelIndex)`    | float   | Brake efficiency                     |

**Drivetrain & Assists**

| Function                              | Returns | Description                          |
|---------------------------------------|---------|--------------------------------------|
| `physics:SetAbsEnabled(bool)`         | —       | Enable/disable ABS                   |
| `physics:GetAbsEnabled()`             | bool    | Whether ABS is enabled               |
| `physics:IsAbsActive(WheelIndex)`     | bool    | Whether ABS is active on a wheel     |
| `physics:IsAbsActiveAny()`            | bool    | Whether ABS is active on any wheel   |
| `physics:SetTcEnabled(bool)`          | —       | Enable/disable traction control      |
| `physics:GetTcEnabled()`              | bool    | Whether TC is enabled                |
| `physics:IsTcActive(WheelIndex)`      | bool    | Whether TC is active on a wheel      |
| `physics:GetTcReduction(WheelIndex)`  | float   | TC throttle reduction                |
| `physics:SetTurboEnabled(bool)`       | —       | Enable/disable turbo                 |
| `physics:GetTurboEnabled()`           | bool    | Whether turbo is enabled             |
| `physics:GetBoostPressure()`          | float   | Current boost pressure               |
| `physics:GetBoostMaxPressure()`       | float   | Max boost pressure                   |
| `physics:SetManualTransmission(bool)` | —       | Switch to manual transmission        |
| `physics:GetManualTransmission()`     | bool    | Whether manual transmission is on    |
| `physics:ShiftUp()`                   | —       | Shift up a gear                      |
| `physics:ShiftDown()`                 | —       | Shift down a gear                    |
| `physics:ShiftToNeutral()`            | —       | Shift to neutral                     |
| `physics:GetCurrentGear()`            | int     | Current gear index                   |
| `physics:GetCurrentGearString()`      | string  | Current gear as string (e.g. "3rd")  |
| `physics:GetEngineRPM()`              | float   | Engine RPM                           |
| `physics:GetEngineTorque()`           | float   | Engine torque                        |
| `physics:GetIdleRPM()`               | float   | Idle RPM                             |
| `physics:GetRedlineRPM()`            | float   | Redline RPM                          |
| `physics:IsShifting()`               | bool    | Whether a gear shift is in progress  |

**Debug**

| Function                                      | Returns | Description                       |
|-----------------------------------------------|---------|-----------------------------------|
| `physics:SetDrawRaycasts(bool)`               | —       | Toggle raycast visualization      |
| `physics:GetDrawRaycasts()`                   | bool    | Whether raycasts are drawn        |
| `physics:SetDrawSuspension(bool)`             | —       | Toggle suspension visualization   |
| `physics:GetDrawSuspension()`                 | bool    | Whether suspension is drawn       |
| `physics:DrawDebugVisualization()`            | —       | Draw all debug visuals            |
| `physics:SyncWheelOffsetsFromEntities()`      | —       | Sync wheel offsets from entities  |

#### Mesh

```lua
local mesh = entity:GetComponent(ComponentType.Renderable)
-- mesh access is typically through the Renderable's underlying mesh
```

| Function               | Returns | Description              |
|------------------------|---------|--------------------------|
| `mesh:GetVertexCount()`| int     | Total vertex count       |
| `mesh:GetIndexCount()` | int     | Total index count        |
| `mesh:Clear()`         | —       | Clear all geometry data  |
| `mesh:SaveToFile(path)`| —       | Save mesh to file        |
| `mesh:LoadFromFile(path)`| —     | Load mesh from file      |

#### Renderable

```lua
local renderable = entity:GetComponent(ComponentType.Renderable)
```

Currently exposed as a type with component base class access. Further properties are accessible through the C++ API as bindings expand.

---

## Example: Rotating an Entity

```lua
Rotator = {}

function Rotator:Tick()
    local speed = 45.0 * Timer.GetDeltaTimeSec()
    local rotation = Quaternion()
    rotation.y = speed
    self:Rotate(rotation)
end

return Rotator
```

## Example: Playing Audio on Start

```lua
Ambience = {}

function Ambience:Start()
    local audio = self:GetComponent(ComponentType.AudioSource)
    if audio then
        audio:PlayClip()
    end
end

function Ambience:Stop()
    local audio = self:GetComponent(ComponentType.AudioSource)
    if audio then
        audio:StopClip()
    end
end

return Ambience
```

## Example: Toggling a Light

```lua
LightToggle = {}

function LightToggle:Start()
    local light = self:GetComponent(ComponentType.Light)
    if light then
        light:SetIntensity(LightIntensity.bulb_100_watt)
        light:SetTemperature(4000)
    end
end

return LightToggle
```

---

## Notes

- **Colon syntax** (`:`) passes `self` automatically. Use it for all script callbacks and component methods.
- **Dot syntax** (`.`) is used for table/namespace access like `Timer.GetDeltaTimeSec()` and `World.CreateEntity()`.
- **Error handling**: runtime errors are caught and logged to the console as `[LUA SCRIPT ERROR]` — they won't crash the engine.
- **Standard libraries** available: `base`, `package`, `coroutine`, `string`, `math`, `table`, `io`.
- **Lua reference**: https://www.lua.org/manual/5.4/manual.html
