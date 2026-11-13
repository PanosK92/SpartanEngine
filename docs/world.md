# Worlds, entities and components

A **world** is a tree of **entities**. An entity has a transform, children and at most one component of each type. Components hold all behavior and data. Worlds are saved as XML `.world` files in `worlds/`, with their assets in `binaries/project/<world>_resources/`.

Key files: [`World.h`](../source/world/World.h), [`Entity.h`](../source/world/Entity.h), [`components/Component.h`](../source/world/components/Component.h).

## World

`World` is a static class that owns every entity.

| Area | API |
| ---- | --- |
| Load and save | `LoadFromFile(path)` queues an asynchronous load; `SaveToFile(path)`, `SaveToFileAsync(path)`; `IsLoadingFromFile()`, `IsPreparing()` |
| Entities | `CreateEntity()`, `RemoveEntity(entity)`, `GetEntityById(id)`, `GetEntities()`, `GetRootEntities()`, `EntityExists(entity)` |
| Typed lists | `GetEntitiesLights()`, `GetEntitiesWithRender()` and similar, cached for the renderer |
| Camera and sun | `GetCamera()`, `SetActiveCamera(entity)`, `GetDirectionalLight()` |
| Environment | `GetTimeOfDay()`/`SetTimeOfDay()`, `GetEnvironment()` for weather, wind and puddles |
| Resources | `GetResourceDirectory()` returns the folder this world's assets belong in |
| Metadata | `GetName()`, `GetFilePath()`, `GetDescription()` |

There is no find-by-name on `World` in C++; walk `GetEntities()` or use `Entity::GetDescendantByName`. Lua has `World.GetEntityByName`.

### Loading

`LoadFromFile` only records the path. On the next frame `World::ProcessPendingLoad` clears the current world and a worker parses the XML. Entities are committed on the main thread, deferred script initialization runs, and the world enters **preparation**: terrain, road meshes, road details and other expensive generated data are built (and cached in `generated_cache/`). When preparation finishes `WorldLoaded` fires.

### Ticking

Each frame `World::Tick` runs, in order: the `before_tick` hook, every entity's `PreTick`, `after_pretick`, camera controls, the parallel render and physics component ticks, the logic ticks, animation skinning, pending road meshes, `after_tick`, ragdolls and weather.

## Entity

```cpp
Entity* entity = World::CreateEntity();
entity->SetObjectName("crate");
entity->SetPosition(Vector3(0.0f, 1.0f, 0.0f));
entity->SetParent(parent);

Render* render = entity->AddComponent<Render>();
render->SetMesh(MeshType::Cube);
render->SetDefaultMaterial();

Physics* physics = entity->AddComponent<Physics>();
physics->SetBodyType(BodyType::Box);
```

| Area | API |
| ---- | --- |
| World transform | `GetPosition`/`SetPosition`, `GetRotation`/`SetRotation`, `GetScale`/`SetScale`, `GetMatrix()` |
| Local transform | `GetPositionLocal`/`SetPositionLocal` and the rotation and scale equivalents, `GetLocalMatrix()` |
| Directions | `GetForward()`, `GetRight()`, `GetUp()` and their opposites |
| Hierarchy | `SetParent`, `GetParent`, `GetChildren`, `GetChildByName`, `GetDescendantByName`, `GetDescendantByPath`, `GetDescendants` |
| Active state | `SetActive`, `IsActive()` (own flag), `GetActive()` (including parents) |
| Components | `AddComponent<T>()`, `GetComponent<T>()`, `RemoveComponent<T>()` |
| Lifecycle | `Start`, `Stop`, `PreTick`, `Tick`, called by the world |
| Misc | `Clone()`, tags, `SetTransient(true)` to exclude from saving |

Rotations are quaternions. Children store local transforms; converting a parent-local position to world space uses the parent's matrix.

### Creating entities off the main thread

Entities built on a worker (model import, traffic spawning) are staged in a `World::EntityBatch` and published atomically:

```cpp
World::EntityBatch batch;
Entity* root = World::CreateEntity(); // goes into the batch
// ... build the hierarchy ...
batch.Commit();                                       // without Commit, the destructor discards it
```

