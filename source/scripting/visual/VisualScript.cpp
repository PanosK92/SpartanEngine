/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===========================
#include "pch.h"
#include "VisualScript.h"
#include "../../world/World.h"
#include "../../world/Entity.h"
#include "../../world/components/Script.h"
#include <filesystem>
//======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan::scripting::visual
{
    namespace
    {
        bool same_file(const string& a, const string& b)
        {
            if (a == b)
            {
                return true;
            }
            error_code error;
            return filesystem::exists(a, error) && filesystem::exists(b, error) && filesystem::equivalent(a, b, error);
        }
    }

    bool load_visual_script(const string& path, Graph& graph, string* error)
    {
        string source;
        if (!FileSystem::ReadFile(path, source))
        {
            if (error)
            {
                *error = "could not read " + path;
            }
            return false;
        }

        string xml;
        if (!extract_graph_xml(source, xml))
        {
            if (error)
            {
                *error = path + " is a plain lua script, it has no graph";
            }
            return false;
        }

        return graph.from_xml(xml, error);
    }

    CompileResult save_visual_script(const string& path, const Graph& graph, uint32_t* reloaded_components)
    {
        CompileResult result = compile(graph);
        if (!FileSystem::WriteFile(path, result.source))
        {
            result.success = false;
            result.diagnostics.push_back({ 0, true, "Could not write " + path + "." });
            return result;
        }

        const uint32_t reloaded = reload_script_components(path);
        if (reloaded_components)
        {
            *reloaded_components = reloaded;
        }
        return result;
    }

    Graph make_starter_graph()
    {
        Graph graph;
        graph.add_node("event.Start", 0.0f, 0.0f);
        graph.add_node("event.Tick", 0.0f, 160.0f);
        return graph;
    }

    bool create_visual_script(const string& path)
    {
        return FileSystem::WriteFile(path, compile(make_starter_graph()).source);
    }

    uint32_t reload_script_components(const string& path)
    {
        uint32_t count = 0;
        for (Entity* entity : World::GetEntities())
        {
            Script* script = entity ? entity->GetComponent<Script>() : nullptr;
            if (script && !script->file_path.empty() && same_file(script->file_path, path))
            {
                script->LoadScriptFile(script->file_path);
                count++;
            }
        }
        return count;
    }
}
