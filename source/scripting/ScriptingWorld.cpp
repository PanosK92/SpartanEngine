/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===========================
#include "pch.h"
#include <sol/sol.hpp>
#include "Scripting.h"
#include "ScriptingInternal.h"
#include "visual/VisualApiReflection.h"
#include "../world/World.h"
#include "../world/Entity.h"
#include "../world/WorldSystems.h"
#include "../world/components/Camera.h"
#include "../world/components/Light.h"
#include "../physics/PhysicsWorld.h"
#include "../world/components/Render.h"
#include "../rendering/Material.h"
#include "../rendering/Renderer.h"
#include "../resource/ResourceCache.h"
#include "../geometry/Mesh.h"
#include "../input/Input.h"
#include "../math/Ray.h"
//======================================

//= NAMESPACES ================
using namespace std;
using namespace spartan::math;
//=============================

namespace spartan::scripting
{
    namespace
    {
        // a renderable's triangles in its own space, kept so hovering every frame does not copy its mesh every frame
        struct pick_geometry
        {
            const Mesh* mesh      = nullptr;
            uint32_t index_offset = 0;
            uint32_t index_count  = 0;
            vector<Vector3> positions;
            vector<uint32_t> indices;
        };
        unordered_map<uint64_t, pick_geometry> pick_cache;

        const pick_geometry& get_pick_geometry(Entity* entity, Render* render)
        {
            pick_geometry& geometry = pick_cache[entity->GetObjectId()];
            if (geometry.mesh != render->GetMesh() || geometry.index_offset != render->GetIndexOffset() || geometry.index_count != render->GetIndexCount())
            {
                geometry.mesh         = render->GetMesh();
                geometry.index_offset = render->GetIndexOffset();
                geometry.index_count  = render->GetIndexCount();
                geometry.positions.clear();
                geometry.indices.clear();

                vector<RHI_Vertex_PosTexNorTan> vertices;
                render->GetGeometry(&geometry.indices, &vertices);
                geometry.positions.reserve(vertices.size());
                for (const RHI_Vertex_PosTexNorTan& vertex : vertices)
                {
                    geometry.positions.emplace_back(vertex.pos[0], vertex.pos[1], vertex.pos[2]);
                }
            }
            return geometry;
        }

        // the nearest front facing triangle of root or its descendants along the ray, instanced renderables are skipped
        sol::object pick(const Vector3& origin, const Vector3& direction, float max_distance, Entity* root)
        {
            if (!root || direction.LengthSquared() == 0.0f)
            {
                return sol::lua_nil;
            }

            // stale entries belong to removed entities, they are only memory, so a full cache is simply dropped
            if (pick_cache.size() > 512)
            {
                pick_cache.clear();
            }

            const Vector3 world_direction = direction.Normalized();
            const Ray ray(origin, world_direction);

            vector<Entity*> entities;
            entities.push_back(root);
            root->GetDescendants(&entities);

            vector<pair<float, Entity*>> candidates;
            for (Entity* entity : entities)
            {
                Render* render = entity->IsActive() ? entity->GetComponent<Render>() : nullptr;
                if (!render || render->HasInstancing() || !render->GetMesh())
                {
                    continue;
                }

                const float distance = ray.HitDistance(render->GetBoundingBox());
                if (distance < max_distance)
                {
                    candidates.emplace_back(distance, entity);
                }
            }
            sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

            float best_distance = max_distance;
            Entity* best_entity = nullptr;
            for (const auto& [box_distance, entity] : candidates)
            {
                if (box_distance >= best_distance)
                {
                    break;
                }

                // the triangles stay in mesh space, the ray goes there instead, an affine map keeps the order of hits along it
                const Matrix to_local      = entity->GetMatrix().Inverted();
                const Vector3 local_origin = origin * to_local;
                const Vector3 local_step   = (origin + world_direction) * to_local - local_origin;
                const float local_scale    = local_step.Length();
                if (local_scale <= 0.0f)
                {
                    continue;
                }
                const Ray local_ray(local_origin, local_step);

                const pick_geometry& geometry = get_pick_geometry(entity, entity->GetComponent<Render>());
                for (size_t i = 0; i + 2 < geometry.indices.size(); i += 3)
                {
                    const float local_distance = local_ray.HitDistance(geometry.positions[geometry.indices[i]], geometry.positions[geometry.indices[i + 1]], geometry.positions[geometry.indices[i + 2]]);
                    const float distance       = local_distance / local_scale;
                    if (distance < best_distance)
                    {
                        best_distance = distance;
                        best_entity   = entity;
                    }
                }
            }

            if (!best_entity)
            {
                return sol::lua_nil;
            }

            sol::state_view lua = get_state();
            sol::table result   = lua.create_table();
            result["entity"]    = best_entity;
            result["position"]  = origin + world_direction * best_distance;
            result["distance"]  = best_distance;
            return result;
        }
    }