## Components

Every component derives from `Component` and overrides only the hooks it needs:

| Hook | Called |
| ---- | ------ |
| `Initialize()` | Right after `AddComponent` |
| `PrepareGeometry()`, `PrepareWorld()` | During world preparation |
| `Start()` / `Stop()` | When play starts and stops. `StartsEarly()` moves a component to the front of the start queue |
| `PreTick()` / `Tick()` | Every frame |
| `Remove()` | When removed by id |
| `Save(xml)` / `Load(xml)` | World and prefab serialization |

Editable fields are registered as **attributes** (`SP_REGISTER_ATTRIBUTE_*` macros), which drive the inspector, cloning and undo. The component list is a single X-macro, `SP_ENGINE_COMPONENT_LIST` in `Component.h`; adding a component means adding it there and implementing the class in `source/world/components/`.

| Component | XML tag | Purpose |
| --------- | ------- | ------- |
| `Render` | `render` | Mesh and material, culling, LOD, shadows, ray tracing flags, surface decals |
| `Camera` | `camera` | Physical camera: aperture, shutter, ISO, FOV, exposure, first-person control |
| `Light` | `light` | Directional, point, spot and area lights, photometric units, IES profiles, shadows |
| `Physics` | `physics` | PhysX body: box, sphere, plane, capsule, mesh, convex, character controller, vehicle, cloth, heightfield |
| `Script` | `script` | A Lua script; see [Lua scripting](scripting.md) |
| `AudioSource` | `audio_source` | Clip playback or procedural synthesis, 3D attenuation, Doppler, reverb, mixer group |
| `Animator` | `animator` | Skeletal clip playback, blending, foot IK |
| `Ragdoll` | `ragdoll` | Physics-driven skeleton |
| `ParticleSystem` | `particle_system` | GPU particles |
| `Terrain` | `terrain` | Heightfield terrain, sculpt layers, GPU vegetation scatter |
| `Water` | `water` | Water surfaces |
| `Spline` | `spline` | Catmull-Rom splines extruded into roads, walls and tubes, with junctions |
| `SplineFollower` | `spline_follower` | Moves an entity along a spline |
| `Volume` | `volume` | Box region that overrides cvars and reverb while the camera is inside |
| `Text3D` | `text_3d` | World-space text |
| `SpawnPoint` | `spawn_point` | Player spawn location |
| `Navigation` | `navigation` | Streamed navigation mesh and crowd ([README](../source/navigation/README.md)) |
| `Pedestrians` | `pedestrians` | Ambient walkers |
| `Traffic` | `traffic` | Ambient road traffic |
| `RaceDriver` | `race_driver` | AI racers on a closed circuit |
| `RouteDriver` | `route_driver` | AI driver from A to B over the road network |
| `CarReset` | `car_reset` | Resets the player car to a spawn point |
| `CarService` | `car_service` | Service bay and car wash volume |
| `SkidMarks` | `skid_marks` | Tire marks from wheel slip |

## The `.world` file

[`worlds/empty_example.world`](../worlds/empty_example.world) is the smallest complete example: a camera on a character body, a sun with a script, and a floor.

```xml
<World name="empty" description="A blank slate, a camera, a floor and a sun">
 <OwnedResources>
  <Resource file="material_ground.xml"/>
 </OwnedResources>
 <Environment ... />
 <ConsoleVariables>
  <Variable name="r.grid" value="0.000000" />
 </ConsoleVariables>
 <Entities>
  <Entity name="floor" id="1001000000000000004" active="true" position="0 0.1 0" rotation="0 0 0 1" scale="1000 1 1000">
   <physics body_type="2" is_static="true" ... />
   <render mesh_name="standard_quad" material_path="./project/empty_example_resources/materials/material_ground.xml" ... />
   <Entity ...> <!-- children nest -->
  </Entity>
 </Entities>
</World>
```

