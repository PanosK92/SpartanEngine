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

//= INCLUDES ===================================
#include "pch.h"
#include "Renderer.h"
#include "../Commands/Console/ConsoleCommands.h"
#include "../Display/Display.h"
#include "../Profiling/Profiler.h"
#include "../RHI/RHI_Device.h"
#include "../RHI/RHI_SwapChain.h"
#include "../RHI/RHI_VendorTechnology.h"
//==============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        // callbacks for cascading changes and validation

        void on_anisotropy_change(const CVarVariant& value)
        {
            float v = clamp(get<float>(value), 0.0f, 16.0f);
            *ConsoleRegistry::Get().Find("r.anisotropy")->m_value_ptr = v;
        }

        void on_resolution_scale_change(const CVarVariant& value)
        {
            float v = clamp(get<float>(value), 0.5f, 1.0f);
            *ConsoleRegistry::Get().Find("r.resolution_scale")->m_value_ptr = v;
        }

        void on_hdr_change(const CVarVariant& value)
        {
            if (get<float>(value) == 1.0f && !Display::GetHdr())
            {
                SP_LOG_WARNING("This display doesn't support HDR");
                *ConsoleRegistry::Get().Find("r.hdr")->m_value_ptr = 0.0f;
                return;
            }

            if (RHI_SwapChain* swapchain = Renderer::GetSwapChain())
            {
                swapchain->SetHdr(get<float>(value) != 0.0f);
            }
        }

        void on_vsync_change(const CVarVariant& value)
        {
            if (RHI_SwapChain* swapchain = Renderer::GetSwapChain())
            {
                swapchain->SetVsync(get<float>(value) != 0.0f);
            }
        }

        void on_vrs_change(const CVarVariant& value)
        {
            if (get<float>(value) == 1.0f && !RHI_Device::IsSupportedVrs())
            {
                SP_LOG_WARNING("This GPU doesn't support variable rate shading");
                *ConsoleRegistry::Get().Find("r.variable_rate_shading")->m_value_ptr = 0.0f;
            }
        }

        void on_ray_traced_reflections_change(const CVarVariant& value)
        {
            if (get<float>(value) == 1.0f && !RHI_Device::IsSupportedRayTracing())
            {
                SP_LOG_WARNING("This GPU doesn't support ray tracing");
                *ConsoleRegistry::Get().Find("r.ray_traced_reflections")->m_value_ptr = 0.0f;
            }
        }

        void on_ray_traced_shadows_change(const CVarVariant& value)
        {
            if (get<float>(value) == 1.0f && !RHI_Device::IsSupportedRayTracing())
            {
                SP_LOG_WARNING("This GPU doesn't support ray tracing");
                *ConsoleRegistry::Get().Find("r.ray_traced_shadows")->m_value_ptr = 0.0f;
            }
        }

        void on_antialiasing_change(const CVarVariant& value)
        {
            float v = get<float>(value);

            if (v == static_cast<float>(Renderer_AntiAliasing_Upsampling::AA_Xess_Upscale_Xess) && !RHI_Device::IsSupportedXess())
            {
                SP_LOG_WARNING("This GPU doesn't support XeSS");
                *ConsoleRegistry::Get().Find("r.antialiasing_upsampling")->m_value_ptr = 0.0f;
                return;
            }

            if (v == static_cast<float>(Renderer_AntiAliasing_Upsampling::AA_Fsr_Upscale_Fsr) ||
                v == static_cast<float>(Renderer_AntiAliasing_Upsampling::AA_Xess_Upscale_Xess))
            {
                RHI_VendorTechnology::ResetHistory();
            }
        }

        void on_performance_metrics_change(const CVarVariant& value)
        {
            static bool was_enabled = false;
            bool is_enabled = get<float>(value) != 0.0f;
            if (!was_enabled && is_enabled)
            {
                Profiler::ClearMetrics();
            }
            was_enabled = is_enabled;
        }
    }

    // debug visualization
    TConsoleVar<float> cvar_aabb                           ("r.aabb",                           0.0f,                                                    "draw axis-aligned bounding boxes");
    TConsoleVar<float> cvar_picking_ray                    ("r.picking_ray",                    0.0f,                                                    "draw picking ray");
    TConsoleVar<float> cvar_grid                           ("r.grid",                           1.0f,                                                    "draw editor grid");
    TConsoleVar<float> cvar_transform_handle               ("r.transform_handle",               1.0f,                                                    "draw transform handles");
    TConsoleVar<float> cvar_selection_outline              ("r.selection_outline",              1.0f,                                                    "draw selection outline");
    TConsoleVar<float> cvar_lights                         ("r.lights",                         1.0f,                                                    "draw light icons");
    TConsoleVar<float> cvar_audio_sources                  ("r.audio_sources",                  1.0f,                                                    "draw audio source icons");
    TConsoleVar<float> cvar_performance_metrics            ("r.performance_metrics",            1.0f,                                                    "show performance metrics",                on_performance_metrics_change);
    TConsoleVar<float> cvar_physics                        ("r.physics",                        0.0f,                                                    "draw physics debug");
    TConsoleVar<float> cvar_wireframe                      ("r.wireframe",                      0.0f,                                                    "render in wireframe mode");
    // post-processing
    TConsoleVar<float> cvar_bloom                          ("r.bloom",                          1.0f,                                                    "bloom intensity, 0 to disable");
    TConsoleVar<float> cvar_fog                            ("r.fog",                            1.0f,                                                    "fog intensity/particle density");
    TConsoleVar<float> cvar_ssao                           ("r.ssao",                           1.0f,                                                    "screen space ambient occlusion");
    TConsoleVar<float> cvar_ray_traced_reflections         ("r.ray_traced_reflections",         static_cast<float>(RHI_Device::IsSupportedRayTracing()), "ray traced reflections",                  on_ray_traced_reflections_change);
    TConsoleVar<float> cvar_ray_traced_shadows             ("r.ray_traced_shadows",             static_cast<float>(RHI_Device::IsSupportedRayTracing()), "ray traced directional shadows",          on_ray_traced_shadows_change);
    TConsoleVar<float> cvar_restir_pt                      ("r.restir_pt",                      0.0f,                                                    "restir path tracing global illumination");
    TConsoleVar<float> cvar_motion_blur                    ("r.motion_blur",                    1.0f,                                                    "motion blur");
    TConsoleVar<float> cvar_depth_of_field                 ("r.depth_of_field",                 1.0f,                                                    "depth of field");
    TConsoleVar<float> cvar_film_grain                     ("r.film_grain",                     0.0f,                                                    "film grain effect");
    TConsoleVar<float> cvar_vhs                            ("r.vhs",                            0.0f,                                                    "vhs retro effect");
    TConsoleVar<float> cvar_chromatic_aberration           ("r.chromatic_aberration",           0.0f,                                                    "chromatic aberration");
    TConsoleVar<float> cvar_dithering                      ("r.dithering",                      0.0f,                                                    "dithering to reduce banding");
    TConsoleVar<float> cvar_sharpness                      ("r.sharpness",                      0.0f,                                                    "sharpening intensity");
    // quality settings
    TConsoleVar<float> cvar_anisotropy                     ("r.anisotropy",                     16.0f,                                                   "anisotropic filtering level (0-16)",      on_anisotropy_change);
    TConsoleVar<float> cvar_tonemapping                    ("r.tonemapping",                    4.0f,                                                    "tonemapping algorithm index");
    TConsoleVar<float> cvar_antialiasing_upsampling        ("r.antialiasing_upsampling",        2.0f,                                                    "aa/upsampling method index",              on_antialiasing_change);
    // display
    TConsoleVar<float> cvar_hdr                            ("r.hdr",                            0.0f,                                                    "enable hdr output",                       on_hdr_change);
    TConsoleVar<float> cvar_gamma                          ("r.gamma",                          2.2f,                                                    "display gamma");
    TConsoleVar<float> cvar_vsync                          ("r.vsync",                          0.0f,                                                    "vertical sync",                           on_vsync_change);
    // resolution
    TConsoleVar<float> cvar_variable_rate_shading          ("r.variable_rate_shading",          0.0f,                                                    "variable rate shading",                   on_vrs_change);
    TConsoleVar<float> cvar_resolution_scale               ("r.resolution_scale",               1.0f,                                                    "render resolution scale (0.5-1.0)",       on_resolution_scale_change);
    TConsoleVar<float> cvar_dynamic_resolution             ("r.dynamic_resolution",             0.0f,                                                    "automatic resolution scaling");
    // misc
    TConsoleVar<float> cvar_hiz_occlusion                  ("r.hiz_occlusion",                  1.0f,                                                    "hi-z occlusion culling for gpu-driven rendering");
    TConsoleVar<float> cvar_auto_exposure_adaptation_speed ("r.auto_exposure_adaptation_speed", 0.5f,                                                    "auto exposure adaptation speed, negative disables");
    // volumetric clouds
    TConsoleVar<float> cvar_cloud_coverage                 ("r.cloud_coverage",                 0.45f,                                                   "sky coverage (0=clear, 1=overcast)");
    TConsoleVar<float> cvar_cloud_shadows                  ("r.cloud_shadows",                  1.0f,                                                    "cloud shadow intensity on ground");
}
