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

//= INCLUDES =================================
#include "pch.h"
SP_WARNINGS_OFF
#ifdef _WIN32
#include <xess/xess_d3d12.h>
#define NRD_STATIC_LIBRARY
#include "NRD.h"
#include "NRI.h"
#include "Extensions/NRIHelper.h"
#include "Extensions/NRIWrapperD3D12.h"
#include "NRDIntegration.hpp"
#endif
SP_WARNINGS_ON
#include "../RHI_VendorTechnology.h"
#include "../RHI_Implementation.h"
#include "../RHI_CommandList.h"
#include "../RHI_Device.h"
#include "../RHI_Texture.h"
#include "../../Rendering/Renderer.h"
#include "../../World/World.h"
#include "../../World/Components/Camera.h"
//============================================

//= NAMESPACES ===============
using namespace spartan::math;
using namespace std;
//============================

namespace spartan
{
    #ifdef _WIN32
    namespace common
    {
        uint32_t resolution_render_width      = 0; // scaled (render * scale), used for per-frame dispatch
        uint32_t resolution_render_height     = 0;
        uint32_t resolution_render_max_width  = 0; // unscaled base render resolution
        uint32_t resolution_render_max_height = 0;
        uint32_t resolution_output_width      = 0;
        uint32_t resolution_output_height     = 0;
        bool reset_history                    = false;
        float resolution_scale                = 1.0f;
        Cb_Frame* cb_frame                    = nullptr;
    }

    namespace intel
    {
        xess_context_handle_t context              = nullptr;
        xess_d3d12_init_params_t params_init       = {};
        xess_d3d12_execute_params_t params_execute = {};
        Vector2 jitter                             = Vector2::Zero;
        const float responsive_mask_value_max      = 0.05f;
        const float exposure_scale                 = 1.0f;
        xess_quality_settings_t quality            = XESS_QUALITY_SETTING_BALANCED;

        xess_quality_settings_t get_quality(const float scale_factor)
        {
            struct QualitySetting
            {
                xess_quality_settings_t quality;
                float scale_factor;
            };

            const QualitySetting quality_settings[] =
            {
                { XESS_QUALITY_SETTING_ULTRA_PERFORMANCE,  0.25f },
                { XESS_QUALITY_SETTING_PERFORMANCE,        0.36f },
                { XESS_QUALITY_SETTING_BALANCED,           0.49f },
                { XESS_QUALITY_SETTING_QUALITY,            0.64f },
                { XESS_QUALITY_SETTING_ULTRA_QUALITY,      0.81f },
                { XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS, 0.91f },
                { XESS_QUALITY_SETTING_AA,                 1.0f  }
            };

            quality              = XESS_QUALITY_SETTING_BALANCED;
            float min_difference = numeric_limits<float>::max();

            for (const auto& setting : quality_settings)
            {
                float difference = abs(scale_factor - setting.scale_factor);
                if (difference < min_difference)
                {
                    min_difference = difference;
                    quality        = setting.quality;
                }
            }

            return quality;
        }

        void context_destroy()
        {
            if (context)
            {
                xessDestroyContext(context);
                context = nullptr;
            }
        }

