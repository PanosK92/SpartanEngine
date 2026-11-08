/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "../../profiling/Profiler.h"
#include "Script.h"
#include "io/pugixml.hpp"
#include "world/Entity.h"
#include "world/World.h"
#include "Light.h"
#include "../../scripting/Scripting.h"

using namespace spartan;

namespace
{
    void invoke_lifecycle(sol::table& script, const char* callback, Entity* entity, const std::string& path)
    {
        if (!script.valid()) return;
        sol::protected_function function = script[callback];
        if (!function.valid()) return; // optional lifecycle callback
        sol::protected_function_result result = function(script, entity);
        if (!result.valid())
        {
            sol::error error = result;
            SP_LOG_ERROR("Lua %s failed (%s, entity %llu): %s", callback, path.c_str(),
                static_cast<unsigned long long>(entity->GetObjectId()), error.what());
        }
    }

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

        // sol reports every lua number as an int, so a fraction like 0.25 has to be written from the double
        if (value.get_type() == sol::type::number)
        {
            char text[32];
            snprintf(text, sizeof(text), "%.15g", value.as<double>());
            attr.set_value(text);
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

    // deferred inits outlive their component when a builder or a clear removes it first,
    // the serial guards against a new script reusing a freed address
    std::mutex live_scripts_mutex;
    std::unordered_map<const Script*, uint64_t> live_scripts;
    std::atomic<uint64_t> next_serial = 1;

    bool is_script_alive(const Script* script, uint64_t serial)
    {
        std::lock_guard lock(live_scripts_mutex);
        auto it = live_scripts.find(script);
        return it != live_scripts.end() && it->second == serial;
    }
}

Script::Script(Entity* Entity)
    :Component(Entity)
{
    m_serial = next_serial.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard lock(live_scripts_mutex);
        live_scripts[this] = m_serial;
    }

    RegisterAttribute("file_path", "std::string",
        [this]()
        {
            return file_path;
        },
        [this](const std::any& value)
        {
            LoadScriptFile(std::any_cast<std::string>(value));
        }, true);
}

Script::~Script()
{
    std::lock_guard lock(live_scripts_mutex);
    live_scripts.erase(this);
}

void Script::LoadScriptFile(std::string_view path)
{
    if (!FileSystem::Exists(std::string(path)))
    {
        SP_LOG_ERROR("Script file does not exist: %s", std::string(path).c_str());
        return;
    }

    sol::protected_function_result Result = scripting::get_state().safe_script_file(std::string(path.data(), path.size()));
    if (!Result.valid())
    {
        sol::error error = Result;
        SP_LOG_ERROR("Failed to load script %s: %s", std::string(path).c_str(), error.what());
        return;
    }

    sol::object ReturnValue = Result;
    if (!ReturnValue.is<sol::table>())
    {
        SP_LOG_ERROR("Script must return a table: %s", std::string(path).c_str());
        return;
    }

    file_path.assign(path.data(), path.size());

    script = ReturnValue;

}

void Script::Initialize()
{
    invoke_lifecycle(script, "Initialize", GetEntity(), file_path);
}

void Script::Start()
{
    invoke_lifecycle(script, "Start", GetEntity(), file_path);
}

void Script::Stop()
{
    invoke_lifecycle(script, "Stop", GetEntity(), file_path);
}

void Script::Remove()
{
    invoke_lifecycle(script, "Remove", GetEntity(), file_path);
}

void Script::PreTick()
{
    if (!is_simulation_active()) return;
    invoke_lifecycle(script, "PreTick", GetEntity(), file_path);
}

void Script::Tick()
{
    SP_PROFILE_CPU();
    if (!is_simulation_active()) return;
    invoke_lifecycle(script, "Tick", GetEntity(), file_path);
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
    // script execution is queued and run on the main thread once entities are published
    if (World::IsDeferringScriptInit())
    {
        // light configuring scripts run first so the scene is lit while heavier builder scripts populate it
        int order = GetEntity() && GetEntity()->GetComponent<Light>() ? 0 : 1;

        // own a copy, prefab files, clones and undo load from documents that die before the init runs
        std::shared_ptr<pugi::xml_document> document = std::make_shared<pugi::xml_document>();
        document->append_copy(node);
        World::AddDeferredScriptInit(order, [this, serial = m_serial, document]()
        {
            if (!is_script_alive(this, serial))
            {
                return;
            }

            pugi::xml_node node_copy = document->first_child();
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
        sol::table saved_data = scripting::get_state().create_table();
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

            if (value.get_type() == sol::type::number)
            {
                // whole numbers stay lua integers, anything with a fraction or exponent stays a double
                const std::string text = attr.as_string();
                if (text.find_first_of(".eEnN") != std::string::npos)
                {
                    script[key_name] = attr.as_double();
                    saved_data[key_name] = attr.as_double();
                }
                else
                {
                    script[key_name] = attr.as_llong();
                    saved_data[key_name] = attr.as_llong();
                }
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
