/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===================================
#include "pch.h"
#include "EditorMcpCommands.h"
#include "Editor.h"
#include "widgets/VisualScriptEditor.h"
#include "scripting/visual/VisualApi.h"
#include "scripting/visual/VisualEdit.h"
#include "file_system/FileSystem.h"
#include "world/Entity.h"
#include "world/World.h"
//==============================================

//= NAMESPACES ===========================
using namespace std;
using namespace spartan;
using namespace spartan::scripting::visual;
//========================================

namespace editor_mcp
{
    namespace
    {
        string number(const float value)
        {
            char text[32];
            snprintf(text, sizeof(text), "%.2f", value);
            return text;
        }

        string pins_json(const vector<PinDefinition>& pins, const Graph* graph, uint32_t node, bool output)
        {
            string json = "[";
            for (size_t i = 0; i < pins.size(); i++)
            {
                const PinDefinition& pin = pins[i];
                json += (i ? "," : "");
                json += "{\"name\":" + quote(pin.name) + ",\"type\":" + quote(pin.type);
                if (pin.optional)
                {
                    json += ",\"optional\":true";
                }
                if (pin.variadic)
                {
                    json += ",\"variadic\":true";
                }
                if (!pin.connectable)
                {
                    json += ",\"inline_only\":true";
                }
                if (!output && !pin.default_value.empty())
                {
                    json += ",\"default\":" + quote(pin.default_value);
                }
                if (graph)
                {
                    json += string(",\"connected\":") + boolean(graph->is_connected(node, pin.name, output));
                }
                json += "}";
            }
            return json + "]";
        }

        string definition_json(const NodeDefinition& definition, bool full)
        {
            string json = "{\"type\":" + quote(definition.id) + ",\"title\":" + quote(definition.title) + ",\"category\":" + quote(definition.category);
            json += string(",\"pure\":") + boolean(definition.pure);
            if (full)
            {
                json += ",\"tooltip\":" + quote(definition.tooltip);
                json += ",\"inputs\":" + pins_json(definition.inputs, nullptr, 0, false);
                json += ",\"outputs\":" + pins_json(definition.outputs, nullptr, 0, true);
                if (definition.function)
                {
                    json += string(",\"reflected\":") + boolean(definition.function->reflected);
                }
            }
            else
            {
                string signature;
                for (const PinDefinition& pin : definition.inputs)
                {
                    if (pin.type != type_exec)
                    {
                        signature += (signature.empty() ? "" : ", ") + pin.name + ": " + pin.type;
                    }
                }
                string returns;
                for (const PinDefinition& pin : definition.outputs)
                {
                    if (pin.type != type_exec)
                    {
                        returns += (returns.empty() ? "" : ", ") + pin.name + ": " + pin.type;
                    }
                }
                json += ",\"signature\":" + quote("(" + signature + ")" + (returns.empty() ? "" : " -> " + returns));
            }
            return json + "}";
        }

        string map_json(const map<string, string>& values)
        {
            string json = "{";
            bool first  = true;
            for (const auto& [key, value] : values)
            {
                json += (first ? "" : ",") + quote(key) + ":" + quote(value);
                first = false;
            }
            return json + "}";
        }

        string diagnostics_json(const CompileResult& result)
        {
            string json = "[";
            for (size_t i = 0; i < result.diagnostics.size(); i++)
            {
                const Diagnostic& diagnostic = result.diagnostics[i];
                json += (i ? "," : "");
                json += "{\"node\":" + to_string(diagnostic.node) + ",\"error\":" + boolean(diagnostic.error) + ",\"message\":" + quote(diagnostic.message) + "}";
            }
            return json + "]";
        }

        string generated_lua(const CompileResult& result)
        {
            const size_t start = result.source.find("\nlocal script");
            return start == string::npos ? result.source : result.source.substr(start + 1);
        }