        void context_create()
        {
            if (!RHI_Device::IsSupportedXess())
            {
                return;
            }

            context_destroy();

            // real support probe, context creation fails on drivers or hardware that can not run xess,
            // bail gracefully instead of asserting so the renderer falls back to another upscaler
            if (xessD3D12CreateContext(RHI_Context::device, &context) != xess_result_t::XESS_RESULT_SUCCESS)
            {
                SP_LOG_WARNING("XeSS context creation failed, disabling XeSS");
                context = nullptr;
                return;
            }

            // calculate the scaling factor using the base (unscaled) render resolution
            uint32_t render_area = common::resolution_render_max_width * common::resolution_render_max_height;
            uint32_t output_area = common::resolution_output_width * common::resolution_output_height;
            float scale_factor   = static_cast<float>(render_area) / static_cast<float>(output_area);

            intel::params_init                      = {};
            intel::params_init.outputResolution.x   = common::resolution_output_width;
            intel::params_init.outputResolution.y   = common::resolution_output_height;
            intel::params_init.qualitySetting       = intel::get_quality(scale_factor);
            // let xess compute its own exposure from input statistics, the engine's tonemapper
            // handles exposure for the displayed image, decoupling these two avoids feeding xess
            // a value that lags or fluctuates relative to its training assumptions
            intel::params_init.initFlags            = XESS_INIT_FLAG_USE_NDC_VELOCITY | XESS_INIT_FLAG_INVERTED_DEPTH | XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE;
            intel::params_init.creationNodeMask     = 0;
            intel::params_init.visibleNodeMask      = 0;
            intel::params_init.pTempBufferHeap      = nullptr;
            intel::params_init.bufferHeapOffset     = 0;
            intel::params_init.pTempTextureHeap     = nullptr;
            intel::params_init.textureHeapOffset    = 0;
            // keep xess off the engine pipeline library, its cold load names spam the debug layer
            intel::params_init.pPipelineLibrary     = nullptr;

            // xess manages its own shader-visible descriptor heap internally when none is supplied at execute time,
            // so no engine heap needs to be handed over here, this keeps the bindless heap free of xess descriptors
            if (xessD3D12Init(intel::context, &intel::params_init) != xess_result_t::XESS_RESULT_SUCCESS)
            {
                SP_LOG_WARNING("XeSS initialization failed, disabling XeSS");
                context_destroy();
                return;
            }

            if (xessSetVelocityScale(intel::context, -1.0f, -1.0f) != xess_result_t::XESS_RESULT_SUCCESS)
            {
                SP_LOG_WARNING("XeSS velocity scale configuration failed");
            }

            if (xessSetMaxResponsiveMaskValue(intel::context, intel::responsive_mask_value_max) != xess_result_t::XESS_RESULT_SUCCESS)
            {
                SP_LOG_WARNING("XeSS responsive mask configuration failed");
            }
        }

        uint32_t get_sample_count()
        {
            uint32_t count = 0;

            switch (quality)
            {
            case XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS:
            case XESS_QUALITY_SETTING_ULTRA_QUALITY:    count = 48; break;
            case XESS_QUALITY_SETTING_QUALITY:          count = 48; break;
            case XESS_QUALITY_SETTING_BALANCED:         count = 64; break;
            case XESS_QUALITY_SETTING_PERFORMANCE:      count = 80; break;
            case XESS_QUALITY_SETTING_ULTRA_PERFORMANCE:count = 96; break;
            case XESS_QUALITY_SETTING_AA:               count = 32; break;
            }

            return count;
        }
    }

    namespace nvidia
    {
        // gi is restir sized, screen is render sized for reflections and shadows
        struct nrd_pool
        {
            nrd::Integration integration;
            bool initialized             = false;
            uint32_t width               = 0;
            uint32_t height              = 0;
            bool reset_history           = false;
            uint32_t last_frame_index    = UINT32_MAX;
            uint32_t last_settings_frame = UINT32_MAX;
            bool is_screen               = false;
            ID3D12CommandQueue* queue    = nullptr;
        };

        nrd_pool pool_gi;
        nrd_pool pool_screen;

        nrd::Resource make_resource(RHI_Texture* texture)
        {
            nrd::Resource resource = {};
            resource.d3d12.resource = static_cast<ID3D12Resource*>(texture->GetRhiResource());
            resource.d3d12.format   = static_cast<DXGIFormat>(d3d12_format[rhi_format_to_index(texture->GetFormat())]);
            resource.userArg        = texture;
            return resource;
        }

        void pool_destroy(nrd_pool& pool)
        {
            if (pool.initialized)
            {
                pool.integration.Destroy();
                pool.initialized = false;
            }
            pool.width               = 0;
            pool.height              = 0;
            pool.last_frame_index    = UINT32_MAX;
            pool.last_settings_frame = UINT32_MAX;
        }

        void context_destroy()
        {
            pool_destroy(pool_gi);
            pool_destroy(pool_screen);
        }

