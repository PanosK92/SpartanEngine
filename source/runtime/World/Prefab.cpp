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

//= INCLUDES =====
#include "pch.h"
#include "Prefab.h"
//================

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
}
