/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===========
#include "pch.h"
#include "VisualEdit.h"
#include <algorithm>
#include <cfloat>
#include <map>
//======================

//= NAMESPACES =========
using namespace std;
//======================

namespace spartan::scripting::visual
{
    namespace
    {
        const set<string> reserved_variable_names = { "Initialize", "Start", "Stop", "Remove", "PreTick", "Tick", "Save", "Load", "file_path" };

        void set_error(string* error, const string& message)
        {
            if (error)
            {
                *error = message;
            }
        }

        bool is_variable_node(const Node& node, const string& name)
        {
            return node.type == "var.get:" + name || node.type == "var.set:" + name;
        }

        bool check_variable_name(const Graph& graph, const string& name, string* error)
        {
            if (!is_identifier(name) || reserved_variable_names.count(name) || name.rfind("Event_", 0) == 0)
            {
                set_error(error, "'" + name + "' can't be a variable name, use letters, digits and underscores, start with a letter, and avoid lua keywords and the script callbacks.");
                return false;
            }
            if (graph.find_variable(name))
            {
                set_error(error, "there is already a variable named " + name + ".");
                return false;
            }
            return true;
        }
    }

    bool connect(Graph& graph, uint32_t from_node, const string& from_pin, uint32_t to_node, const string& to_pin, string* error)
    {
        if (from_node == to_node)
        {
            set_error(error, "a node can't link to itself.");
            return false;
        }

        const Node* source = graph.find_node(from_node);
        const Node* target = graph.find_node(to_node);
        if (!source || !target)
        {
            set_error(error, "no node with id " + to_string(source ? to_node : from_node) + ".");
            return false;
        }

        NodeDefinition source_definition;
        NodeDefinition target_definition;
        if (!resolve_definition(source->type, graph, source_definition) || !resolve_definition(target->type, graph, target_definition))
        {
            set_error(error, "one of the nodes has an unknown type.");
            return false;
        }

        const PinDefinition* output = find_pin(source_definition, from_pin, true);
        const PinDefinition* input  = find_pin(target_definition, to_pin, false);
        if (!output)
        {
            set_error(error, source_definition.title + " has no output named '" + from_pin + "'.");
            return false;
        }
        if (!input)
        {
            set_error(error, target_definition.title + " has no input named '" + to_pin + "'.");
            return false;
        }
        if (!input->connectable)
        {
            set_error(error, "'" + to_pin + "' on " + target_definition.title + " is set inline, it can't take a link.");
            return false;
        }

        const bool output_exec = output->type == type_exec;
        const bool input_exec  = input->type == type_exec;
        if (output_exec != input_exec)
        {
            set_error(error, "execution pins only link to execution pins.");
            return false;
        }
        if (!types_compatible(output->type, input->type))
        {
            set_error(error, "a " + output->type + " can't go into a " + input->type + " input.");
            return false;
        }

        // execution fans in but not out, data fans out but not in
        if (output_exec)
        {
            graph.disconnect(from_node, from_pin, true);
        }
        else
        {
            graph.disconnect(to_node, to_pin, false);
        }

        Link link;
        link.from_node = from_node;
        link.from_pin  = from_pin;
        link.to_node   = to_node;
        link.to_pin    = to_pin;
        graph.links.push_back(link);
        return true;
    }

    string find_compatible_pin(const Graph& graph, uint32_t node_id, const string& type, bool output)
    {
        const Node* node = graph.find_node(node_id);
        NodeDefinition definition;
        if (!node || !resolve_definition(node->type, graph, definition))
        {
            return "";
        }

        const vector<PinDefinition>& pins = output ? definition.outputs : definition.inputs;
        // an exact type match beats a loose one, "any" takes everything and would otherwise always win
        for (int pass = 0; pass < 2; pass++)
        {
            for (const PinDefinition& pin : pins)
            {
                if (!pin.connectable)
                {
                    continue;
                }
                const bool match = pass == 0 ? pin.type == type : (output ? types_compatible(pin.type, type) : types_compatible(type, pin.type));
                if (match)
                {
                    return pin.name;
                }
            }
        }
        return "";
    }