    void register_entity(sol::state_view State)
    {
        visual::usertype<Entity>(State, "Entity",
            "GetComponent", [](Entity* Self, ComponentType Type) -> sol::object
            {
                if (Component* comp = Self->GetComponentByType(Type))
                {
                    return to_lua(get_state(), comp);
                }

                return sol::lua_nil;
            },
            "AddComponent", [](Entity* Self, ComponentType Type) -> sol::object
            {
                if (Component* comp = Self->AddComponent(Type))
                {
                    return to_lua(get_state(), comp);
                }

                return sol::lua_nil;
            },
            "RemoveComponent", [](Entity* Self, ComponentType Type)
            {
                Self->RemoveComponentByType(Type);
            },
            "ForEachChild",    [](Entity* Self, const sol::function& Callback)
            {
                for (Entity* Child : Self->GetChildren())
                {
                    Callback(Child);
                }
            },
            "ForEachDescendant", [](Entity* Self, const sol::function& Callback)
            {
                std::vector<Entity*> descendants;
                Self->GetDescendants(&descendants);
                for (Entity* descendant : descendants)
                {
                    Callback(descendant);
                }
            },
            "GetDescendants", [](Entity* Self) -> sol::table
            {
                sol::state_view lua = get_state();
                sol::table result   = lua.create_table();
                std::vector<Entity*> descendants;
                Self->GetDescendants(&descendants);
                for (size_t i = 0; i < descendants.size(); i++)
                {
                    result[i + 1] = descendants[i];
                }
                return result;
            },

            "GetAllComponents", [](Entity* self)
            {
                // Lua observes components; their lifetime belongs to the entity.
                std::array<Component*, static_cast<uint32_t>(ComponentType::Max)> components{};
                for (size_t i = 0; i < components.size(); ++i)
                    components[i] = self->GetComponentByType(static_cast<ComponentType>(i));
                return components;
            },
            "GetComponentCount",        &Entity::GetComponentCount,
            "GetName",                  &Entity::GetObjectName,
            "SetName",                  &Entity::SetObjectName,
            "GetObjectSize",            &Entity::GetObjectSize,
            // return as string so lua does not lose uint64 precision
            "GetObjectID", [](Entity* self) -> std::string
            {
                return self ? std::to_string(self->GetObjectId()) : "0";
            },

            "Clone",                    &Entity::Clone,
            "IsActive",                 &Entity::IsActive,
            "GetActive",                &Entity::GetActive,
            "SetActive",                &Entity::SetActive,
            "IsDynamic",                &Entity::IsDynamic,
            "GetChildren", [](Entity* Self) -> sol::table
            {
                sol::state_view lua = get_state();
                sol::table result = lua.create_table();
                const std::vector<Entity*> children = Self->GetChildren();
                for (size_t i = 0; i < children.size(); i++)
                {
                    result[i + 1] = children[i];
                }
                return result;
            },
            "HasChildren",              &Entity::HasChildren,
            "SetParent",                &Entity::SetParent,
            "GetParent",                &Entity::GetParent,
            "GetChildByName",           &Entity::GetChildByName,
            "GetChildByIndex",          &Entity::GetChildByIndex,
            "GetChildrenCount",         &Entity::GetChildrenCount,
            "GetDescendantByName",      &Entity::GetDescendantByName,
            "IsDescendantOf",           &Entity::IsDescendantOf,

            "Translate",                &Entity::Translate,
            "Rotate",                   &Entity::Rotate,

            "IsTransient",              &Entity::IsTransient,
            "SetTransient",             &Entity::SetTransient,

            "GetUp",                    &Entity::GetUp,
            "GetDown",                  &Entity::GetDown,
            "GetForward",               &Entity::GetForward,
            "GetBackward",              &Entity::GetBackward,
            "GetRight",                 &Entity::GetRight,
            "GetLeft",                  &Entity::GetLeft,

            "GetPosition",              &Entity::GetPosition,
            "GetPositionLocal",         &Entity::GetPositionLocal,
            "SetPosition",              &Entity::SetPosition,
            "SetPositionLocal",         &Entity::SetPositionLocal,

            "GetRotation",              &Entity::GetRotation,
            "GetRotationLocal",         &Entity::GetRotationLocal,
            "SetRotation",              &Entity::SetRotation,
            "SetRotationLocal",         &Entity::SetRotationLocal,

            "GetScale",                 &Entity::GetScale,
            "GetScaleLocal",            &Entity::GetScaleLocal,
            "SetScale",                 &Entity::SetScale,
            "SetScaleLocal",            &Entity::SetScaleLocal
            );
    }

