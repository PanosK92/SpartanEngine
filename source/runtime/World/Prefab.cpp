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

//= INCLUDES =========================
#include "pch.h"
#include "Prefab.h"
#include "Entity.h"
#include "World.h"
#include "../FileSystem/FileSystem.h"
//====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    unordered_map<string, PrefabCreateFn>& Prefab::GetRegistry()
    {
        static unordered_map<string, PrefabCreateFn> registry;
        return registry;
    }

    void Prefab::Register(const string& type_name, PrefabCreateFn create_fn)
    {
        GetRegistry()[type_name] = create_fn;
        SP_LOG_INFO("Registered prefab type: %s", type_name.c_str());
    }

    Entity* Prefab::Create(pugi::xml_node& node, Entity* parent)
    {
        string type_name = node.attribute("type").as_string();
        if (type_name.empty())
        {
            SP_LOG_WARNING("Prefab node missing 'type' attribute");
            return nullptr;
        }

        auto& registry = GetRegistry();
        auto it = registry.find(type_name);
        if (it == registry.end())
        {
            SP_LOG_WARNING("Unknown prefab type: %s", type_name.c_str());
            return nullptr;
        }

        return it->second(node, parent);
    }

    bool Prefab::IsRegistered(const string& type_name)
    {
        return GetRegistry().find(type_name) != GetRegistry().end();
    }

    bool Prefab::SaveToFile(Entity* entity, const string& file_path)
    {
        if (!entity)
        {
            SP_LOG_WARNING("Cannot save null entity as prefab");
            return false;
        }

        // ensure the parent directory exists
        string directory = FileSystem::GetDirectoryFromFilePath(file_path);
        if (!directory.empty())
        {
            FileSystem::CreateDirectory_(directory);
        }

        pugi::xml_document doc;

        // declaration
        pugi::xml_node declaration = doc.append_child(pugi::node_declaration);
        declaration.append_attribute("version")  = "1.0";
        declaration.append_attribute("encoding") = "utf-8";

        // root <Prefab> node - we manually serialize components and children
        // instead of calling Entity::Save() because that would early-return
        // for prefab entities (writing only the prefab reference)
        pugi::xml_node prefab_node = doc.append_child("Prefab");
        prefab_node.append_attribute("name") = entity->GetObjectName().c_str();

        // save all components directly onto the prefab node
        for (const auto& component : entity->GetAllComponents())
        {
            if (component)
            {
                string type_name              = Component::TypeToString(component->GetType());
                pugi::xml_node component_node = prefab_node.append_child(type_name.c_str());
                component->Save(component_node);
            }
        }

        // save children as child entity nodes
        for (Entity* child : entity->GetChildren())
        {
            if (child->IsTransient())
                continue;

            pugi::xml_node child_node = prefab_node.append_child("Entity");
            child->Save(child_node);
        }

        // write to disk
        if (!doc.save_file(file_path.c_str(), " ", pugi::format_indent | pugi::format_indent_attributes))
        {
            SP_LOG_ERROR("Failed to save prefab file: %s", file_path.c_str());
            return false;
        }

        SP_LOG_INFO("Saved prefab to: %s", file_path.c_str());
        return true;
    }

    bool Prefab::LoadFromFile(const string& file_path, Entity* parent)
    {
        if (!parent)
        {
            SP_LOG_WARNING("Cannot load prefab into null parent entity");
            return false;
        }

        if (!FileSystem::Exists(file_path))
        {
            SP_LOG_WARNING("Prefab file not found: %s", file_path.c_str());
            return false;
        }

        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_file(file_path.c_str());
        if (!result)
        {
            SP_LOG_ERROR("Failed to parse prefab file: %s (%s)", file_path.c_str(), result.description());
            return false;
        }

        pugi::xml_node prefab_node = doc.child("Prefab");
        if (!prefab_node)
        {
            SP_LOG_ERROR("Prefab file missing <Prefab> root node: %s", file_path.c_str());
            return false;
        }

        // load components defined on the prefab node directly onto the parent entity
        for (pugi::xml_node component_node = prefab_node.first_child(); component_node; component_node = component_node.next_sibling())
        {
            string type_name = component_node.name();
            if (type_name == "Entity")
                continue; // children are handled below

            ComponentType type = Component::StringToType(type_name);
            if (type != ComponentType::Max)
            {
                if (Component* component = parent->AddComponent(type))
                {
                    component->Load(component_node);
                }
            }
        }

        // load child entities
        for (pugi::xml_node child_node = prefab_node.child("Entity"); child_node; child_node = child_node.next_sibling("Entity"))
        {
            Entity* child = World::CreateEntity();
            child->Load(child_node);
            child->SetParent(parent);
        }

        SP_LOG_INFO("Loaded prefab from: %s", file_path.c_str());
        return true;
    }

    vector<string> Prefab::GetRegisteredTypes()
    {
        vector<string> types;
        for (const auto& [name, fn] : GetRegistry())
        {
            types.push_back(name);
        }
        return types;
    }
}