    bool has_inline_value(const PinDefinition& pin)
    {
        const string& type = pin.type;
        if (type == type_exec)
        {
            return false;
        }
        return type == "boolean" || type == "number" || type == "integer" || type == "string" || type == "any" ||
               type == "Vector2" || type == "Vector3" || type == "Vector4" || type == "Quaternion" || is_enum(type);
    }

    Node* add_node(Graph& graph, const string& type, float x, float y, string* error)
    {
        NodeDefinition definition;
        if (!resolve_definition(type, graph, definition))
        {
            set_error(error, "no node type '" + type + "', see visual_script_api.");
            return nullptr;
        }

        // lifecycle events run once per script, a second one would never be called
        if (definition.kind == NodeKind::Event)
        {
            for (const Node& node : graph.nodes)
            {
                if (node.type == type)
                {
                    set_error(error, "the graph already has " + definition.title + " (node " + to_string(node.id) + ").");
                    return nullptr;
                }
            }
        }

        Node& node = graph.add_node(type, x, y);
        if (definition.kind == NodeKind::CustomEvent)
        {
            // a fresh custom event needs a name no other event has
            set<string> names;
            for (const Node& other : graph.nodes)
            {
                auto it = other.data.find("name");
                if (other.type == "event.custom" && it != other.data.end())
                {
                    names.insert(it->second);
                }
            }
            string name = "MyEvent";
            for (uint32_t i = 2; names.count(name); i++)
            {
                name = "MyEvent" + to_string(i);
            }
            node.data["name"] = name;
        }
        else if (definition.kind == NodeKind::Comment)
        {
            node.data["text"] = "Comment";
        }
        return &node;
    }

    string copy_nodes(const Graph& graph, const set<uint32_t>& nodes)
    {
        Graph copy;
        set<string> variables;
        for (const Node& node : graph.nodes)
        {
            if (!nodes.count(node.id))
            {
                continue;
            }
            copy.nodes.push_back(node);
            for (const char* prefix : { "var.get:", "var.set:" })
            {
                if (node.type.rfind(prefix, 0) == 0)
                {
                    variables.insert(node.type.substr(strlen(prefix)));
                }
            }
        }
        for (const Link& link : graph.links)
        {
            if (nodes.count(link.from_node) && nodes.count(link.to_node))
            {
                copy.links.push_back(link);
            }
        }
        for (const Variable& variable : graph.variables)
        {
            if (variables.count(variable.name))
            {
                copy.variables.push_back(variable);
            }
        }
        return copy.to_xml();
    }

    vector<uint32_t> paste_nodes(Graph& graph, const string& xml, float x, float y)
    {
        vector<uint32_t> result;
        Graph source;
        if (!source.from_xml(xml) || source.nodes.empty())
        {
            return result;
        }

        for (const Variable& variable : source.variables)
        {
            if (!graph.find_variable(variable.name))
            {
                graph.variables.push_back(variable);
            }
        }

        float min_x = FLT_MAX;
        float min_y = FLT_MAX;
        for (const Node& node : source.nodes)
        {
            min_x = min(min_x, node.x);
            min_y = min(min_y, node.y);
        }

        map<uint32_t, uint32_t> remap;
        for (const Node& node : source.nodes)
        {
            NodeDefinition definition;
            // a pasted lifecycle event would be a duplicate, its body still comes along
            if (resolve_definition(node.type, graph, definition) && definition.kind == NodeKind::Event)
            {
                bool exists = false;
                for (const Node& other : graph.nodes)
                {
                    exists = exists || other.type == node.type;
                }
                if (exists)
                {
                    continue;
                }
            }

            Node& added  = graph.add_node(node.type, node.x - min_x + x, node.y - min_y + y);
            added.values = node.values;
            added.data   = node.data;
            remap[node.id] = added.id;
            result.push_back(added.id);
        }

        for (const Link& link : source.links)
        {
            auto from = remap.find(link.from_node);
            auto to   = remap.find(link.to_node);
            if (from != remap.end() && to != remap.end())
            {
                Link added      = link;
                added.from_node = from->second;
                added.to_node   = to->second;
                graph.links.push_back(added);
            }
        }
        return result;
    }