        string state_json(VisualScriptEditor* editor, bool lua, bool xml)
        {
            Graph& graph                 = editor->GetGraph();
            const CompileResult& result  = editor->Compile();

            string nodes = "[";
            for (size_t i = 0; i < graph.nodes.size(); i++)
            {
                const Node& node = graph.nodes[i];
                NodeDefinition definition;
                const bool known = resolve_definition(node.type, graph, definition);
                nodes += (i ? "," : "");
                nodes += "{\"id\":" + to_string(node.id) + ",\"type\":" + quote(node.type) + ",\"title\":" + quote(known ? definition.title : "unknown") +
                    ",\"x\":" + number(node.x) + ",\"y\":" + number(node.y);
                if (!node.values.empty())
                {
                    nodes += ",\"values\":" + map_json(node.values);
                }
                if (!node.data.empty())
                {
                    nodes += ",\"data\":" + map_json(node.data);
                }
                if (known)
                {
                    nodes += ",\"inputs\":" + pins_json(definition.inputs, &graph, node.id, false);
                    nodes += ",\"outputs\":" + pins_json(definition.outputs, &graph, node.id, true);
                }
                nodes += "}";
            }
            nodes += "]";

            string links = "[";
            for (size_t i = 0; i < graph.links.size(); i++)
            {
                const Link& link = graph.links[i];
                links += (i ? "," : "");
                links += "{\"from_node\":" + to_string(link.from_node) + ",\"from_pin\":" + quote(link.from_pin) + ",\"to_node\":" + to_string(link.to_node) + ",\"to_pin\":" + quote(link.to_pin) + "}";
            }
            links += "]";

            string variables = "[";
            for (size_t i = 0; i < graph.variables.size(); i++)
            {
                const Variable& variable = graph.variables[i];
                variables += (i ? "," : "");
                variables += "{\"name\":" + quote(variable.name) + ",\"type\":" + quote(variable.type) + ",\"value\":" + quote(variable.value) + "}";
            }
            variables += "]";

            string selection = "[";
            bool first       = true;
            for (uint32_t id : editor->GetSelection())
            {
                selection += (first ? "" : ",") + to_string(id);
                first = false;
            }
            selection += "]";

            string json = "{\"ok\":true,\"path\":" + quote(editor->GetPath()) + ",\"dirty\":" + boolean(editor->IsDirty()) + ",\"visible\":" + boolean(editor->GetVisible()) +
                ",\"compiles\":" + boolean(result.success) + ",\"diagnostics\":" + diagnostics_json(result) +
                ",\"nodes\":" + nodes + ",\"links\":" + links + ",\"variables\":" + variables + ",\"selection\":" + selection;
            if (lua)
            {
                json += ",\"lua\":" + quote(generated_lua(result));
            }
            if (xml)
            {
                json += ",\"xml\":" + quote(graph.to_xml());
            }
            return json + "}";
        }

        VisualScriptEditor* get_open(Editor* editor, string& error)
        {
            VisualScriptEditor* widget = editor->GetWidget<VisualScriptEditor>();
            if (!widget)
            {
                error = "the visual script editor is not available";
                return nullptr;
            }
            if (!widget->IsOpen())
            {
                error = "no visual script is open, call visual_script_open with a path first";
                return nullptr;
            }
            return widget;
        }

        optional<uint32_t> node_id(const McpRequest& request, initializer_list<const char*> names)
        {
            const optional<uint64_t> value = as_uint(find_any(request, names));
            return value ? optional<uint32_t>(static_cast<uint32_t>(*value)) : nullopt;
        }

        vector<uint32_t> node_ids(const string& text)
        {
            vector<uint32_t> ids;
            size_t start = 0;
            while (start <= text.size())
            {
                size_t end = text.find_first_of(",; ", start);
                end        = end == string::npos ? text.size() : end;
                if (end > start)
                {
                    ids.push_back(static_cast<uint32_t>(strtoul(text.substr(start, end - start).c_str(), nullptr, 10)));
                }
                start = end + 1;
            }
            return ids;
        }

