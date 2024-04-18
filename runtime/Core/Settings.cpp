/*
Copyright(c) 2016-2024 Panos Karabelas

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
using namespace Spartan::Math;
//=============================

namespace Spartan
{
    namespace
    { 
        bool m_is_fullscreen            = false;
        bool m_is_mouse_visible         = false;
        float fps_limit                 = 0.0f;
        bool m_has_loaded_user_settings = false;
        string file_path                = "spartan.xml";
        unordered_map<Renderer_Option, float> m_render_options;
        vector<third_party_lib> m_third_party_libs;

        const char* renderer_option_to_string(const Renderer_Option option)
        {
            switch (option)
            {
                case Renderer_Option::Aabb:                          return "Aabb";
                case Renderer_Option::PickingRay:                    return "PickingRay";
                case Renderer_Option::Grid:                          return "Grid";
                case Renderer_Option::TransformHandle:               return "TransformHandle";
                case Renderer_Option::SelectionOutline:              return "SelectionOutline";
                case Renderer_Option::Lights:                        return "Lights";
                case Renderer_Option::PerformanceMetrics:            return "PerformanceMetrics";
                case Renderer_Option::Physics:                       return "Physics";
                case Renderer_Option::Wireframe:                     return "Wireframe";
                case Renderer_Option::Bloom:                         return "Bloom";
                case Renderer_Option::Fog:                           return "Fog";
                case Renderer_Option::FogVolumetric:                 return "FogVolumetric";
                case Renderer_Option::ScreenSpaceGlobalIllumination: return "ScreenSpaceGlobalIllumination";
                case Renderer_Option::ScreenSpaceShadows:            return "ScreenSpaceShadows";
                case Renderer_Option::ScreenSpaceReflections:        return "ScreenSpaceReflections";
                case Renderer_Option::MotionBlur:                    return "MotionBlur";
                case Renderer_Option::DepthOfField:                  return "DepthOfField";
                case Renderer_Option::FilmGrain:                     return "FilmGrain";
                case Renderer_Option::ChromaticAberration:           return "ChromaticAberration";
                case Renderer_Option::Anisotropy:                    return "Anisotropy";
                case Renderer_Option::ShadowResolution:              return "ShadowResolution";
                case Renderer_Option::Exposure:                      return "Exposure";
                case Renderer_Option::WhitePoint:                    return "WhitePoint";
                case Renderer_Option::Antialiasing:                  return "Antialiasing";
                case Renderer_Option::Tonemapping:                   return "Tonemapping";
                case Renderer_Option::Upsampling:                    return "Upsampling";
                case Renderer_Option::Sharpness:                     return "Sharpness";
                case Renderer_Option::Hdr:                           return "Hdr";
                case Renderer_Option::Vsync:                         return "Vsync";
                case Renderer_Option::VariableRateShading:           return "VariableRateShading";
                case Renderer_Option::ResolutionScale:               return "ResolutionScale";
                case Renderer_Option::DynamicResolution:             return "DynamicResolution";
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
                root.append_child("ResolutionOutputWidth").text().set(Renderer::GetResolutionOutput().x);
                root.append_child("ResolutionOutputHeight").text().set(Renderer::GetResolutionOutput().y);
                root.append_child("ResolutionRenderWidth").text().set(Renderer::GetResolutionRender().x);
                root.append_child("ResolutionRenderHeight").text().set(Renderer::GetResolutionRender().y);
                root.append_child("FPSLimit").text().set(fps_limit);

                for (auto& [option, value] : m_render_options)
                {
                    root.append_child(renderer_option_to_string(option)).text().set(value);
                }

                root.append_child("UseRootShaderDirectory").text().set(ResourceCache::GetUseRootShaderDirectory());
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
                m_is_fullscreen    = root.child("FullScreen").text().as_bool();
                m_is_mouse_visible = root.child("IsMouseVisible").text().as_bool();
                fps_limit          = root.child("FPSLimit").text().as_float();

                Renderer::SetResolutionOutput(root.child("ResolutionOutputWidth").text().as_float(), root.child("ResolutionOutputHeight").text().as_float());
                Renderer::SetResolutionRender(root.child("ResolutionRenderWidth").text().as_float(), root.child("ResolutionRenderHeight").text().as_float());

                m_render_options.clear();
                for (uint32_t i = 0; i < static_cast<uint32_t>(Renderer_Option::Max); i++)
                {
                    Renderer_Option option = static_cast<Renderer_Option>(i);
                    m_render_options[option] = root.child(renderer_option_to_string(option)).text().as_float();
                }

                // this setting can be mapped directly to the resource cache (no need to wait for it to initialize)
                ResourceCache::SetUseRootShaderDirectory(root.child("UseRootShaderDirectory").text().as_bool());
            }

            m_has_loaded_user_settings = true;
        }

        static void map()
        {
            if (!m_has_loaded_user_settings)
                return;

            Timer::SetFpsLimit(fps_limit);
            Input::SetMouseCursorVisible(m_is_mouse_visible);
            Renderer::SetOptions(m_render_options);

            if (m_is_fullscreen)
            {
                Window::FullScreen();
            }
        }

        static void reflect()
        {
            fps_limit          = Timer::GetFpsLimit();
            m_is_fullscreen    = Window::IsFullScreen();
            m_is_mouse_visible = Input::GetMouseCursorVisible();
            m_render_options   = Renderer::GetOptions();
        }
    }

    void Settings::Initialize()
    {
        // register third party libs which don't register on their own as they are not part of some other initialization procedure
        RegisterThirdPartyLib("pugixml",               "1.11.4",    "https://github.com/zeux/pugixml");
        RegisterThirdPartyLib("SPIRV-Cross",           "03-06-2022", "https://github.com/KhronosGroup/SPIRV-Cross");
        RegisterThirdPartyLib("DirectXShaderCompiler", "1.7.2207.3", "https://github.com/microsoft/DirectXShaderCompiler");

        if (FileSystem::Exists(file_path))
        {
            load();
        }
    }
    
    void Settings::PostInitialize()
    {
        map();
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