    bool add_variable(Graph& graph, const string& name, const string& type, const string& value, string* error)
    {
        if (!check_variable_name(graph, name, error))
        {
            return false;
        }
        const vector<string> types = get_variable_types();
        if (find(types.begin(), types.end(), type) == types.end())
        {
            set_error(error, "'" + type + "' is not a variable type, use boolean, number, integer, string, a vector, an entity, a usertype or an enum.");
            return false;
        }

        Variable variable;
        variable.name  = name;
        variable.type  = type;
        variable.value = value.empty() ? default_value_for(type) : value;
        graph.variables.push_back(variable);
        return true;
    }

    bool rename_variable(Graph& graph, const string& name, const string& new_name, string* error)
    {
        Variable* variable = graph.find_variable(name);
        if (!variable)
        {
            set_error(error, "no variable named " + name + ".");
            return false;
        }
        if (name == new_name)
        {
            return true;
        }
        if (!check_variable_name(graph, new_name, error))
        {
            return false;
        }

        // the variable's pins carry its name, so the links and inline values follow it
        set<uint32_t> nodes;
        for (Node& node : graph.nodes)
        {
            if (node.type == "var.get:" + name)
            {
                node.type = "var.get:" + new_name;
                nodes.insert(node.id);
            }
            else if (node.type == "var.set:" + name)
            {
                node.type = "var.set:" + new_name;
                nodes.insert(node.id);
                auto value = node.values.find(name);
                if (value != node.values.end())
                {
                    node.values[new_name] = value->second;
                    node.values.erase(name);
                }
            }
        }
        for (Link& link : graph.links)
        {
            if (nodes.count(link.from_node) && link.from_pin == name)
            {
                link.from_pin = new_name;
            }
            if (nodes.count(link.to_node) && link.to_pin == name)
            {
                link.to_pin = new_name;
            }
        }
        graph.find_variable(name)->name = new_name;
        return true;
    }

    void remove_variable(Graph& graph, const string& name)
    {
        vector<uint32_t> nodes;
        for (const Node& node : graph.nodes)
        {
            if (is_variable_node(node, name))
            {
                nodes.push_back(node.id);
            }
        }
        for (uint32_t id : nodes)
        {
            graph.remove_node(id);
        }
        graph.variables.erase(remove_if(graph.variables.begin(), graph.variables.end(), [&](const Variable& variable) { return variable.name == name; }), graph.variables.end());
    }

    void set_variable_type(Graph& graph, const string& name, const string& type)
    {
        Variable* variable = graph.find_variable(name);
        if (!variable || variable->type == type)
        {
            return;
        }
        variable->type  = type;
        variable->value = default_value_for(type);

        // links that no longer fit the new type would only produce errors
        set<uint32_t> nodes;
        for (Node& node : graph.nodes)
        {
            if (is_variable_node(node, name))
            {
                nodes.insert(node.id);
                node.values.erase(name);
            }
        }
        graph.links.erase(remove_if(graph.links.begin(), graph.links.end(), [&](const Link& link)
        {
            if (nodes.count(link.from_node) && link.from_pin == name)
            {
                const Node* target = graph.find_node(link.to_node);
                NodeDefinition definition;
                const PinDefinition* pin = target && resolve_definition(target->type, graph, definition) ? find_pin(definition, link.to_pin, false) : nullptr;
                return !pin || !types_compatible(type, pin->type);
            }
            if (nodes.count(link.to_node) && link.to_pin == name)
            {
                const Node* source = graph.find_node(link.from_node);
                NodeDefinition definition;
                const PinDefinition* pin = source && resolve_definition(source->type, graph, definition) ? find_pin(definition, link.from_pin, true) : nullptr;
                return !pin || !types_compatible(pin->type, type);
            }
            return false;
        }), graph.links.end());
    }
}