        void request_history_reset()
        {
            pool_gi.reset_history     = true;
            pool_screen.reset_history = true;
        }

        bool pool_create(nrd_pool& pool, uint32_t resource_width, uint32_t resource_height, bool is_screen)
        {
            pool_destroy(pool);
            pool.is_screen = is_screen;

            pool.queue = static_cast<ID3D12CommandQueue*>(RHI_Device::GetQueueRhiResource(RHI_Queue_Type::Compute));
            if (!RHI_Context::device || !pool.queue || resource_width == 0 || resource_height == 0)
            {
                return false;
            }

            nri::QueueFamilyD3D12Desc queue_family = {};
            queue_family.d3d12Queues = &pool.queue;
            queue_family.queueNum    = 1;
            queue_family.queueType   = nri::QueueType::COMPUTE;

            nri::DeviceCreationD3D12Desc device_desc = {};
            device_desc.d3d12Device                  = RHI_Context::device;
            device_desc.queueFamilies                = &queue_family;
            device_desc.queueFamilyNum               = 1;
            device_desc.disableD3D12EnhancedBarriers = true;

            nrd::DenoiserDesc denoisers_gi[] =
            {
                { nrd_common::id_gi, nrd::Denoiser::REBLUR_DIFFUSE }
            };
            nrd::DenoiserDesc denoisers_screen[] =
            {
                { nrd_common::id_reflections, nrd::Denoiser::REBLUR_SPECULAR },
                { nrd_common::id_shadows,     nrd::Denoiser::SIGMA_SHADOW }
            };

            nrd::InstanceCreationDesc instance_desc = {};
            if (pool.is_screen)
            {
                instance_desc.denoisers    = denoisers_screen;
                instance_desc.denoisersNum = 2;
            }
            else
            {
                instance_desc.denoisers    = denoisers_gi;
                instance_desc.denoisersNum = 1;
            }

            nrd::IntegrationCreationDesc integration_desc = {};
            strncpy_s(integration_desc.name, pool.is_screen ? "NRD_Screen" : "NRD_GI", _TRUNCATE);
            integration_desc.queuedFrameNum                       = 3;
            integration_desc.enableWholeLifetimeDescriptorCaching = false;
            integration_desc.autoWaitForIdle                      = true;
            integration_desc.resourceWidth                        = static_cast<uint16_t>(resource_width);
            integration_desc.resourceHeight                       = static_cast<uint16_t>(resource_height);

            if (pool.integration.RecreateD3D12(integration_desc, instance_desc, device_desc) != nrd::Result::SUCCESS)
            {
                SP_LOG_WARNING("NRD recreate failed");
                return false;
            }

            pool.width       = resource_width;
            pool.height      = resource_height;
            pool.initialized = true;
            return true;
        }

        nrd_pool& pool_for_preset(Nrd_Preset preset)
        {
            return preset == Nrd_Preset::Gi ? pool_gi : pool_screen;
        }
    }
    #endif

    void RHI_VendorTechnology::Initialize()
    {

    }

    void RHI_VendorTechnology::Shutdown()
    {
    #ifdef _WIN32
        intel::context_destroy();
        nvidia::context_destroy();
    #endif
    }

    void RHI_VendorTechnology::Tick(Cb_Frame* cb_frame, const Vector2& resolution_render, const Vector2& resolution_output, const float resolution_scale)
    {
    #ifdef _WIN32
        common::cb_frame         = cb_frame;
        common::resolution_scale = resolution_scale;

        common::resolution_render_width  = Renderer::GetScaledDimension(static_cast<uint32_t>(resolution_render.x), resolution_scale);
        common::resolution_render_height = Renderer::GetScaledDimension(static_cast<uint32_t>(resolution_render.y), resolution_scale);

        uint32_t base_render_width  = static_cast<uint32_t>(resolution_render.x);
        uint32_t base_render_height = static_cast<uint32_t>(resolution_render.y);
        uint32_t output_width       = static_cast<uint32_t>(resolution_output.x);
        uint32_t output_height      = static_cast<uint32_t>(resolution_output.y);

        bool base_render_changed = base_render_width  != common::resolution_render_max_width ||
                                   base_render_height != common::resolution_render_max_height;
        bool output_changed      = output_width  != common::resolution_output_width ||
                                   output_height != common::resolution_output_height;

        common::resolution_render_max_width  = base_render_width;
        common::resolution_render_max_height = base_render_height;
        common::resolution_output_width      = output_width;
        common::resolution_output_height     = output_height;

        if (base_render_changed || output_changed)
        {
            RHI_Device::QueueWaitAll();
            intel::context_create();
            common::reset_history = true;
            nvidia::request_history_reset();
            nvidia::context_destroy();
        }
    #endif
    }

