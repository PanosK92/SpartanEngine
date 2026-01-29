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

#pragma once

//= INCLUDES ==================
#include <string>
#include <unordered_map>
#include <functional>
SP_WARNINGS_OFF
#include "../IO/pugixml.hpp"
SP_WARNINGS_ON
//=============================

namespace spartan
{
    class Entity;

    // prefab factory function signature
    // takes the xml node with prefab attributes and parent entity
    // returns the created entity (or nullptr on failure)
    using PrefabCreateFn = std::function<Entity*(pugi::xml_node& node, Entity* parent)>;

    class Prefab
    {
    public:
        // register a prefab type with its factory function
        static void Register(const std::string& type_name, PrefabCreateFn create_fn);

        // create a prefab from xml node, returns the created entity
        static Entity* Create(pugi::xml_node& node, Entity* parent);

        // check if a prefab type is registered
        static bool IsRegistered(const std::string& type_name);

    private:
        static std::unordered_map<std::string, PrefabCreateFn>& GetRegistry();
    };
}
