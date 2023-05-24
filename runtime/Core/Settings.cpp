/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES ======================
#include "pch.h"
#include "Window.h"
#include "../Core/ThreadPool.h"
#include "../Rendering/Renderer.h"
#include "../Input/Input.h"
//=================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
    static bool m_is_fullscreen            = false;
    static bool m_is_mouse_visible         = true;
    static Vector2 m_resolution_output     = Vector2::Zero;
    static Vector2 m_resolution_render     = Vector2::Zero;
    static uint32_t m_max_thread_count     = 0;
    static double m_fps_limit              = 0;
    static bool m_has_loaded_user_settings = false;
    string file_path                       = "spartan.ini";
    ofstream fout;
    ifstream fin;
    static std::array<float, 34> m_render_options;
    static std::vector<third_party_lib> m_third_party_libs;

    template <class T>
    void write_setting(const string& name, T value)
    {
        fout << name << "=" << value << endl;
    }

    template <class T>
    void read_setting(const string& name, T& value)
    {
        for (string line; getline(fin, line); )
        {
            const auto first_index = line.find_first_of('=');
            if (name == line.substr(0, first_index))
            {
                const auto lastindex = line.find_last_of('=');
                const auto read_value = line.substr(lastindex + 1, line.length());
                value = static_cast<T>(stof(read_value));
                return;
            }
        }
    }

    static void save()
    {
        // Create a settings file
        fout.open(file_path, ofstream::out);

        // Write the settings
        write_setting("bFullScreen",             m_is_fullscreen);
        write_setting("bIsMouseVisible",         m_is_mouse_visible);
        write_setting("iResolutionOutputWidth",  m_resolution_output.x);
        write_setting("iResolutionOutputHeight", m_resolution_output.y);
        write_setting("iResolutionRenderWidth",  m_resolution_render.x);
        write_setting("iResolutionRenderHeight", m_resolution_render.y);
        write_setting("fFPSLimit",               m_fps_limit);
        write_setting("iMaxThreadCount",         m_max_thread_count);

        for (uint32_t i = 0; i < static_cast<uint32_t>(m_render_options.size()); i++)
        {
            write_setting("render_option_" + to_string(i), m_render_options[i]);
        }

        // Close the file.
        fout.close();
    }

    static void load()
    {
        // Create a settings file
        fin.open(file_path, ifstream::in);

        // Read the settings
        read_setting("bFullScreen",             m_is_fullscreen);
        read_setting("bIsMouseVisible",         m_is_mouse_visible);
        read_setting("iResolutionOutputWidth",  m_resolution_output.x);
        read_setting("iResolutionOutputHeight", m_resolution_output.y);
        read_setting("iResolutionRenderWidth",  m_resolution_render.x);
        read_setting("iResolutionRenderHeight", m_resolution_render.y);
        read_setting("fFPSLimit",               m_fps_limit);
        read_setting("iMaxThreadCount",         m_max_thread_count);

        for (uint32_t i = 0; i < static_cast<uint32_t>(m_render_options.size()); i++)
        {
            read_setting("render_option_" + to_string(i), m_render_options[i]);
        }

        // Close the file.
        fin.close();

        m_has_loaded_user_settings = true;
    }

    static void map()
    {
        Timer::SetFpsLimit(static_cast<float>(m_fps_limit));

        Input::SetMouseCursorVisible(m_is_mouse_visible);

        Renderer::SetResolutionOutput(static_cast<uint32_t>(m_resolution_output.x), static_cast<uint32_t>(m_resolution_output.y));
        Renderer::SetResolutionRender(static_cast<uint32_t>(m_resolution_render.x), static_cast<uint32_t>(m_resolution_render.y));
        Renderer::SetOptions(m_render_options);

        if (m_is_fullscreen)
        {
            Window::FullScreen();
        }
    }

    static void reflect()
    {
        m_fps_limit         = Timer::GetFpsLimit();
        m_max_thread_count  = ThreadPool::GetSupportedThreadCount();
        m_is_fullscreen     = Window::IsFullScreen();
        m_is_mouse_visible  = Input::GetMouseCursorVisible();
        m_resolution_output = Renderer::GetResolutionOutput();
        m_resolution_render = Renderer::GetResolutionRender();
        m_render_options    = Renderer::GetOptions();
    }

    void Settings::Initialize()
    {
        // Register third party libs which don't register on their own as they are not part of some other initialization procedure
        RegisterThirdPartyLib("pugixml", "1.11.4", "https://github.com/zeux/pugixml");
        RegisterThirdPartyLib("SPIRV-Cross", "03-06-2022", "https://github.com/KhronosGroup/SPIRV-Cross");
        RegisterThirdPartyLib("DirectXShaderCompiler", "1.7.2207.3", "https://github.com/microsoft/DirectXShaderCompiler");

        reflect();

        if (FileSystem::Exists(file_path))
        {
            load();
            map();
        }
        else
        {
            save();
        }

        SP_LOG_INFO("FPS Limit: %f.", m_fps_limit);
        SP_LOG_INFO("Max threads: %d.", m_max_thread_count);
    }
    
    void Settings::Shutdown()
    {
        reflect();
        save();
    }

    void Settings::RegisterThirdPartyLib(const std::string& name, const std::string& version, const std::string& url)
    {
        m_third_party_libs.emplace_back(name, version, url);
    }

    const vector<third_party_lib>& Settings::GetThirdPartyLibs()
    {
        return m_third_party_libs;
    }
}
