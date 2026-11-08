/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ========
#include <cstdint>
#include <map>
#include <string>
#include <vector>
//===================

namespace spartan::scripting::visual
{
    struct Node
    {
        uint32_t id = 0;
        std::string type; // a node definition id, see VisualNodes.h
        float x     = 0.0f;
        float y     = 0.0f;
        // what an unconnected input holds, as text, by pin name
        std::map<std::string, std::string> values;
        // definition specific settings, the name of a custom event or the text of a comment
        std::map<std::string, std::string> data;
    };

    // pins are referenced by name, so a graph survives the api growing new parameters
    struct Link
    {
        uint32_t from_node = 0;
        std::string from_pin;
        uint32_t to_node = 0;
        std::string to_pin;
    };

    // becomes a field of the script table: numbers, booleans and strings show up in the inspector and are saved with the entity
    struct Variable
    {
        std::string name;
        std::string type;
        std::string value;
    };

    struct Graph
    {
        std::vector<Node> nodes;
        std::vector<Link> links;
        std::vector<Variable> variables;
        uint32_t next_id = 1;

        // where the editor last looked, kept so a reopened graph lands in the same place
        float view_x    = 0.0f;
        float view_y    = 0.0f;
        float view_zoom = 1.0f;

        Node* find_node(uint32_t id);
        const Node* find_node(uint32_t id) const;
        Variable* find_variable(const std::string& name);
        const Variable* find_variable(const std::string& name) const;

        Node& add_node(const std::string& type, float x, float y);
        void remove_node(uint32_t id);
        // drops every link touching the pin
        void disconnect(uint32_t node, const std::string& pin, bool output);
        bool is_connected(uint32_t node, const std::string& pin, bool output) const;
        // the link feeding an input, nullptr when the input is unconnected
        const Link* find_input_link(uint32_t node, const std::string& pin) const;

        std::string to_xml() const;
        bool from_xml(const std::string& xml, std::string* error = nullptr);
    };

    // a visual script is a lua file: the graph rides in a comment block at its top and the generated code follows,
    // so the script component, game builds and drag and drop treat it like any other script
    bool is_visual_script_source(const std::string& lua_source);
    bool is_visual_script_file(const std::string& path);
    bool extract_graph_xml(const std::string& lua_source, std::string& xml);
    // the comment block for a graph, its long bracket level is picked so the xml can never close it early
    std::string make_graph_block(const std::string& xml);

    bool is_identifier(const std::string& name);
}
