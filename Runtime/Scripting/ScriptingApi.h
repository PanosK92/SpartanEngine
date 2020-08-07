/*
Copyright(c) 2016-2020 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

//= INCLUDES =============================
#include "../World/World.h"
#include "../Input/Input.h"
#include "../World/Components/Transform.h"
//========================================

namespace Spartan::ScriptingApi
{
    // Dependencies
    static Input* g_input       = nullptr;
    static World* g_world       = nullptr;

    // Type wrappers
    struct _vector2 { float x, y; };
    struct _vector3 { float x, y, z; };
    
    // Callbacks - Log
    static void Debug_LogFloat(float delta_time, LogType type)            { Log::Write(std::to_string(delta_time), type); }
    static void Debug_LogString(MonoString* mono_string, LogType type)    { Log::Write(std::string(mono_string_to_utf8(mono_string)), type); }

    // Callbacks - Transform
    static _vector3 Transform_GetPosition(void* handle)
    {
        Transform* transform = static_cast<Transform*>(handle);
        return _vector3{ transform->GetPosition().x, transform->GetPosition().y, transform->GetPosition().z };
    }
    static void Transform_SetPosition(void* handle, _vector3 v) { static_cast<Transform*>(handle)->SetPosition(Math::Vector3(v.x, v.y, v.z)); }

    // Callbacks - Input
    static bool Input_GetKey(const KeyCode key)       { return g_input->GetKey(key); }
    static bool Input_GetKeyDown(const KeyCode key)   { return g_input->GetKeyDown(key); }
    static bool Input_GetKeyUp(const KeyCode key)     { return g_input->GetKeyUp(key); }
    static _vector2 Input_GetMousePosition()          { return _vector2{ g_input->GetMousePosition().x, g_input->GetMousePosition().y }; }
    static _vector2 Input_GetMouseDelta()             { return _vector2{ g_input->GetMouseDelta().x, g_input->GetMouseDelta().y }; }
    static float Input_GetMouseWheelDelta()           { return g_input->GetMouseWheelDelta(); }

    // Callbacks - World
    static bool World_Save(const std::string& file_path) { return g_world->SaveToFile(file_path); }
    static bool World_Load(const std::string& file_path) { return g_world->LoadFromFile(file_path); }
    
    static void RegisterCallbacks(Context* context)
    {
        // Dependencies
        g_input = context->GetSubsystem<Input>();
        g_world = context->GetSubsystem<World>();
 
        // Debug
        mono_add_internal_call("Spartan.Debug::Log(single,Spartan.DebugType)", Debug_LogFloat);
        mono_add_internal_call("Spartan.Debug::Log(string,Spartan.DebugType)", Debug_LogString);

        // Transform
        mono_add_internal_call("Spartan.Transform::_internal_GetPosition()", Transform_GetPosition);
        mono_add_internal_call("Spartan.Transform::_internal_SetPosition()", Transform_SetPosition);

        // Input         
        mono_add_internal_call("Spartan.Input::GetKey(Spartan.KeyCode)",        Input_GetKey);
        mono_add_internal_call("Spartan.Input::GetKeyDown(Spartan.KeyCode)",    Input_GetKeyDown);
        mono_add_internal_call("Spartan.Input::GetKeyUp(Spartan.KeyCode)",      Input_GetKeyUp);
        mono_add_internal_call("Spartan.Input::GetMousePosition()",             Input_GetMousePosition);
        mono_add_internal_call("Spartan.Input::GetMouseDelta()",                Input_GetMouseDelta);
        mono_add_internal_call("Spartan.Input::GetMouseWheelDelta()",           Input_GetMouseWheelDelta);
    
        // World         
        mono_add_internal_call("Spartan.World::Save(single)", World_Save);
        mono_add_internal_call("Spartan.World::Load(string)", World_Load);
    }
}
