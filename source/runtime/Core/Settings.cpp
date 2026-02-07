/*
Copyright(c) 2015-2026 Panos Karabelas

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
#include "pch.h"
#include "Window.h"
#include "../Rendering/Renderer.h"
#include "../Resource/ResourceCache.h"
#include "../Input/Input.h"
SP_WARNINGS_OFF
#include "../IO/pugixml.hpp"
SP_WARNINGS_ON
//====================================

//= NAMESPACES ================
using namespace std;
using namespace spartan::math;
//=============================

namespace spartan
{
    namespace
    { 
        bool m_has_loaded_user_settings = false;
        string file_path                = "spartan.xml";

        // helper to convert cvar name to xml-safe name (e.g., "r.bloom" -> "r_bloom")
        string cvar_name_to_xml(const char* name)
        {
            string result(name);
            for (char& c : result)
            {
                if (c == '.')
                    c = '_';
            }
            return result;
        }

        void save()
        {
            pugi::xml_document doc;

            // write settings
            pugi::xml_node root = doc.append_child("Settings");
            {
                root.append_child("FullScreen").text().set(Window::IsFullScreen());
                root.append_child("IsMouseVisible").text().set(Input::GetMouseCursorVisible());
                root.append_child("ResolutionOutputWidth").text().set(Renderer::GetResolutionOutput().x);
                root.append_child("ResolutionOutputHeight").text().set(Renderer::GetResolutionOutput().y);
                root.append_child("ResolutionRenderWidth").text().set(Renderer::GetResolutionRender().x);
                root.append_child("ResolutionRenderHeight").text().set(Renderer::GetResolutionRender().y);
                root.append_child("FPSLimit").text().set(Timer::GetFpsLimit());
                for (const auto& [name, cvar] : ConsoleRegistry::Get().GetAll())
                {
                    if (name.size() >= 2 && name[0] == 'r' && name[1] == '.')
                    {
                        root.append_child(cvar_name_to_xml(string(name).c_str()).c_str()).text().set(get<float>(*cvar.m_value_ptr));
                    }
                }

                root.append_child("UseRootShaderDirectory").text().set(ResourceCache::GetUseRootShaderDirectory());
            }

            doc.save_file(file_path.c_str());
        }

        void load()
        {
            // attempt to load file
            pugi::xml_document doc;
            if (!doc.load_file(file_path.c_str()))
            {
                SP_LOG_ERROR("Failed to load XML file");
                return;
            }

            pugi::xml_node root = doc.child("Settings");

            // load settings
            {
                if ((root.child("FullScreen").text().as_bool()))
                {
                    Window::FullScreen();
                }

                Input::SetMouseCursorVisible(root.child("IsMouseVisible").text().as_bool());
                Timer::SetFpsLimit(root.child("FPSLimit").text().as_float());

                Renderer::SetResolutionRender(root.child("ResolutionRenderWidth").text().as_int(), root.child("ResolutionRenderHeight").text().as_int());
                Renderer::SetResolutionOutput(root.child("ResolutionOutputWidth").text().as_int(), root.child("ResolutionOutputHeight").text().as_int());

                // load render options from xml
                for (const auto& [name, cvar] : ConsoleRegistry::Get().GetAll())
                {
                    if (name.size() >= 2 && name[0] == 'r' && name[1] == '.')
                    {
                        pugi::xml_node child = root.child(cvar_name_to_xml(string(name).c_str()).c_str());
                        if (child)
                        {
                            ConsoleRegistry::Get().SetValueFromString(string(name).c_str(), child.text().as_string());
                        }
                    }
                }

                // this setting can be mapped directly to the resource cache (no need to wait for it to initialize)
                ResourceCache::SetUseRootShaderDirectory(root.child("UseRootShaderDirectory").text().as_bool());
            }

            m_has_loaded_user_settings = true;
        }
    }

    void Settings::Initialize()
    {
        if (FileSystem::Exists(file_path))
        {
            load();
        }
    }
    
    void Settings::Shutdown()
    {
        save();
    }

    bool Settings::HasLoadedUserSettingsFromFile()
    {
        return m_has_loaded_user_settings;
    }
}