    void register_world_helpers(sol::state_view state)
    {
        // material texture types
        visual::enumeration(state, "MaterialTextureType",
            "Color",     MaterialTextureType::Color,
            "Roughness", MaterialTextureType::Roughness,
            "Metalness", MaterialTextureType::Metalness,
            "Normal",    MaterialTextureType::Normal,
            "Occlusion", MaterialTextureType::Occlusion,
            "Emission",  MaterialTextureType::Emission,
            "Height",    MaterialTextureType::Height,
            "AlphaMask", MaterialTextureType::AlphaMask,
            "Packed",    MaterialTextureType::Packed
        );

        // material properties (subset commonly needed by builders)
        visual::enumeration(state, "MaterialProperty",
            "WorldSpaceUv",               MaterialProperty::WorldSpaceUv,
            "Tessellation",               MaterialProperty::Tessellation,
            "Roughness",                  MaterialProperty::Roughness,
            "Metalness",                  MaterialProperty::Metalness,
            "Normal",                     MaterialProperty::Normal,
            "Height",                     MaterialProperty::Height,
            "Clearcoat",                  MaterialProperty::Clearcoat,
            "Clearcoat_Roughness",        MaterialProperty::Clearcoat_Roughness,
            "SubsurfaceScattering",       MaterialProperty::SubsurfaceScattering,
            "EmissiveFromAlbedo",         MaterialProperty::EmissiveFromAlbedo,
            "EmissiveColorR",             MaterialProperty::EmissiveColorR,
            "EmissiveColorG",             MaterialProperty::EmissiveColorG,
            "EmissiveColorB",             MaterialProperty::EmissiveColorB,
            "EmissiveIntensity",          MaterialProperty::EmissiveIntensity,
            "TextureTilingX",             MaterialProperty::TextureTilingX,
            "TextureTilingY",             MaterialProperty::TextureTilingY,
            "IsTerrain",                  MaterialProperty::IsTerrain,
            "IsGrassBlade",               MaterialProperty::IsGrassBlade,
            "IsFoliage",                  MaterialProperty::IsFoliage,
            "IsFlower",                   MaterialProperty::IsFlower,
            "WindAnimation",              MaterialProperty::WindAnimation,
            "ColorVariationFromInstance", MaterialProperty::ColorVariationFromInstance,
            "IsWater",                    MaterialProperty::IsWater,
            "MotionBlurRadial",           MaterialProperty::MotionBlurRadial,
            "IsSkidMark",                 MaterialProperty::IsSkidMark,
            "TerrainBlend",               MaterialProperty::TerrainBlend,
            "TerrainBlendSharpness",      MaterialProperty::TerrainBlendSharpness,
            "CullMode",                   MaterialProperty::CullMode
        );

        visual::usertype<Material>(state, "Material",
            "New",             sol::factories([]() { return make_shared<Material>(); }),
            "SetProperty",     &Material::SetProperty,
            "GetProperty",     &Material::GetProperty,
            "SetColor",        [](Material& self, float r, float g, float b, float a) { self.SetColor(Color(r, g, b, a)); },
            "SetTexture",      [](Material& self, MaterialTextureType type, const string& path, sol::optional<int> slot)
            {
                self.SetTexture(type, path, static_cast<uint8_t>(slot.value_or(0)));
            },
            "SetResourceName", &Material::SetResourceName
        );

        // resource cache helpers
        visual::table resource_cache(state, "ResourceCache");
        resource_cache["LoadMesh"] = sol::overload(
            [](const string& path) -> Mesh*
            {
                shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>(path);
                return mesh ? mesh.get() : nullptr;
            },
            [](const string& path, uint32_t flags) -> Mesh*
            {
                shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>(path, flags);
                return mesh ? mesh.get() : nullptr;
            }
        );
    }

