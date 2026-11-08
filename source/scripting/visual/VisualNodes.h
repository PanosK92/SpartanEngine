/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ===========
#include <cstdint>
#include <string>
#include <vector>
#include "VisualApi.h"
#include "VisualGraph.h"
//======================

namespace spartan::scripting::visual
{
    // execution pins carry this type, everything else is a lua value type
    inline constexpr const char* type_exec = "exec";

    enum class NodeKind : uint8_t
    {
        Event,           // a script lifecycle callback, payload is its name
        KeyEvent,        // runs from Tick when a key goes down or up
        CustomEvent,     // a function of the script other nodes and scripts can call
        CallCustomEvent,
        ApiCall,         // anything the engine exposes to lua
        Branch,
        Sequence,
        ForLoop,
        ForEach,
        WhileLoop,
        DoOnce,
        FlipFlop,
        Delay,
        VariableGet,
        VariableSet,
        Literal,         // payload is the type
        Expression,      // payload is lua with {0}, {1}... standing for the inputs
        Statement,       // like Expression but runs in the execution flow
        Break,           // splits a value into its fields, payload is the type
        Cast,            // passes a value through under a narrower type
        GetComponent,    // payload is the component usertype
        SelfEntity,
        Comment
    };

    struct PinDefinition
    {
        std::string name;
        std::string type;
        bool optional    = false;
        bool variadic    = false;
        bool nullable    = false;
        bool connectable = true; // literals keep their value inline, nothing can feed it
        std::string default_value;
    };

    struct NodeDefinition
    {
        std::string id;
        std::string title;
        std::string category;  // "Flow", "Entity", "Math|Number", a bar nests it in the add menu
        std::string tooltip;
        NodeKind kind = NodeKind::ApiCall;
        bool pure     = false; // no execution pins, evaluated wherever its outputs are read
        std::vector<PinDefinition> inputs;
        std::vector<PinDefinition> outputs;
        std::string payload;
        const ApiFunction* function = nullptr;
        bool has_target             = false; // the first data input is the object a method runs on
    };

    // the fixed part of the library: built in nodes and one node per lua function, rebuilt when the api changes
    const std::vector<NodeDefinition>& get_definitions();
    const NodeDefinition* find_definition(const std::string& id);

    // any definition, including the ones a graph creates through its variables and custom events
    bool resolve_definition(const std::string& type, const Graph& graph, NodeDefinition& out);
    // the graph dependent definitions the add menu offers next to the fixed ones
    std::vector<NodeDefinition> get_graph_definitions(const Graph& graph);

    const PinDefinition* find_pin(const NodeDefinition& definition, const std::string& name, bool output);
    bool types_compatible(const std::string& from, const std::string& to);

    // types a variable can have, primitives first, then every usertype
    std::vector<std::string> get_variable_types();
    // the text a fresh pin or variable of this type starts with
    std::string default_value_for(const std::string& type);
    // camel case to words, "SetPositionLocal" reads "Set Position Local"
    std::string pretty_name(const std::string& name);
}
