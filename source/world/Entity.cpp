/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES ======================
#include "pch.h"
#include <cstdio>
#include <bit>
#include <sstream>
#include "Entity.h"
#include "Prefab.h"
#include "components/AudioSource.h"
#include "components/Camera.h"
#include "components/Light.h"
#include "components/Physics.h"
#include "components/Render.h"
#include "components/Script.h"
#include "components/Spline.h"
#include "components/SplineFollower.h"
#include "components/Terrain.h"
#include "components/Volume.h"
#include "components/ParticleSystem.h"
#include "components/SkidMarks.h"
#include "components/Water.h"
#include "components/Traffic.h"
#include "components/Pedestrians.h"
#include "components/Navigation.h"
#include "components/SpawnPoint.h"
#include "components/CarReset.h"
#include "components/Text3D.h"
#include "components/Animator.h"
#include "components/Ragdoll.h"
SP_WARNINGS_OFF
#include "../io/pugixml.hpp"
SP_WARNINGS_ON
//=================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    struct Entity::HierarchyTraversal
    {
        uint64_t revision = 0;
        vector<Entity*> descendants; // depth first, parent before child, sibling order preserved
    };

    struct Entity::TransformCache
    {
        atomic<uint32_t> valid{0};
        Matrix inverse;
        Quaternion hierarchy_rotation;
    };

    namespace
    {
        // Transform jobs finish before the primary renderer commits history.
        // Workers only read this counter; entities retain their own first old pose.
        atomic<uint64_t> transform_history_epoch{1};
        // Structural edits are infrequent. Serialize them and traversal construction;
        // steady-state traversal uses immutable snapshots without taking this lock.
        recursive_mutex hierarchy_mutex;
        void save_transform(pugi::xml_node& node, const Vector3& position, const Quaternion& rotation, const Vector3& scale)
        {
            char position_text[96];
            char rotation_text[128];
            char scale_text[96];
            std::snprintf(position_text, sizeof(position_text), "%g %g %g", position.x, position.y, position.z);
            std::snprintf(rotation_text, sizeof(rotation_text), "%g %g %g %g", rotation.x, rotation.y, rotation.z, rotation.w);
            std::snprintf(scale_text, sizeof(scale_text), "%g %g %g", scale.x, scale.y, scale.z);
            node.append_attribute("position") = position_text;
            node.append_attribute("rotation") = rotation_text;
            node.append_attribute("scale")    = scale_text;
        }

        void load_transform_override(Entity* entity, pugi::xml_node& node)
        {
            if (pugi::xml_attribute attr = node.attribute("position"))
            {
                Vector3 position = entity->GetPositionLocal();
                stringstream ss(attr.as_string());
                ss >> position.x >> position.y >> position.z;
                entity->SetPositionLocal(position);
            }

            if (pugi::xml_attribute attr = node.attribute("rotation"))
            {
                Quaternion rotation = entity->GetRotationLocal();
                stringstream ss(attr.as_string());
                ss >> rotation.x >> rotation.y >> rotation.z >> rotation.w;
                entity->SetRotationLocal(rotation);
            }

            if (pugi::xml_attribute attr = node.attribute("scale"))
            {
                Vector3 scale = entity->GetScaleLocal();
                stringstream ss(attr.as_string());
                ss >> scale.x >> scale.y >> scale.z;
                entity->SetScaleLocal(scale);
            }
        }

        // Copy one node; hierarchy construction belongs to the caller.
        Entity* clone_shallow(Entity* entity)
        {
            // clone basic properties
            Entity* clone = World::CreateEntity();
            clone->SetObjectName(entity->GetObjectName());
            // copy the local active flag, not the parent aware state, the clone derives its effective
            // visibility from its own hierarchy, a temporarily deactivated shared source must not bake in
            clone->SetActive(entity->IsActive());
            clone->SetPosition(entity->GetPositionLocal());
            clone->SetRotation(entity->GetRotationLocal());
            clone->SetScale(entity->GetScaleLocal());
            clone->SetTagsString(entity->GetTagsString());
            // a clone of a runtime only entity is runtime only too, without this a cloned prop is
            // written into the world file and comes back on load as an orphan nothing can clean up
            clone->SetTransient(entity->IsTransient());

            // clone all the components
            for (const auto& component_original : entity->GetAllComponents())
            {
                if (component_original != nullptr)
                {
                    // component
                    Component* component_clone = clone->AddComponent(component_original->GetType());

                    // component's properties
                    component_clone->SetAttributes(component_original->GetAttributes());
                }
            }

            return clone;
        };

        // clones descendants that carry components straight onto clone_root, component-less
        // nodes are dropped and their transform is folded into each surviving child
        void clone_visual_children(Entity* source, Entity* clone_root, const Matrix& source_to_root)
        {
            for (Entity* child : source->GetChildren())
            {
                if (!child)
                {
                    continue;
                }

                // row vector convention, child_world = child_local * parent_world
                const Matrix child_to_root = child->GetLocalMatrix() * source_to_root;

                if (child->GetComponentCount() > 0)
                {
                    Entity* child_clone = clone_shallow(child);
                    child_clone->SetParent(clone_root);
                    child_clone->SetPositionLocal(child_to_root.GetTranslation());
                    child_clone->SetRotationLocal(child_to_root.GetRotation());
                    child_clone->SetScaleLocal(child_to_root.GetScale());
                }

                clone_visual_children(child, clone_root, child_to_root);
            }
        };

    }

    Entity::Entity()
    {
        m_object_name = "Entity";
        m_last_transform_time_sec = Timer::GetTimeSec();
    }

    Entity::~Entity()
    {
        for (auto& component : m_components) component.reset();

        // the selection holds raw pointers, drop this one wherever it sits in the list, not just when
        // it happens to be the primary pick
        Camera::RemoveFromSelection(this);
        delete m_transform_cache.load(memory_order_relaxed);
    }

    Entity* Entity::Clone()
    {
        Entity* clone = clone_shallow(this);
        for (Entity* child : GetChildren()) child->Clone()->SetParent(clone);
        return clone;
    }

    Entity* Entity::CloneVisualOnly()
    {
        Entity* clone_root = clone_shallow(this);
        clone_visual_children(this, clone_root, Matrix::Identity);

        return clone_root;
    }

    void Entity::AddTag(const string& tag)
    {
        if (!tag.empty() && !HasTag(tag))
        {
            m_tags.push_back(tag);
            if (m_parent) ++m_parent->m_child_data_revision;
        }
    }

    void Entity::RemoveTag(const string& tag)
    {
        m_tags.erase(remove(m_tags.begin(), m_tags.end(), tag), m_tags.end());
        if (m_parent) ++m_parent->m_child_data_revision;
    }

    bool Entity::HasTag(const string& tag) const
    {
        return find(m_tags.begin(), m_tags.end(), tag) != m_tags.end();
    }

    string Entity::GetTagsString() const
    {
        string result;
        for (const string& tag : m_tags)
        {
            if (!result.empty())
            {
                result += ",";
            }
            result += tag;
        }
        return result;
    }

    void Entity::SetTagsString(const string& comma_separated)
    {
        m_tags.clear();
        if (m_parent) ++m_parent->m_child_data_revision;
        stringstream ss(comma_separated);
        string tag;
        while (getline(ss, tag, ','))
        {
            // trim spaces around each tag
            const size_t first = tag.find_first_not_of(' ');
            if (first == string::npos)
            {
                continue;
            }
            const size_t last = tag.find_last_not_of(' ');
            AddTag(tag.substr(first, last - first + 1));
        }
    }

    void Entity::RegisterForScripting(sol::state_view State)
    {
        State.new_usertype<Entity>("Entity",
            "GetComponent", [](Entity* Self, ComponentType Type) -> sol::reference
            {
                if (Component* comp = Self->GetComponentByType(Type))
                {
                    return comp->AsLua(World::GetLuaState());
                }

                return sol::nil;
            },
            "AddComponent", [](Entity* Self, ComponentType Type) -> sol::reference
            {
                if (Component* comp = Self->AddComponent(Type))
                {
                    return comp->AsLua(World::GetLuaState());
                }

                return sol::nil;
            },
            "RemoveComponent", [](Entity* Self, ComponentType Type)
            {
                Self->RemoveComponentByType(Type);
            },
            "ForEachChild",    [](Entity* Self, const sol::function& Callback)
            {
                for (Entity* Child : Self->m_children)
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
                sol::state_view lua = World::GetLuaState();
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
                    components[i] = self->m_components[i].get();
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
                sol::state_view lua = World::GetLuaState();
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

    void Entity::Start()
    {
        for (auto& component : m_components)
        {
            if (component)
            {
                component->Start();
            }
        }
    }

    bool Entity::IsDynamic() const
    {
        for (const Entity* entity = this; entity; entity = entity->m_parent)
        {
            if (entity->HasTag("dynamic") || entity->GetComponentByType(ComponentType::Animator))
                return true;
            if (const Physics* physics = static_cast<const Physics*>(entity->GetComponentByType(ComponentType::Physics)))
            {
                const BodyType type = physics->GetBodyType();
                if (!physics->IsStatic() || physics->IsKinematic() ||
                    type == BodyType::Controller || type == BodyType::Vehicle || type == BodyType::Cloth)
                    return true;
            }
        }
        return false;
    }

    void Entity::Stop()
    {
        for (auto& component : m_components)
        {
            if (component)
            {
                component->Stop();
            }
        }
    }

    Entity::PreTickGate* Entity::GetPreTickGate()
    {
        if (!m_pretick_gate)
        {
            m_pretick_gate = std::make_unique<PreTickGate>();
            m_pretick_gate->entity = this;
            RefreshPreTickGate();
        }
        return m_pretick_gate.get();
    }

    void Entity::RefreshPreTickGate()
    {
        if (!m_pretick_gate) return;
        Physics* physics = GetComponent<Physics>();
        m_pretick_gate->active = GetActive();
        m_pretick_gate->physics_running = physics && physics->NeedsRunningPreTick();
        m_pretick_gate->other_work = GetComponent<Script>() || GetComponent<Ragdoll>();
        WakePhysicsPreTick();
    }

    void Entity::SleepPhysicsPreTick(const Vector3& camera_position, float radius)
    {
        PreTickGate* gate = GetPreTickGate();
        gate->sleep_origin = camera_position;
        gate->sleep_radius_squared = radius * radius;
    }

    void Entity::PreTick()
    {
        // only these components override pretick, skip the full component array walk
        if (Physics* physics = GetComponent<Physics>())
        {
            physics->PreTick();
        }
        if (Script* script = GetComponent<Script>())
        {
            script->PreTick();
        }
        if (Ragdoll* ragdoll = GetComponent<Ragdoll>())
        {
            ragdoll->PreTick();
        }
    }

    void Entity::Tick(bool tick_render)
    {
        const uint64_t render_bit = uint64_t(1) << static_cast<uint32_t>(ComponentType::Render);
        const uint64_t included = tick_render ? ~uint64_t(0) : ~render_bit;
        // Re-read membership after callbacks: scripts may add or remove later components.
        for (uint32_t next = 0; ((m_component_mask & included) >> next) != 0;)
        {
            const uint32_t index = next + std::countr_zero((m_component_mask & included) >> next);
            next = index + 1;
            if (Component* component = m_components[index].get()) component->Tick();
        }
    }

    float Entity::GetTimeSinceLastTransform() const
    {
        // Static render-only entities need no per-frame clock write. Keep the
        // timestamp in double precision so long sessions retain sub-frame ages.
        return static_cast<float>(max(0.0, Timer::GetTimeSec() - m_last_transform_time_sec));
    }

    void Entity::Save(pugi::xml_node& node)
    {
        // self
        {
            node.append_attribute("name")   = m_object_name.c_str();
            node.append_attribute("id")     = m_object_id;
            node.append_attribute("active") = m_is_active;
            save_transform(node, m_position_local, m_rotation_local, m_scale_local);

            if (!m_tags.empty())
            {
                node.append_attribute("tags") = GetTagsString().c_str();
            }

            // save the prefab reference first so it is recreated before any user-added components are loaded
            if (HasPrefabData())
            {
                pugi::xml_node prefab_node = node.append_child("prefab");

                if (!m_prefab_type.empty())
                {
                    prefab_node.append_attribute("type") = m_prefab_type.c_str();
                }

                if (!m_prefab_file_path.empty())
                {
                    prefab_node.append_attribute("file") = m_prefab_file_path.c_str();
                }

                for (const auto& [key, value] : m_prefab_attributes)
                {
                    prefab_node.append_attribute(key.c_str()) = value.c_str();
                }
            }

            // components
            // for prefab instances only user-added components are saved, the base rebuilds its own
            for (uint32_t i = 0; i < static_cast<uint32_t>(ComponentType::Max); i++)
            {
                auto& component = m_components[i];
                if (!component)
                {
                    continue;
                }

                if (HasPrefabData() && (m_prefab_component_mask & (uint64_t(1) << i)))
                {
                    continue;
                }

                string type_name              = Component::TypeToString(component->GetType());
                pugi::xml_node component_node = node.append_child(type_name.c_str());
                component->Save(component_node);
            }
        }

        const vector<Entity*> children = GetChildren();
        if (HasPrefabData())
        {
            // the prefab base is rebuilt on load, so only persist what the user added on top
            for (Entity* child : children)
            {
                if (child->IsTransient())
                {
                    continue;
                }

                if (child->m_prefab_owned)
                {
                    // base child, scan it for deeper user additions
                    child->SaveOverrides(node, child->GetObjectName());
                }
                else
                {
                    // user added child directly under the instance root, save it in full
                    pugi::xml_node child_node = node.append_child("Entity");
                    child->Save(child_node);
                }
            }
        }
        else
        {
            for (Entity* child : children)
            {
                if (child->IsTransient())
                {
                    continue;
                }

                pugi::xml_node child_node = node.append_child("Entity");
                child->Save(child_node);
            }
        }
    }

    void Entity::SaveOverrides(pugi::xml_node& root_node, const string& path)
    {
        const bool has_user_components = (m_component_mask & ~m_prefab_component_mask) != 0;
        const vector<Entity*> children = GetChildren();
        bool has_user_children = false;
        for (Entity* child : children)
        {
            if (!child->m_prefab_owned && !child->IsTransient())
            {
                has_user_children = true;
                break;
            }
        }

        const bool has_transform = HasPrefabTransformChanged();

        if (has_transform || has_user_components || has_user_children)
        {
            pugi::xml_node override_node      = root_node.append_child("prefab_override");
            override_node.append_attribute("path") = path.c_str();

            if (has_transform)
            {
                save_transform(override_node, m_position_local, m_rotation_local, m_scale_local);
            }

            for (uint32_t i = 0; i < static_cast<uint32_t>(ComponentType::Max); i++)
            {
                auto& component = m_components[i];
                if (component && !(m_prefab_component_mask & (uint64_t(1) << i)))
                {
                    string type_name              = Component::TypeToString(component->GetType());
                    pugi::xml_node component_node = override_node.append_child(type_name.c_str());
                    component->Save(component_node);
                }
            }

            for (Entity* child : children)
            {
                if (!child->m_prefab_owned && !child->IsTransient())
                {
                    pugi::xml_node child_node = override_node.append_child("Entity");
                    child->Save(child_node);
                }
            }
        }

        // recurse into base children to capture additions deeper in the hierarchy
        for (Entity* child : children)
        {
            if (child->m_prefab_owned)
            {
                child->SaveOverrides(root_node, path + "/" + child->GetObjectName());
            }
        }
    }

    bool Entity::HasPrefabTransformChanged() const
    {
        return m_position_local != m_prefab_position_local ||
               m_rotation_local != m_prefab_rotation_local ||
               m_scale_local    != m_prefab_scale_local;
    }

    void Entity::SetPrefabData(const string& type, const unordered_map<string, string>& attributes)
    {
        m_prefab_type       = type;
        m_prefab_attributes = attributes;
    }

    void Entity::SetPrefabFilePath(const string& path)
    {
        m_prefab_file_path = path;
    }

    void Entity::ClearPrefabData()
    {
        m_prefab_type.clear();
        m_prefab_file_path.clear();
        m_prefab_attributes.clear();
    }

    void Entity::MarkPrefabBaseline()
    {
        m_prefab_position_local = m_position_local;
        m_prefab_rotation_local = m_rotation_local;
        m_prefab_scale_local    = m_scale_local;

        m_prefab_component_mask = m_component_mask;

        // mark descendants as prefab owned and recurse into them
        for (Entity* child : m_children)
        {
            child->m_prefab_owned = true;
            child->MarkPrefabBaseline();
        }
    }

    void Entity::Load(pugi::xml_node& node, bool load_children, const TerrainSculptSnapshots* sculpt_snapshots)
    {
        // self
        {
            SetActive(node.attribute("active").as_bool(true));
            SetObjectId(node.attribute("id").as_ullong());
            SetObjectName(node.attribute("name").as_string(m_object_name.c_str()));
            SetTagsString(node.attribute("tags").as_string(""));

            {
                string pos_str = node.attribute("position").as_string();
                stringstream ss(pos_str);
                ss >> m_position_local.x >> m_position_local.y >> m_position_local.z;
            }

            {
                string rot_str = node.attribute("rotation").as_string();
                stringstream ss(rot_str);
                ss >> m_rotation_local.x >> m_rotation_local.y >> m_rotation_local.z >> m_rotation_local.w;
            }

            {
                string scale_str = node.attribute("scale").as_string();
                stringstream ss(scale_str);
                ss >> m_scale_local.x >> m_scale_local.y >> m_scale_local.z;
            }
            // Load can also restore an existing entity, whose local matrix is
            // already clean. These deserialized fields bypass the normal setters.
            m_local_matrix_dirty = true;

            // components and prefabs
            for (pugi::xml_node component_node = node.first_child(); component_node; component_node = component_node.next_sibling())
            {
                string type_name = component_node.name();
                if (type_name == "Entity")
                {
                    continue;
                } // skip children, handled below

                // check for prefab node - creates complex entity hierarchies
                if (type_name == "prefab")
                {
                    // store prefab data for saving later
                    string prefab_type = component_node.attribute("type").as_string();
                    string prefab_file = component_node.attribute("file").as_string();

                    unordered_map<string, string> prefab_attributes;
                    for (pugi::xml_attribute attr = component_node.first_attribute(); attr; attr = attr.next_attribute())
                    {
                        string attr_name = attr.name();
                        if (attr_name == "type" || attr_name == "file")
                        {
                            continue;
                        } // type and file are stored separately
                        prefab_attributes[attr_name] = attr.value();
                    }
                    SetPrefabData(prefab_type, prefab_attributes);

                    if (!prefab_file.empty())
                    {
                        SetPrefabFilePath(prefab_file);
                    }

                    // code prefab - use registered factory function
                    if (!prefab_type.empty() && Prefab::IsRegistered(prefab_type))
                    {
                        Prefab::Create(component_node, this);
                    }
                    // file prefab - load entity hierarchy from .prefab file
                    else if (!prefab_file.empty())
                    {
                        Prefab::LoadFromFile(prefab_file, this);
                    }

                    // snapshot the base so later additions are detected as overrides
                    MarkPrefabBaseline();

                    continue;
                }

                // apply a user override onto an existing base node, resolved by name path
                if (type_name == "prefab_override")
                {
                    string path     = component_node.attribute("path").as_string();
                    Entity* target  = GetDescendantByPath(path);
                    if (!target)
                    {
                        SP_LOG_WARNING("Prefab override path no longer exists, skipping: %s", path.c_str());
                        continue;
                    }

                    load_transform_override(target, component_node);

                    for (pugi::xml_node override_child = component_node.first_child(); override_child; override_child = override_child.next_sibling())
                    {
                        string override_name = override_child.name();
                        if (override_name == "Entity")
                        {
                            Entity* child = World::CreateEntity();
                            child->Load(override_child);
                            child->SetParent(target);
                        }
                        else
                        {
                            ComponentType override_type = Component::StringToType(override_name);
                            if (override_type != ComponentType::Max)
                            {
                                if (Component* component = target->AddComponent(override_type))
                                {
                                    component->Load(override_child);
                                }
                            }
                        }
                    }

                    continue;
                }

                ComponentType type = Component::StringToType(type_name);
                if (type != ComponentType::Max)
                {
                    if (Component* component = AddComponent(type))
                    {
                        auto sculpt = sculpt_snapshots ? sculpt_snapshots->find(GetObjectId()) : TerrainSculptSnapshots::const_iterator();
                        if (type == ComponentType::Terrain && sculpt_snapshots && sculpt != sculpt_snapshots->end())
                            static_cast<Terrain*>(component)->LoadEditorState(component_node, sculpt->second.get());
                        else
                            component->Load(component_node);
                    }
                }
            }
        }

        // children, skipped when the world loader flattens the hierarchy for parallel load
        if (load_children)
        {
            for (pugi::xml_node child_node = node.child("Entity"); child_node; child_node = child_node.next_sibling("Entity"))
            {
                Entity* child = World::CreateEntity();
                child->Load(child_node, true, sculpt_snapshots);
                child->SetParent(this);
            }
        }

        UpdateTransform();
    }

    void Entity::UpdateActiveState()
    {
        const bool active = m_is_active.load() && (!m_parent || m_parent->GetActive());
        if (m_effective_active.exchange(active) == active) return;
        if (m_pretick_gate) m_pretick_gate->active = active;
        if (Render* render = GetComponent<Render>()) render->SetEntityActive(active);
        if (active) m_last_transform_time_sec = Timer::GetTimeSec();
        if (!HasChildren()) return;
        const auto traversal = GetHierarchyTraversal();
        for (Entity* entity : traversal->descendants)
        {
            const bool child_active = entity->m_is_active.load() && (!entity->m_parent || entity->m_parent->GetActive());
            if (entity->m_pretick_gate) entity->m_pretick_gate->active = child_active;
            if (Render* render = entity->GetComponent<Render>()) render->SetEntityActive(child_active);
            if (entity->m_effective_active.exchange(child_active) != child_active && child_active)
                entity->m_last_transform_time_sec = Timer::GetTimeSec();
        }
    }

    void Entity::SetActive(const bool active)
    {
        if (active == m_is_active)
        {
            return;
        }

        m_is_active = active;
        UpdateActiveState();
    }

    void Entity::RemoveComponentByType(ComponentType type)
    {
        const uint32_t index = static_cast<uint32_t>(type);
        if (!m_components[index]) return;
        m_components[index].reset();
        m_component_mask &= ~(uint64_t(1) << index);
        RefreshPreTickGate();
    }

    Component* Entity::AddComponent(ComponentType type)
    {
        switch (type)
        {
            #define X(type, str) case ComponentType::type: return AddComponent<type>();
            SP_COMPONENT_LIST
            #undef X
            default: SP_ASSERT(false); return nullptr;
        }
    }

    void Entity::RemoveComponentById(uint64_t id)
    {
        for (const auto& component : m_components)
        {
            if (component && component->GetObjectId() == id)
            {
                const ComponentType type = component->GetType();
                component->Remove();
                RemoveComponentByType(type);
                return;
            }
        }
    }

    void Entity::InvalidateHierarchy()
    {
        // Called under hierarchy_mutex, so the ancestor chain cannot change here.
        for (Entity* entity = this; entity; entity = entity->m_parent)
            entity->m_hierarchy_revision.fetch_add(1, memory_order_release);
    }

    shared_ptr<const Entity::HierarchyTraversal> Entity::GetHierarchyTraversal() const
    {
        auto cached = m_hierarchy_traversal.load(memory_order_acquire);
        if (cached && cached->revision == m_hierarchy_revision.load(memory_order_acquire))
            return cached;

        lock_guard lock(hierarchy_mutex);
        cached = m_hierarchy_traversal.load(memory_order_acquire);
        const uint64_t revision = m_hierarchy_revision.load(memory_order_acquire);
        if (cached && cached->revision == revision) return cached;

        auto traversal = make_shared<HierarchyTraversal>();
        traversal->revision = revision;
        vector<Entity*> pending(m_children.rbegin(), m_children.rend());
        while (!pending.empty())
        {
            Entity* entity = pending.back();
            pending.pop_back();
            traversal->descendants.push_back(entity);
            pending.insert(pending.end(), entity->m_children.rbegin(), entity->m_children.rend());
        }
        shared_ptr<const HierarchyTraversal> result = traversal;
        m_hierarchy_traversal.store(result, memory_order_release);
        return result;
    }

    Entity::TransformCache& Entity::GetTransformCache() const
    {
        if (TransformCache* cache = m_transform_cache.load(memory_order_acquire)) return *cache;
        lock_guard lock(m_mutex_parent);
        TransformCache* cache = m_transform_cache.load(memory_order_relaxed);
        if (!cache)
        {
            cache = new TransformCache();
            m_transform_cache.store(cache, memory_order_release);
        }
        return *cache;
    }

    const Matrix& Entity::GetMatrixInverse() const
    {
        TransformCache& cache = GetTransformCache();
        if (!(cache.valid.load(memory_order_acquire) & 1u))
        {
            lock_guard lock(m_mutex_parent);
            if (!(cache.valid.load(memory_order_relaxed) & 1u))
            {
                cache.inverse = m_matrix.Inverted();
                cache.valid.fetch_or(1u, memory_order_release);
            }
        }
        return cache.inverse;
    }

    const Quaternion& Entity::GetHierarchyRotation() const
    {
        TransformCache& cache = GetTransformCache();
        if (!(cache.valid.load(memory_order_acquire) & 2u))
        {
            lock_guard lock(m_mutex_parent);
            if (!(cache.valid.load(memory_order_relaxed) & 2u))
            {
                Quaternion rotation = Quaternion::Identity;
                for (const Entity* ancestor = this; ancestor; ancestor = ancestor->m_parent)
                    rotation = ancestor->m_rotation_local * rotation;
                cache.hierarchy_rotation = rotation;
                cache.valid.fetch_or(2u, memory_order_release);
            }
        }
        return cache.hierarchy_rotation;
    }

    void Entity::SetTransformLocalDeferred(const Vector3& position, const Quaternion& rotation, const Vector3& scale)
    {
        if (!position.IsFinite() || !rotation.IsFinite() || !scale.IsFinite()) return;
        if (m_position_local == position && m_rotation_local == rotation && m_scale_local == scale) return;
        m_position_local = position;
        m_rotation_local = rotation;
        m_scale_local = scale;
        m_local_matrix_dirty = true;
    }

    const Matrix& Entity::GetMatrixPrevious() const
    {
        return m_transform_history_epoch == transform_history_epoch.load(memory_order_relaxed)
            ? m_matrix_previous : m_matrix;
    }

    void Entity::CommitTransformHistory()
    {
        transform_history_epoch.fetch_add(1, memory_order_relaxed);
    }

    void Entity::UpdateTransformSelf()
    {
        const uint64_t parent_revision = m_parent ? m_parent->m_transform_revision : 0;
        if (!m_local_matrix_dirty && m_transform_revision != 0 &&
            m_transform_parent == m_parent && m_parent_transform_revision == parent_revision)
            return;

        if (m_local_matrix_dirty)
        {
            if (m_parent) ++m_parent->m_child_data_revision;
            m_matrix_local = Matrix(m_position_local, m_rotation_local, m_scale_local);
            m_local_matrix_dirty = false;
        }
        // Capture once per rendered frame, before the first local or inherited
        // change. Repeated edits retain the last rendered pose, including off-screen.
        const uint64_t history_epoch = transform_history_epoch.load(memory_order_relaxed);
        if (m_transform_history_epoch != history_epoch)
        {
            m_matrix_previous = m_matrix;
            m_transform_history_epoch = history_epoch;
        }
        m_matrix = m_parent ? m_matrix_local * m_parent->m_matrix : m_matrix_local;
        m_transform_parent = m_parent;
        m_parent_transform_revision = parent_revision;
        ++m_transform_revision;
        WakePhysicsPreTick();
        m_last_transform_time_sec = Timer::GetTimeSec();
        if (TransformCache* cache = m_transform_cache.load(memory_order_acquire))
            cache->valid.store(0, memory_order_release);
    }

    void Entity::UpdateTransform()
    {
        UpdateTransformSelf();
        if (m_children_count.load(memory_order_acquire) == 0) return;
        // One contiguous traversal replaces recursive discovery, child-list copies,
        // and a mutex acquisition at every internal node. Deferred animation poses
        // are still inspected even when the instance root itself did not move.
        const auto traversal = GetHierarchyTraversal();
        for (Entity* entity : traversal->descendants)
            entity->UpdateTransformSelf();
    }

    void Entity::SetPosition(const Vector3& position)
    {
        if (GetPosition() == position)
        {
            return;
        }

        SetPositionLocal(!GetParent() ? position : position * GetParent()->GetMatrixInverse());
    }

    void Entity::SetPositionAndRotation(const Vector3& position, const Quaternion& rotation)
    {
        if (!position.IsFinite() || !rotation.IsFinite()) return;
        const Vector3 local_position = m_parent ? position * m_parent->GetMatrixInverse() : position;
        const Quaternion local_rotation = m_parent ? m_parent->GetHierarchyRotation().Inverse() * rotation : rotation;
        if (!local_position.IsFinite() || !local_rotation.IsFinite()) return;
        if (m_position_local == local_position && m_rotation_local == local_rotation) return;
        m_position_local = local_position;
        m_rotation_local = local_rotation;
        m_local_matrix_dirty = true;
        UpdateTransform();
    }

    void Entity::SetPositionLocal(const Vector3& position)
    {
        if (m_position_local == position)
        {
            return;
        }

        // refuse non finite inputs, a NaN or inf position seeps into the world matrix and
        // then into every render component bbox derived from it, which crashes the frustum culler
        if (!position.IsFinite())
        {
            SP_LOG_WARNING("Entity::SetPositionLocal: rejecting non finite position on '%s'", GetObjectName().c_str());
            return;
        }

        m_position_local = position;
        m_local_matrix_dirty = true;
        UpdateTransform();
    }

    void Entity::SetRotation(const Quaternion& rotation)
    {
        // Compose authored quaternions, preserving the existing scale/shear behavior.
        // Siblings reuse their parent's result until its transform changes.
        if (!rotation.IsFinite()) return;
        SetRotationLocal(m_parent ? m_parent->GetHierarchyRotation().Inverse() * rotation : rotation);
    }

    void Entity::SetRotationLocal(const Quaternion& rotation)
    {
        if (m_rotation_local == rotation)
        {
            return;
        }

        // refuse non finite inputs, a NaN quaternion produces a NaN 3x3 rotation block in the
        // world matrix even when translation stays finite, which still NaNs every derived bbox
        if (!rotation.IsFinite())
        {
            SP_LOG_WARNING("Entity::SetRotationLocal: rejecting non finite rotation on '%s'", GetObjectName().c_str());
            return;
        }

        m_rotation_local = rotation;
        m_local_matrix_dirty = true;
        UpdateTransform();
    }

    void Entity::SetScale(const Vector3& scale)
    {
        if (GetScale() == scale)
        {
            return;
        }

        SetScaleLocal(!GetParent() ? scale : scale / GetParent()->GetScale());
    }

    void Entity::SetScaleLocal(const Vector3& scale)
    {
        if (m_scale_local == scale)
        {
            return;
        }

        // refuse non finite inputs, a NaN scale propagates into the world matrix and every
        // bbox computed off it, which crashes the frustum culler assert downstream
        if (!scale.IsFinite())
        {
            SP_LOG_WARNING("Entity::SetScaleLocal: rejecting non finite scale on '%s'", GetObjectName().c_str());
            return;
        }

        m_scale_local = scale;
        m_local_matrix_dirty = true;

        // a scale of 0 will cause a division by zero when decomposing the world transform matrix
        m_scale_local.x = (m_scale_local.x == 0.0f) ? numeric_limits<float>::min() : m_scale_local.x;
        m_scale_local.y = (m_scale_local.y == 0.0f) ? numeric_limits<float>::min() : m_scale_local.y;
        m_scale_local.z = (m_scale_local.z == 0.0f) ? numeric_limits<float>::min() : m_scale_local.z;

        UpdateTransform();
    }

    void Entity::Translate(const Vector3& delta)
    {
        if (!GetParent())
        {
            SetPositionLocal(m_position_local + delta);
        }
        else
        {
            // go through SetPosition, Matrix * Vector3 treats the vector as a point so a displacement cannot pass through the inverse
            SetPosition(GetPosition() + delta);
        }
    }

    void Entity::Rotate(const Quaternion& delta)
    {
        if (!GetParent())
        {
            SetRotationLocal((delta * m_rotation_local).Normalized());
        }
        else
        {
            SetRotationLocal(GetParent()->GetRotation().Inverse() * delta * GetParent()->GetRotation() * m_rotation_local);
        }
    }

    Entity* Entity::GetChildByIndex(const uint32_t index)
    {
        lock_guard lock(m_mutex_children);
        if (index >= m_children.size())
        {
            return nullptr;
        }

        return m_children[index];
    }

    Entity* Entity::GetChildByName(const string& name)
    {
        lock_guard lock(m_mutex_children);
        for (Entity* child : m_children)
        {
            if (child->GetObjectName() == name)
            {
                return child;
            }
        }

        return nullptr;
    }

    Entity* Entity::GetDescendantByPath(const string& path)
    {
        // path is a slash separated chain of child names relative to this entity
        // sibling names are assumed unique within a prefab, duplicates resolve to the first match
        Entity* current = this;
        stringstream ss(path);
        string segment;
        while (getline(ss, segment, '/'))
        {
            if (segment.empty())
            {
                continue;
            }

            current = current->GetChildByName(segment);
            if (!current)
            {
                return nullptr;
            }
        }

        return current;
    }

    void Entity::SetParent(Entity* new_parent)
    {
        lock_guard hierarchy_lock(hierarchy_mutex);
        // Reject cycles without detaching unrelated children or corrupting the
        // inverse links. Ancestry is an O(depth) parent walk, not a subtree search.
        if (m_parent == new_parent || new_parent == this) return;
        if (new_parent && new_parent->IsDescendantOf(this)) return;
        {
            lock_guard lock(m_mutex_parent);
            if (m_parent) m_parent->RemoveChild(this, false);
            m_parent = new_parent;
            if (m_parent) m_parent->AddChild(this);
        }
        UpdateActiveState();
        UpdateTransform();
    }

    void Entity::AddChild(Entity* child)
    {
        SP_ASSERT(child != nullptr);
        lock_guard hierarchy_lock(hierarchy_mutex);
        if (child == this) return;
        if (child->m_parent != this)
        {
            child->SetParent(this);
            return;
        }
        lock_guard lock(m_mutex_children);

        // if this is not already a child, add it
        if (find(m_children.begin(), m_children.end(), child) == m_children.end())
        {
            m_children.emplace_back(child);
            m_children_count.store(static_cast<uint32_t>(m_children.size()), memory_order_release);
            ++m_child_data_revision;
            InvalidateHierarchy();
        }
    }

    void Entity::MoveChildToIndex(Entity* child, uint32_t index)
    {
        SP_ASSERT(child != nullptr);
        lock_guard hierarchy_lock(hierarchy_mutex);
        lock_guard lock(m_mutex_children);

        // find the child in the list
        auto it = find(m_children.begin(), m_children.end(), child);
        if (it == m_children.end())
        {
            return;
        } // child not found

        // get current position before removing
        uint32_t current_index = static_cast<uint32_t>(distance(m_children.begin(), it));

        // remove from current position
        m_children.erase(it);

        // adjust target index if the child was before the target position
        // (removing it shifts all subsequent indices down by 1)
        if (current_index < index && index > 0)
        {
            index--;
        }

        // clamp index to valid range
        if (index > m_children.size())
        {
            index = static_cast<uint32_t>(m_children.size());
        }

        // insert at new position
        m_children.insert(m_children.begin() + index, child);
        ++m_child_data_revision;
        InvalidateHierarchy();
    }

    void Entity::RemoveChild(Entity* child, bool update_child_with_null_parent)
    {
        SP_ASSERT(child != nullptr);
        if (child == this) return;
        lock_guard hierarchy_lock(hierarchy_mutex);
        {
            lock_guard lock(m_mutex_children);
            auto it = find(m_children.begin(), m_children.end(), child);
            if (it == m_children.end()) return;
            m_children.erase(it);
            m_children_count.store(static_cast<uint32_t>(m_children.size()), memory_order_release);
            ++m_child_data_revision;
            InvalidateHierarchy();
        }
        // The false path deliberately preserves the parent pointer for deferred
        // world deletion. It must still invalidate every surviving ancestor cache.
        if (update_child_with_null_parent && child->m_parent == this)
            child->SetParent(nullptr);
    }

    void Entity::ClearParent()
    {
        lock_guard hierarchy_lock(hierarchy_mutex);
        {
            lock_guard lock(m_mutex_parent);
            m_parent = nullptr;
        }
        UpdateActiveState();
        UpdateTransform();
    }

    uint32_t Entity::GetChildrenCount() const
    {
        return m_children_count.load(memory_order_acquire);
    }

    vector<Entity*> Entity::GetChildren() const
    {
        lock_guard lock(m_mutex_children);
        return m_children;
    }

    void Entity::AcquireChildren()
    {
        // Compatibility repair for editor/import operations. Build the adjacency
        // index once; the old recursive version scanned the entire world per node.
        lock_guard hierarchy_lock(hierarchy_mutex);
        unordered_map<Entity*, vector<Entity*>> children_by_parent;
        for (Entity* entity : World::GetEntities())
            if (entity && entity->m_parent && entity != entity->m_parent)
                children_by_parent[entity->m_parent].push_back(entity);

        InvalidateHierarchy();
        vector<Entity*> pending{this};
        while (!pending.empty())
        {
            Entity* entity = pending.back();
            pending.pop_back();
            lock_guard lock(entity->m_mutex_children);
            auto it = children_by_parent.find(entity);
            if (it != children_by_parent.end()) entity->m_children = move(it->second);
            else entity->m_children.clear();
            entity->m_children_count.store(static_cast<uint32_t>(entity->m_children.size()), memory_order_release);
            ++entity->m_child_data_revision;
            entity->m_hierarchy_revision.fetch_add(1, memory_order_release);
            pending.insert(pending.end(), entity->m_children.begin(), entity->m_children.end());
        }
    }

    bool Entity::IsDescendantOf(Entity* transform) const
    {
        SP_ASSERT(transform != nullptr);
        for (const Entity* ancestor = m_parent; ancestor; ancestor = ancestor->m_parent)
            if (ancestor == transform) return true;
        return false;
    }

    void Entity::GetDescendants(vector<Entity*>* descendants)
    {
        if (!HasChildren()) return;
        const auto traversal = GetHierarchyTraversal();
        descendants->insert(descendants->end(), traversal->descendants.begin(), traversal->descendants.end());
    }

    Entity* Entity::GetDescendantByName(const string& name)
    {
        if (!HasChildren()) return nullptr;
        const auto traversal = GetHierarchyTraversal();
        for (Entity* entity : traversal->descendants)
            if (entity->GetObjectName() == name) return entity;
        return nullptr;
    }

}