        // value_<pin>=text and data_<key>=text arguments, so an agent can set any number of pins in one call
        void apply_prefixed(const McpRequest& request, Node& node)
        {
            for (const auto& [key, value] : request.arguments)
            {
                if (key.rfind("value_", 0) == 0 && key.size() > 6)
                {
                    node.values[key.substr(6)] = value;
                }
                else if (key.rfind("data_", 0) == 0 && key.size() > 5)
                {
                    node.data[key.substr(5)] = value;
                }
            }
        }
    }

    void register_visual_script(Editor* editor)
    {
        add(
            "visual_script_api",
            [](const McpRequest& request) -> string
            {
                const ApiCoverage& coverage = get_coverage();
                string json = "{\"ok\":true,\"coverage\":{\"functions_reflected\":" + to_string(coverage.functions_reflected) +
                    ",\"functions_unreflected\":" + to_string(coverage.functions_unreflected) + ",\"usertypes\":" + to_string(coverage.usertypes) +
                    ",\"tables\":" + to_string(coverage.tables) + ",\"enums\":" + to_string(coverage.enums) + "}";

                // one node in full
                if (const string* type = find_any(request, { "type", "node_type" }))
                {
                    const NodeDefinition* definition = find_definition(*type);
                    if (!definition)
                    {
                        return failure("no node type " + *type + ", search with visual_script_api search=...");
                    }
                    return json + ",\"node\":" + definition_json(*definition, true) + "}";
                }

                const string* search_text   = find_any(request, { "search", "query" });
                const string* category_text = find(request, "category");
                const string search         = search_text ? to_lower(*search_text) : "";
                const string category       = category_text ? to_lower(*category_text) : "";
                const size_t limit          = static_cast<size_t>(as_uint(find(request, "limit")).value_or(60));
                const size_t offset         = static_cast<size_t>(as_uint(find(request, "offset")).value_or(0));

                // without a query, the categories are the useful answer
                if (search.empty() && category.empty())
                {
                    map<string, size_t> categories;
                    for (const NodeDefinition& definition : get_definitions())
                    {
                        categories[definition.category]++;
                    }
                    string list = "[";
                    bool first  = true;
                    for (const auto& [name, count] : categories)
                    {
                        list += (first ? "" : ",") + string("{\"category\":") + quote(name) + ",\"nodes\":" + to_string(count) + "}";
                        first = false;
                    }
                    return json + ",\"node_types\":" + to_string(get_definitions().size()) + ",\"categories\":" + list + "]" +
                        ",\"hint\":\"pass search (words that must all appear) or category to list nodes, type for one node's pins\"}";
                }

                vector<string> words;
                size_t start = 0;
                while (start < search.size())
                {
                    size_t end = search.find(' ', start);
                    end        = end == string::npos ? search.size() : end;
                    if (end > start)
                    {
                        words.push_back(search.substr(start, end - start));
                    }
                    start = end + 1;
                }

                string list  = "[";
                size_t total = 0;
                size_t shown = 0;
                for (const NodeDefinition& definition : get_definitions())
                {
                    if (!category.empty() && to_lower(definition.category) != category && to_lower(definition.category).rfind(category + "|", 0) != 0)
                    {
                        continue;
                    }
                    const string haystack = to_lower(definition.id + " " + definition.title + " " + definition.category);
                    bool all              = true;
                    for (const string& word : words)
                    {
                        all = all && haystack.find(word) != string::npos;
                    }
                    if (!all)
                    {
                        continue;
                    }
                    total++;
                    if (total <= offset || shown >= limit)
                    {
                        continue;
                    }
                    list += (shown ? "," : "") + definition_json(definition, false);
                    shown++;
                }
                return json + ",\"total\":" + to_string(total) + ",\"nodes\":" + list + "]}";
            }
        );

        add(
            "visual_script_open",
            [editor](const McpRequest& request) -> string
            {
                VisualScriptEditor* widget = editor->GetWidget<VisualScriptEditor>();
                if (!widget)
                {
                    return failure("the visual script editor is not available");
                }
                const string* path = find_any(request, { "path", "file", "file_path" });
                if (!path || path->empty())
                {
                    return failure("pass path, a .lua file, create=true makes a new one");
                }

                string error;
                const bool create = as_bool(find(request, "create")).value_or(false);
                const bool ok     = create && !FileSystem::Exists(*path) ? widget->Create(*path, &error) : widget->Open(*path, &error);
                if (!ok)
                {
                    return failure(error);
                }
                widget->SetVisible(as_bool(find(request, "visible")).value_or(true));
                return state_json(widget, false, false);
            }
        );

        add(
            "visual_script_state",
            [editor](const McpRequest& request) -> string
            {
                string error;
                VisualScriptEditor* widget = get_open(editor, error);
                if (!widget)
                {
                    return failure(error);
                }
                return state_json(widget, as_bool(find(request, "lua")).value_or(false), as_bool(find(request, "xml")).value_or(false));
            }
        );

        add(
            "visual_script_add_node",
            [editor](const McpRequest& request) -> string
            {
                string error;
                VisualScriptEditor* widget = get_open(editor, error);
                if (!widget)
                {
                    return failure(error);
                }
                const string* type = find_any(request, { "type", "node_type" });
                if (!type)
                {
                    return failure("pass type, a node type from visual_script_api");
                }

                float x = 0.0f, y = 0.0f;
                widget->GetFreePosition(x, y);
                x = as_float(find(request, "x")).value_or(x);
                y = as_float(find(request, "y")).value_or(y);

                widget->BeginChange();
                Node* node = add_node(widget->GetGraph(), *type, x, y, &error);
                if (!node)
                {
                    widget->EndChange();
                    return failure(error);
                }
                apply_prefixed(request, *node);
                if (const string* name = find(request, "name"))
                {
                    node->data["name"] = *name;
                }
                if (const string* text = find(request, "text"))
                {
                    node->data["text"] = *text;
                }
                const uint32_t id = node->id;

                // optional wiring in the same call: from_node/from_pin feeds this node's in_pin, this node's out_pin feeds to_node/to_pin,
                // a missing in_pin or out_pin is the first pin whose type fits
                string link_error;
                if (const optional<uint32_t> from = node_id(request, { "from_node", "connect_from" }))
                {
                    const string* from_pin = find(request, "from_pin");
                    const string* in_pin   = find(request, "in_pin");
                    string pin             = in_pin ? *in_pin : "";
                    if (pin.empty() && from_pin)
                    {
                        NodeDefinition definition;
                        const Node* source = widget->GetGraph().find_node(*from);
                        if (source && resolve_definition(source->type, widget->GetGraph(), definition))
                        {
                            if (const PinDefinition* output = find_pin(definition, *from_pin, true))
                            {
                                pin = find_compatible_pin(widget->GetGraph(), id, output->type, false);
                            }
                        }
                    }
                    if (!from_pin || pin.empty() || !connect(widget->GetGraph(), *from, *from_pin, id, pin, &link_error))
                    {
                        link_error = link_error.empty() ? "could not link from node " + to_string(*from) + ", pass from_pin and to_pin" : link_error;
                    }
                }
                if (const optional<uint32_t> to = node_id(request, { "to_node", "connect_to" }))
                {
                    const string* to_pin  = find(request, "to_pin");
                    const string* out_pin = find(request, "out_pin");
                    string pin            = out_pin ? *out_pin : "";
                    if (pin.empty() && to_pin)
                    {
                        NodeDefinition definition;
                        const Node* target = widget->GetGraph().find_node(*to);
                        if (target && resolve_definition(target->type, widget->GetGraph(), definition))
                        {
                            if (const PinDefinition* input = find_pin(definition, *to_pin, false))
                            {
                                pin = find_compatible_pin(widget->GetGraph(), id, input->type, true);
                            }
                        }
                    }
                    if (!to_pin || pin.empty() || !connect(widget->GetGraph(), id, pin, *to, *to_pin, &link_error))
                    {
                        link_error = link_error.empty() ? "could not link to node " + to_string(*to) + ", pass to_pin" : link_error;
                    }
                }
                widget->EndChange();
                widget->SetSelection({ id });

                const CompileResult& result = widget->Compile();
                string json = "{\"ok\":true,\"node\":" + to_string(id) + ",\"compiles\":" + boolean(result.success) + ",\"diagnostics\":" + diagnostics_json(result);
                if (!link_error.empty())
                {
                    json += ",\"link_error\":" + quote(link_error);
                }
                return json + "}";
            }
        );

        add(
            "visual_script_remove_node",
            [editor](const McpRequest& request) -> string
            {
                string error;
                VisualScriptEditor* widget = get_open(editor, error);
                if (!widget)
                {
                    return failure(error);
                }
                const string* nodes = find_any(request, { "node", "nodes", "id", "ids" });
                if (!nodes)
                {
                    return failure("pass node, one id or several separated by commas");
                }
                widget->BeginChange();
                size_t removed = 0;
                for (uint32_t id : node_ids(*nodes))
                {
                    if (widget->GetGraph().find_node(id))
                    {
                        widget->GetGraph().remove_node(id);
                        removed++;
                    }
                }
                widget->EndChange();
                widget->SetSelection({});
                return "{\"ok\":true,\"removed\":" + to_string(removed) + "}";
            }
        );

        add(
            "visual_script_connect",
            [editor](const McpRequest& request) -> string
            {
                string error;
                VisualScriptEditor* widget = get_open(editor, error);
                if (!widget)
                {
                    return failure(error);
                }
                const optional<uint32_t> from = node_id(request, { "from_node", "from" });
                const optional<uint32_t> to   = node_id(request, { "to_node", "to" });
                const string* from_pin        = find(request, "from_pin");
                const string* to_pin          = find(request, "to_pin");
                if (!from || !to)
                {
                    return failure("pass from_node, from_pin, to_node and to_pin, execution pins are usually exec and then");
                }

                Graph& graph = widget->GetGraph();
                string output = from_pin ? *from_pin : "";
                string input  = to_pin ? *to_pin : "";
                // a missing pin name is guessed from the other side's type
                if (output.empty() || input.empty())
                {
                    NodeDefinition source_definition, target_definition;
                    const Node* source = graph.find_node(*from);
                    const Node* target = graph.find_node(*to);
                    if (!source || !target || !resolve_definition(source->type, graph, source_definition) || !resolve_definition(target->type, graph, target_definition))
                    {
                        return failure("both nodes have to exist");
                    }
                    if (output.empty() && !input.empty())
                    {
                        if (const PinDefinition* pin = find_pin(target_definition, input, false))
                        {
                            output = find_compatible_pin(graph, *from, pin->type, true);
                        }
                    }
                    else if (input.empty() && !output.empty())
                    {
                        if (const PinDefinition* pin = find_pin(source_definition, output, true))
                        {
                            input = find_compatible_pin(graph, *to, pin->type, false);
                        }
                    }
                    if (output.empty() || input.empty())
                    {
                        return failure("pass from_pin or to_pin, the other one is guessed from its type");
                    }
                }

                widget->BeginChange();
                const bool ok = connect(graph, *from, output, *to, input, &error);
                widget->EndChange();
                if (!ok)
                {
                    return failure(error);
                }
                const CompileResult& result = widget->Compile();
                return "{\"ok\":true,\"from_pin\":" + quote(output) + ",\"to_pin\":" + quote(input) + ",\"compiles\":" + boolean(result.success) + ",\"diagnostics\":" + diagnostics_json(result) + "}";
            }
        );

        add(
            "visual_script_disconnect",
            [editor](const McpRequest& request) -> string
            {
                string error;
                VisualScriptEditor* widget = get_open(editor, error);
                if (!widget)
                {
                    return failure(error);
                }
                const optional<uint32_t> node = node_id(request, { "node", "id" });
                const string* pin             = find(request, "pin");
                if (!node || !pin)
                {
                    return failure("pass node and pin, output=true for an output pin");
                }
                widget->BeginChange();
                widget->GetGraph().disconnect(*node, *pin, as_bool(find(request, "output")).value_or(false));
                widget->EndChange();
                return "{\"ok\":true}";
            }
        );

        add(
            "visual_script_set_value",
            [editor](const McpRequest& request) -> string
            {
                string error;
                VisualScriptEditor* widget = get_open(editor, error);
                if (!widget)
                {
                    return failure(error);
                }
                const optional<uint32_t> id = node_id(request, { "node", "id" });
                Node* node                  = id ? widget->GetGraph().find_node(*id) : nullptr;
                if (!node)
                {
                    return failure("pass node, the id of an existing node");
                }

                widget->BeginChange();
                if (const string* pin = find(request, "pin"))
                {
                    const string* value = find(request, "value");
                    node->values[*pin]  = value ? *value : "";
                }
                apply_prefixed(request, *node);
                if (const string* name = find(request, "name"))
                {
                    node->data["name"] = *name;
                }
                if (const string* text = find(request, "text"))
                {
                    node->data["text"] = *text;
                }
                if (const optional<float> x = as_float(find(request, "x")))
                {
                    node->x = *x;
                }
                if (const optional<float> y = as_float(find(request, "y")))
                {
                    node->y = *y;
                }
                widget->EndChange();

                const CompileResult& result = widget->Compile();
                return "{\"ok\":true,\"compiles\":" + string(boolean(result.success)) + ",\"diagnostics\":" + diagnostics_json(result) + "}";
            }
        );

        add(
            "visual_script_variable",
            [editor](const McpRequest& request) -> string
            {
                string error;
                VisualScriptEditor* widget = get_open(editor, error);
                if (!widget)
                {
                    return failure(error);
                }
                const string* action = find(request, "action");
                const string* name   = find(request, "name");
                if (!action || !name)
                {
                    return failure("pass action (add, remove, rename, set) and name");
                }

                Graph& graph = widget->GetGraph();
                const string verb = to_lower(*action);
                bool ok = true;
                widget->BeginChange();
                if (verb == "add")
                {
                    const string* type  = find(request, "type");
                    const string* value = find(request, "value");
                    ok = add_variable(graph, *name, type ? *type : "number", value ? *value : "", &error);
                }
                else if (verb == "remove")
                {
                    remove_variable(graph, *name);
                }
                else if (verb == "rename")
                {
                    const string* new_name = find_any(request, { "new_name", "to" });
                    ok = new_name && rename_variable(graph, *name, *new_name, &error);
                    if (!new_name)
                    {
                        error = "pass new_name";
                    }
                }
                else if (verb == "set")
                {
                    Variable* variable = graph.find_variable(*name);
                    if (!variable)
                    {
                        ok    = false;
                        error = "no variable named " + *name;
                    }
                    else
                    {
                        if (const string* type = find(request, "type"))
                        {
                            set_variable_type(graph, *name, *type);
                        }
                        if (const string* value = find(request, "value"))
                        {
                            graph.find_variable(*name)->value = *value;
                        }
                    }
                }
                else
                {
                    ok    = false;
                    error = "action is add, remove, rename or set";
                }
                widget->EndChange();
                if (!ok)
                {
                    return failure(error);
                }
                return state_json(widget, false, false);
            }
        );

        add(
            "visual_script_set_graph",
            [editor](const McpRequest& request) -> string
            {
                string error;
                VisualScriptEditor* widget = get_open(editor, error);
                if (!widget)
                {
                    return failure(error);
                }
                const string* xml = find(request, "xml");
                if (!xml)
                {
                    return failure("pass xml, the format visual_script_state xml=true returns");
                }
                Graph graph;
                if (!graph.from_xml(*xml, &error))
                {
                    return failure("the xml did not parse: " + error);
                }
                widget->BeginChange();
                widget->GetGraph() = graph;
                widget->EndChange();
                widget->FrameAll();
                return state_json(widget, false, false);
            }
        );

        add(
            "visual_script_compile",
            [editor](const McpRequest&) -> string
            {
                string error;
                VisualScriptEditor* widget = get_open(editor, error);
                if (!widget)
                {
                    return failure(error);
                }
                const CompileResult& result = widget->Compile();
                return "{\"ok\":true,\"compiles\":" + string(boolean(result.success)) + ",\"diagnostics\":" + diagnostics_json(result) + ",\"lua\":" + quote(generated_lua(result)) + "}";
            }
        );

        add(
            "visual_script_save",
            [editor](const McpRequest&) -> string
            {
                string error;
                VisualScriptEditor* widget = get_open(editor, error);
                if (!widget)
                {
                    return failure(error);
                }
                uint32_t reloaded           = 0;
                const CompileResult result  = widget->Save(&reloaded);
                return "{\"ok\":true,\"path\":" + quote(widget->GetPath()) + ",\"compiles\":" + boolean(result.success) + ",\"reloaded_components\":" + to_string(reloaded) +
                    ",\"diagnostics\":" + diagnostics_json(result) + "}";
            }
        );

        add(
            "visual_script_attach",
            [editor](const McpRequest& request) -> string
            {
                string error;
                VisualScriptEditor* widget = get_open(editor, error);
                if (!widget)
                {
                    return failure(error);
                }
                Entity* entity = nullptr;
                if (const optional<uint64_t> id = as_uint(find_any(request, { "entity", "entity_id", "id" })))
                {
                    entity = World::GetEntityById(*id);
                    if (!entity)
                    {
                        return failure("no entity with id " + to_string(*id));
                    }
                }
                const bool ok = entity ? widget->AttachToEntity(entity, &error) : widget->AttachToSelectedEntity(&error);
                if (!ok)
                {
                    return failure(error);
                }
                return "{\"ok\":true,\"path\":" + quote(widget->GetPath()) + "}";
            }
        );

        add(
            "visual_script_view",
            [editor](const McpRequest& request) -> string
            {
                VisualScriptEditor* widget = editor->GetWidget<VisualScriptEditor>();
                if (!widget)
                {
                    return failure("the visual script editor is not available");
                }
                if (const optional<bool> visible = as_bool(find(request, "visible")))
                {
                    widget->SetVisible(*visible);
                }
                if (const optional<uint32_t> focus = node_id(request, { "focus", "node" }))
                {
                    widget->FocusNode(*focus);
                }
                if (as_bool(find(request, "frame")).value_or(false))
                {
                    widget->FrameAll();
                }
                if (const string* selection = find(request, "select"))
                {
                    const vector<uint32_t> ids = node_ids(*selection);
                    widget->SetSelection(set<uint32_t>(ids.begin(), ids.end()));
                }
                if (as_bool(find(request, "undo")).value_or(false))
                {
                    widget->Undo();
                }
                if (as_bool(find(request, "redo")).value_or(false))
                {
                    widget->Redo();
                }
                if (const optional<float> size_x = as_float(find(request, "width")))
                {
                    const float size_y = as_float(find(request, "height")).value_or(800.0f);
                    widget->RequestSize(math::Vector2(*size_x, size_y));
                }
                return "{\"ok\":true,\"open\":" + string(boolean(widget->IsOpen())) + ",\"visible\":" + boolean(widget->GetVisible()) + ",\"path\":" + quote(widget->GetPath()) + "}";
            }
        );
    }
}
