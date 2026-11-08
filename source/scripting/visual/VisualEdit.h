/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ==========
#include <cstdint>
#include <set>
#include <string>
#include <vector>
#include "VisualGraph.h"
#include "VisualNodes.h"
//=====================

namespace spartan::scripting::visual
{
    // graph edits shared by the node editor and the mcp commands, so both follow the same rules

    // links an output to an input, a data input or an execution output can only have one link so the old one is replaced
    bool connect(Graph& graph, uint32_t from_node, const std::string& from_pin, uint32_t to_node, const std::string& to_pin, std::string* error = nullptr);

    // the first pin on a node that can take a link of this type, used when a dragged link is dropped on a new node
    std::string find_compatible_pin(const Graph& graph, uint32_t node, const std::string& type, bool output);

    // whether an unconnected input gets an inline editor, and so keeps a value in node.values
    bool has_inline_value(const PinDefinition& pin);

    // adds a node after checking its type resolves, the pointer lives until the graph's nodes change
    Node* add_node(Graph& graph, const std::string& type, float x, float y, std::string* error = nullptr);

    // a standalone graph holding the nodes, the links between them and the variables they use
    std::string copy_nodes(const Graph& graph, const std::set<uint32_t>& nodes);
    // pastes with the top left node at x, y and returns the ids of the new nodes
    std::vector<uint32_t> paste_nodes(Graph& graph, const std::string& xml, float x, float y);

    bool add_variable(Graph& graph, const std::string& name, const std::string& type, const std::string& value, std::string* error = nullptr);
    // renames a variable and the nodes reading and writing it
    bool rename_variable(Graph& graph, const std::string& name, const std::string& new_name, std::string* error = nullptr);
    // removes a variable and the nodes reading and writing it
    void remove_variable(Graph& graph, const std::string& name);
    // keeps inline values in step when a variable changes type
    void set_variable_type(Graph& graph, const std::string& name, const std::string& type);
}
