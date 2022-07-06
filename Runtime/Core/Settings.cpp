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
#include "Spartan.h"
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

namespace _Settings
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

namespace Spartan
{
    Settings::Settings(Context* context) : Subsystem(context)
    {
        m_context = context;

        // In case no Spartan.ini file exists, set the resolution to whatever the display is using.
        m_resolution_output.x = static_cast<float>(Display::GetWidth());
        m_resolution_output.y = static_cast<float>(Display::GetHeight());
        m_resolution_render   = m_resolution_output;

        // Register third party libs which don't register on their own as they are not part of some other initiliasation procedure
        RegisterThirdPartyLib("pugixml", "1.11.4", "https://github.com/zeux/pugixml");
        RegisterThirdPartyLib("SPIRV-Cross", "2021-01-15", "https://github.com/KhronosGroup/SPIRV-Cross");
        RegisterThirdPartyLib("DirectXShaderCompiler", "1.6.2109", "https://github.com/microsoft/DirectXShaderCompiler");
    }

    Settings::~Settings()
    {

    }

    void Settings::OnPostInitialize()
    {
        // We are in initialising during OnPreTick() as
        // we need all the subsystems to be initialised
        Reflect();

        if (FileSystem::Exists(_Settings::file_name))
        {
            Load();
            Map();
        }
        else
        {
            Save();
        }

        LOG_INFO("FPS Limit: %f.", m_fps_limit);
        LOG_INFO("Max threads: %d.", m_max_thread_count);
    }
    
    void Settings::OnShutdown()
    {
        Reflect();
        Save();
    }

    void Settings::RegisterThirdPartyLib(const std::string& name, const std::string& version, const std::string& url)
    {
        m_third_party_libs.emplace_back(name, version, url);
    }

    void Settings::Save() const
    {
        // Create a settings file
        _Settings::fout.open(_Settings::file_name, ofstream::out);

        // Write the settings
        _Settings::write_setting(_Settings::fout, "bFullScreen",             m_is_fullscreen);
        _Settings::write_setting(_Settings::fout, "bIsMouseVisible",         m_is_mouse_visible);
        _Settings::write_setting(_Settings::fout, "iResolutionOutputWidth",  m_resolution_output.x);
        _Settings::write_setting(_Settings::fout, "iResolutionOutputHeight", m_resolution_output.y);
        _Settings::write_setting(_Settings::fout, "iResolutionRenderWidth",  m_resolution_render.x);
        _Settings::write_setting(_Settings::fout, "iResolutionRenderHeight", m_resolution_render.y);
        _Settings::write_setting(_Settings::fout, "fFPSLimit",               m_fps_limit);
        _Settings::write_setting(_Settings::fout, "iMaxThreadCount",         m_max_thread_count);

        uint32_t index = 0;
        for (auto& it : m_render_options)
        {
            _Settings::write_setting(_Settings::fout, "render_option_" + to_string(index++), it.second);
        }

        // Close the file.
        _Settings::fout.close();
    }

    void Settings::Load()
    {
        // Create a settings file
        _Settings::fin.open(_Settings::file_name, ifstream::in);

        // Read the settings
        _Settings::read_setting(_Settings::fin, "bFullScreen",             m_is_fullscreen);
        _Settings::read_setting(_Settings::fin, "bIsMouseVisible",         m_is_mouse_visible);
        _Settings::read_setting(_Settings::fin, "iResolutionOutputWidth",  m_resolution_output.x);
        _Settings::read_setting(_Settings::fin, "iResolutionOutputHeight", m_resolution_output.y);
        _Settings::read_setting(_Settings::fin, "iResolutionRenderWidth",  m_resolution_render.x);
        _Settings::read_setting(_Settings::fin, "iResolutionRenderHeight", m_resolution_render.y);
        _Settings::read_setting(_Settings::fin, "fFPSLimit",               m_fps_limit);
        _Settings::read_setting(_Settings::fin, "iMaxThreadCount",         m_max_thread_count);

        uint32_t index = 0;
        for (auto& it : m_render_options)
        {
            _Settings::read_setting(_Settings::fin, "render_option_" + to_string(index++), it.second);
        }

        // Close the file.
        _Settings::fin.close();

        m_has_loaded_user_settings = true;
    }

    void Settings::Map() const
    {
        if (Timer* timer = m_context->GetSubsystem<Timer>())
        {
            timer->SetFpsLimit(m_fps_limit);
        }

        if (Input* input = m_context->GetSubsystem<Input>())
        {
            input->SetMouseCursorVisible(m_is_mouse_visible);
        }

        if (Renderer* renderer = m_context->GetSubsystem<Renderer>())
        {
            renderer->SetResolutionOutput(static_cast<uint32_t>(m_resolution_output.x), static_cast<uint32_t>(m_resolution_output.y));
            renderer->SetResolutionRender(static_cast<uint32_t>(m_resolution_render.x), static_cast<uint32_t>(m_resolution_render.y));
            renderer->SetOptions(m_render_options);
        }

        if (Window* window = m_context->GetSubsystem<Window>())
        {
            if (m_is_fullscreen)
            {
                window->FullScreen();
            }
        }
    }

    void Settings::Reflect()
    {
        Renderer* renderer = m_context->GetSubsystem<Renderer>();

        m_fps_limit         = m_context->GetSubsystem<Timer>()->GetFpsLimit();
        m_max_thread_count  = m_context->GetSubsystem<Threading>()->GetThreadCountSupport();
        m_is_fullscreen     = m_context->GetSubsystem<Window>()->IsFullScreen();
        m_is_mouse_visible  = m_context->GetSubsystem<Input>()->GetMouseCursorVisible();
        m_resolution_output = renderer->GetResolutionOutput();
        m_resolution_render = renderer->GetResolutionRender();
        m_render_options    = renderer->GetOptions();
    }
}
