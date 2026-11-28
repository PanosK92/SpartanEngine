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
#include "../world/Entity.h"
#include "../rendering/Material.h"
#include "../resource/ResourceCache.h"
#include "../geometry/Mesh.h"
//======================================

//= NAMESPACES ================
using namespace std;
using namespace spartan::math;
//=============================

namespace spartan::scripting
{
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
        visual::enumeration<MaterialTextureType>(state, "MaterialTextureType", {
            {"Color",     MaterialTextureType::Color},
            {"Roughness", MaterialTextureType::Roughness},
            {"Metalness", MaterialTextureType::Metalness},
            {"Normal",    MaterialTextureType::Normal},
            {"Occlusion", MaterialTextureType::Occlusion},
            {"Emission",  MaterialTextureType::Emission},
            {"Height",    MaterialTextureType::Height},
            {"AlphaMask", MaterialTextureType::AlphaMask},
            {"Packed",    MaterialTextureType::Packed}
        });

        // material properties (subset commonly needed by builders)
        visual::enumeration<MaterialProperty>(state, "MaterialProperty", {
            {"WorldSpaceUv",               MaterialProperty::WorldSpaceUv},
            {"Tessellation",               MaterialProperty::Tessellation},
            {"Roughness",                  MaterialProperty::Roughness},
            {"Metalness",                  MaterialProperty::Metalness},
            {"Normal",                     MaterialProperty::Normal},
            {"Height",                     MaterialProperty::Height},
            {"Clearcoat",                  MaterialProperty::Clearcoat},
            {"Clearcoat_Roughness",        MaterialProperty::Clearcoat_Roughness},
            {"SubsurfaceScattering",       MaterialProperty::SubsurfaceScattering},
            {"SubsurfaceRadius",           MaterialProperty::SubsurfaceRadius},
            {"SubsurfaceColorR",           MaterialProperty::SubsurfaceColorR},
            {"SubsurfaceColorG",           MaterialProperty::SubsurfaceColorG},
            {"SubsurfaceColorB",           MaterialProperty::SubsurfaceColorB},
            {"EmissiveFromAlbedo",         MaterialProperty::EmissiveFromAlbedo},
            {"EmissiveColorR",             MaterialProperty::EmissiveColorR},
            {"EmissiveColorG",             MaterialProperty::EmissiveColorG},
            {"EmissiveColorB",             MaterialProperty::EmissiveColorB},
            {"EmissiveIntensity",          MaterialProperty::EmissiveIntensity},
            {"TextureTilingX",             MaterialProperty::TextureTilingX},
            {"TextureTilingY",             MaterialProperty::TextureTilingY},
            {"IsTerrain",                  MaterialProperty::IsTerrain},
            {"IsGrassBlade",               MaterialProperty::IsGrassBlade},
            {"IsFoliage",                  MaterialProperty::IsFoliage},
            {"IsHair",                     MaterialProperty::IsHair},
            {"IsFlower",                   MaterialProperty::IsFlower},
            {"WindAnimation",              MaterialProperty::WindAnimation},
            {"ColorVariationFromInstance", MaterialProperty::ColorVariationFromInstance},
            {"IsWater",                    MaterialProperty::IsWater},
            {"MotionBlurRadial",           MaterialProperty::MotionBlurRadial},
            {"IsSkidMark",                 MaterialProperty::IsSkidMark},
            {"TerrainBlend",               MaterialProperty::TerrainBlend},
            {"TerrainBlendSharpness",      MaterialProperty::TerrainBlendSharpness},
            {"CullMode",                   MaterialProperty::CullMode}
        });

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
}
