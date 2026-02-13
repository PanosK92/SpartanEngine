#include "pch.h"
#include "Script.h"

#include <tracy/Tracy.hpp>

#include "IO/pugixml.hpp"
#include "World/Entity.h"
#include "World/World.h"

using namespace spartan;

Script::Script(Entity* Entity)
    :Component(Entity)
{
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
    script["Entity"] = GetEntity();
}

void Script::Initialize()
{
    if (script.valid())
    {
        sol::protected_function TickFunction = script["Initialize"];
        if (TickFunction.valid())
        {
            (void)TickFunction(script);
        }
    }
}

void Script::Start()
{
    if (script.valid())
    {
        sol::protected_function TickFunction = script["Stop"];
        if (TickFunction.valid())
        {
            (void)TickFunction(script);
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
            (void)TickFunction(script);
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
            (void)TickFunction(script);
        }
    }
}

void Script::PreTick()
{
    if (script.valid())
    {
        sol::protected_function TickFunction = script["PreTick"];
        if (TickFunction.valid())
        {
            (void)TickFunction(script);
        }
    }
}

void Script::Tick()
{
    ZoneScoped;

    if (script.valid())
    {
        sol::protected_function TickFunction = script["Tick"];
        if (TickFunction.valid())
        {
            sol::protected_function_result Result = TickFunction(script);
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
            sol::protected_function_result Result = TickFunction(script);
            if (!Result.valid())
            {
                sol::error Error = Result;
                SP_LOG_ERROR("[LUA SCRIPT ERROR] - %s", Error.what())
            }
        }
    }
}

void Script::Load(pugi::xml_node& node)
{
    file_path        = node.attribute("file_path").as_string("N/A");
    LoadScriptFile(file_path);

    if (script.valid())
    {
        sol::protected_function TickFunction = script["Load"];
        if (TickFunction.valid())
        {
            sol::protected_function_result Result = TickFunction(script);
            if (!Result.valid())
            {
                sol::error Error = Result;
                SP_LOG_ERROR("[LUA SCRIPT ERROR] - %s", Error.what())
            }
        }
    }
}

void Script::OnTriggerEntered(Entity* other)
{
    if (script.valid())
    {
        sol::protected_function TickFunction = script["OnTriggerEntered"];
        if (TickFunction.valid())
        {
            sol::protected_function_result Result = TickFunction(script, other);
            if (!Result.valid())
            {
                sol::error Error = Result;
                SP_LOG_ERROR("[LUA SCRIPT ERROR] - %s", Error.what())
            }
        }
    }
}

void Script::OnTriggerExited(Entity* other)
{
    if (script.valid())
    {
        sol::protected_function TickFunction = script["OnTriggerExited"];
        if (TickFunction.valid())
        {
            sol::protected_function_result Result = TickFunction(script, other);
            if (!Result.valid())
            {
                sol::error Error = Result;
                SP_LOG_ERROR("[LUA SCRIPT ERROR] - %s", Error.what())
            }
        }
    }
}

void Script::OnContact(Entity* other, const math::Vector3& contactPoint, const math::Vector3& contactNormal, float impulse)
{
    if (script.valid())
    {
        sol::protected_function TickFunction = script["OnContact"];
        if (TickFunction.valid())
        {
            sol::protected_function_result Result = TickFunction(script, other, contactPoint, contactNormal, impulse);
            if (!Result.valid())
            {
                sol::error Error = Result;
                SP_LOG_ERROR("[LUA SCRIPT ERROR] - %s", Error.what())
            }
        }
    }
}

void Script::OnContactEnd(Entity* other)
{
    if (script.valid())
    {
        sol::protected_function TickFunction = script["OnContactEnd"];
        if (TickFunction.valid())
        {
            sol::protected_function_result Result = TickFunction(script, other);
            if (!Result.valid())
            {
                sol::error Error = Result;
                SP_LOG_ERROR("[LUA SCRIPT ERROR] - %s", Error.what())
            }
        }
    }
}
