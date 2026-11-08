/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===========
#include "pch.h"
#include "VisualNodes.h"
#include <algorithm>
#include <set>
#include <unordered_map>
//======================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan::scripting::visual
{
    namespace
    {
        vector<NodeDefinition> definitions;
        unordered_map<string, size_t> definition_lookup;
        uint64_t built_revision = UINT64_MAX;

        const set<string> math_types = { "Vector2", "Vector3", "Vector4", "Quaternion", "BoundingBox" };

        PinDefinition exec_pin(const char* name)
        {
            PinDefinition pin;
            pin.name = name;
            pin.type = type_exec;
            return pin;
        }

        PinDefinition data_pin(const string& name, const string& type, const string& default_value = "", bool optional = false)
        {
            PinDefinition pin;
            pin.name          = name;
            pin.type          = type;
            pin.default_value = default_value.empty() ? default_value_for(type) : default_value;
            pin.optional      = optional;
            return pin;
        }

        NodeDefinition make(const string& id, const string& title, const string& category, NodeKind kind, const string& tooltip)
        {
            NodeDefinition definition;
            definition.id       = id;
            definition.title    = title;
            definition.category = category;
            definition.kind     = kind;
            definition.tooltip  = tooltip;
            return definition;
        }

        NodeDefinition make_event(const string& name, const string& title, const string& tooltip)
        {
            NodeDefinition definition = make("event." + name, title, "Events", NodeKind::Event, tooltip);
            definition.payload        = name;
            definition.outputs        = { exec_pin("then"), data_pin("entity", "Entity") };
            if (name == "Tick")
            {
                definition.outputs.push_back(data_pin("delta", "number"));
            }
            return definition;
        }

        // a pure node whose lua is an expression over its inputs
        NodeDefinition make_expression(const string& id, const string& title, const string& category, const string& lua, vector<PinDefinition> inputs, const string& output_type, const string& tooltip, const string& output_name = "result")
        {
            NodeDefinition definition = make(id, title, category, NodeKind::Expression, tooltip);
            definition.pure           = true;
            definition.payload        = lua;
            definition.inputs         = move(inputs);
            definition.outputs        = { data_pin(output_name, output_type) };
            return definition;
        }

        // an execution node whose lua is a statement over its inputs
        NodeDefinition make_statement(const string& id, const string& title, const string& category, const string& lua, vector<PinDefinition> inputs, const string& tooltip)
        {
            NodeDefinition definition = make(id, title, category, NodeKind::Statement, tooltip);
            definition.payload        = lua;
            definition.inputs         = { exec_pin("exec") };
            definition.inputs.insert(definition.inputs.end(), inputs.begin(), inputs.end());
            definition.outputs        = { exec_pin("then") };
            return definition;
        }

        bool is_pure(const ApiFunction& function)
        {
            if (!function.reflected)
            {
                return false;
            }

            switch (function.kind)
            {
                case ApiKind::Constructor:
                case ApiKind::PropertyGet:
                case ApiKind::Constant:
                case ApiKind::Operator:
                    return true;
                case ApiKind::PropertySet:
                    return false;
                default:
                    break;
            }

            if (function.returns.empty())
            {
                return false;
            }

            if (math_types.count(function.owner))
            {
                return function.name != "Normalize" && function.name != "Merge";
            }

            for (const char* prefix : { "Get", "Is", "Has", "Can", "Contains", "Intersects" })
            {
                if (function.name.rfind(prefix, 0) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        string operator_title(const string& op)
        {
            if (op == "+")   return "Add";
            if (op == "-")   return "Subtract";
            if (op == "*")   return "Multiply";
            if (op == "/")   return "Divide";
            if (op == "%")   return "Modulo";
            if (op == "^")   return "Power";
            if (op == "..")  return "Concatenate";
            if (op == "==")  return "Equal";
            if (op == "<")   return "Less";
            if (op == "<=")  return "Less Or Equal";
            if (op == "unm") return "Negate";
            return op;
        }

        string signature_text(const ApiFunction& function)
        {
            string text;
            switch (function.kind)
            {
                case ApiKind::Method:      text = function.owner + ":" + function.name; break;
                case ApiKind::Constructor: text = function.owner;                       break;
                case ApiKind::PropertyGet: text = function.owner + "." + function.name; break;
                case ApiKind::PropertySet: text = function.owner + "." + function.name + " ="; break;
                case ApiKind::Constant:    text = function.owner + "." + function.name; break;
                case ApiKind::Operator:    text = function.owner + " " + function.op;   break;
                default:                   text = function.owner.empty() ? function.name : function.owner + "." + function.name; break;
            }

            if (function.kind != ApiKind::PropertyGet && function.kind != ApiKind::Constant)
            {
                text += "(";
                for (size_t i = 0; i < function.parameters.size(); i++)
                {
                    const ApiParameter& parameter = function.parameters[i];
                    text += (i > 0 ? ", " : "") + parameter.name + ": " + parameter.type + (parameter.optional ? "?" : "") + (parameter.variadic ? "..." : "");
                }
                text += ")";
            }

            if (!function.returns.empty())
            {
                text += " -> ";
                for (size_t i = 0; i < function.returns.size(); i++)
                {
                    text += (i > 0 ? ", " : "") + function.returns[i].type;
                }
            }
            return text;
        }

        NodeDefinition make_api_node(const ApiFunction& function)
        {
            NodeDefinition definition;
            definition.id       = "api:" + function.id();
            definition.kind     = NodeKind::ApiCall;
            definition.function = &function;
            definition.pure     = is_pure(function);
            definition.category = function.owner.empty() ? "Lua" : function.owner;
            definition.tooltip  = signature_text(function);
            if (!function.reflected)
            {
                definition.tooltip += "\nNo recorded signature, the arguments are passed as given.";
            }

            switch (function.kind)
            {
                case ApiKind::Constructor: definition.title = "Make " + function.owner;            break;
                case ApiKind::PropertyGet: definition.title = "Get " + function.name;               break;
                case ApiKind::PropertySet: definition.title = "Set " + function.name;               break;
                case ApiKind::Operator:    definition.title = operator_title(function.op);          break;
                default:                   definition.title = pretty_name(function.name);           break;
            }

            if (function.overload_count > 1)
            {
                string types;
                for (size_t i = 0; i < function.parameters.size(); i++)
                {
                    types += (i > 0 ? ", " : "") + function.parameters[i].type;
                }
                definition.title += " (" + types + ")";
            }

            if (!definition.pure)
            {
                definition.inputs.push_back(exec_pin("exec"));
                definition.outputs.push_back(exec_pin("then"));
            }

            if (function.kind == ApiKind::Method || function.kind == ApiKind::PropertyGet || function.kind == ApiKind::PropertySet)
            {
                definition.has_target = true;
                definition.inputs.push_back(data_pin("target", function.owner));
            }

            for (const ApiParameter& parameter : function.parameters)
            {
                PinDefinition pin = data_pin(parameter.name, parameter.type, "", parameter.optional);
                pin.variadic      = parameter.variadic;
                pin.nullable      = parameter.nullable;
                if (parameter.optional)
                {
                    pin.default_value.clear();
                }
                definition.inputs.push_back(pin);
            }

            for (const ApiParameter& value : function.returns)
            {
                PinDefinition pin = data_pin(value.name, value.type);
                pin.nullable      = value.nullable;
                definition.outputs.push_back(pin);
            }

            return definition;
        }

        void add(NodeDefinition&& definition)
        {
            definition_lookup[definition.id] = definitions.size();
            definitions.push_back(move(definition));
        }

        void build()
        {
            definitions.clear();
            definition_lookup.clear();

            // events
            add(make_event("Initialize", "On Initialize", "Runs when the script loads, in the editor as well, before play starts."));
            add(make_event("Start",      "On Start",      "Runs once when play starts."));
            add(make_event("Stop",       "On Stop",       "Runs once when play stops."));
            add(make_event("Remove",     "On Remove",     "Runs when the script component is removed from its entity."));
            add(make_event("PreTick",    "On Pre Tick",   "Runs every frame of play, before Tick."));
            add(make_event("Tick",       "On Tick",       "Runs every frame of play, delta is the frame time in seconds."));
            {
                NodeDefinition definition = make("event.key", "On Key", "Events", NodeKind::KeyEvent, "Runs during play when the key goes down (pressed) or comes back up (released).");
                PinDefinition key         = data_pin("key", "KeyCode", find_enum("KeyCode") ? "Space" : "");
                key.connectable           = false;
                definition.inputs         = { key };
                definition.outputs        = { exec_pin("pressed"), exec_pin("released"), data_pin("entity", "Entity") };
                add(move(definition));
            }
            {
                NodeDefinition definition = make("event.custom", "Custom Event", "Events", NodeKind::CustomEvent, "A function of this script. Call Event nodes run it, other scripts can call script:Event_<name>(entity).");
                definition.outputs        = { exec_pin("then"), data_pin("entity", "Entity") };
                add(move(definition));
            }

            // flow
            {
                NodeDefinition definition = make("flow.branch", "Branch", "Flow", NodeKind::Branch, "Runs true or false depending on the condition.");
                definition.inputs         = { exec_pin("exec"), data_pin("condition", "boolean") };
                definition.outputs        = { exec_pin("true"), exec_pin("false") };
                add(move(definition));
            }
            {
                NodeDefinition definition = make("flow.sequence", "Sequence", "Flow", NodeKind::Sequence, "Runs each output in order.");
                definition.inputs         = { exec_pin("exec") };
                definition.outputs        = { exec_pin("then 1"), exec_pin("then 2"), exec_pin("then 3"), exec_pin("then 4") };
                add(move(definition));
            }
            {
                NodeDefinition definition = make("flow.for", "For Loop", "Flow", NodeKind::ForLoop, "Runs body once for every index from first to last, both included, then completed.");
                definition.inputs         = { exec_pin("exec"), data_pin("first", "integer", "1"), data_pin("last", "integer", "10") };
                definition.outputs        = { exec_pin("body"), data_pin("index", "integer"), exec_pin("completed") };
                add(move(definition));
            }
            {
                NodeDefinition definition = make("flow.for_each", "For Each", "Flow", NodeKind::ForEach, "Runs body once for every item of an array table, then completed.");
                definition.inputs         = { exec_pin("exec"), data_pin("table", "table") };
                definition.outputs        = { exec_pin("body"), data_pin("item", "any"), data_pin("index", "integer"), exec_pin("completed") };
                add(move(definition));
            }
            {
                NodeDefinition definition = make("flow.while", "While Loop", "Flow", NodeKind::WhileLoop, "Runs body for as long as the condition holds, then completed. The frame waits for it.");
                definition.inputs         = { exec_pin("exec"), data_pin("condition", "boolean") };
                definition.outputs        = { exec_pin("body"), exec_pin("completed") };
                add(move(definition));
            }
            {
                NodeDefinition definition = make("flow.do_once", "Do Once", "Flow", NodeKind::DoOnce, "Lets execution through the first time only, until reset.");
                definition.inputs         = { exec_pin("exec"), exec_pin("reset") };
                definition.outputs        = { exec_pin("then") };
                add(move(definition));
            }
            {
                NodeDefinition definition = make("flow.flip_flop", "Flip Flop", "Flow", NodeKind::FlipFlop, "Alternates between a and b every time it runs.");
                definition.inputs         = { exec_pin("exec") };
                definition.outputs        = { exec_pin("a"), exec_pin("b"), data_pin("is a", "boolean") };
                add(move(definition));
            }
            {
                NodeDefinition definition = make("flow.delay", "Delay", "Flow", NodeKind::Delay, "Runs completed after the given seconds of play have passed.");
                definition.inputs         = { exec_pin("exec"), data_pin("seconds", "number", "1") };
                definition.outputs        = { exec_pin("completed") };
                add(move(definition));
            }

            // values
            for (const char* type : { "boolean", "number", "integer", "string" })
            {
                NodeDefinition definition = make(string("literal.") + type, pretty_name(type), "Values", NodeKind::Literal, string("A constant ") + type + ".");
                definition.pure           = true;
                definition.payload        = type;
                PinDefinition value       = data_pin("value", type);
                value.connectable         = false;
                definition.inputs         = { value };
                definition.outputs        = { data_pin("value", type) };
                definition.title[0]       = static_cast<char>(toupper(static_cast<unsigned char>(definition.title[0])));
                add(move(definition));
            }
            for (const ApiEnum& entry : get_enums())
            {
                NodeDefinition definition = make("enum:" + entry.name, entry.name, "Values|Enums", NodeKind::Literal, "A " + entry.name + " value.");
                definition.pure           = true;
                definition.payload        = entry.name;
                PinDefinition value       = data_pin("value", entry.name);
                value.connectable         = false;
                definition.inputs         = { value };
                definition.outputs        = { data_pin("value", entry.name) };
                add(move(definition));
            }
            {
                NodeDefinition definition = make("util.self", "Self", "Utilities", NodeKind::SelfEntity, "The entity this script runs on.");
                definition.pure           = true;
                definition.outputs        = { data_pin("entity", "Entity") };
                add(move(definition));
            }
            add(make_expression("util.is_valid",  "Is Valid",  "Utilities", "({0} ~= nil)", { data_pin("value", "any") }, "boolean", "True when the value is not nil."));
            add(make_expression("util.select",    "Select",    "Utilities", "(function() if {0} then return {1} end return {2} end)()", { data_pin("condition", "boolean"), data_pin("a", "any"), data_pin("b", "any") }, "any", "a when the condition holds, b otherwise."));
            add(make_expression("util.to_string", "To String", "Utilities", "tostring({0})", { data_pin("value", "any") }, "string", "The value as text."));
            add(make_expression("util.to_number", "To Number", "Utilities", "tonumber({0})", { data_pin("value", "any") }, "number", "The text as a number, nil when it is not one."));
            add(make_expression("util.type",      "Type Of",   "Utilities", "type({0})", { data_pin("value", "any") }, "string", "Lua's name for the value's type."));
            {
                NodeDefinition definition = make("util.comment", "Comment", "Utilities", NodeKind::Comment, "A note on the graph, it does not run.");
                add(move(definition));
            }

            // math on numbers
            const vector<PinDefinition> ab = { data_pin("a", "number"), data_pin("b", "number") };
            const vector<PinDefinition> x  = { data_pin("x", "number") };
            add(make_expression("math.add",        "Add",            "Math|Number", "({0} + {1})", ab, "number", "a + b"));
            add(make_expression("math.subtract",   "Subtract",       "Math|Number", "({0} - {1})", ab, "number", "a - b"));
            add(make_expression("math.multiply",   "Multiply",       "Math|Number", "({0} * {1})", ab, "number", "a * b"));
            add(make_expression("math.divide",     "Divide",         "Math|Number", "({0} / {1})", { data_pin("a", "number"), data_pin("b", "number", "1") }, "number", "a / b"));
            add(make_expression("math.modulo",     "Modulo",         "Math|Number", "({0} % {1})", { data_pin("a", "number"), data_pin("b", "number", "1") }, "number", "The remainder of a / b."));
            add(make_expression("math.power",      "Power",          "Math|Number", "({0} ^ {1})", ab, "number", "a raised to b."));
            add(make_expression("math.negate",     "Negate",         "Math|Number", "(-{0})", x, "number", "-x"));
            add(make_expression("math.abs",        "Absolute",       "Math|Number", "math.abs({0})", x, "number", "x without its sign."));
            add(make_expression("math.min",        "Min",            "Math|Number", "math.min({0}, {1})", ab, "number", "The smaller of a and b."));
            add(make_expression("math.max",        "Max",            "Math|Number", "math.max({0}, {1})", ab, "number", "The larger of a and b."));
            add(make_expression("math.clamp",      "Clamp",          "Math|Number", "math.min(math.max({0}, {1}), {2})", { data_pin("x", "number"), data_pin("min", "number"), data_pin("max", "number", "1") }, "number", "x kept between min and max."));
            add(make_expression("math.lerp",       "Lerp",           "Math|Number", "({0} + ({1} - {0}) * {2})", { data_pin("a", "number"), data_pin("b", "number", "1"), data_pin("t", "number", "0.5") }, "number", "From a to b by t."));
            add(make_expression("math.floor",      "Floor",          "Math|Number", "math.floor({0})", x, "integer", "x rounded down."));
            add(make_expression("math.ceil",       "Ceil",           "Math|Number", "math.ceil({0})", x, "integer", "x rounded up."));
            add(make_expression("math.round",      "Round",          "Math|Number", "math.floor({0} + 0.5)", x, "integer", "x rounded to the nearest integer."));
            add(make_expression("math.sqrt",       "Square Root",    "Math|Number", "math.sqrt({0})", x, "number", "The square root of x."));
            add(make_expression("math.sin",        "Sin",            "Math|Number", "math.sin({0})", x, "number", "Sine of x radians."));
            add(make_expression("math.cos",        "Cos",            "Math|Number", "math.cos({0})", x, "number", "Cosine of x radians."));
            add(make_expression("math.tan",        "Tan",            "Math|Number", "math.tan({0})", x, "number", "Tangent of x radians."));
            add(make_expression("math.atan2",      "Atan2",          "Math|Number", "math.atan({0}, {1})", { data_pin("y", "number"), data_pin("x", "number", "1") }, "number", "The angle of (x, y) in radians."));
            add(make_expression("math.radians",    "To Radians",     "Math|Number", "math.rad({0})", { data_pin("degrees", "number") }, "number", "Degrees to radians."));
            add(make_expression("math.degrees",    "To Degrees",     "Math|Number", "math.deg({0})", { data_pin("radians", "number") }, "number", "Radians to degrees."));
            add(make_expression("math.pi",         "Pi",             "Math|Number", "math.pi", {}, "number", "3.14159...", "pi"));
            add(make_expression("math.random",     "Random",         "Math|Number", "math.random()", {}, "number", "A random number from 0 up to 1."));
            add(make_expression("math.random_range", "Random Range", "Math|Number", "({0} + math.random() * ({1} - {0}))", { data_pin("min", "number"), data_pin("max", "number", "1") }, "number", "A random number between min and max."));
            add(make_expression("math.random_integer", "Random Integer", "Math|Number", "math.random({0}, {1})", { data_pin("min", "integer", "1"), data_pin("max", "integer", "6") }, "integer", "A random integer between min and max, both included."));

            // comparisons and logic
            add(make_expression("compare.less",          "Less",             "Math|Compare", "({0} < {1})",  ab, "boolean", "a < b"));
            add(make_expression("compare.less_equal",    "Less Or Equal",    "Math|Compare", "({0} <= {1})", ab, "boolean", "a <= b"));
            add(make_expression("compare.greater",       "Greater",          "Math|Compare", "({0} > {1})",  ab, "boolean", "a > b"));
            add(make_expression("compare.greater_equal", "Greater Or Equal", "Math|Compare", "({0} >= {1})", ab, "boolean", "a >= b"));
            add(make_expression("compare.equal",         "Equal",            "Math|Compare", "({0} == {1})", { data_pin("a", "any"), data_pin("b", "any") }, "boolean", "a == b, usertypes compare through their own equality."));
            add(make_expression("compare.not_equal",     "Not Equal",        "Math|Compare", "({0} ~= {1})", { data_pin("a", "any"), data_pin("b", "any") }, "boolean", "a ~= b"));
            add(make_expression("logic.and", "And", "Math|Logic", "({0} and {1})", { data_pin("a", "boolean"), data_pin("b", "boolean") }, "boolean", "True when both are."));
            add(make_expression("logic.or",  "Or",  "Math|Logic", "({0} or {1})",  { data_pin("a", "boolean"), data_pin("b", "boolean") }, "boolean", "True when either is."));
            add(make_expression("logic.not", "Not", "Math|Logic", "(not {0})",     { data_pin("a", "boolean") }, "boolean", "The opposite."));

            // strings
            add(make_expression("string.concat",   "Append",    "String", "(tostring({0}) .. tostring({1}))", { data_pin("a", "any"), data_pin("b", "any") }, "string", "a followed by b, as text."));
            add(make_expression("string.format",   "Format",    "String", "string.format({0}, {1})", { data_pin("format", "string", "%.2f"), data_pin("value", "any") }, "string", "string.format with one value, %d %.2f %s..."));
            add(make_expression("string.length",   "Length",    "String", "(#{0})", { data_pin("text", "string") }, "integer", "The number of bytes in the text."));
            add(make_expression("string.upper",    "Upper",     "String", "string.upper({0})", { data_pin("text", "string") }, "string", "The text in capitals."));
            add(make_expression("string.lower",    "Lower",     "String", "string.lower({0})", { data_pin("text", "string") }, "string", "The text in lower case."));
            add(make_expression("string.contains", "Contains",  "String", "(string.find({0}, {1}, 1, true) ~= nil)", { data_pin("text", "string"), data_pin("part", "string") }, "boolean", "True when part appears in the text."));
            add(make_expression("string.sub",      "Substring", "String", "string.sub({0}, {1}, {2})", { data_pin("text", "string"), data_pin("first", "integer", "1"), data_pin("last", "integer", "-1") }, "string", "The characters from first to last, negatives count from the end."));

            // tables
            add(make_expression("table.length", "Length", "Table", "(#{0})", { data_pin("table", "table") }, "integer", "The number of items in an array table."));
            add(make_expression("table.get",    "Get",    "Table", "{0}[{1}]", { data_pin("table", "table"), data_pin("key", "any", "1") }, "any", "table[key]", "value"));
            add(make_statement("table.set",    "Set",    "Table", "{0}[{1}] = {2}", { data_pin("table", "table"), data_pin("key", "any", "1"), data_pin("value", "any") }, "table[key] = value"));
            add(make_statement("table.insert", "Insert", "Table", "table.insert({0}, {1})", { data_pin("table", "table"), data_pin("value", "any") }, "Appends the value to an array table."));
            add(make_statement("table.clear",  "Clear",  "Table", "for key in pairs({0}) do {0}[key] = nil end", { data_pin("table", "table") }, "Removes every item."));

            // usertype helpers
            const ApiEnum* component_types = find_enum("ComponentType");
            for (const ApiOwner& owner : get_owners())
            {
                if (!owner.is_usertype)
                {
                    continue;
                }

                {
                    NodeDefinition definition = make("cast:" + owner.name, "As " + owner.name, "Casts", NodeKind::Cast, "Treats the value as a " + owner.name + ", for values lua hands over untyped.");
                    definition.pure           = true;
                    definition.payload        = owner.name;
                    definition.inputs         = { data_pin("value", "any") };
                    definition.outputs        = { data_pin(owner.name, owner.name) };
                    definition.outputs[0].nullable = true;
                    add(move(definition));
                }

                const bool is_component = component_types && any_of(component_types->values.begin(), component_types->values.end(), [&](const auto& value) { return value.first == owner.name; });
                if (is_component)
                {
                    NodeDefinition definition = make("component:" + owner.name, "Get " + owner.name, "Components", NodeKind::GetComponent, "The entity's " + owner.name + " component, nil when it has none.");
                    definition.pure           = true;
                    definition.payload        = owner.name;
                    definition.has_target     = true;
                    definition.inputs         = { data_pin("target", "Entity") };
                    definition.outputs        = { data_pin(pretty_name(owner.name), owner.name) };
                    definition.outputs[0].name[0] = static_cast<char>(tolower(static_cast<unsigned char>(definition.outputs[0].name[0])));
                    definition.outputs[0].nullable = true;
                    add(move(definition));
                }

                vector<PinDefinition> fields;
                for (const ApiFunction& function : get_functions())
                {
                    if (function.owner == owner.name && function.kind == ApiKind::PropertyGet && !function.returns.empty())
                    {
                        fields.push_back(data_pin(function.name, function.returns[0].type));
                    }
                }
                if (!fields.empty())
                {
                    NodeDefinition definition = make("break:" + owner.name, "Break " + owner.name, owner.name, NodeKind::Break, "Splits a " + owner.name + " into its fields.");
                    definition.pure           = true;
                    definition.payload        = owner.name;
                    definition.inputs         = { data_pin("value", owner.name) };
                    definition.outputs        = fields;
                    add(move(definition));
                }
            }

            // the engine api
            for (const ApiFunction& function : get_functions())
            {
                add(make_api_node(function));
            }

            built_revision = get_revision();
        }

        void ensure_built()
        {
            if (built_revision != get_revision())
            {
                build();
            }
        }
    }

    const vector<NodeDefinition>& get_definitions()
    {
        ensure_built();
        return definitions;
    }

    const NodeDefinition* find_definition(const string& id)
    {
        ensure_built();
        auto it = definition_lookup.find(id);
        return it != definition_lookup.end() ? &definitions[it->second] : nullptr;
    }

    bool resolve_definition(const string& type, const Graph& graph, NodeDefinition& out)
    {
        if (const NodeDefinition* definition = find_definition(type))
        {
            out = *definition;
            return true;
        }

        auto suffix = [&](const char* prefix) -> const char*
        {
            const size_t length = strlen(prefix);
            return type.compare(0, length, prefix) == 0 ? type.c_str() + length : nullptr;
        };

        if (const char* name = suffix("var.get:"))
        {
            const Variable* variable = graph.find_variable(name);
            const string value_type  = variable ? variable->type : "any";
            out                      = make(type, string("Get ") + name, "Variables", NodeKind::VariableGet, string("Reads the variable ") + name + ".");
            out.pure                 = true;
            out.payload              = name;
            out.outputs              = { data_pin(name, value_type) };
            return true;
        }

        if (const char* name = suffix("var.set:"))
        {
            const Variable* variable = graph.find_variable(name);
            const string value_type  = variable ? variable->type : "any";
            out                      = make(type, string("Set ") + name, "Variables", NodeKind::VariableSet, string("Writes the variable ") + name + ".");
            out.payload              = name;
            out.inputs               = { exec_pin("exec"), data_pin(name, value_type) };
            out.outputs              = { exec_pin("then"), data_pin(name, value_type) };
            return true;
        }

        if (const char* name = suffix("call.custom:"))
        {
            out         = make(type, string("Call ") + name, "Events", NodeKind::CallCustomEvent, string("Runs the custom event ") + name + ".");
            out.payload = name;
            out.inputs  = { exec_pin("exec") };
            out.outputs = { exec_pin("then") };
            return true;
        }

        return false;
    }

    vector<NodeDefinition> get_graph_definitions(const Graph& graph)
    {
        vector<NodeDefinition> result;
        NodeDefinition definition;
        for (const Variable& variable : graph.variables)
        {
            if (resolve_definition("var.get:" + variable.name, graph, definition))
            {
                result.push_back(definition);
            }
            if (resolve_definition("var.set:" + variable.name, graph, definition))
            {
                result.push_back(definition);
            }
        }

        set<string> events;
        for (const Node& node : graph.nodes)
        {
            auto name = node.data.find("name");
            if (node.type == "event.custom" && name != node.data.end() && !name->second.empty() && events.insert(name->second).second)
            {
                if (resolve_definition("call.custom:" + name->second, graph, definition))
                {
                    result.push_back(definition);
                }
            }
        }
        return result;
    }

    const PinDefinition* find_pin(const NodeDefinition& definition, const string& name, bool output)
    {
        const vector<PinDefinition>& pins = output ? definition.outputs : definition.inputs;
        for (const PinDefinition& pin : pins)
        {
            if (pin.name == name)
            {
                return &pin;
            }
        }
        return nullptr;
    }

    bool types_compatible(const string& from, const string& to)
    {
        if (from == to)
        {
            return true;
        }
        if (from == type_exec || to == type_exec)
        {
            return false;
        }
        if (from == "any" || to == "any")
        {
            return true;
        }

        auto is_numeric = [](const string& type)
        {
            return type == "number" || type == "integer";
        };
        if (is_numeric(from) && is_numeric(to))
        {
            return true;
        }

        // lua enums are integers
        if ((is_enum(from) && is_numeric(to)) || (is_numeric(from) && is_enum(to)))
        {
            return true;
        }

        return derives_from(from, to);
    }

    vector<string> get_variable_types()
    {
        vector<string> types = { "boolean", "number", "integer", "string", "Vector2", "Vector3", "Vector4", "Quaternion", "Entity", "table", "any" };
        for (const ApiOwner& owner : get_owners())
        {
            if (owner.is_usertype && find(types.begin(), types.end(), owner.name) == types.end())
            {
                types.push_back(owner.name);
            }
        }
        for (const ApiEnum& entry : get_enums())
        {
            types.push_back(entry.name);
        }
        return types;
    }

    string default_value_for(const string& type)
    {
        if (type == "boolean")
        {
            return "false";
        }
        if (type == "number" || type == "integer")
        {
            return "0";
        }
        if (type == "Vector2")
        {
            return "0, 0";
        }
        if (type == "Vector3" || type == "Quaternion")
        {
            return "0, 0, 0";
        }
        if (type == "Vector4")
        {
            return "0, 0, 0, 0";
        }
        if (const ApiEnum* entry = find_enum(type))
        {
            return entry->values.empty() ? "" : entry->values.front().first;
        }
        return "";
    }

    string pretty_name(const string& name)
    {
        string result;
        for (size_t i = 0; i < name.size(); i++)
        {
            const char c = name[i];
            if (c == '_')
            {
                result += ' ';
                continue;
            }

            const bool upper      = isupper(static_cast<unsigned char>(c)) != 0;
            const bool prev_lower = i > 0 && (islower(static_cast<unsigned char>(name[i - 1])) != 0);
            const bool prev_upper = i > 0 && (isupper(static_cast<unsigned char>(name[i - 1])) != 0);
            const bool next_lower = i + 1 < name.size() && (islower(static_cast<unsigned char>(name[i + 1])) != 0);
            if (upper && !result.empty() && result.back() != ' ' && (prev_lower || (prev_upper && next_lower)))
            {
                result += ' ';
            }
            result += c;
        }
        return result;
    }
}
