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

//= INCLUDES ==============
#include "../World/World.h"
#include "../Input/Input.h"
//=========================

namespace Spartan::ScriptingInterface
{
    // Dependencies
    static Input* g_input       = nullptr;
    static World* g_world       = nullptr;
    static MonoDomain* g_domain = nullptr;

    // Type wrappers
    struct _vector2 { float x, y; };

    // Log
    static void LogFloat(float delta_time, LogType type)            { Log::Write(std::to_string(delta_time), type); }
    static void LogString(MonoString* mono_string, LogType type)    { Log::Write(std::string(mono_string_to_utf8(mono_string)), type); }

    // Input
    static bool GetKey(const KeyCode key)       { return g_input->GetKey(key); }
    static bool GetKeyDown(const KeyCode key)   { return g_input->GetKeyDown(key); }
    static bool GetKeyUp(const KeyCode key)     { return g_input->GetKeyUp(key); }
    static _vector2 GetMousePosition()          { return _vector2{ g_input->GetMousePosition().x, g_input->GetMousePosition().y }; }
    static _vector2 GetMouseDelta()             { return _vector2{ g_input->GetMouseDelta().x, g_input->GetMouseDelta().y }; }
    static float GetMouseWheelDelta()           { return g_input->GetMouseWheelDelta(); }

    // World
    static bool WorldSave(const std::string& file_path) { return g_world->SaveToFile(file_path); }
    static bool WorldLoad(const std::string& file_path) { return g_world->LoadFromFile(file_path); }
    
    static void RegisterCallbacks(Context* context, MonoDomain* domain, MonoImage* callbacks_image, MonoAssembly* callbacks_assembly)
    {
        g_input     = context->GetSubsystem<Input>();
        g_world     = context->GetSubsystem<World>();
        g_domain    = domain;

        // Get Vector2 class
        //g_class_vector2 = mono_class_from_name(callbacks_image, "Spartan", "Vector2");
        //if (!g_class_vector2)
        //{
        //    LOG_ERROR("Failed to get Vector2 class");
        //    return;
        //}
    
        // Namespace.Class::Method(T1,...Tn)
    
        // Debug
        mono_add_internal_call("Spartan.Debug::Log(single,Spartan.DebugType)", LogFloat);
        mono_add_internal_call("Spartan.Debug::Log(string,Spartan.DebugType)", LogString);
    
        // Input         
        mono_add_internal_call("Spartan.Input::GetKey(Spartan.KeyCode)",        GetKey);
        mono_add_internal_call("Spartan.Input::GetKeyDown(Spartan.KeyCode)",    GetKeyDown);
        mono_add_internal_call("Spartan.Input::GetKeyUp(Spartan.KeyCode)",      GetKeyUp);
        mono_add_internal_call("Spartan.Input::GetMousePosition()",             GetMousePosition);
        mono_add_internal_call("Spartan.Input::GetMouseDelta()",                GetMouseDelta);
        mono_add_internal_call("Spartan.Input::GetMouseWheelDelta()",           GetMouseWheelDelta);
    
        // World         
        mono_add_internal_call("Spartan.World::Save(single)", WorldSave);
        mono_add_internal_call("Spartan.World::Load(string)", WorldLoad);
    }
}