    void RHI_VendorTechnology::ResetHistory()
    {
    #ifdef _WIN32
        common::reset_history = true;
        nvidia::request_history_reset();
    #endif
    }

    void RHI_VendorTechnology::XeSS_GenerateJitterSample(float* x, float* y)
    {
    #ifdef _WIN32
        // generate a single halton value for a given base and index
        auto get_corput = [](uint32_t index, uint32_t base) -> float
        {
            float result = 0.0f;
            float bk     = 1.0f;
            while (index > 0)
            {
                bk     /= static_cast<float>(base);
                result += static_cast<float>(index % base) * bk;
                index  /= base;
            }
            return result;
        };

        static vector<pair<float, float>> halton_points;
        static size_t halton_index = 0;

        // restart the sequence whenever history is wiped so the first accumulated frame
        // samples a known sub-pixel position rather than continuing from an arbitrary phase
        if (common::reset_history)
        {
            halton_index = 0;
        }

        if (halton_points.empty())
        {
            const uint32_t xess_sample_limit = 96;

            uint32_t base_x      = 2;
            uint32_t base_y      = 3;
            uint32_t start_index = 1;
            halton_points.reserve(xess_sample_limit);
            for (uint32_t i = start_index; i < start_index + xess_sample_limit; ++i)
            {
                float jitter_x = get_corput(i, base_x) - 0.5f;
                float jitter_y = get_corput(i, base_y) - 0.5f;
                halton_points.emplace_back(jitter_x, jitter_y);
            }
        }

        auto jitter = halton_points[halton_index];

        intel::jitter.x = jitter.first;
        intel::jitter.y = jitter.second;

        *x =  2.0f * jitter.first  / static_cast<float>(common::resolution_render_width);
        *y = -2.0f * jitter.second / static_cast<float>(common::resolution_render_height);

        uint32_t sample_count_at_current_quality_level = intel::get_sample_count();
        halton_index = (halton_index + 1) % sample_count_at_current_quality_level;
    #endif
    }