    void register_world(sol::state_view state)
    {
        visual::table WorldTable(state, "World");
        WorldTable["GetName"]                   = &World::GetName;
        WorldTable["GetFilePath"]               = &World::GetFilePath;
        WorldTable["GetBoundingBox"]            = &World::GetBoundingBox;
        WorldTable["GetEntities"]               = []() -> sol::table
        {
            sol::state_view lua = get_state();
            sol::table result = lua.create_table();
            const std::vector<Entity*>& entities = World::GetEntities();
            for (size_t i = 0; i < entities.size(); i++)
            {
                result[i + 1] = entities[i];
            }
            return result;
        };
        WorldTable["GetEntitiesLights"]         = []() -> sol::table
        {
            sol::state_view lua = get_state();
            sol::table result = lua.create_table();
            const std::vector<Entity*>& entities = World::GetEntitiesLights();
            for (size_t i = 0; i < entities.size(); i++)
            {
                result[i + 1] = entities[i];
            }
            return result;
        };
        WorldTable["CreateEntity"]              = &World::CreateEntity;
        WorldTable["RemoveEntity"]              = &World::RemoveEntity;
        WorldTable["GetLightCount"]             = &World::GetLightCount;
        WorldTable["GetAudioSourceCount"]       = &World::GetAudioSourceCount;
        WorldTable["GetTimeOfDay"]              = &World::GetTimeOfDay;
        WorldTable["SetTimeOfDay"]              = &World::SetTimeOfDay;
        WorldTable["GetWind"]                   = &World::GetWind;
        WorldTable["SetWind"]                   = &World::SetWind;
        WorldTable["GetPuddliness"]             = &World::GetPuddliness;
        WorldTable["SetPuddliness"]             = &World::SetPuddliness;
        WorldTable["SetRain"] = &Environment::SetRain;
        WorldTable["GetRain"] = &Environment::GetRain;
        WorldTable["SetCloudCoverage"] = &Environment::SetCloudCoverage;
        WorldTable["GetCloudCoverage"] = &Environment::GetCloudCoverage;
        WorldTable["SetDateUtc"] = &Environment::SetDate;
        WorldTable["GetAirTemperature"] = []() { return World::GetEnvironment().air_temperature; };
        WorldTable["GetRoadTemperature"] = []() { return World::GetEnvironment().road_temperature; };
        WorldTable["GetDirectionalLight"]       = &World::GetDirectionalLight;
        WorldTable["GetDaylightIlluminance"]    = &World::GetDaylightIlluminance;
        WorldTable["GetCameraEntity"]           = []() -> Entity*
        {
            Camera* camera = World::GetCamera();
            return camera ? camera->GetEntity() : nullptr;
        };
        // an entity with a camera component takes over the view until SetActiveCamera(nil)
        WorldTable["SetActiveCamera"] = [](sol::optional<Entity*> entity)
        {
            World::SetActiveCamera(entity.value_or(nullptr));
        };
        WorldTable["GetActiveCameraOverride"] = &World::GetActiveCameraOverride;
        WorldTable["GetEntityByName"] = [](const std::string& name) -> Entity*
        {
            for (Entity* entity : World::GetEntities())
            {
                if (entity && entity->GetObjectName() == name)
                {
                    return entity;
                }
            }

            return nullptr;
        };
        // ids exceed lua number precision, so they pass as strings
        WorldTable["GetEntityById"] = [](const std::string& id) -> Entity*
        {
            return World::GetEntityById(std::strtoull(id.c_str(), nullptr, 10));
        };
        WorldTable["Raycast"] = [](const Vector3& origin, const Vector3& direction, float max_distance) -> sol::object
        {
            Vector3 hit_position;
            Entity* hit_entity = nullptr;
            if (PhysicsWorld::RaycastStatic(origin, direction, max_distance, hit_position, hit_entity) && hit_entity)
            {
                sol::state_view lua = get_state();
                sol::table result = lua.create_table();
                result["entity"]   = hit_entity;
                result["position"] = hit_position;
                return result;
            }
            return sol::lua_nil;
        };
        // the ray from the camera through the mouse cursor, nil while the cursor is outside the viewport
        WorldTable["GetCursorRay"] = []() -> tuple<sol::object, sol::object>
        {
            Camera* camera = World::GetCamera();
            if (!camera || !camera->GetEntity() || !Input::GetMouseIsInViewport())
            {
                return { sol::lua_nil, sol::lua_nil };
            }

            // the ray carries a far plane point in its direction field
            const Ray ray           = camera->ComputeRay(Input::GetMousePositionRelativeToEditorViewport());
            const Vector3 direction = (ray.GetDirection() - ray.GetStart()).Normalized();
            sol::state_view lua     = get_state();
            return { sol::make_object(lua, ray.GetStart()), sol::make_object(lua, direction) };
        };
        // triangle exact, against the rendered meshes of root and its descendants: { entity, position, distance } or nil
        WorldTable["Pick"] = [](const Vector3& origin, const Vector3& direction, float max_distance, Entity* root) -> sol::object
        {
            return pick(origin, direction, max_distance, root);
        };
        // outlines an entity and its descendants while playing, nil clears it
        WorldTable["SetHighlight"] = [](sol::optional<Entity*> entity)
        {
            Renderer::SetGameplayHighlight(entity && entity.value() ? entity.value()->GetObjectId() : 0);
        };
    }

    void register_world_systems(sol::state_view lua)
    {
        visual::table table(lua, "WorldSystems");
        table["SetAmbientWildlife"]   = [](bool enabled) { world_systems::GetMutableConfig().ambient_wildlife = enabled; };
        table["SetAmbientAtmosphere"] = [](bool enabled) { world_systems::GetMutableConfig().ambient_atmosphere = enabled; };
        table["SetRoadDetails"]       = [](bool enabled) { world_systems::GetMutableConfig().road_details = enabled; };
        table["SetLayeredRoads"]      = [](const std::string& parent_name, const std::string& material_dir, sol::optional<std::string> material_prefix)
        {
            world_systems::Config& config        = world_systems::GetMutableConfig();
            config.layered_roads_parent          = parent_name;
            config.layered_roads_material_dir    = material_dir;
            config.layered_roads_material_prefix = material_prefix.value_or("road");
        };
    }
}
