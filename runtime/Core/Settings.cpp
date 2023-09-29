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
SP_WARNINGS_OFF
#include "../IO/pugixml.hpp"
SP_WARNINGS_ON
//=================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
    namespace
    { 
        static bool m_is_fullscreen            = false;
        static bool m_is_mouse_visible         = true;
        static Vector2 m_resolution_output     = Vector2::Zero;
        static Vector2 m_resolution_render     = Vector2::Zero;
        static double fps_limit                = 0;
        static bool m_has_loaded_user_settings = false;
        string file_path                       = "spartan.xml";
        static unordered_map<Renderer_Option, float> m_render_options;
        static vector<third_party_lib> m_third_party_libs;

        const char* renderer_option_to_string(const Renderer_Option option)
        {
            switch (option)
            {
                case Renderer_Option::Debug_Aabb:               return "Debug_Aabb";
                case Renderer_Option::Debug_PickingRay:         return "Debug_PickingRay";
                case Renderer_Option::Debug_Grid:               return "Debug_Grid";
                case Renderer_Option::Debug_ReflectionProbes:   return "Debug_ReflectionProbes";
                case Renderer_Option::Debug_TransformHandle:    return "Debug_TransformHandle";
                case Renderer_Option::Debug_SelectionOutline:   return "Debug_SelectionOutline";
                case Renderer_Option::Debug_Lights:             return "Debug_Lights";
                case Renderer_Option::Debug_PerformanceMetrics: return "Debug_PerformanceMetrics";
                case Renderer_Option::Debug_Physics:            return "Debug_Physics";
                case Renderer_Option::Debug_Wireframe:          return "Debug_Wireframe";
                case Renderer_Option::Bloom:                    return "Bloom";
                case Renderer_Option::VolumetricFog:            return "VolumetricFog";
                case Renderer_Option::Ssgi:                     return "Ssgi";
                case Renderer_Option::ScreenSpaceShadows:       return "ScreenSpaceShadows";
                case Renderer_Option::ScreenSpaceReflections:   return "ScreenSpaceReflections";
                case Renderer_Option::MotionBlur:               return "MotionBlur";
                case Renderer_Option::DepthOfField:             return "DepthOfField";
                case Renderer_Option::FilmGrain:                return "FilmGrain";
                case Renderer_Option::ChromaticAberration:      return "ChromaticAberration";
                case Renderer_Option::Debanding:                return "Debanding";
                case Renderer_Option::DepthPrepass:             return "DepthPrepass";
                case Renderer_Option::Anisotropy:               return "Anisotropy";
                case Renderer_Option::ShadowResolution:         return "ShadowResolution";
                case Renderer_Option::Gamma:                    return "Gamma";
                case Renderer_Option::Exposure:                 return "Exposure";
                case Renderer_Option::PaperWhite:               return "PaperWhite";
                case Renderer_Option::FogDensity:               return "FogDensity";
                case Renderer_Option::Antialiasing:             return "Antialiasing";
                case Renderer_Option::Tonemapping:              return "Tonemapping";
                case Renderer_Option::Upsampling:               return "Upsampling";
                case Renderer_Option::UpsamplingSharpness:      return "UpsamplingSharpness";
                case Renderer_Option::Sharpness:                return "Sharpness";
                case Renderer_Option::Hdr:                      return "Hdr";
                case Renderer_Option::Vsync:                    return "Vsync";
                default:
                {
                    SP_ASSERT_MSG(false, "Renderer_Option not handled");
                    return "";
                }
            }
        }

        static void save()
        {
            pugi::xml_document doc;

            // write settings
            pugi::xml_node root = doc.append_child("Settings");
            {
                root.append_child("FullScreen").text().set(m_is_fullscreen);
                root.append_child("IsMouseVisible").text().set(m_is_mouse_visible);
                root.append_child("ResolutionOutputWidth").text().set(m_resolution_output.x);
                root.append_child("ResolutionOutputHeight").text().set(m_resolution_output.y);
                root.append_child("ResolutionRenderWidth").text().set(m_resolution_render.x);
                root.append_child("ResolutionRenderHeight").text().set(m_resolution_render.y);
                root.append_child("FPSLimit").text().set(fps_limit);

                for (auto& [option, value] : m_render_options)
                {
                    root.append_child(renderer_option_to_string(option)).text().set(value);
                }
            }

            doc.save_file(file_path.c_str());
        }

        static void load()
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
                m_is_fullscreen       = root.child("FullScreen").text().as_bool();
                m_is_mouse_visible    = root.child("IsMouseVisible").text().as_bool();
                m_resolution_output.x = root.child("ResolutionOutputWidth").text().as_float();
                m_resolution_output.y = root.child("ResolutionOutputHeight").text().as_float();
                m_resolution_render.x = root.child("ResolutionRenderWidth").text().as_float();
                m_resolution_render.y = root.child("ResolutionRenderHeight").text().as_float();
                fps_limit             = root.child("FPSLimit").text().as_int();

                m_render_options.clear();
                for (uint32_t i = 0; i < static_cast<uint32_t>(Renderer_Option::Max); i++)
                {
                    Renderer_Option option = static_cast<Renderer_Option>(i);
                    m_render_options[option] = root.child(renderer_option_to_string(option)).text().as_float();
                }
            }

            m_has_loaded_user_settings = true;
        }

        static void map()
        {
            Timer::SetFpsLimit(static_cast<float>(fps_limit));

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
            fps_limit           = Timer::GetFpsLimit();
            m_is_fullscreen     = Window::IsFullScreen();
            m_is_mouse_visible  = Input::GetMouseCursorVisible();
            m_resolution_output = Renderer::GetResolutionOutput();
            m_resolution_render = Renderer::GetResolutionRender();
            m_render_options    = Renderer::GetOptions();
        }
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

    bool Settings::HasLoadedUserSettingsFromFile()
    {
        return m_has_loaded_user_settings;
    }
}
