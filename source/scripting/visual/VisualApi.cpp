/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==========
#include "pch.h"
#include <sol/sol.hpp>
#include "VisualApi.h"
#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>
//=====================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan::scripting::visual
{
    namespace
    {
        struct PendingOwner
        {
            string name;
            bool is_usertype = false;
            type_index cpp   = typeid(void);
            vector<type_index> bases;
        };

        struct PendingEnum
        {
            string name;
            type_index cpp = typeid(void);
            vector<pair<string, int64_t>> values;
        };

        vector<PendingOwner> pending_owners;
        vector<PendingEnum> pending_enums;
        vector<detail::FunctionCapture> pending_functions;

        vector<ApiFunction> functions;
        vector<ApiEnum> enums;
        vector<ApiOwner> owners;
        unordered_map<string, size_t> function_lookup;
        unordered_map<string, size_t> enum_lookup;
        unordered_map<string, size_t> owner_lookup;
        ApiCoverage coverage;
        uint64_t revision = 0;

        // names the c++ signature cannot carry, "parameters -> returns", either side may be empty
        const map<string, const char*> parameter_hints =
        {
            { "print",                         "values" },
            { "Hud.GetSize",                   "-> width, height" },
            { "Hud.GetMousePosition",          "-> x, y" },
            { "Hud.LoadFont",                  "name, path -> loaded" },
            { "Hud.Rect",                      "x, y, width, height, color, rounding" },
            { "Hud.RectOutline",               "x, y, width, height, color, thickness, rounding" },
            { "Hud.RectGradient",              "x, y, width, height, top left, top right, bottom right, bottom left" },
            { "Hud.Line",                      "x1, y1, x2, y2, color, thickness" },
            { "Hud.Circle",                    "x, y, radius, color, thickness" },
            { "Hud.Triangle",                  "x1, y1, x2, y2, x3, y3, color" },
            { "Hud.TextWidth",                 "text, size, font, spacing -> width" },
            { "Hud.Text",                      "text, x, y, size, color, font, align, spacing -> width" },
            { "World.Raycast",                 "origin, direction, max distance -> hit" },
            { "World.GetEntityByName",         "name -> entity" },
            { "World.GetEntityById",           "id -> entity" },
            { "World.SetTimeOfDay",            "time of day" },
            { "World.SetActiveCamera",         "camera entity" },
            { "Console.Set",                   "name, value -> applied" },
            { "Console.Get",                   "name -> value" },
            { "Sequencer.Play",                "name, car -> started" },
            { "Material.SetColor",             "r, g, b, a" },
            { "Material.SetTexture",           "type, path, slot" },
            { "Material.SetProperty",          "property, value" },
            { "Material.GetProperty",          "property -> value" },
            { "Entity.GetComponent",           "type -> component" },
            { "Entity.AddComponent",           "type -> component" },
            { "Entity.RemoveComponent",        "type" },
            { "Entity.GetChildByName",         "name -> child" },
            { "Entity.GetChildByIndex",        "index -> child" },
            { "Entity.GetDescendantByName",    "name -> descendant" },
            { "Entity.SetName",                "name" },
            { "Entity.SetActive",              "active" },
            { "Entity.SetParent",              "parent" },
            { "Entity.Translate",              "delta" },
            { "Entity.Rotate",                 "delta" },
            { "Entity.SetPosition",            "position" },
            { "Entity.SetPositionLocal",       "position" },
            { "Entity.SetRotation",            "rotation" },
            { "Entity.SetRotationLocal",       "rotation" },
            { "Entity.SetScale",               "scale" },
            { "Entity.SetScaleLocal",          "scale" },
            { "Quaternion.FromEulerAngles",    "pitch, yaw, roll -> rotation" },
            { "Quaternion.FromLookRotation",   "direction, up -> rotation" },
            { "Quaternion.Lerp",               "to, t -> rotation" },
            { "Light.SetColor",                "color" },
            { "Light.SetRange",                "range" },
            { "Light.SetAngle",                "angle" },
            { "Light.SetTemperature",          "kelvin" },
            { "Light.SetFlag",                 "flag, enabled" },
            { "Render.SetFlag",                "flag, enabled" },
            { "Render.AddDecal",               "position, rotation, half size, color, roughness, relief, seed, source" },
            { "ParticleSystem.SetStartColor",  "r, g, b, a" },
            { "ParticleSystem.SetEndColor",    "r, g, b, a" },
            { "ParticleSystem.SetEmissionDirection", "x, y, z" },
            { "ParticleSystem.SetVortexCenter", "x, y, z" },
            { "ParticleSystem.SetVortexAxis",  "x, y, z" },
            { "Physics.SetBodyTransform",      "position, rotation" },
            { "Physics.SetVehicleThrottle",    "throttle" },
            { "Physics.SetVehicleBrake",       "brake" },
            { "Physics.SetVehicleSteering",    "steering" },
            { "Physics.SetVehicleHandbrake",   "handbrake" },
            { "CarTuning.SetStage",            "car, part, stage" },
            { "CarTuning.GetStage",            "car, part -> stage" },
            { "CarTuning.SetSetting",          "car, setting, value" },
            { "CarTuning.GetSetting",          "car, setting -> value" },
            { "CarTuning.PlaceAt",             "car, ground position, rotation" },
            { "CarTuning.SetHeld",             "car, held" },
            { "CarTuning.SetNitrous",          "car, active" },
            { "CarTuning.Service",             "car, service" },
            { "CarTuning.PreviewStats",        "car, part, stage -> stats" },
            { "WorldSystems.SetLayeredRoads",  "parent name, material directory, material prefix" },
            { "ResourceCache.LoadMesh",        "path, flags -> mesh" },
        };

        // functions lua ships with, the scan leaves them to the node library's own lua nodes
        const set<string> lua_globals =
        {
            "_G", "_VERSION", "assert", "collectgarbage", "dofile", "error", "getmetatable", "ipairs", "load", "loadfile", "next", "pairs", "pcall",
            "print", "rawequal", "rawget", "rawlen", "rawset", "require", "select", "setmetatable", "tonumber", "tostring", "type", "xpcall", "warn",
            "coroutine", "string", "math", "table", "io", "os", "package", "utf8", "debug"
        };

        // sol2 puts these on usertypes for its own casting
        const set<string> sol_internal = { "class_check", "class_cast", "new", "__type" };

        vector<string> split(const string& text, char separator)
        {
            vector<string> parts;
            string part;
            for (char c : text)
            {
                if (c == separator)
                {
                    parts.push_back(part);
                    part.clear();
                }
                else
                {
                    part += c;
                }
            }
            parts.push_back(part);

            for (string& value : parts)
            {
                const size_t first = value.find_first_not_of(' ');
                const size_t last  = value.find_last_not_of(' ');
                value              = first == string::npos ? "" : value.substr(first, last - first + 1);
            }
            return parts;
        }

        // "class spartan::math::Vector3" becomes "Vector3"
        string short_cpp_name(const type_index& cpp)
        {
            string name = cpp.name();
            for (const char* prefix : { "class ", "struct ", "enum ", "union " })
            {
                if (name.rfind(prefix, 0) == 0)
                {
                    name = name.substr(strlen(prefix));
                }
            }
            const size_t template_start = name.find('<');
            const size_t scope          = name.rfind("::", template_start);
            if (scope != string::npos)
            {
                name = name.substr(scope + 2);
            }
            return name;
        }

        string default_name(const string& type)
        {
            if (type == "number" || type == "integer")
            {
                return "value";
            }
            if (type == "string")
            {
                return "text";
            }
            if (type == "boolean")
            {
                return "enabled";
            }
            string name = type;
            if (!name.empty())
            {
                name[0] = static_cast<char>(tolower(static_cast<unsigned char>(name[0])));
            }
            return name;
        }

        // unnamed values read by their type, a repeated name gets a number so pins stay unique
        void name_values(vector<ApiParameter>& values, const vector<string>& hints, const char* fallback_single)
        {
            const bool use_hints = !hints.empty() && hints.size() == values.size();
            map<string, int> seen;
            for (size_t i = 0; i < values.size(); i++)
            {
                ApiParameter& value = values[i];
                string name         = use_hints && !hints[i].empty() ? hints[i] : (fallback_single && values.size() == 1 ? string(fallback_single) : default_name(value.type));
                const int count     = ++seen[name];
                if (count > 1)
                {
                    name += " " + to_string(count);
                }
                value.name = name;
            }

            // the first of a repeated name becomes "value 1" too, so "value" and "value 2" do not read as different things
            for (auto& [name, count] : seen)
            {
                if (count < 2)
                {
                    continue;
                }
                for (ApiParameter& value : values)
                {
                    if (value.name == name)
                    {
                        value.name = name + " 1";
                        break;
                    }
                }
            }
        }

        ApiFunction make_unreflected(const string& owner, const string& name, bool is_method)
        {
            ApiFunction function;
            function.owner     = owner;
            function.name      = name;
            function.kind      = is_method ? ApiKind::Method : ApiKind::Function;
            function.reflected = false;

            ApiParameter arguments;
            arguments.name     = "arguments";
            arguments.type     = "any";
            arguments.variadic = true;
            arguments.optional = true;
            function.parameters.push_back(arguments);

            ApiParameter result;
            result.name = "result";
            result.type = "any";
            function.returns.push_back(result);
            return function;
        }

        void collect_lua_functions(const sol::table& table, set<string>& names)
        {
            for (auto& [key, value] : table)
            {
                if (key.get_type() == sol::type::string && value.get_type() == sol::type::function)
                {
                    names.insert(key.as<string>());
                }
            }
        }
    }

    string ApiFunction::id() const
    {
        string result = owner.empty() ? name : owner + "." + name;
        switch (kind)
        {
            case ApiKind::Constructor: result = owner + ".new"; break;
            case ApiKind::PropertyGet: result += ":get";        break;
            case ApiKind::PropertySet: result += ":set";        break;
            case ApiKind::Operator:    result = owner + ".op" + op; break;
            default: break;
        }

        // overloads are told apart by their signature, which survives reordering the bindings
        if (overload_count > 1)
        {
            result += "(";
            for (size_t i = 0; i < parameters.size(); i++)
            {
                result += (i > 0 ? "," : "") + parameters[i].type;
            }
            result += ")";
        }
        return result;
    }

    const vector<ApiFunction>& get_functions()
    {
        return functions;
    }

    const vector<ApiEnum>& get_enums()
    {
        return enums;
    }

    const vector<ApiOwner>& get_owners()
    {
        return owners;
    }

    const ApiFunction* find_function(const string& id)
    {
        auto it = function_lookup.find(id);
        return it != function_lookup.end() ? &functions[it->second] : nullptr;
    }

    const ApiEnum* find_enum(const string& name)
    {
        auto it = enum_lookup.find(name);
        return it != enum_lookup.end() ? &enums[it->second] : nullptr;
    }

    const ApiOwner* find_owner(const string& name)
    {
        auto it = owner_lookup.find(name);
        return it != owner_lookup.end() ? &owners[it->second] : nullptr;
    }

    bool is_usertype(const string& type)
    {
        const ApiOwner* owner = find_owner(type);
        return owner && owner->is_usertype;
    }

    bool is_enum(const string& type)
    {
        return find_enum(type) != nullptr;
    }

    bool derives_from(const string& type, const string& base)
    {
        if (type == base)
        {
            return true;
        }

        const ApiOwner* owner = find_owner(type);
        if (!owner)
        {
            return false;
        }

        for (const string& parent : owner->bases)
        {
            if (derives_from(parent, base))
            {
                return true;
            }
        }
        return false;
    }

    const ApiCoverage& get_coverage()
    {
        return coverage;
    }

    uint64_t get_revision()
    {
        return revision;
    }

    namespace detail
    {
        void reset()
        {
            pending_owners.clear();
            pending_enums.clear();
            pending_functions.clear();
            functions.clear();
            enums.clear();
            owners.clear();
            function_lookup.clear();
            enum_lookup.clear();
            owner_lookup.clear();
            coverage = ApiCoverage();
            revision++;
        }

        void add_owner(const string& name, bool is_usertype, type_index cpp)
        {
            for (PendingOwner& owner : pending_owners)
            {
                if (owner.name == name)
                {
                    owner.is_usertype = owner.is_usertype || is_usertype;
                    return;
                }
            }

            PendingOwner owner;
            owner.name        = name;
            owner.is_usertype = is_usertype;
            owner.cpp         = cpp;
            pending_owners.push_back(owner);
        }

        void add_base(const string& owner_name, type_index base)
        {
            for (PendingOwner& owner : pending_owners)
            {
                if (owner.name == owner_name)
                {
                    owner.bases.push_back(base);
                    return;
                }
            }
        }

        void add_function(FunctionCapture&& function)
        {
            pending_functions.push_back(move(function));
        }

        void add_enum(const string& name, type_index cpp, vector<pair<string, int64_t>>&& values)
        {
            PendingEnum entry;
            entry.name   = name;
            entry.cpp    = cpp;
            entry.values = move(values);
            pending_enums.push_back(move(entry));
        }

        void add_enum_value(const string& name, const string& key, int64_t value)
        {
            for (PendingEnum& entry : pending_enums)
            {
                if (entry.name == name)
                {
                    entry.values.emplace_back(key, value);
                    return;
                }
            }
        }

        void finalize(sol::state_view state)
        {
            unordered_map<type_index, string> usertype_names;
            unordered_map<type_index, string> enum_names;
            for (const PendingOwner& owner : pending_owners)
            {
                if (owner.is_usertype && owner.cpp != typeid(void))
                {
                    usertype_names[owner.cpp] = owner.name;
                }
            }
            for (const PendingEnum& entry : pending_enums)
            {
                if (entry.cpp != typeid(void))
                {
                    enum_names[entry.cpp] = entry.name;
                }
            }

            auto resolve = [&](const TypeCapture& capture) -> string
            {
                if (capture.primitive)
                {
                    return capture.primitive;
                }
                if (capture.is_enum)
                {
                    auto it = enum_names.find(capture.cpp);
                    return it != enum_names.end() ? it->second : "integer";
                }
                auto it = usertype_names.find(capture.cpp);
                return it != usertype_names.end() ? it->second : short_cpp_name(capture.cpp);
            };

            auto convert = [&](const ParameterCapture& capture)
            {
                ApiParameter parameter;
                parameter.type     = resolve(capture.type);
                parameter.optional = capture.optional;
                parameter.variadic = capture.variadic;
                parameter.nullable = capture.type.nullable;
                return parameter;
            };

            // owners
            owners.clear();
            owner_lookup.clear();
            for (const PendingOwner& pending : pending_owners)
            {
                ApiOwner owner;
                owner.name        = pending.name;
                owner.is_usertype = pending.is_usertype;
                for (const type_index& base : pending.bases)
                {
                    auto it = usertype_names.find(base);
                    owner.bases.push_back(it != usertype_names.end() ? it->second : short_cpp_name(base));
                }
                owner_lookup[owner.name] = owners.size();
                owners.push_back(move(owner));
            }

            // enums
            enums.clear();
            enum_lookup.clear();
            for (const PendingEnum& pending : pending_enums)
            {
                ApiEnum entry;
                entry.name   = pending.name;
                entry.values = pending.values;
                enum_lookup[entry.name] = enums.size();
                enums.push_back(move(entry));
            }

            // functions, a later registration of the same name replaces an earlier one like the lua assignment did
            functions.clear();
            map<string, vector<size_t>> by_name;
            for (const FunctionCapture& pending : pending_functions)
            {
                ApiFunction function;
                function.owner     = pending.owner;
                function.name      = pending.name;
                function.kind      = pending.kind;
                function.op        = pending.op;
                function.reflected = pending.reflected;
                for (const ParameterCapture& parameter : pending.parameters)
                {
                    function.parameters.push_back(convert(parameter));
                }
                for (const ParameterCapture& value : pending.returns)
                {
                    function.returns.push_back(convert(value));
                }

                const string key = function.owner + "." + function.name + "#" + to_string(static_cast<int>(function.kind)) + function.op;
                by_name[key].push_back(functions.size());
                functions.push_back(move(function));
            }

            for (auto& [key, indices] : by_name)
            {
                const uint32_t count = static_cast<uint32_t>(indices.size());
                for (uint32_t i = 0; i < count; i++)
                {
                    functions[indices[i]].overload       = i;
                    functions[indices[i]].overload_count = count;
                }
            }

            // names for pins
            for (ApiFunction& function : functions)
            {
                const string hint_key = function.owner.empty() ? function.name : function.owner + "." + function.name;
                vector<string> parameter_names;
                vector<string> return_names;
                auto hint = parameter_hints.find(hint_key);
                if (hint != parameter_hints.end())
                {
                    const string text   = hint->second;
                    const size_t arrow  = text.find("->");
                    const string inputs = arrow == string::npos ? text : text.substr(0, arrow);
                    const string output = arrow == string::npos ? "" : text.substr(arrow + 2);
                    if (inputs.find_first_not_of(' ') != string::npos)
                    {
                        parameter_names = split(inputs, ',');
                    }
                    if (output.find_first_not_of(' ') != string::npos)
                    {
                        return_names = split(output, ',');
                    }
                }

                if (function.kind == ApiKind::Operator)
                {
                    parameter_names = function.parameters.size() == 2 ? vector<string>{ "a", "b" } : vector<string>{ "value" };
                }
                if (function.kind == ApiKind::PropertySet)
                {
                    parameter_names = { function.name };
                }
                if (function.kind == ApiKind::PropertyGet || function.kind == ApiKind::Constant)
                {
                    return_names = { function.name };
                }

                name_values(function.parameters, parameter_names, nullptr);
                name_values(function.returns, return_names, "result");
            }

            // anything lua can call that was registered around the recorder still gets a node, untyped
            set<string> known;
            for (const ApiFunction& function : functions)
            {
                known.insert(function.owner + "." + function.name);
            }

            coverage = ApiCoverage();
            for (const ApiOwner& owner : owners)
            {
                sol::object object = state[owner.name];
                if (object.get_type() != sol::type::table)
                {
                    continue;
                }

                set<string> names;
                sol::table table = object.as<sol::table>();
                collect_lua_functions(table, names);
                if (sol::optional<sol::table> metatable = table[sol::metatable_key])
                {
                    if (sol::optional<sol::table> index = (*metatable)[sol::meta_function::index])
                    {
                        collect_lua_functions(*index, names);
                    }
                }

                for (const string& name : names)
                {
                    if (name.rfind("__", 0) == 0 || sol_internal.count(name) || known.count(owner.name + "." + name))
                    {
                        continue;
                    }
                    functions.push_back(make_unreflected(owner.name, name, owner.is_usertype));
                    coverage.unreflected.push_back(owner.name + "." + name);
                }
            }

            for (auto& [key, value] : state.globals())
            {
                if (key.get_type() != sol::type::string)
                {
                    continue;
                }

                const string name = key.as<string>();
                if (lua_globals.count(name) || owner_lookup.count(name) || enum_lookup.count(name))
                {
                    continue;
                }

                if (value.get_type() == sol::type::function && !known.count("." + name))
                {
                    functions.push_back(make_unreflected("", name, false));
                    coverage.unreflected.push_back(name);
                }
                else if (value.get_type() == sol::type::table)
                {
                    set<string> names;
                    collect_lua_functions(value.as<sol::table>(), names);
                    if (names.empty())
                    {
                        continue;
                    }

                    ApiOwner owner;
                    owner.name          = name;
                    owner_lookup[name]  = owners.size();
                    owners.push_back(owner);
                    for (const string& function_name : names)
                    {
                        functions.push_back(make_unreflected(name, function_name, false));
                        coverage.unreflected.push_back(name + "." + function_name);
                    }
                }
            }

            function_lookup.clear();
            for (size_t i = 0; i < functions.size(); i++)
            {
                function_lookup[functions[i].id()] = i;
            }

            for (const ApiFunction& function : functions)
            {
                function.reflected ? coverage.functions_reflected++ : coverage.functions_unreflected++;
            }
            for (const ApiOwner& owner : owners)
            {
                owner.is_usertype ? coverage.usertypes++ : coverage.tables++;
            }
            coverage.enums = static_cast<uint32_t>(enums.size());

            revision++;

            SP_LOG_INFO("Visual scripting: %u lua functions reflected across %u usertypes, %u tables and %u enums, %u without a signature",
                coverage.functions_reflected, coverage.usertypes, coverage.tables, coverage.enums, coverage.functions_unreflected);
            for (const string& name : coverage.unreflected)
            {
                SP_LOG_WARNING("Visual scripting: %s is exposed to lua without a recorded signature, register it through VisualApiReflection.h", name.c_str());
            }
        }
    }
}
