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

//= INCLUDES ==========================
#include "Spartan.h"
#include "Scripting.h"
#include "ScriptingHelper.h"
#include "ScriptingApi.h"
#include "../Resource/ResourceCache.h"
#include "../World/Components/Script.h"
//=====================================

//= LIBRARIES =====================
#pragma comment(lib, "version.lib")
#pragma comment(lib, "Bcrypt.lib")
#pragma comment(lib, "Winmm.lib")
//=================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    Scripting::Scripting(Context* context) : ISubsystem(context)
    {
        // Subscribe to events
        SUBSCRIBE_TO_EVENT(EventType::WorldUnload, EVENT_HANDLER(Clear));
    }

    Scripting::~Scripting()
    {
        mono_jit_cleanup(m_domain);
    }

    bool Scripting::Initialize()
    {
        ScriptingHelper::resource_cache = m_context->GetSubsystem<ResourceCache>();

        // Get file paths
        const string dir_scripts    = ScriptingHelper::resource_cache->GetDataDirectory(Asset_Scripts) + "\\";
        const string dir_mono_lib   = dir_scripts + string("mono\\lib");
        const string dir_mono_etc   = dir_scripts + string("mono\\etc");

        // Point mono to the libs and configuration files
        mono_set_dirs(dir_mono_lib.c_str(), dir_mono_etc.c_str());

        // Initialise a domain
        m_domain = mono_jit_init_version("Spartan", "v4.0.30319");
        if (!m_domain)
        {
            LOG_ERROR("mono_jit_init failed");
            return false;
        }

        if (!mono_domain_set(m_domain, false))
        {
            LOG_ERROR("mono_domain_set failed");
            return false;
        }

        // soft debugger needs this
        mono_thread_set_main(mono_thread_current());

        // Get version
        m_context->GetSubsystem<Settings>()->RegisterThirdPartyLib("Mono", "6.10.0.104", "https://www.mono-project.com/");

        return true;
    }

    uint32_t Scripting::Load(const std::string& file_path, Script* script_component)
    {
        if (!m_api_assembly_compiled)
        {
            m_api_assembly_compiled = CompileApiAssembly();
            if (!m_api_assembly_compiled)
                return SCRIPT_NOT_LOADED;
        }

        ScriptInstance script;
        const string class_name = FileSystem::GetFileNameNoExtensionFromFilePath(file_path);

        script.assembly = ScriptingHelper::compile_and_load_assembly(m_domain, file_path);
        if (!script.assembly)
        {
            LOG_ERROR("Failed to load assembly");
            return SCRIPT_NOT_LOADED;
        }

        // Get image from script assembly
        script.image = mono_assembly_get_image(script.assembly);
        if (!script.image)
        {
            LOG_ERROR("Failed to get image");
            return SCRIPT_NOT_LOADED;
        }

        // Get the class
        script.klass = mono_class_from_name(script.image, "", class_name.c_str());
        if (!script.klass)
        {
            mono_image_close(script.image);
            LOG_ERROR("Failed to get class");
            return SCRIPT_NOT_LOADED;
        }
        
        // Create class instance
        script.object = mono_object_new(m_domain, script.klass);
        if (!script.object)
        {
            mono_image_close(script.image);
            LOG_ERROR("Failed to create class instance");
            return SCRIPT_NOT_LOADED;
        }

        // Get methods
        script.method_start     = ScriptingHelper::get_method(script.image, class_name + ":Start()");
        script.method_update    = ScriptingHelper::get_method(script.image, class_name + ":Update(single)");

        // Set entity handle
        if (!script.SetValue(script_component->GetEntity(), "_internal_entity_handle"))
        {
            mono_image_close(script.image);
            LOG_ERROR("Failed to set entity handle");
            return SCRIPT_NOT_LOADED;
        }

        // Set transform handle
        if (!script.SetValue(script_component->GetTransform(), "_internal_transform_handle"))
        {
            mono_image_close(script.image);
            LOG_ERROR("Failed to set transform handle");
            return SCRIPT_NOT_LOADED;
        }

        // Call the default constructor
        mono_runtime_object_init(script.object);
        if (!script.object)
        {
            mono_image_close(script.image);
            LOG_ERROR("Failed to run class constructor");
            return SCRIPT_NOT_LOADED;
        }

        // Add script
        m_scripts[++m_script_id] = script;

        // Return script id
        return m_script_id;
    }

    ScriptInstance* Scripting::GetScript(const uint32_t id)
    {
        if (m_scripts.find(id) == m_scripts.end())
            nullptr;

        return &m_scripts[id];
    }

    bool Scripting::CallScriptFunction_Start(const ScriptInstance* script_instance)
    {
        if (!script_instance->method_start || !script_instance->object)
        {
            LOG_ERROR("Invalid script instance");
            return false;
        }

        mono_runtime_invoke(script_instance->method_start, script_instance->object, nullptr, nullptr);
        return true;
    }

    bool Scripting::CallScriptFunction_Update(const ScriptInstance* script_instance, float delta_time)
    {
        if (!script_instance->method_update || !script_instance->object)
        {
            LOG_ERROR("Invalid script instance");
            return false;
        }

        // Set method argument
        void* args[1];
        args[0] = &delta_time;

        mono_runtime_invoke(script_instance->method_update, script_instance->object, args, nullptr);
        return true;
    }

    void Scripting::Clear()
    {
        m_scripts.clear();
        m_script_id = SCRIPT_NOT_LOADED;
    }

    bool Scripting::CompileApiAssembly()
    {
        // Get callbacks assembly
        const string api_cs           = ScriptingHelper::resource_cache->GetDataDirectory(Asset_Scripts) + "/" + "Spartan.cs";
        MonoAssembly* api_assembly    = ScriptingHelper::compile_and_load_assembly(m_domain, api_cs, false);
        if (!api_assembly)
        {
            LOG_ERROR("Failed to get api assembly");
            return false;
        }

        // Get image from script assembly
        MonoImage* callbacks_image = mono_assembly_get_image(api_assembly);
        if (!callbacks_image)
        {
            LOG_ERROR("Failed to get callbacks image");
            return false;
        }

        // Register static callbacks
        ScriptingApi::RegisterCallbacks(m_context);

        return true;
    }
}
