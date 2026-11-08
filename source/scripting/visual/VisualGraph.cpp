/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ============
#include "pch.h"
#include "VisualGraph.h"
#include "io/pugixml.hpp"
#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
//=======================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan::scripting::visual
{
    namespace
    {
        const char* graph_marker = "spartan-visual-script";
        const uint32_t graph_version = 1;

        const set<string> lua_keywords =
        {
            "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in",
            "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"
        };
    }

    Node* Graph::find_node(uint32_t id)
    {
        for (Node& node : nodes)
        {
            if (node.id == id)
            {
                return &node;
            }
        }
        return nullptr;
    }

    const Node* Graph::find_node(uint32_t id) const
    {
        return const_cast<Graph*>(this)->find_node(id);
    }

    Variable* Graph::find_variable(const string& name)
    {
        for (Variable& variable : variables)
        {
            if (variable.name == name)
            {
                return &variable;
            }
        }
        return nullptr;
    }

    const Variable* Graph::find_variable(const string& name) const
    {
        return const_cast<Graph*>(this)->find_variable(name);
    }

    Node& Graph::add_node(const string& type, float x, float y)
    {
        Node node;
        node.id   = next_id++;
        node.type = type;
        node.x    = x;
        node.y    = y;
        nodes.push_back(node);
        return nodes.back();
    }

    void Graph::remove_node(uint32_t id)
    {
        nodes.erase(remove_if(nodes.begin(), nodes.end(), [id](const Node& node) { return node.id == id; }), nodes.end());
        links.erase(remove_if(links.begin(), links.end(), [id](const Link& link) { return link.from_node == id || link.to_node == id; }), links.end());
    }

    void Graph::disconnect(uint32_t node, const string& pin, bool output)
    {
        links.erase(remove_if(links.begin(), links.end(), [&](const Link& link)
        {
            return output ? (link.from_node == node && link.from_pin == pin) : (link.to_node == node && link.to_pin == pin);
        }), links.end());
    }

    bool Graph::is_connected(uint32_t node, const string& pin, bool output) const
    {
        for (const Link& link : links)
        {
            if (output ? (link.from_node == node && link.from_pin == pin) : (link.to_node == node && link.to_pin == pin))
            {
                return true;
            }
        }
        return false;
    }

    const Link* Graph::find_input_link(uint32_t node, const string& pin) const
    {
        for (const Link& link : links)
        {
            if (link.to_node == node && link.to_pin == pin)
            {
                return &link;
            }
        }
        return nullptr;
    }

    string Graph::to_xml() const
    {
        pugi::xml_document document;
        pugi::xml_node root = document.append_child("visual_script");
        root.append_attribute("version") = graph_version;
        root.append_attribute("next_id") = next_id;

        pugi::xml_node view = root.append_child("view");
        view.append_attribute("x")    = view_x;
        view.append_attribute("y")    = view_y;
        view.append_attribute("zoom") = view_zoom;

        pugi::xml_node variables_node = root.append_child("variables");
        for (const Variable& variable : variables)
        {
            pugi::xml_node entry = variables_node.append_child("variable");
            entry.append_attribute("name")  = variable.name.c_str();
            entry.append_attribute("type")  = variable.type.c_str();
            entry.append_attribute("value") = variable.value.c_str();
        }

        pugi::xml_node nodes_node = root.append_child("nodes");
        for (const Node& node : nodes)
        {
            pugi::xml_node entry = nodes_node.append_child("node");
            entry.append_attribute("id")   = node.id;
            entry.append_attribute("type") = node.type.c_str();
            entry.append_attribute("x")    = static_cast<int>(node.x);
            entry.append_attribute("y")    = static_cast<int>(node.y);
            for (const auto& [pin, value] : node.values)
            {
                pugi::xml_node value_node = entry.append_child("value");
                value_node.append_attribute("pin")   = pin.c_str();
                value_node.append_attribute("value") = value.c_str();
            }
            for (const auto& [key, value] : node.data)
            {
                pugi::xml_node data_node = entry.append_child("data");
                data_node.append_attribute("key")   = key.c_str();
                data_node.append_attribute("value") = value.c_str();
            }
        }

        pugi::xml_node links_node = root.append_child("links");
        for (const Link& link : links)
        {
            pugi::xml_node entry = links_node.append_child("link");
            entry.append_attribute("from")     = link.from_node;
            entry.append_attribute("from_pin") = link.from_pin.c_str();
            entry.append_attribute("to")       = link.to_node;
            entry.append_attribute("to_pin")   = link.to_pin.c_str();
        }

        ostringstream stream;
        document.save(stream, "  ", pugi::format_default | pugi::format_no_declaration);
        return stream.str();
    }

    bool Graph::from_xml(const string& xml, string* error)
    {
        pugi::xml_document document;
        pugi::xml_parse_result result = document.load_string(xml.c_str());
        if (!result)
        {
            if (error)
            {
                *error = string("the graph is not valid xml: ") + result.description();
            }
            return false;
        }

        pugi::xml_node root = document.child("visual_script");
        if (!root)
        {
            if (error)
            {
                *error = "the graph has no visual_script element";
            }
            return false;
        }

        *this = Graph();
        next_id = root.attribute("next_id").as_uint(1);

        if (pugi::xml_node view = root.child("view"))
        {
            view_x    = view.attribute("x").as_float(0.0f);
            view_y    = view.attribute("y").as_float(0.0f);
            view_zoom = view.attribute("zoom").as_float(1.0f);
        }

        for (pugi::xml_node entry : root.child("variables").children("variable"))
        {
            Variable variable;
            variable.name  = entry.attribute("name").as_string();
            variable.type  = entry.attribute("type").as_string("number");
            variable.value = entry.attribute("value").as_string();
            variables.push_back(variable);
        }

        for (pugi::xml_node entry : root.child("nodes").children("node"))
        {
            Node node;
            node.id   = entry.attribute("id").as_uint();
            node.type = entry.attribute("type").as_string();
            node.x    = entry.attribute("x").as_float();
            node.y    = entry.attribute("y").as_float();
            for (pugi::xml_node value_node : entry.children("value"))
            {
                node.values[value_node.attribute("pin").as_string()] = value_node.attribute("value").as_string();
            }
            for (pugi::xml_node data_node : entry.children("data"))
            {
                node.data[data_node.attribute("key").as_string()] = data_node.attribute("value").as_string();
            }
            next_id = max(next_id, node.id + 1);
            nodes.push_back(node);
        }

        for (pugi::xml_node entry : root.child("links").children("link"))
        {
            Link link;
            link.from_node = entry.attribute("from").as_uint();
            link.from_pin  = entry.attribute("from_pin").as_string();
            link.to_node   = entry.attribute("to").as_uint();
            link.to_pin    = entry.attribute("to_pin").as_string();
            links.push_back(link);
        }

        return true;
    }

    bool extract_graph_xml(const string& lua_source, string& xml)
    {
        // --[==[spartan-visual-script ... ]==], any long bracket level
        const size_t marker = lua_source.find(graph_marker);
        if (marker == string::npos || marker < 4)
        {
            return false;
        }

        size_t open = marker;
        if (lua_source[open - 1] != '[')
        {
            return false;
        }
        open--;
        size_t level = 0;
        while (open > 0 && lua_source[open - 1] == '=')
        {
            open--;
            level++;
        }
        if (open < 3 || lua_source.compare(open - 3, 3, "--[") != 0)
        {
            return false;
        }

        const string close = "]" + string(level, '=') + "]";
        const size_t start = lua_source.find('\n', marker);
        const size_t end   = lua_source.find(close, marker);
        if (start == string::npos || end == string::npos || end < start)
        {
            return false;
        }

        xml = lua_source.substr(start + 1, end - start - 1);
        return true;
    }

    bool is_visual_script_source(const string& lua_source)
    {
        string xml;
        return extract_graph_xml(lua_source, xml);
    }

    bool is_visual_script_file(const string& path)
    {
        if (FileSystem::GetExtensionFromFilePath(path) != ".lua" || !FileSystem::Exists(path))
        {
            return false;
        }

        // the block sits at the top, a peek is enough and keeps the asset browser fast
        ifstream file(path, ios::binary);
        if (!file)
        {
            return false;
        }
        string head(512, '\0');
        file.read(head.data(), head.size());
        head.resize(static_cast<size_t>(file.gcount()));
        return head.find(graph_marker) != string::npos;
    }

    string make_graph_block(const string& xml)
    {
        string level = "==";
        while (xml.find("]" + level + "]") != string::npos)
        {
            level += "=";
        }

        string block = "--[" + level + "[" + graph_marker + "\n" + xml;
        if (!xml.empty() && xml.back() != '\n')
        {
            block += "\n";
        }
        block += "]" + level + "]\n";
        return block;
    }

    bool is_identifier(const string& name)
    {
        if (name.empty() || lua_keywords.count(name))
        {
            return false;
        }
        if (!(isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_'))
        {
            return false;
        }
        for (char c : name)
        {
            if (!(isalnum(static_cast<unsigned char>(c)) || c == '_'))
            {
                return false;
            }
        }
        return true;
    }
}