| Section | Contents |
| ------- | -------- |
| `OwnedResources` | Asset file names in the world's resource folder that belong to this world |
| `Environment` | Date, location, time of day, clouds, rain, wind, puddles |
| `ConsoleVariables` | Cvars this world sets on load (bloom, mist and so on), so worlds do not inherit each other's look |
| `Entities` | The entity tree. Attributes are the transform (rotation is an `x y z w` quaternion); child elements are components, named by their XML tag, and child entities |

Edit worlds in the editor or through the [MCP bridge](mcp.md), not by hand: ids, owned resources and component attributes are kept consistent by the serializer.

A floor that a player walks on should not be one `standard_quad` scaled to hundreds of metres, as in the minimal example above. Interpolation across two triangles that large runs out of precision near the camera, and the texture swims and shakes at grazing angles. Use a subdivided plane instead, for example MCP `mesh_generate` with `shape: "plane"`, `size: [1, 0.001, 1]`, `segments: 64`, `uv_projection: "planar"`, `uv_axis: "y"` and `uv_offset: [0.5, 0.5]`, kept at the same entity scale and material tiling. The physics `Plane` body is independent of the render mesh, so collisions do not change.

### Asset ownership

On save the world records the native assets it references in `OwnedResources` and deletes unreferenced files that were previously recorded there. Files never recorded (shared libraries, sculpt data, caches) are left alone, and older worlds adopt only the assets they reference on their first save. `generated_cache/` is trimmed to 2 GiB, least recently used first.

## World companion scripts and world systems

If `project/<name>_resources/scripts/<name>.lua` exists for `<name>.world`, it runs on every load, before the entity scripts and before roads are built. If it returns a table, its `Start`, `Tick(dt)` and `Stop` run during play. This is where a world turns on the optional world systems:

```lua
WorldSystems.SetAmbientWildlife(true)
WorldSystems.SetAmbientAtmosphere(true)
WorldSystems.SetRoadDetails(true)
WorldSystems.SetLayeredRoads("roads", "project/materials/island_roads/", "island")
```

All four are off by default. See `project/game_resources/scripts/game.lua` and [Lua scripting](scripting.md#world-companion-scripts).

## Prefabs

[`source/world/Prefab.h`](../source/world/Prefab.h) supports two kinds:

- **File prefabs** (`.prefab`): an entity subtree saved with `Prefab::SaveToFile` and instanced with `<prefab file="project/game_resources/cars/ferrari_laferrari/ferrari_laferrari.prefab" .../>`. Attributes on the instance node become overrides, and components or children added to an instance are saved as `prefab_override` entries. Ids are regenerated on load.
- **Code prefabs**: a type name registered with `Prefab::Register(name, create_fn)` and instanced with `<prefab type="car" .../>`.

A car's prefab lives in its own folder next to its physics preset and model, `project/game_resources/cars/<car>/<car>.prefab`. Every car in the bundled worlds is the `ferrari_laferrari.prefab`, with per-scene overrides.

## Resources

[`ResourceCache`](../source/resource/ResourceCache.h) loads and deduplicates resources by path:

```cpp
std::shared_ptr<Material> material = ResourceCache::Load<Material>("project/my_world_resources/brick.xml");
```

| Type | Class | Source formats |
| ---- | ----- | -------------- |
| Mesh | `Mesh` ([`geometry/Mesh.h`](../source/geometry/Mesh.h)) | 40+ formats through Assimp (`ModelImporter`), including skeletons and animations; native `.mesh` |
| Texture | `RHI_Texture` | 30+ formats through FreeImage (`ImageImporter`); compressed and mipmapped on the GPU at load; native `.texture` |
| Material | `Material` | XML; see [Rendering](rendering.md#materials) |
| Font | `Font` | 10+ formats through FreeType |
| Animation | `Animation`, `AnimationClip` | From model import or native clip files |
| Audio | Clip cache in `AudioSource.cpp` | WAV and other formats through SDL3 |

Importing a model creates an entity hierarchy with `Render` components and, if the file has a skeleton, an `Animator`. Resources that are generated at runtime are marked non-persistent and skipped on save.

Paths are relative to `binaries/`: `project/...` for assets, `data/...` for engine data. The rules for which folder an asset belongs in are in [`worlds/README.md`](../worlds/README.md).
