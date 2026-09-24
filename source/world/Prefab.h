/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ==================
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
//=============================

namespace pugi
{
    class xml_node;
}

namespace spartan
{
    class Entity;

    // takes the prefab xml node and a parent, returns the created entity or nullptr
    using PrefabCreateFn = std::function<Entity*(pugi::xml_node& node, Entity* parent)>;

    class Prefab
    {
    public:
        // register a prefab type with its factory function
        static void Register(const std::string& type_name, PrefabCreateFn create_fn);

        // create a prefab from xml node (code prefab), returns the created entity
        static Entity* Create(pugi::xml_node& node, Entity* parent);

        // check if a prefab type is registered
        static bool IsRegistered(const std::string& type_name);

        // save an entity hierarchy as a .prefab file
        static bool SaveToFile(Entity* entity, const std::string& file_path);

        // load a .prefab file, creating children/components under the given parent entity
        static bool LoadFromFile(const std::string& file_path, Entity* parent);

        // get all registered code prefab type names
        static std::vector<std::string> GetRegisteredTypes();

    private:
        static std::unordered_map<std::string, PrefabCreateFn>& GetRegistry();
    };
}
