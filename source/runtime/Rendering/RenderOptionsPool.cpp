/*
Copyright(c) 2015-2025 Panos Karabelas, George Mavroeidis

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

//= INCLUDES =================================
#include "pch.h"
#include "RenderOptionsPool.h"
//============================================

#include "Display/Display.h"
#include "Profiling/Profiler.h"
#include "Rendering/Renderer.h"
#include "RHI/RHI_Device.h"
#include "RHI/RHI_SwapChain.h"
#include "RHI/RHI_VendorTechnology.h"
//============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    RenderOptionsPool::RenderOptionsPool() : RenderOptionsPool(RenderOptionsListType::Global)
    {

    }

    RenderOptionsPool::RenderOptionsPool(const RenderOptionsListType list_type)
    {
        m_options.clear();
        m_options[Renderer_Option::WhitePoint] =                  350.0f; // float
        m_options[Renderer_Option::Tonemapping] =                 static_cast<uint32_t>(Renderer_Tonemapping::Max); // enum
        m_options[Renderer_Option::Bloom] =                       1.0f;  // non-zero values activate it and control the intensity, float
        m_options[Renderer_Option::MotionBlur] =                  true;  // bool
        m_options[Renderer_Option::DepthOfField] =                true;  // bool
        m_options[Renderer_Option::FilmGrain] =                   false; // bool
        m_options[Renderer_Option::ChromaticAberration] =         false; // bool
        m_options[Renderer_Option::Vhs] =                         false; // bool
        m_options[Renderer_Option::Dithering] =                   false; // bool
        m_options[Renderer_Option::ScreenSpaceAmbientOcclusion] = true;  // bool
        m_options[Renderer_Option::ScreenSpaceReflections] =      true;  // bool
        m_options[Renderer_Option::Fog] =                         1.0f;  // float
        m_options[Renderer_Option::VariableRateShading] =         false; // bool
        m_options[Renderer_Option::Vsync] =                       false; // bool
        m_options[Renderer_Option::TransformHandle] =             true;  // bool
        m_options[Renderer_Option::SelectionOutline] =            false; // bool
        m_options[Renderer_Option::Grid] =                        false; // bool
        m_options[Renderer_Option::Lights] =                      true;  // bool
        m_options[Renderer_Option::AudioSources] =                true;  // bool
        m_options[Renderer_Option::Physics] =                     false; // bool
        m_options[Renderer_Option::PerformanceMetrics] =          true;  // bool
        m_options[Renderer_Option::Gamma] =                       Display::GetGamma(); // float
        m_options[Renderer_Option::Hdr] =                         false; // bool
        m_options[Renderer_Option::AutoExposureAdaptationSpeed] = 0.5f;  // float

        if (list_type == RenderOptionsListType::Global) // Global is default (For Render Options Window)
        {
            m_options[Renderer_Option::Aabb] =                        false; // bool
            m_options[Renderer_Option::PickingRay] =                  false; // bool
            m_options[Renderer_Option::Wireframe] =                   false; // bool
            m_options[Renderer_Option::Anisotropy] =                  16.0f; // float
            m_options[Renderer_Option::Sharpness] =                   1.0f;  // float
            m_options[Renderer_Option::AntiAliasing_Upsampling] =     static_cast<uint32_t>(Renderer_AntiAliasing_Upsampling::AA_Fsr_Upscale_Fsr); // enum
            m_options[Renderer_Option::ResolutionScale] =             1.0f;  // float
            m_options[Renderer_Option::DynamicResolution] =           false; // bool
            m_options[Renderer_Option::OcclusionCulling] =            false; // boo
        }
    }

    RenderOptionsPool::RenderOptionsPool(const std::map<Renderer_Option, RenderOptionType>& options)
    {
        m_options.clear();
        m_options = options;
    }

    RenderOptionsPool::RenderOptionsPool(RenderOptionsPool& other)
    {
        m_options.clear();
        for (const auto& [key, value] : other.m_options)
        {
            m_options[key] = value; // replaces if key exists, inserts otherwise
        }
    }

    void RenderOptionsPool::SetOption(Renderer_Option option, const RenderOptionType& value)
    {
        // Handle clamping for float options
        if (std::holds_alternative<float>(value))
        {
            float v = std::get<float>(value);

            if (option == Renderer_Option::Anisotropy)
            {
                v = clamp(v, 0.0f, 16.0f);
            }
            else if (option == Renderer_Option::ResolutionScale)
            {
                v = clamp(v, 0.5f, 1.0f);
            }

            // HDR, VRS, etc. validation (float enums)
            if (option == Renderer_Option::Hdr && v == 1.0f && !Display::GetHdr())
            {
                SP_LOG_WARNING("This display doesn't support HDR");
                return;
            }
            else if (option == Renderer_Option::VariableRateShading && v == 1.0f && !RHI_Device::PropertyIsShadingRateSupported())
            {
                SP_LOG_WARNING("This GPU doesn't support variable rate shading");
                return;
            }

            m_options[option] = v;
        }
        else if (std::holds_alternative<bool>(value))
        {
            bool v = std::get<bool>(value);

            m_options[option] = v;

            // cascade for toggles
            if (option == Renderer_Option::Vsync && Renderer::GetSwapChain())
            {
                Renderer::GetSwapChain()->SetVsync(v);
            }
            else if (option == Renderer_Option::PerformanceMetrics)
            {
                static bool enabled = false;
                if (!enabled && v)
                    Profiler::ClearMetrics();
                enabled = v;
            }
        }
        else if (std::holds_alternative<uint32_t>(value))
        {
            uint32_t v = std::get<uint32_t>(value);
            m_options[option] = v;
        }
        else if (std::holds_alternative<int>(value))
        {
            int v = std::get<int>(value);
            m_options[option] = v;
        }

        // ---- Cascading logic after setting ----
        if (std::holds_alternative<float>(value))
        {
            float v = std::get<float>(value);

            if (option == Renderer_Option::AntiAliasing_Upsampling)
            {
                if (v == static_cast<float>(Renderer_AntiAliasing_Upsampling::AA_Fsr_Upscale_Fsr) ||
                    v == static_cast<float>(Renderer_AntiAliasing_Upsampling::AA_Xess_Upscale_Xess))
                {
                    RHI_VendorTechnology::ResetHistory();
                }
            }
            else if (option == Renderer_Option::Hdr && Renderer::GetSwapChain())
            {
                Renderer::GetSwapChain()->SetHdr(v == 1.0f);
            }
        }
    }

    bool RenderOptionsPool::operator!=(const RenderOptionsPool& other) const
    {
        return m_options != other.m_options;
    }

    // For Editor
    std::string RenderOptionsPool::EnumToString(Renderer_Option option)
    {
        switch (option)
        {
            case Renderer_Option::Aabb:                        return "AABB";
            case Renderer_Option::PickingRay:                  return "Picking Ray";
            case Renderer_Option::Grid:                        return "Grid";
            case Renderer_Option::TransformHandle:             return "Transform Handle";
            case Renderer_Option::SelectionOutline:            return "Selection Outline";
            case Renderer_Option::Lights:                      return "Lights";
            case Renderer_Option::AudioSources:                return "Audio Sources";
            case Renderer_Option::PerformanceMetrics:          return "Performance Metrics";
            case Renderer_Option::Physics:                     return "Physics";
            case Renderer_Option::Wireframe:                   return "Wireframe";
            case Renderer_Option::Bloom:                       return "Bloom";
            case Renderer_Option::Fog:                         return "Fog";
            case Renderer_Option::ScreenSpaceAmbientOcclusion: return "Ambient Occlusion (SSAO)";
            case Renderer_Option::ScreenSpaceReflections:      return "Reflections (SSR)";
            case Renderer_Option::MotionBlur:                  return "Motion Blur";
            case Renderer_Option::DepthOfField:                return "Depth Of Field";
            case Renderer_Option::FilmGrain:                   return "Film Grain";
            case Renderer_Option::Vhs:                         return "VHS Effect";
            case Renderer_Option::ChromaticAberration:         return "Chromatic Aberration";
            case Renderer_Option::Anisotropy:                  return "Anisotropy";
            case Renderer_Option::Tonemapping:                 return "Tone Mapping";
            case Renderer_Option::AntiAliasing_Upsampling:     return "Anti-Aliasing Upsampling";
            case Renderer_Option::Sharpness:                   return "Sharpness";
            case Renderer_Option::Dithering:                   return "Dithering";
            case Renderer_Option::Hdr:                         return "HDR";
            case Renderer_Option::WhitePoint:                  return "White Point";
            case Renderer_Option::Gamma:                       return "Gamma";
            case Renderer_Option::Vsync:                       return "VSync";
            case Renderer_Option::VariableRateShading:         return "Variable Rate Shading";
            case Renderer_Option::ResolutionScale:             return "Resolution Scale";
            case Renderer_Option::DynamicResolution:           return "Dynamic Resolution";
            case Renderer_Option::OcclusionCulling:            return "Occlusion Culling";
            case Renderer_Option::AutoExposureAdaptationSpeed: return "Exposure Adaptation Speed";
            default:                                           return "Max";
        }
    }

    Renderer_Option RenderOptionsPool::StringToEnum(const std::string& name)
    {
        if (name == "AABB")                           return Renderer_Option::Aabb;
        else if (name == "Picking Ray")               return Renderer_Option::PickingRay;
        else if (name == "Grid")                      return Renderer_Option::Grid;
        else if (name == "Transform Handle")          return Renderer_Option::TransformHandle;
        else if (name == "Selection Outline")         return Renderer_Option::SelectionOutline;
        else if (name == "Lights")                    return Renderer_Option::Lights;
        else if (name == "Audio Sources")             return Renderer_Option::AudioSources;
        else if (name == "Performance Metrics")       return Renderer_Option::PerformanceMetrics;
        else if (name == "Physics")                   return Renderer_Option::Physics;
        else if (name == "Wireframe")                 return Renderer_Option::Wireframe;
        else if (name == "Bloom")                     return Renderer_Option::Bloom;
        else if (name == "Fog")                       return Renderer_Option::Fog;
        else if (name == "Ambient Occlusion (SSAO)")  return Renderer_Option::ScreenSpaceAmbientOcclusion;
        else if (name == "Reflections (SSR)")         return Renderer_Option::ScreenSpaceReflections;
        else if (name == "Motion Blur")               return Renderer_Option::MotionBlur;
        else if (name == "Depth Of Field")            return Renderer_Option::DepthOfField;
        else if (name == "Film Grain")                return Renderer_Option::FilmGrain;
        else if (name == "VHS Effect")                return Renderer_Option::Vhs;
        else if (name == "Chromatic Aberration")      return Renderer_Option::ChromaticAberration;
        else if (name == "Anisotropy")                return Renderer_Option::Anisotropy;
        else if (name == "Tone Mapping")              return Renderer_Option::Tonemapping;
        else if (name == "Anti-Aliasing Upsampling")  return Renderer_Option::AntiAliasing_Upsampling;
        else if (name == "Sharpness")                 return Renderer_Option::Sharpness;
        else if (name == "Dithering")                 return Renderer_Option::Dithering;
        else if (name == "HDR")                       return Renderer_Option::Hdr;
        else if (name == "White Point")               return Renderer_Option::WhitePoint;
        else if (name == "Gamma")                     return Renderer_Option::Gamma;
        else if (name == "VSync")                     return Renderer_Option::Vsync;
        else if (name == "Variable Rate Shading")     return Renderer_Option::VariableRateShading;
        else if (name == "Resolution Scale")          return Renderer_Option::ResolutionScale;
        else if (name == "Dynamic Resolution")        return Renderer_Option::DynamicResolution;
        else if (name == "Occlusion Culling")         return Renderer_Option::OcclusionCulling;
        else if (name == "Exposure Adaptation Speed") return Renderer_Option::AutoExposureAdaptationSpeed;
        else                                          return Renderer_Option::Max; // Default fallback
    }
}
