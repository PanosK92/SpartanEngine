/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==============
#include "pch.h"
#include "VisualCompiler.h"
#include "VisualNodes.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
//=========================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan::scripting::visual
{
    namespace
    {
        using PinKey = pair<uint32_t, string>;

        // the lifecycle callbacks the script component calls, in the order the generated file lists them
        const char* lifecycle_events[] = { "Initialize", "Start", "Stop", "Remove", "PreTick", "Tick" };

        const set<string> reserved_names = { "Initialize", "Start", "Stop", "Remove", "PreTick", "Tick", "Save", "Load", "file_path" };

        string trim(const string& text)
        {
            const size_t first = text.find_first_not_of(" \t\r\n");
            const size_t last  = text.find_last_not_of(" \t\r\n");
            return first == string::npos ? "" : text.substr(first, last - first + 1);
        }

        bool parse_number(const string& text, double& value)
        {
            const string clean = trim(text);
            if (clean.empty())
            {
                return false;
            }
            char* end = nullptr;
            value     = strtod(clean.c_str(), &end);
            return end && *end == '\0' && isfinite(value);
        }

        string number_text(double value, bool integer)
        {
            if (integer)
            {
                return to_string(static_cast<long long>(llround(value)));
            }
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "%.9g", value);
            return buffer;
        }

        vector<double> parse_numbers(const string& text)
        {
            vector<double> values;
            string part;
            auto flush = [&]()
            {
                double value = 0.0;
                if (!trim(part).empty())
                {
                    values.push_back(parse_number(part, value) ? value : 0.0);
                }
                part.clear();
            };
            for (char c : text)
            {
                if (c == ',' || c == ' ' || c == ';')
                {
                    flush();
                }
                else
                {
                    part += c;
                }
            }
            flush();
            return values;
        }

        string quote(const string& text)
        {
            string result = "\"";
            for (char c : text)
            {
                switch (c)
                {
                    case '"':  result += "\\\""; break;
                    case '\\': result += "\\\\"; break;
                    case '\n': result += "\\n";  break;
                    case '\r': result += "\\r";  break;
                    case '\t': result += "\\t";  break;
                    default:   result += c;      break;
                }
            }
            return result + "\"";
        }

        string sanitize(const string& name)
        {
            string result;
            for (char c : name)
            {
                result += isalnum(static_cast<unsigned char>(c)) ? c : '_';
            }
            return result;
        }

        // a.b.c or a, safe to put a ':' or '.' after without parentheses
        bool is_name_path(const string& text)
        {
            if (text.empty() || !(isalpha(static_cast<unsigned char>(text[0])) || text[0] == '_'))
            {
                return false;
            }
            for (char c : text)
            {
                if (!(isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.'))
                {
                    return false;
                }
            }
            return true;
        }

        string prefix(const string& expression)
        {
            return is_name_path(expression) ? expression : "(" + expression + ")";
        }

        class Compiler
        {
        public:
            explicit Compiler(const Graph& graph) : m_graph(graph)
            {
            }

            CompileResult run()
            {
                CompileResult result;
                analyze();

                vector<string> function_text;
                vector<vector<uint32_t>> function_lines;
                if (!has_errors())
                {
                    generate(function_text, function_lines);
                }

                for (const Node& node : m_graph.nodes)
                {
                    const NodeDefinition* definition = find(node.id);
                    if (!definition || definition->pure || m_reached.count(node.id))
                    {
                        continue;
                    }
                    const NodeKind kind = definition->kind;
                    if (kind == NodeKind::Event || kind == NodeKind::KeyEvent || kind == NodeKind::CustomEvent || kind == NodeKind::Comment)
                    {
                        continue;
                    }
                    warning(node.id, "Nothing leads into this node, it never runs.");
                }

                result.success     = !has_errors();
                result.diagnostics = m_diagnostics;
                assemble(result, function_text, function_lines);
                return result;
            }

        private:
            struct Line
            {
                int indent;
                string text;
                uint32_t node;
            };

            const Graph& m_graph;
            map<uint32_t, NodeDefinition> m_definitions;
            map<PinKey, PinKey> m_data_sources;
            map<PinKey, PinKey> m_exec_targets;
            set<PinKey> m_used_outputs;
            set<uint32_t> m_reached;
            vector<uint32_t> m_exec_stack;
            vector<uint32_t> m_expression_stack;
            vector<Diagnostic> m_diagnostics;
            vector<Line> m_lines;
            uint32_t m_current_node = 0;
            bool m_uses_delay       = false;

            bool has_errors() const
            {
                return any_of(m_diagnostics.begin(), m_diagnostics.end(), [](const Diagnostic& diagnostic) { return diagnostic.error; });
            }

            void error(uint32_t node, const string& message)
            {
                for (const Diagnostic& diagnostic : m_diagnostics)
                {
                    if (diagnostic.node == node && diagnostic.message == message)
                    {
                        return;
                    }
                }
                m_diagnostics.push_back({ node, true, message });
            }

            void warning(uint32_t node, const string& message)
            {
                m_diagnostics.push_back({ node, false, message });
            }

            const NodeDefinition* find(uint32_t node) const
            {
                auto it = m_definitions.find(node);
                return it != m_definitions.end() ? &it->second : nullptr;
            }

            string value_of(const Node& node, const PinDefinition& pin) const
            {
                auto it = node.values.find(pin.name);
                return it != node.values.end() ? it->second : pin.default_value;
            }

            // definitions, links, variables and events are checked before any code is written
            void analyze()
            {
                for (const Node& node : m_graph.nodes)
                {
                    NodeDefinition definition;
                    if (resolve_definition(node.type, m_graph, definition))
                    {
                        m_definitions[node.id] = move(definition);
                    }
                    else
                    {
                        error(node.id, "Unknown node '" + node.type + "', the lua api no longer has it.");
                    }
                }

                for (const Link& link : m_graph.links)
                {
                    const NodeDefinition* from = find(link.from_node);
                    const NodeDefinition* to   = find(link.to_node);
                    if (!from || !to)
                    {
                        continue;
                    }

                    const PinDefinition* output = find_pin(*from, link.from_pin, true);
                    const PinDefinition* input  = find_pin(*to, link.to_pin, false);
                    if (!output || !input)
                    {
                        error(link.to_node, "A link points at pin '" + (!output ? link.from_pin : link.to_pin) + "', which this node no longer has.");
                        continue;
                    }

                    const bool exec = output->type == type_exec;
                    if (exec != (input->type == type_exec))
                    {
                        error(link.to_node, "Execution and data pins cannot be linked ('" + link.from_pin + "' to '" + link.to_pin + "').");
                        continue;
                    }

                    if (exec)
                    {
                        const PinKey key = { link.from_node, link.from_pin };
                        if (m_exec_targets.count(key))
                        {
                            error(link.from_node, "Execution output '" + link.from_pin + "' leads to more than one node, use a Sequence.");
                            continue;
                        }
                        m_exec_targets[key] = { link.to_node, link.to_pin };
                        continue;
                    }

                    if (!input->connectable)
                    {
                        error(link.to_node, "'" + link.to_pin + "' takes its value inline, it cannot be linked.");
                        continue;
                    }

                    if (!types_compatible(output->type, input->type))
                    {
                        error(link.to_node, "'" + link.to_pin + "' expects " + input->type + ", it is linked to " + output->type + ".");
                        continue;
                    }

                    const PinKey key = { link.to_node, link.to_pin };
                    if (m_data_sources.count(key))
                    {
                        error(link.to_node, "Input '" + link.to_pin + "' has more than one link.");
                        continue;
                    }
                    m_data_sources[key] = { link.from_node, link.from_pin };
                    m_used_outputs.insert({ link.from_node, link.from_pin });
                }

                set<string> variable_names;
                for (const Variable& variable : m_graph.variables)
                {
                    if (!is_identifier(variable.name) || reserved_names.count(variable.name) || variable.name.rfind("Event_", 0) == 0)
                    {
                        error(0, "Variable '" + variable.name + "' needs a lua name: letters, digits and underscores, not a keyword or a lifecycle callback.");
                    }
                    if (!variable_names.insert(variable.name).second)
                    {
                        error(0, "Variable '" + variable.name + "' is declared twice.");
                    }
                }

                set<string> lifecycle_seen;
                set<string> custom_seen;
                for (const Node& node : m_graph.nodes)
                {
                    const NodeDefinition* definition = find(node.id);
                    if (!definition)
                    {
                        continue;
                    }

                    if (definition->kind == NodeKind::Event && !lifecycle_seen.insert(definition->payload).second)
                    {
                        error(node.id, "There is already an " + definition->title + " node, a script has one of each event.");
                    }

                    if (definition->kind == NodeKind::CustomEvent)
                    {
                        const string name = custom_event_name(node);
                        if (!is_identifier(name))
                        {
                            error(node.id, "The custom event needs a lua name: letters, digits and underscores.");
                        }
                        else if (!custom_seen.insert(name).second)
                        {
                            error(node.id, "Custom event '" + name + "' is declared twice.");
                        }
                    }

                    if ((definition->kind == NodeKind::VariableGet || definition->kind == NodeKind::VariableSet) && !m_graph.find_variable(definition->payload))
                    {
                        error(node.id, "Variable '" + definition->payload + "' does not exist, add it or delete this node.");
                    }
                }

                for (const Node& node : m_graph.nodes)
                {
                    const NodeDefinition* definition = find(node.id);
                    if (definition && definition->kind == NodeKind::CallCustomEvent && !custom_seen.count(definition->payload))
                    {
                        error(node.id, "Custom event '" + definition->payload + "' does not exist.");
                    }
                }
            }

            static string custom_event_name(const Node& node)
            {
                auto it = node.data.find("name");
                return it != node.data.end() ? trim(it->second) : "";
            }

            void emit(int indent, const string& text)
            {
                // lua reads a line that opens with ( as a call on the previous statement, the ; ends that statement
                m_lines.push_back({ indent, !text.empty() && text[0] == '(' ? ";" + text : text, m_current_node });
            }

            // follows an execution output to whatever it leads into
            void emit_exec(uint32_t node, const string& output, int indent)
            {
                auto it = m_exec_targets.find({ node, output });
                if (it != m_exec_targets.end())
                {
                    emit_node(it->second.first, it->second.second, indent);
                }
            }

            bool exec_linked(uint32_t node, const string& output) const
            {
                return m_exec_targets.count({ node, output }) != 0;
            }

            void emit_node(uint32_t id, const string& entry, int indent)
            {
                const NodeDefinition* definition = find(id);
                const Node* node                 = m_graph.find_node(id);
                if (!definition || !node)
                {
                    return;
                }

                if (find_if(m_exec_stack.begin(), m_exec_stack.end(), [id](uint32_t value) { return value == id; }) != m_exec_stack.end())
                {
                    error(id, "Execution loops back into this node, use a loop node to repeat work.");
                    return;
                }

                m_exec_stack.push_back(id);
                m_reached.insert(id);
                const uint32_t previous_node = m_current_node;
                m_current_node               = id;
                const string node_id         = to_string(id);

                emit(indent, "-- " + definition->title + " [" + node_id + "]");

                switch (definition->kind)
                {
                    case NodeKind::ApiCall:
                    {
                        const ApiFunction* function = definition->function;
                        if (function && function->kind == ApiKind::PropertySet)
                        {
                            emit(indent, prefix(input(*node, *definition, "target")) + "." + function->name + " = " + input(*node, *definition, function->parameters.empty() ? "" : function->parameters[0].name));
                        }
                        else
                        {
                            const string call = api_call(*node, *definition);
                            vector<string> slots;
                            bool used = false;
                            for (const PinDefinition& pin : definition->outputs)
                            {
                                if (pin.type == type_exec)
                                {
                                    continue;
                                }
                                slots.push_back(output_slot(id, pin.name));
                                used = used || m_used_outputs.count({ id, pin.name });
                            }

                            if (used)
                            {
                                string left;
                                for (size_t i = 0; i < slots.size(); i++)
                                {
                                    left += (i > 0 ? ", " : "") + slots[i];
                                }
                                emit(indent, left + " = " + call);
                            }
                            else if (function && (function->kind == ApiKind::Function || function->kind == ApiKind::Method || function->kind == ApiKind::Constructor))
                            {
                                emit(indent, call);
                            }
                            else
                            {
                                // a value lua cannot use as a statement, kept for its side effects only when it has some
                                emit(indent, "local _ = " + call);
                            }
                        }
                        emit_exec(id, "then", indent);
                        break;
                    }

                    case NodeKind::Statement:
                    {
                        emit(indent, fill(definition->payload, data_inputs(*node, *definition)));
                        emit_exec(id, "then", indent);
                        break;
                    }

                    case NodeKind::VariableSet:
                    {
                        emit(indent, "self." + definition->payload + " = " + input(*node, *definition, definition->payload));
                        emit_exec(id, "then", indent);
                        break;
                    }

                    case NodeKind::CallCustomEvent:
                    {
                        emit(indent, "self:Event_" + definition->payload + "(entity)");
                        emit_exec(id, "then", indent);
                        break;
                    }

                    case NodeKind::Branch:
                    {
                        emit(indent, "if " + input(*node, *definition, "condition") + " then");
                        emit_exec(id, "true", indent + 1);
                        if (exec_linked(id, "false"))
                        {
                            emit(indent, "else");
                            emit_exec(id, "false", indent + 1);
                        }
                        emit(indent, "end");
                        break;
                    }

                    case NodeKind::Sequence:
                    {
                        for (const PinDefinition& pin : definition->outputs)
                        {
                            if (pin.type == type_exec && exec_linked(id, pin.name))
                            {
                                emit(indent, "do");
                                emit_exec(id, pin.name, indent + 1);
                                emit(indent, "end");
                            }
                        }
                        break;
                    }

                    case NodeKind::ForLoop:
                    {
                        const string index = "i" + node_id;
                        emit(indent, "for " + index + " = " + input(*node, *definition, "first") + ", " + input(*node, *definition, "last") + " do");
                        emit(indent + 1, output_slot(id, "index") + " = " + index);
                        emit_exec(id, "body", indent + 1);
                        emit(indent, "end");
                        emit_exec(id, "completed", indent);
                        break;
                    }

                    case NodeKind::ForEach:
                    {
                        const string index = "i" + node_id;
                        const string item  = "v" + node_id;
                        emit(indent, "for " + index + ", " + item + " in ipairs(" + input(*node, *definition, "table") + " or {}) do");
                        emit(indent + 1, output_slot(id, "index") + " = " + index);
                        emit(indent + 1, output_slot(id, "item") + " = " + item);
                        emit_exec(id, "body", indent + 1);
                        emit(indent, "end");
                        emit_exec(id, "completed", indent);
                        break;
                    }

                    case NodeKind::WhileLoop:
                    {
                        emit(indent, "while " + input(*node, *definition, "condition") + " do");
                        emit_exec(id, "body", indent + 1);
                        emit(indent, "end");
                        emit_exec(id, "completed", indent);
                        break;
                    }

                    case NodeKind::DoOnce:
                    {
                        const string flag = "vs.once" + node_id;
                        if (entry == "reset")
                        {
                            emit(indent, flag + " = false");
                        }
                        else
                        {
                            emit(indent, "if not " + flag + " then");
                            emit(indent + 1, flag + " = true");
                            emit_exec(id, "then", indent + 1);
                            emit(indent, "end");
                        }
                        break;
                    }

                    case NodeKind::FlipFlop:
                    {
                        const string flag = "vs.flip" + node_id;
                        emit(indent, flag + " = not " + flag);
                        emit(indent, "if " + flag + " then");
                        emit_exec(id, "a", indent + 1);
                        if (exec_linked(id, "b"))
                        {
                            emit(indent, "else");
                            emit_exec(id, "b", indent + 1);
                        }
                        emit(indent, "end");
                        break;
                    }

                    case NodeKind::Delay:
                    {
                        m_uses_delay = true;
                        emit(indent, "vs_delay(" + input(*node, *definition, "seconds") + ", function()");
                        emit_exec(id, "completed", indent + 1);
                        emit(indent, "end)");
                        break;
                    }

                    default:
                        error(id, "A " + definition->title + " node cannot be part of the execution flow.");
                        break;
                }

                m_current_node = previous_node;
                m_exec_stack.pop_back();
            }

            static string output_slot(uint32_t node, const string& pin)
            {
                return "vs.o" + to_string(node) + "_" + sanitize(pin);
            }

            vector<string> data_inputs(const Node& node, const NodeDefinition& definition)
            {
                vector<string> values;
                for (const PinDefinition& pin : definition.inputs)
                {
                    if (pin.type != type_exec)
                    {
                        values.push_back(input(node, definition, pin.name));
                    }
                }
                return values;
            }

            static string fill(const string& pattern, const vector<string>& arguments)
            {
                string result;
                for (size_t i = 0; i < pattern.size(); i++)
                {
                    if (pattern[i] == '{' && i + 2 < pattern.size() && isdigit(static_cast<unsigned char>(pattern[i + 1])) && pattern[i + 2] == '}')
                    {
                        const size_t index = static_cast<size_t>(pattern[i + 1] - '0');
                        result += index < arguments.size() ? arguments[index] : "nil";
                        i += 2;
                        continue;
                    }
                    result += pattern[i];
                }
                return result;
            }

            // what an input reads: the linked output, or its inline value
            string input(const Node& node, const NodeDefinition& definition, const string& pin_name)
            {
                const PinDefinition* pin = find_pin(definition, pin_name, false);
                if (!pin)
                {
                    error(node.id, "Missing input '" + pin_name + "'.");
                    return "nil";
                }

                auto source = m_data_sources.find({ node.id, pin_name });
                if (source != m_data_sources.end())
                {
                    return output(source->second.first, source->second.second);
                }

                if (definition.has_target && pin_name == "target")
                {
                    return default_target(node, pin->type);
                }

                return literal(*pin, value_of(node, *pin), node.id);
            }

            // an unlinked target means the script's own entity, or that entity's component of the method's type
            string default_target(const Node& node, const string& type)
            {
                if (type == "Entity")
                {
                    return "entity";
                }

                if (const ApiEnum* component_types = find_enum("ComponentType"))
                {
                    for (const auto& value : component_types->values)
                    {
                        if (value.first == type)
                        {
                            return "entity:GetComponent(ComponentType." + type + ")";
                        }
                    }
                }

                error(node.id, "Link a " + type + " to target.");
                return "nil";
            }

            string literal(const PinDefinition& pin, const string& text, uint32_t node)
            {
                const string& type = pin.type;
                const string clean = trim(text);

                if (clean.empty() && (pin.optional || pin.variadic))
                {
                    return "nil";
                }

                if (type == "boolean")
                {
                    return clean == "true" || clean == "1" ? "true" : "false";
                }

                if (type == "number" || type == "integer")
                {
                    double value = 0.0;
                    if (!clean.empty() && !parse_number(clean, value))
                    {
                        error(node, "'" + pin.name + "' holds '" + clean + "', which is not a number.");
                    }
                    return number_text(value, type == "integer");
                }

                if (type == "string")
                {
                    return quote(text);
                }

                if (const ApiEnum* entry = find_enum(type))
                {
                    for (const auto& value : entry->values)
                    {
                        if (value.first == clean)
                        {
                            return type + "." + value.first;
                        }
                    }
                    double number = 0.0;
                    if (parse_number(clean, number))
                    {
                        return number_text(number, true);
                    }
                    if (!entry->values.empty())
                    {
                        if (!clean.empty())
                        {
                            error(node, "'" + clean + "' is not a " + type + " value.");
                        }
                        return type + "." + entry->values.front().first;
                    }
                    return "0";
                }

                if (type == "Vector2" || type == "Vector3" || type == "Vector4")
                {
                    const size_t count     = static_cast<size_t>(type.back() - '0');
                    vector<double> numbers = parse_numbers(clean);
                    numbers.resize(count, 0.0);
                    string result = type + "(";
                    for (size_t i = 0; i < count; i++)
                    {
                        result += (i > 0 ? ", " : "") + number_text(numbers[i], false);
                    }
                    return result + ")";
                }

                if (type == "Quaternion")
                {
                    vector<double> numbers = parse_numbers(clean);
                    numbers.resize(3, 0.0);
                    return "Quaternion.FromEulerAngles(" + number_text(numbers[0], false) + ", " + number_text(numbers[1], false) + ", " + number_text(numbers[2], false) + ")";
                }

                if (type == "any")
                {
                    double number = 0.0;
                    if (clean.empty() || clean == "nil")
                    {
                        return "nil";
                    }
                    if (clean == "true" || clean == "false")
                    {
                        return clean;
                    }
                    if (parse_number(clean, number))
                    {
                        return number_text(number, false);
                    }
                    return quote(text);
                }

                if (type == "table")
                {
                    return "{}";
                }

                if (pin.nullable || pin.optional)
                {
                    return "nil";
                }

                error(node, "Link a " + type + " to '" + pin.name + "'.");
                return "nil";
            }

            // what reading an output gives: an expression for pure nodes, the stored value for the rest
            string output(uint32_t id, const string& pin)
            {
                const NodeDefinition* definition = find(id);
                const Node* node                 = m_graph.find_node(id);
                if (!definition || !node)
                {
                    return "nil";
                }

                if (definition->pure)
                {
                    return expression(*node, *definition, pin);
                }

                switch (definition->kind)
                {
                    case NodeKind::Event:
                    case NodeKind::KeyEvent:
                    case NodeKind::CustomEvent:
                        return pin == "delta" ? "vs.delta" : "entity";
                    case NodeKind::VariableSet:
                        return "self." + definition->payload;
                    case NodeKind::FlipFlop:
                        return "(vs.flip" + to_string(id) + " == true)";
                    default:
                        return output_slot(id, pin);
                }
            }

            string expression(const Node& node, const NodeDefinition& definition, const string& pin)
            {
                if (std::find(m_expression_stack.begin(), m_expression_stack.end(), node.id) != m_expression_stack.end())
                {
                    error(node.id, "This value depends on itself.");
                    return "nil";
                }
                m_expression_stack.push_back(node.id);

                string result = "nil";
                switch (definition.kind)
                {
                    case NodeKind::Literal:
                        result = literal(definition.inputs[0], value_of(node, definition.inputs[0]), node.id);
                        break;
                    case NodeKind::SelfEntity:
                        result = "entity";
                        break;
                    case NodeKind::Expression:
                        result = fill(definition.payload, data_inputs(node, definition));
                        break;
                    case NodeKind::VariableGet:
                        result = "self." + definition.payload;
                        break;
                    case NodeKind::Cast:
                        result = input(node, definition, "value");
                        break;
                    case NodeKind::GetComponent:
                        result = prefix(input(node, definition, "target")) + ":GetComponent(ComponentType." + definition.payload + ")";
                        break;
                    case NodeKind::Break:
                        result = prefix(input(node, definition, "value")) + "." + pin;
                        break;
                    case NodeKind::ApiCall:
                    {
                        const string call = api_call(node, definition);
                        size_t index      = 0;
                        size_t count      = 0;
                        for (const PinDefinition& output : definition.outputs)
                        {
                            if (output.type == type_exec)
                            {
                                continue;
                            }
                            if (output.name == pin)
                            {
                                index = count;
                            }
                            count++;
                        }
                        const ApiKind kind = definition.function ? definition.function->kind : ApiKind::Function;
                        if (count <= 1 || kind == ApiKind::PropertyGet || kind == ApiKind::Constant || kind == ApiKind::Operator)
                        {
                            result = call;
                        }
                        else if (index == 0)
                        {
                            result = "(" + call + ")";
                        }
                        else
                        {
                            result = "(select(" + to_string(index + 1) + ", " + call + "))";
                        }
                        break;
                    }
                    default:
                        error(node.id, "A " + definition.title + " node has no value to read.");
                        break;
                }

                m_expression_stack.pop_back();
                return result;
            }

            string api_call(const Node& node, const NodeDefinition& definition)
            {
                const ApiFunction* function = definition.function;
                if (!function)
                {
                    return "nil";
                }

                // optional arguments nobody set are left out from the end, a gap in the middle passes nil
                vector<string> arguments;
                size_t last_needed = 0;
                for (const ApiParameter& parameter : function->parameters)
                {
                    const PinDefinition* pin = find_pin(definition, parameter.name, false);
                    if (!pin)
                    {
                        continue;
                    }
                    const bool linked = m_data_sources.count({ node.id, pin->name }) != 0;
                    const bool set    = !trim(value_of(node, *pin)).empty();
                    arguments.push_back(input(node, definition, pin->name));
                    if (linked || set || !(pin->optional || pin->variadic))
                    {
                        last_needed = arguments.size();
                    }
                }
                arguments.resize(last_needed);

                string list;
                for (size_t i = 0; i < arguments.size(); i++)
                {
                    list += (i > 0 ? ", " : "") + arguments[i];
                }

                const string owner = function->owner;
                switch (function->kind)
                {
                    case ApiKind::Method:
                        return prefix(input(node, definition, "target")) + ":" + function->name + "(" + list + ")";
                    case ApiKind::Constructor:
                        return owner + "(" + list + ")";
                    case ApiKind::PropertyGet:
                        return prefix(input(node, definition, "target")) + "." + function->name;
                    case ApiKind::Constant:
                        return owner + "." + function->name;
                    case ApiKind::Operator:
                        if (function->op == "unm")
                        {
                            return "(-" + (arguments.empty() ? string("nil") : arguments[0]) + ")";
                        }
                        arguments.resize(2, "nil");
                        return "(" + arguments[0] + " " + function->op + " " + arguments[1] + ")";
                    default:
                        return (owner.empty() ? function->name : owner + "." + function->name) + "(" + list + ")";
                }
            }

            struct Function
            {
                string header;
                vector<Line> lines;
            };

            Function emit_event(const string& header, uint32_t event_node, const char* output)
            {
                m_lines.clear();
                m_current_node = event_node;
                m_reached.insert(event_node);
                emit_exec(event_node, output, 1);
                m_current_node = 0;
                return { header, m_lines };
            }

            void generate(vector<string>& function_text, vector<vector<uint32_t>>& function_lines)
            {
                vector<Function> functions;
                uint32_t tick_node = 0;
                vector<uint32_t> key_nodes;

                for (const char* event : lifecycle_events)
                {
                    for (const Node& node : m_graph.nodes)
                    {
                        const NodeDefinition* definition = find(node.id);
                        if (!definition || definition->kind != NodeKind::Event || definition->payload != event)
                        {
                            continue;
                        }
                        if (string(event) == "Tick")
                        {
                            tick_node = node.id;
                        }
                        else if (exec_linked(node.id, "then"))
                        {
                            functions.push_back(emit_event(string("function script:") + event + "(entity)", node.id, "then"));
                        }
                        break;
                    }
                }

                for (const Node& node : m_graph.nodes)
                {
                    const NodeDefinition* definition = find(node.id);
                    if (definition && definition->kind == NodeKind::CustomEvent)
                    {
                        functions.push_back(emit_event("function script:Event_" + custom_event_name(node) + "(entity)", node.id, "then"));
                    }
                    if (definition && definition->kind == NodeKind::KeyEvent)
                    {
                        key_nodes.push_back(node.id);
                    }
                }

                // tick hosts the frame time, the delay queue and the key events alongside its own chain
                {
                    m_lines.clear();
                    for (uint32_t key_node : key_nodes)
                    {
                        const Node* node                 = m_graph.find_node(key_node);
                        const NodeDefinition* definition = find(key_node);
                        m_current_node                   = key_node;
                        m_reached.insert(key_node);
                        const string key = input(*node, *definition, "key");
                        if (exec_linked(key_node, "pressed"))
                        {
                            emit(1, "if Input.GetKeyDown(" + key + ") then");
                            emit_exec(key_node, "pressed", 2);
                            emit(1, "end");
                        }
                        if (exec_linked(key_node, "released"))
                        {
                            emit(1, "if Input.GetKeyUp(" + key + ") then");
                            emit_exec(key_node, "released", 2);
                            emit(1, "end");
                        }
                    }
                    vector<Line> key_lines = m_lines;

                    Function tick;
                    const bool tick_linked = tick_node && exec_linked(tick_node, "then");
                    if (tick_linked)
                    {
                        tick = emit_event("function script:Tick(entity)", tick_node, "then");
                    }

                    if (tick_linked || !key_lines.empty() || m_uses_delay)
                    {
                        Function result;
                        result.header = "function script:Tick(entity)";
                        result.lines.push_back({ 1, "vs.delta = Timer.GetDeltaTimeSec()", tick_node });
                        if (m_uses_delay)
                        {
                            result.lines.push_back({ 1, "vs_tick_delays(vs.delta)", 0 });
                        }
                        result.lines.insert(result.lines.end(), key_lines.begin(), key_lines.end());
                        result.lines.insert(result.lines.end(), tick.lines.begin(), tick.lines.end());
                        functions.push_back(result);
                    }
                }

                for (const Function& function : functions)
                {
                    string text = function.header + "\n";
                    vector<uint32_t> lines = { 0 };
                    for (const Line& line : function.lines)
                    {
                        text += string(static_cast<size_t>(line.indent) * 4, ' ') + line.text + "\n";
                        lines.push_back(line.node);
                    }
                    text += "end\n";
                    lines.push_back(0);
                    function_text.push_back(text);
                    function_lines.push_back(lines);
                }
            }

            void assemble(CompileResult& result, const vector<string>& function_text, const vector<vector<uint32_t>>& function_lines)
            {
                string& source = result.source;
                source  = "-- A visual script. Spartan generated this file from the graph below, edit the graph in the editor's Visual Script window,\n";
                source += "-- changes made to this file by hand are replaced the next time the graph is saved.\n";
                source += make_graph_block(m_graph.to_xml());
                source += "\n";

                const bool success = result.success;
                if (!success)
                {
                    source += "-- the graph has errors, so the script only reports them:\n";
                    for (const Diagnostic& diagnostic : m_diagnostics)
                    {
                        if (diagnostic.error)
                        {
                            source += "--   " + (diagnostic.node ? "node " + to_string(diagnostic.node) + ": " : string()) + diagnostic.message + "\n";
                        }
                    }
                    source += "\n";
                }

                source += "local script = {}\n";
                for (const Variable& variable : m_graph.variables)
                {
                    if (!is_identifier(variable.name))
                    {
                        continue;
                    }
                    PinDefinition pin;
                    pin.name     = variable.name;
                    pin.type     = variable.type;
                    pin.nullable = true;
                    source += "script." + variable.name + " = " + literal(pin, variable.value.empty() ? default_value_for(variable.type) : variable.value, 0) + "\n";
                }
                source += "\n";
                source += "-- per entity state the graph keeps between frames, out of the inspector's sight\n";
                source += "local vs = {}\n";

                if (success && m_uses_delay)
                {
                    source += "\n";
                    source += "local vs_delays = {}\n";
                    source += "local function vs_delay(seconds, callback)\n";
                    source += "    vs_delays[#vs_delays + 1] = { remaining = seconds, callback = callback }\n";
                    source += "end\n";
                    source += "local function vs_tick_delays(delta)\n";
                    source += "    local due = {}\n";
                    source += "    for i = #vs_delays, 1, -1 do\n";
                    source += "        local delay = vs_delays[i]\n";
                    source += "        delay.remaining = delay.remaining - delta\n";
                    source += "        if delay.remaining <= 0 then\n";
                    source += "            table.remove(vs_delays, i)\n";
                    source += "            due[#due + 1] = delay.callback\n";
                    source += "        end\n";
                    source += "    end\n";
                    source += "    for i = #due, 1, -1 do\n";
                    source += "        due[i]()\n";
                    source += "    end\n";
                    source += "end\n";
                }

                result.line_nodes.assign(static_cast<size_t>(count(source.begin(), source.end(), '\n')), 0);

                if (success)
                {
                    for (size_t i = 0; i < function_text.size(); i++)
                    {
                        source += "\n";
                        result.line_nodes.push_back(0);
                        source += function_text[i];
                        result.line_nodes.insert(result.line_nodes.end(), function_lines[i].begin(), function_lines[i].end());
                    }
                }
                else
                {
                    size_t errors = 0;
                    for (const Diagnostic& diagnostic : m_diagnostics)
                    {
                        errors += diagnostic.error ? 1 : 0;
                    }
                    source += "\n";
                    source += "function script:Start(entity)\n";
                    source += "    print(\"visual script on '\" .. entity:GetName() .. \"' has " + to_string(errors) + " error(s), open it in the Visual Script window\")\n";
                    source += "end\n";
                }

                source += "\nreturn script\n";
                result.line_nodes.resize(static_cast<size_t>(count(source.begin(), source.end(), '\n')), 0);
            }
        };
    }

    CompileResult compile(const Graph& graph)
    {
        Compiler compiler(graph);
        return compiler.run();
    }

    uint32_t node_from_lua_error(const CompileResult& result, const string& error)
    {
        // "<chunk>:<line>:" is how lua prefixes runtime and syntax errors
        for (size_t i = 0; i < error.size(); i++)
        {
            if (error[i] != ':' || i + 1 >= error.size() || !isdigit(static_cast<unsigned char>(error[i + 1])))
            {
                continue;
            }
            size_t end  = i + 1;
            size_t line = 0;
            while (end < error.size() && isdigit(static_cast<unsigned char>(error[end])))
            {
                line = line * 10 + static_cast<size_t>(error[end] - '0');
                end++;
            }
            if (end < error.size() && error[end] == ':' && line > 0 && line <= result.line_nodes.size())
            {
                return result.line_nodes[line - 1];
            }
        }
        return 0;
    }
}
