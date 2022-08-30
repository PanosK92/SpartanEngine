/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "../Core/FileSystem.h"
#include "../Rendering/Renderer.h"
#include "../Threading/Threading.h"
#include "../Display/Display.h"
#include "../Input/Input.h"
//=================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
    namespace io_helper
    {
        ofstream fout;
        ifstream fin;
        string file_name = "spartan.ini";
    
        template <class T>
        void write_setting(ofstream& fout, const string& name, T value)
        {
            fout << name << "=" << value << endl;
        }
    
        template <class T>
        void read_setting(ifstream& fin, const string& name, T& value)
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
    }
    
    static bool m_is_fullscreen            = false;
    static bool m_is_mouse_visible         = true;
    static Vector2 m_resolution_output     = Vector2::Zero;
    static Vector2 m_resolution_render     = Vector2::Zero;
    static uint32_t m_max_thread_count     = 0;
    static double m_fps_limit              = 0;
    static bool m_has_loaded_user_settings = false;
    static Context* m_context              = nullptr;
    static std::array<float, 32> m_render_options;
    static std::vector<third_party_lib> m_third_party_libs;

    void Settings::Initialize(Context* context)
    {
        m_context = context;

        // In case no Spartan.ini file exists, set the resolution to whatever the display is using.
        m_resolution_output.x = static_cast<float>(Display::GetWidth());
        m_resolution_output.y = static_cast<float>(Display::GetHeight());
        m_resolution_render = m_resolution_output;

        // Register third party libs which don't register on their own as they are not part of some other initialization procedure
        RegisterThirdPartyLib("pugixml", "1.11.4",                   "https://github.com/zeux/pugixml");
        RegisterThirdPartyLib("SPIRV-Cross", "03-06-2022",           "https://github.com/KhronosGroup/SPIRV-Cross");
        RegisterThirdPartyLib("DirectXShaderCompiler", "1.7.2207.3", "https://github.com/microsoft/DirectXShaderCompiler");
    }

    void Settings::PostInitialize()
    {
        // We are in initialising during OnPreTick() as
        // we need all the subsystems to be initialized
        Reflect();

        if (FileSystem::Exists(io_helper::file_name))
        {
            Load();
            Map();
        }
        else
        {
            Save();
        }

        SP_LOG_INFO("FPS Limit: %f.", m_fps_limit);
        SP_LOG_INFO("Max threads: %d.", m_max_thread_count);
    }
    
    void Settings::Shutdown()
    {
        Reflect();
        Save();
    }

    void Settings::RegisterThirdPartyLib(const std::string& name, const std::string& version, const std::string& url)
    {
        m_third_party_libs.emplace_back(name, version, url);
    }

    const vector<third_party_lib>& Settings::GetThirdPartyLibs()
    {
        return m_third_party_libs;
    }

    void Settings::Save()
    {
        // Create a settings file
        io_helper::fout.open(io_helper::file_name, ofstream::out);

        // Write the settings
        io_helper::write_setting(io_helper::fout, "bFullScreen",             m_is_fullscreen);
        io_helper::write_setting(io_helper::fout, "bIsMouseVisible",         m_is_mouse_visible);
        io_helper::write_setting(io_helper::fout, "iResolutionOutputWidth",  m_resolution_output.x);
        io_helper::write_setting(io_helper::fout, "iResolutionOutputHeight", m_resolution_output.y);
        io_helper::write_setting(io_helper::fout, "iResolutionRenderWidth",  m_resolution_render.x);
        io_helper::write_setting(io_helper::fout, "iResolutionRenderHeight", m_resolution_render.y);
        io_helper::write_setting(io_helper::fout, "fFPSLimit",               m_fps_limit);
        io_helper::write_setting(io_helper::fout, "iMaxThreadCount",         m_max_thread_count);

        for (uint32_t i = 0; i < static_cast<uint32_t>(m_render_options.size()); i++)
        {
            io_helper::write_setting(io_helper::fout, "render_option_" + to_string(i), m_render_options[i]);
        }

        // Close the file.
        io_helper::fout.close();
    }

    void Settings::Load()
    {
        // Create a settings file
        io_helper::fin.open(io_helper::file_name, ifstream::in);

        // Read the settings
        io_helper::read_setting(io_helper::fin, "bFullScreen",             m_is_fullscreen);
        io_helper::read_setting(io_helper::fin, "bIsMouseVisible",         m_is_mouse_visible);
        io_helper::read_setting(io_helper::fin, "iResolutionOutputWidth",  m_resolution_output.x);
        io_helper::read_setting(io_helper::fin, "iResolutionOutputHeight", m_resolution_output.y);
        io_helper::read_setting(io_helper::fin, "iResolutionRenderWidth",  m_resolution_render.x);
        io_helper::read_setting(io_helper::fin, "iResolutionRenderHeight", m_resolution_render.y);
        io_helper::read_setting(io_helper::fin, "fFPSLimit",               m_fps_limit);
        io_helper::read_setting(io_helper::fin, "iMaxThreadCount",         m_max_thread_count);

        for (uint32_t i = 0; i < static_cast<uint32_t>(m_render_options.size()); i++)
        {
            io_helper::read_setting(io_helper::fin, "render_option_" + to_string(i), m_render_options[i]);
        }

        // Close the file.
        io_helper::fin.close();

        m_has_loaded_user_settings = true;
    }

    void Settings::Map()
    {
        if (Timer* timer = m_context->GetSystem<Timer>())
        {
            timer->SetFpsLimit(m_fps_limit);
        }

        if (Input* input = m_context->GetSystem<Input>())
        {
            input->SetMouseCursorVisible(m_is_mouse_visible);
        }

        if (Renderer* renderer = m_context->GetSystem<Renderer>())
        {
            renderer->SetResolutionOutput(static_cast<uint32_t>(m_resolution_output.x), static_cast<uint32_t>(m_resolution_output.y));
            renderer->SetResolutionRender(static_cast<uint32_t>(m_resolution_render.x), static_cast<uint32_t>(m_resolution_render.y));
            renderer->SetOptions(m_render_options);
        }

        if (Window* window = m_context->GetSystem<Window>())
        {
            if (m_is_fullscreen)
            {
                window->FullScreen();
            }
        }
    }

    void Settings::Reflect()
    {
        Renderer* renderer = m_context->GetSystem<Renderer>();

        m_fps_limit         = m_context->GetSystem<Timer>()->GetFpsLimit();
        m_max_thread_count  = Threading::GetSupportedThreadCount();
        m_is_fullscreen     = m_context->GetSystem<Window>()->IsFullScreen();
        m_is_mouse_visible  = m_context->GetSystem<Input>()->GetMouseCursorVisible();
        m_resolution_output = renderer->GetResolutionOutput();
        m_resolution_render = renderer->GetResolutionRender();
        m_render_options    = renderer->GetOptions();
    }
}
