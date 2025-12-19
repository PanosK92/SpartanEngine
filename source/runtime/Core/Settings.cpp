/*
Copyright(c) 2015-2025 Panos Karabelas

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
        vector<third_party_lib> m_third_party_libs;
        mutex mutex_register;

        const char* renderer_option_to_string(const Renderer_Option option)
        {
            switch (option)
            {
                case Renderer_Option::Aabb:                        return "Aabb";
                case Renderer_Option::PickingRay:                  return "PickingRay";
                case Renderer_Option::Grid:                        return "Grid";
                case Renderer_Option::TransformHandle:             return "TransformHandle";
                case Renderer_Option::SelectionOutline:            return "SelectionOutline";
                case Renderer_Option::Lights:                      return "Lights";
                case Renderer_Option::AudioSources:                return "AudioSpurces";
                case Renderer_Option::PerformanceMetrics:          return "PerformanceMetrics";
                case Renderer_Option::Physics:                     return "Physics";
                case Renderer_Option::Wireframe:                   return "Wireframe";
                case Renderer_Option::Bloom:                       return "Bloom";
                case Renderer_Option::Fog:                         return "Fog";
                case Renderer_Option::ScreenSpaceAmbientOcclusion: return "ScreenSpaceAmbientOcclusion";
                case Renderer_Option::ScreenSpaceReflections:      return "ScreenSpaceReflections";
                case Renderer_Option::MotionBlur:                  return "MotionBlur";
                case Renderer_Option::DepthOfField:                return "DepthOfField";
                case Renderer_Option::FilmGrain:                   return "FilmGrain";
                case Renderer_Option::ChromaticAberration:         return "ChromaticAberration";
                case Renderer_Option::Anisotropy:                  return "Anisotropy";
                case Renderer_Option::WhitePoint:                  return "WhitePoint";
                case Renderer_Option::Tonemapping:                 return "Tonemapping";
                case Renderer_Option::AntiAliasing_Upsampling:     return "AntiAliasing_Upsampling";
                case Renderer_Option::Sharpness:                   return "Sharpness";
                case Renderer_Option::Hdr:                         return "Hdr";
                case Renderer_Option::Gamma:                       return "Gamma";
                case Renderer_Option::Vsync:                       return "Vsync";
                case Renderer_Option::VariableRateShading:         return "VariableRateShading";
                case Renderer_Option::ResolutionScale:             return "ResolutionScale";
                case Renderer_Option::DynamicResolution:           return "DynamicResolution";
                case Renderer_Option::Dithering:                   return "Dithering";
                case Renderer_Option::Vhs:                         return "VHS";
                case Renderer_Option::OcclusionCulling:            return "OcclusionCulling";
                case Renderer_Option::AutoExposureAdaptationSpeed: return "AutoExposureAdaptationSpeed";
                case Renderer_Option::RayTracedReflections:        return "RayTracedReflections";
                default:
                {
                    SP_ASSERT_MSG(false, "Renderer_Option not handled");
                    return "";
                }
            }
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
                for (auto& [option, value] : Renderer::GetRenderOptionsPool().GetOptions())
                {
                    // Force variant value to become a float WITHOUT ASSIGNING BACK TO MAP!
                    float float_value = 0.0f;
                    if (std::holds_alternative<float>(value))
                    {
                        float_value = std::get<float>(value);
                    }
                    else if (std::holds_alternative<int>(value))
                    {
                        float_value = static_cast<float>(std::get<int>(value));
                    }
                    else if (std::holds_alternative<uint32_t>(value))
                    {
                        float_value = static_cast<float>(std::get<uint32_t>(value));
                    }
                    else if (std::holds_alternative<bool>(value))
                    {
                        float_value = std::get<bool>(value) ? 1.0f : 0.0f;
                    }

                    root.append_child(renderer_option_to_string(option)).text().set(float_value);
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

                unordered_map<Renderer_Option, float> m_render_options;
                map<Renderer_Option, RenderOptionType> m_options;
                for (auto& [option, value] : m_options)
                {
                    const char* name = RenderOptionsPool::EnumToString(option).c_str();
                    auto node = root.child(name);
                    if (!node)
                        continue;

                    if (std::holds_alternative<bool>(value))
                    {
                        bool val = node.text().as_bool();
                        value = val;
                    }
                    else if (std::holds_alternative<int>(value))
                    {
                        int val = node.text().as_int();
                        value = val;
                    }
                    else if (std::holds_alternative<float>(value))
                    {
                        float val = node.text().as_float();
                        value = val;
                    }
                    else if (std::holds_alternative<uint32_t>(value))
                    {
                        uint32_t val = static_cast<uint32_t>(node.text().as_uint());
                        value = val;
                    }
                }
                Renderer::GetRenderOptionsPool().SetOptions(m_options);

                // this setting can be mapped directly to the resource cache (no need to wait for it to initialize)
                ResourceCache::SetUseRootShaderDirectory(root.child("UseRootShaderDirectory").text().as_bool());
            }

            m_has_loaded_user_settings = true;
        }
    }

    void Settings::Initialize()
    {
        RegisterThirdPartyLib("pugixml", "1.13", "https://github.com/zeux/pugixml");

        if (FileSystem::Exists(file_path))
        {
            load();
        }
    }
    
    void Settings::Shutdown()
    {
        save();
    }

    void Settings::RegisterThirdPartyLib(const string& name, const string& version, const string& url)
    {
        lock_guard<mutex> lock(mutex_register);

        m_third_party_libs.emplace_back(name, version, url);

        // maintain alphabetical order
        sort(m_third_party_libs.begin(), m_third_party_libs.end(),
            [](const third_party_lib& a, const third_party_lib& b)
            {
                return a.name < b.name;
            }
        );
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
