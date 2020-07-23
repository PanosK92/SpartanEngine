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

//= INCLUDES =========================
#include "Spartan.h"
#include "Scripting.h"
#include "../Resource/ResourceCache.h"
//====================================

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
    static ResourceCache* resource_cache = nullptr;

    static std::string execute_command(const char* cmd)
    {
        std::array<char, 1024> buffer;
        std::string result;
        std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(cmd, "r"), _pclose);
        if (!pipe)
        {
            LOG_ERROR("popen() failed");
            return result;
        }

        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr)
        {
            result += buffer.data();
        }

        return result;
    }

    static bool compile_script(const std::string& script, const std::string& dll_reference = "")
    {
        // Get paths
        const string dir_scripts    = resource_cache->GetDataDirectory(Asset_Scripts) + "\\";
        const string dir_compiler   = dir_scripts + "mono\\roslyn\\csc.exe";

        // Compile script
        string command = dir_compiler + " -target:library -nologo";
        if (!dll_reference.empty())
        {
            command += " -reference:" + dll_reference;
        }
        command += " -out:" + FileSystem::ReplaceExtension(script, ".dll") + " " + string(script);
        string result = execute_command(command.c_str());

        // Log compilation output
        std::istringstream f(result);
        std::string line;
        bool compilation_result = true;
        while (std::getline(f, line))
        {
            if (FileSystem::IsEmptyOrWhitespace(line))
                continue;

            const auto is_error = line.find("error") != string::npos;
            if (is_error)
            {
                LOG_ERROR(line);
                compilation_result = false;
            }
            else
            {
                LOG_INFO(line);
            }
        }

        if (compilation_result)
        {
            LOG_INFO("Successfully compiled C# script \"%s\"", script.c_str());
            return true;
        }

        return false;
    }

    static MonoAssembly* load_assembly(MonoDomain* domain, const std::string& script)
    {
        // Ensure that the directory of the script contains the callback dll (otherwise mono will crash)
        const string callbacks_cs_source    = resource_cache->GetDataDirectory(Asset_Scripts) + "\\" + "Spartan.dll";
        const string callbacks_cs_dest      = FileSystem::GetDirectoryFromFilePath(script) + "Spartan.dll";
        if (!FileSystem::Exists(callbacks_cs_dest))
        {
            FileSystem::CopyFileFromTo(callbacks_cs_source, callbacks_cs_dest);
        }

        // Compile script
        if (!compile_script(script, callbacks_cs_dest))
        {
            LOG_ERROR("Failed to compile script");
            return nullptr;
        }

        // Open assembly
        string dll_path = FileSystem::ReplaceExtension(script, ".dll");
        return mono_domain_assembly_open(domain, dll_path.c_str());
    }

    static MonoMethod* get_method(MonoImage* image, const std::string& method)
    {
        // Get method description
        MonoMethodDesc* mono_method_desc = mono_method_desc_new(method.c_str(), NULL);
        if (!mono_method_desc)
        {
            LOG_ERROR("Failed to get method description %s", method.c_str());
            return nullptr;
        }

        // Search the method in the image
        MonoMethod* mono_method = mono_method_desc_search_in_image(mono_method_desc, image);
        if (!mono_method)
        {
            LOG_ERROR("Failed to get method %s", method.c_str());
            return nullptr;
        }

        return mono_method;
    }

	Scripting::Scripting(Context* context) : ISubsystem(context)
	{
        // Get file paths
        resource_cache              = context->GetSubsystem<ResourceCache>();
        const string dir_scripts    = resource_cache->GetDataDirectory(Asset_Scripts) + "\\";
        const string dir_mono_lib   = dir_scripts + string("mono\\lib");
        const string dir_mono_etc   = dir_scripts + string("mono\\etc");

        // Point mono to the libs and configuration files
        mono_set_dirs(dir_mono_lib.c_str(), dir_mono_etc.c_str());

        // Initialise a domain
        m_domain = mono_jit_init_version("Spartan", "v4.0.30319");
        if (!m_domain)
        {
            LOG_ERROR("mono_jit_init failed");
            return;
        }

        if (!mono_domain_set(m_domain, false))
        {
            LOG_ERROR("mono_domain_set failed");
            return;
        }

        mono_thread_set_main(mono_thread_current());

		// Subscribe to events
		SUBSCRIBE_TO_EVENT(EventType::WorldUnload, EVENT_HANDLER(Clear));
	}

	Scripting::~Scripting()
	{
        mono_jit_cleanup(m_domain);
	}

    static void Test(float delta_time)
    {
        LOG_ERROR("%f", delta_time);
    }

    bool Scripting::Initialize()
    {
        // Compile callbacks
        ResourceCache* resource_cache = m_context->GetSubsystem<ResourceCache>();
        const string callbacks_cs = resource_cache->GetDataDirectory(Asset_Scripts) + "/" + "Spartan.cs";
        if (!compile_script(callbacks_cs))
        {
            LOG_ERROR("Failed to compile Spartan callbacks");
            return false;
        }

        // Register callbacks
        mono_add_internal_call("Spartan.Debug::Log", reinterpret_cast<const void*>(Test));

        // Get version
        //const string major = to_string(ANGELSCRIPT_VERSION).erase(1, 4);
        //const string minor = to_string(ANGELSCRIPT_VERSION).erase(0, 1).erase(2, 2);
        //const string rev = to_string(ANGELSCRIPT_VERSION).erase(0, 3);
        //m_context->GetSubsystem<Settings>()->RegisterThirdPartyLib("AngelScript", major + "." + minor + "." + rev, "https://www.angelcode.com/angelscript/downloads.html");

        return true;
    }

    uint32_t Scripting::Load(const std::string& file_path)
    {
        ScriptInstance script;

        string class_name = FileSystem::GetFileNameNoExtensionFromFilePath(file_path);

        script.assembly = load_assembly(m_domain, file_path);
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
            LOG_ERROR("Failed to get class");
            return SCRIPT_NOT_LOADED;
        }

        // Create a instance of the class
        script.object = mono_object_new(m_domain, script.klass);
        if (!script.object)
        {
            LOG_ERROR("Failed to create object");
            return SCRIPT_NOT_LOADED;
        }

        // Call its default constructor
        mono_runtime_object_init(script.object);

        // Get methods
        script.method_start     = get_method(script.image, class_name + ":Start()");
        script.method_update    = get_method(script.image, class_name + ":Update(single)");

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
    }
}