    void RHI_VendorTechnology::XeSS_Dispatch(
        RHI_CommandList* cmd_list,
        RHI_Texture* tex_color,
        RHI_Texture* tex_depth,
        RHI_Texture* tex_velocity,
        RHI_Texture* tex_output
    )
    {
    #ifdef _WIN32
        if (!intel::context)
        {
            return;
        }

        // d3d12 xess wants non_pixel_shader_resource for inputs and unordered_access for output,
        // shader_read includes pixel which xess silently strips, desyncing our state tracker
        RHI_Texture* tex_mask = Renderer::GetStandardTexture(Renderer_StandardTexture::Black);
        cmd_list->EnsureComputeShaderResource(tex_color);
        cmd_list->EnsureComputeShaderResource(tex_velocity);
        cmd_list->EnsureComputeShaderResource(tex_depth);
        cmd_list->EnsureComputeShaderResource(tex_mask);
        tex_output->SetLayout(RHI_Image_Layout::General, cmd_list);
        cmd_list->FlushBarriers();

        intel::params_execute                              = {};
        intel::params_execute.pColorTexture                = static_cast<ID3D12Resource*>(tex_color->GetRhiResource());
        intel::params_execute.pVelocityTexture             = static_cast<ID3D12Resource*>(tex_velocity->GetRhiResource());
        intel::params_execute.pDepthTexture                = static_cast<ID3D12Resource*>(tex_depth->GetRhiResource());
        intel::params_execute.pExposureScaleTexture        = nullptr; // ignored, autoexposure flag is set
        intel::params_execute.pResponsivePixelMaskTexture  = static_cast<ID3D12Resource*>(tex_mask->GetRhiResource());
        intel::params_execute.pOutputTexture               = static_cast<ID3D12Resource*>(tex_output->GetRhiResource());
        intel::params_execute.jitterOffsetX                = intel::jitter.x;
        intel::params_execute.jitterOffsetY                = intel::jitter.y;
        intel::params_execute.exposureScale                = intel::exposure_scale;
        intel::params_execute.resetHistory                 = common::reset_history ? 1 : 0;
        intel::params_execute.inputWidth                   = common::resolution_render_width;
        intel::params_execute.inputHeight                  = common::resolution_render_height;
        intel::params_execute.inputColorBase               = { 0, 0 };
        intel::params_execute.inputMotionVectorBase        = { 0, 0 };
        intel::params_execute.inputDepthBase               = { 0, 0 };
        intel::params_execute.inputResponsiveMaskBase      = { 0, 0 };
        intel::params_execute.reserved0                    = { 0, 0 };
        intel::params_execute.outputColorBase              = { 0, 0 };
        intel::params_execute.pDescriptorHeap              = nullptr;
        intel::params_execute.descriptorHeapOffset         = 0;

        common::reset_history = false;

        ID3D12GraphicsCommandList* d3d12_cmd_list = static_cast<ID3D12GraphicsCommandList*>(cmd_list->GetRhiResource());
        xess_result_t result = xessD3D12Execute(intel::context, d3d12_cmd_list, &intel::params_execute);
        if (result != XESS_RESULT_SUCCESS)
        {
            SP_LOG_WARNING("XeSS dispatch failed with result %d", static_cast<int>(result));
        }

        // xess may have rebarriered, adopt the states it leaves so later transitions use a correct state_before
        cmd_list->AdoptComputeShaderResource(tex_color);
        cmd_list->AdoptComputeShaderResource(tex_velocity);
        cmd_list->AdoptComputeShaderResource(tex_depth);
        cmd_list->AdoptComputeShaderResource(tex_mask);
        cmd_list->AdoptUnorderedAccess(tex_output);

        // xess binds its own descriptor heap when pDescriptorHeap is null, restore engine heaps/root sig state
        cmd_list->RestoreAfterExternalPass();
    #endif
    }

