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

//= INCLUDES =========================
#include "../Resource/ResourceCache.h"
//====================================

namespace Spartan::ScriptingHelper
{
    static ResourceCache* resource_cache = nullptr;

    static std::string execute_command(const char* cmd)
    {
        std::array<char, 1024> buffer;
        std::string result;
        const std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(cmd, "r"), _pclose);
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
        const std::string dir_scripts    = resource_cache->GetDataDirectory(Asset_Scripts) + "\\";
        const std::string dir_compiler   = dir_scripts + "mono\\roslyn\\csc.exe";

        // Compile script
        std::string command = dir_compiler + " -target:library -nologo";
        if (!dll_reference.empty())
        {
            command += " -reference:" + dll_reference;
        }
        command += " -out:" + FileSystem::ReplaceExtension(script, ".dll") + " " + std::string(script);
        const std::string result = execute_command(command.c_str());

        // Log compilation output
        std::istringstream f(result);
        std::string line;
        bool compilation_result = true;
        while (std::getline(f, line))
        {
            if (FileSystem::IsEmptyOrWhitespace(line))
                continue;

            const auto is_error = line.find("error") != std::string::npos;
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

    static MonoAssembly* compile_and_load_assembly(MonoDomain* domain, const std::string& script, bool is_script = true)
    {
        // Ensure that the directory of the script contains the callback dll (otherwise mono will crash)
        if (is_script)
        {
            const std::string callbacks_cs_source = resource_cache->GetDataDirectory(Asset_Scripts) + "\\" + "Spartan.dll";
            const std::string callbacks_cs_dest = FileSystem::GetDirectoryFromFilePath(script) + "Spartan.dll";
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
        }
        else
        {
            // Compile script
            if (!compile_script(script))
            {
                LOG_ERROR("Failed to compile script");
                return nullptr;
            }
        }

        // Open assembly
        const std::string dll_path = FileSystem::ReplaceExtension(script, ".dll");
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
}
