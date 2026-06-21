#include "pch.h"
#include "Script.h"
#include "IO/pugixml.hpp"
#include "World/Entity.h"
#include "World/World.h"
#include "Light.h"

using namespace spartan;

namespace
{
    bool is_simulation_active()
    {
        return Engine::IsFlagSet(EngineMode::Playing) && !Engine::IsFlagSet(EngineMode::Paused);
    }

    void save_lua_value(pugi::xml_node& node, const std::string& key_name, const sol::object& value)
    {
        if (key_name.empty() || key_name == "file_path")
        {
            return;
        }

        pugi::xml_attribute attr = node.attribute(key_name.c_str());
        if (!attr)
        {
            attr = node.append_attribute(key_name.c_str());
        }

        if (value.is<int>())
        {
            attr = value.as<int>();
        }
        else if (value.is<float>() || value.is<double>())
        {
            attr = value.as<float>();
        }
        else if (value.is<bool>())
        {
            attr = value.as<bool>();
        }
        else if (value.is<std::string>())
        {
            const std::string string_value = value.as<std::string>();
            attr = string_value.c_str();
        }
    }

    void save_lua_table(pugi::xml_node& node, const sol::table& table)
    {
        for (auto& [key, value] : table)
        {
            if (!key.is<std::string>())
            {
                continue;
            }

            save_lua_value(node, key.as<std::string>(), value);
        }
    }
}

Script::Script(Entity* Entity)
    :Component(Entity)
{
    RegisterAttribute("file_path", "std::string",
        [this]()
        {
            return file_path;
        },
        [this](const std::any& value)
        {
            LoadScriptFile(std::any_cast<std::string>(value));
        });
}

sol::reference Script::AsLua(sol::state_view state)
{
    return sol::make_reference(state, this);
}

void Script::LoadScriptFile(std::string_view path)
{
    if (!FileSystem::Exists(std::string(path.data(), path.length())))
    {
        return;
    }

    sol::protected_function_result Result = World::GetLuaState().safe_script_file(std::string(path.data(), path.size()));
    if (!Result.valid())
    {
        SP_LOG_ERROR("Failed to load script at path %s", path.data())
        return;
    }

    sol::object ReturnValue = Result;
    if (!ReturnValue.is<sol::table>())
    {
        SP_LOG_ERROR("Failed to load script at path %s", path.data())
        return;
    }

    file_path.assign(path.data(), path.size());

    script = ReturnValue;

}

void Script::Initialize()
{
    if (script.valid())
    {
        sol::protected_function TickFunction = script["Initialize"];
        if (TickFunction.valid())
        {
            (void)TickFunction(script, GetEntity());
        }
    }
}

void Script::Start()
{
    if (script.valid())
    {
        sol::protected_function TickFunction = script["Start"];
        if (TickFunction.valid())
        {
            (void)TickFunction(script, GetEntity());
        }
    }
}

void Script::Stop()
{
    if (script.valid())
    {
        sol::protected_function TickFunction = script["Stop"];
        if (TickFunction.valid())
        {
            (void)TickFunction(script, GetEntity());
        }
    }
}

void Script::Remove()
{
    if (script.valid())
    {
        sol::protected_function TickFunction = script["Remove"];
        if (TickFunction.valid())
        {
            (void)TickFunction(script, GetEntity());
        }
    }
}

void Script::PreTick()
{
    if (!is_simulation_active())
    {
        return;
    }

    if (script.valid())
    {
        sol::protected_function TickFunction = script["PreTick"];
        if (TickFunction.valid())
        {
            (void)TickFunction(script, GetEntity());
        }
    }
}

void Script::Tick()
{
    if (!is_simulation_active())
    {
        return;
    }

    if (script.valid())
    {
        sol::protected_function TickFunction = script["Tick"];
        if (TickFunction.valid())
        {
            sol::protected_function_result Result = TickFunction(script, GetEntity());
            if (!Result.valid())
            {
                sol::error Error = Result;
                SP_LOG_ERROR("[LUA SCRIPT ERROR] - %s", Error.what())
            }
        }
    }
}

void Script::Save(pugi::xml_node& node)
{
    node.append_attribute("file_path") = file_path.c_str();

    if (script.valid())
    {
        sol::protected_function TickFunction = script["Save"];
        if (TickFunction.valid())
        {
            sol::protected_function_result Result = TickFunction(script, GetEntity());
            if (!Result.valid())
            {
                sol::error Error = Result;
                SP_LOG_ERROR("[LUA SCRIPT ERROR] - %s", Error.what())
                save_lua_table(node, script);
            }
            else
            {
                save_lua_table(node, script);

                sol::object ReturnValue = Result;
                if (ReturnValue.is<sol::table>())
                {
                    save_lua_table(node, ReturnValue.as<sol::table>());
                }
            }
        }
        else
        {
            save_lua_table(node, script);
        }
    }
}

void Script::Load(pugi::xml_node& node)
{
    // during a bulk world load entities load across the thread pool, lua is single threaded so the actual
    // script execution is queued and run sequentially on the load thread once all entities exist
    if (World::IsDeferringScriptInit())
    {
        // light configuring scripts run first so the scene is lit while heavier builder scripts populate it
        int order = GetEntity() && GetEntity()->GetComponent<Light>() ? 0 : 1;

        pugi::xml_node node_copy = node; // lightweight handle, stays valid until the load task finishes
        World::AddDeferredScriptInit(order, [this, node_copy]() mutable
        {
            LoadInternal(node_copy);
        });
        return;
    }

    LoadInternal(node);
}

void Script::LoadInternal(pugi::xml_node& node)
{
    file_path        = node.attribute("file_path").as_string("N/A");
    LoadScriptFile(file_path);

    if (script.valid())
    {
        sol::table saved_data = World::GetLuaState().create_table();
        for (pugi::xml_attribute attr = node.first_attribute(); attr; attr = attr.next_attribute())
        {
            std::string attr_name = attr.name();
            if (attr_name != "file_path")
            {
                saved_data[attr_name] = attr.value();
            }
        }

        for (auto& [key, value] : script)
        {
            if (!key.is<std::string>())
            {
                continue;
            }

            std::string key_name = key.as<std::string>();
            auto attr = node.attribute(key_name.c_str());
            if (attr.empty())
            {
                continue;
            }

            if (value.is<int>())
            {
                script[key_name] = attr.as_int();
                saved_data[key_name] = attr.as_int();
            }
            else if (value.is<float>() || value.is<double>())
            {
                script[key_name] = attr.as_float();
                saved_data[key_name] = attr.as_float();
            }
            else if (value.is<bool>())
            {
                script[key_name] = attr.as_bool();
                saved_data[key_name] = attr.as_bool();
            }
            else if (value.is<std::string>())
            {
                script[key_name] = attr.as_string();
                saved_data[key_name] = attr.as_string();
            }
        }

        // run the load-time builder hook now that the script file and its properties are in place
        sol::protected_function InitializeFunction = script["Initialize"];
        if (InitializeFunction.valid())
        {
            sol::protected_function_result Result = InitializeFunction(script, GetEntity());
            if (!Result.valid())
            {
                sol::error Error = Result;
                SP_LOG_ERROR("[LUA SCRIPT ERROR] - %s", Error.what())
            }
        }

        sol::protected_function TickFunction = script["Load"];
        if (TickFunction.valid())
        {
            sol::protected_function_result Result = TickFunction(script, GetEntity(), saved_data);
            if (!Result.valid())
            {
                sol::error Error = Result;
                SP_LOG_ERROR("[LUA SCRIPT ERROR] - %s", Error.what())
            }
        }
    }
}