    bool RHI_VendorTechnology::NRD_Dispatch(
        RHI_CommandList* cmd_list,
        Nrd_Preset preset,
        RHI_Texture* tex_mv,
        RHI_Texture* tex_normal_roughness,
        RHI_Texture* tex_view_z,
        RHI_Texture* tex_signal_in,
        RHI_Texture* tex_signal_out,
        const math::Vector3* light_direction
    )
    {
    #ifdef _WIN32
        if (!common::cb_frame || !tex_mv || !tex_normal_roughness || !tex_view_z || !tex_signal_in || !tex_signal_out)
        {
            return false;
        }

        if (preset == Nrd_Preset::Shadows && !light_direction)
        {
            return false;
        }

        nvidia::nrd_pool& pool = nvidia::pool_for_preset(preset);
        const uint32_t width  = tex_mv->GetWidth();
        const uint32_t height = tex_mv->GetHeight();
        if (!pool.initialized || pool.width != width || pool.height != height)
        {
            RHI_Device::QueueWaitAll();
            if (!nvidia::pool_create(pool, width, height, preset != Nrd_Preset::Gi))
            {
                return false;
            }
            pool.reset_history = true;
        }

        cmd_list->EnsureComputeShaderResource(tex_mv);
        cmd_list->EnsureComputeShaderResource(tex_normal_roughness);
        cmd_list->EnsureComputeShaderResource(tex_view_z);
        cmd_list->EnsureComputeShaderResource(tex_signal_in);
        tex_signal_out->SetLayout(RHI_Image_Layout::General, cmd_list);
        cmd_list->FlushBarriers();

        if (pool.last_frame_index != common::cb_frame->frame)
        {
            pool.integration.NewFrame();
            pool.last_frame_index = common::cb_frame->frame;
        }

        const bool reset_history = nrd_common::resolve_history_reset(pool.reset_history, common::cb_frame->frame, pool.last_settings_frame);
        pool.reset_history = false;

        nrd::CommonSettings common_settings = {};
        nrd_common::fill_common_settings(common_settings, common::cb_frame, width, height, reset_history);
        if (pool.integration.SetCommonSettings(common_settings) != nrd::Result::SUCCESS)
        {
            SP_LOG_WARNING("NRD SetCommonSettings failed");
            return false;
        }

        nrd::Identifier denoiser_id = nrd_common::id_gi;
        nrd::ResourceSnapshot snapshot = {};
        snapshot.restoreInitialState = true;
        snapshot.SetResource(nrd::ResourceType::IN_MV, nvidia::make_resource(tex_mv));
        snapshot.SetResource(nrd::ResourceType::IN_NORMAL_ROUGHNESS, nvidia::make_resource(tex_normal_roughness));
        snapshot.SetResource(nrd::ResourceType::IN_VIEWZ, nvidia::make_resource(tex_view_z));

        if (preset == Nrd_Preset::Gi)
        {
            denoiser_id = nrd_common::id_gi;
            nrd::ReblurSettings reblur = {};
            nrd_common::fill_preset_gi(reblur);
            if (pool.integration.SetDenoiserSettings(denoiser_id, &reblur) != nrd::Result::SUCCESS)
            {
                return false;
            }
            snapshot.SetResource(nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST, nvidia::make_resource(tex_signal_in));
            snapshot.SetResource(nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST, nvidia::make_resource(tex_signal_out));
        }
        else if (preset == Nrd_Preset::Reflections)
        {
            denoiser_id = nrd_common::id_reflections;
            nrd::ReblurSettings reblur = {};
            nrd_common::fill_preset_reflections(reblur);
            if (pool.integration.SetDenoiserSettings(denoiser_id, &reblur) != nrd::Result::SUCCESS)
            {
                return false;
            }
            snapshot.SetResource(nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST, nvidia::make_resource(tex_signal_in));
            snapshot.SetResource(nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST, nvidia::make_resource(tex_signal_out));
        }
        else
        {
            denoiser_id = nrd_common::id_shadows;
            const float light_dir[3] = { light_direction->x, light_direction->y, light_direction->z };
            nrd::SigmaSettings sigma = {};
            nrd_common::fill_preset_shadows(sigma, light_dir);
            if (pool.integration.SetDenoiserSettings(denoiser_id, &sigma) != nrd::Result::SUCCESS)
            {
                return false;
            }
            snapshot.SetResource(nrd::ResourceType::IN_PENUMBRA, nvidia::make_resource(tex_signal_in));
            snapshot.SetResource(nrd::ResourceType::OUT_SHADOW_TRANSLUCENCY, nvidia::make_resource(tex_signal_out));
        }

        nri::CommandBufferD3D12Desc cmd_desc = {};
        cmd_desc.d3d12CommandList = static_cast<ID3D12GraphicsCommandList*>(cmd_list->GetRhiResource());

        const nrd::Identifier denoisers[] = { denoiser_id };
        pool.integration.DenoiseD3D12(denoisers, 1, cmd_desc, snapshot);

        cmd_list->AdoptComputeShaderResource(tex_mv);
        cmd_list->AdoptComputeShaderResource(tex_normal_roughness);
        cmd_list->AdoptComputeShaderResource(tex_view_z);
        cmd_list->AdoptComputeShaderResource(tex_signal_in);
        cmd_list->AdoptUnorderedAccess(tex_signal_out);
        cmd_list->RestoreAfterExternalPass();
        return true;
    #else
        return false;
    #endif
    }

}
