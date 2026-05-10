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
#include "../RHI_VendorTechnology.h"
#include "../RHI_Implementation.h"
#include "../RHI_CommandList.h"
#include "../RHI_Device.h"
#include "../RHI_Texture.h"
#include "../../Rendering/Renderer.h"
#include "../../Rendering/Color.h"
#include "../../World/World.h"
#include "../../World/Components/Camera.h"
SP_WARNINGS_OFF
#ifdef _WIN32
#include <xess/xess_d3d12.h>
#endif
SP_WARNINGS_ON
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

        RHI_Texture* get_upscaler_exposure_texture(RHI_CommandList* cmd_list)
        {
            RHI_Texture* texture = Renderer::GetRenderTarget(Renderer_RenderTarget::auto_exposure_previous);
            SP_ASSERT(texture != nullptr);

            const bool auto_exposure_enabled = cvar_auto_exposure_adaptation_speed.GetValue() > 0.0f;
            const float camera_exposure      = World::GetCamera() ? World::GetCamera()->GetExposure() : 1.0f;
            const float exposure             = max(camera_exposure, 0.000001f);

            if (!auto_exposure_enabled || reset_history)
            {
                cmd_list->ClearTexture(texture, Color(exposure, exposure, exposure, 1.0f));
            }

            texture->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);

            return texture;
        }
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
                return;

            context_destroy();
            SP_ASSERT(xessD3D12CreateContext(RHI_Context::device, &context) == xess_result_t::XESS_RESULT_SUCCESS);

            // calculate the scaling factor using the base (unscaled) render resolution
            uint32_t render_area = common::resolution_render_max_width * common::resolution_render_max_height;
            uint32_t output_area = common::resolution_output_width * common::resolution_output_height;
            float scale_factor   = static_cast<float>(render_area) / static_cast<float>(output_area);

            intel::params_init                      = {};
            intel::params_init.outputResolution.x   = common::resolution_output_width;
            intel::params_init.outputResolution.y   = common::resolution_output_height;
            intel::params_init.qualitySetting       = intel::get_quality(scale_factor);
            intel::params_init.initFlags            = XESS_INIT_FLAG_USE_NDC_VELOCITY | XESS_INIT_FLAG_INVERTED_DEPTH | XESS_INIT_FLAG_EXPOSURE_SCALE_TEXTURE;
            intel::params_init.creationNodeMask     = 0;
            intel::params_init.visibleNodeMask      = 0;
            intel::params_init.pTempBufferHeap      = nullptr;
            intel::params_init.bufferHeapOffset     = 0;
            intel::params_init.pTempTextureHeap     = nullptr;
            intel::params_init.textureHeapOffset    = 0;
            intel::params_init.pPipelineLibrary     = static_cast<ID3D12PipelineLibrary*>(RHI_Device::GetPipelineCache());
            SP_ASSERT(xessD3D12Init(intel::context, &intel::params_init) == xess_result_t::XESS_RESULT_SUCCESS);

            SP_ASSERT(xessSetVelocityScale(intel::context, -1.0f, -1.0f) == xess_result_t::XESS_RESULT_SUCCESS);
            SP_ASSERT(xessSetMaxResponsiveMaskValue(intel::context, intel::responsive_mask_value_max) == xess_result_t::XESS_RESULT_SUCCESS);
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
    #endif

    void RHI_VendorTechnology::Initialize()
    {

    }

    void RHI_VendorTechnology::Shutdown()
    {
    #ifdef _WIN32
        intel::context_destroy();
    #endif
    }

    void RHI_VendorTechnology::Tick(Cb_Frame* cb_frame, const Vector2& resolution_render, const Vector2& resolution_output, const float resolution_scale)
    {
    #ifdef _WIN32
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
        }
    #endif
    }

    void RHI_VendorTechnology::ResetHistory()
    {
    #ifdef _WIN32
        common::reset_history = true;
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
            return;

        RHI_Texture* tex_exposure = common::get_upscaler_exposure_texture(cmd_list);

        // d3d12 xess wants NON_PIXEL_SHADER_RESOURCE for inputs and UNORDERED_ACCESS for output
        // SetLayout to General gives us read+write states; xess only reads inputs and writes output
        tex_color->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
        tex_velocity->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
        tex_depth->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
        tex_output->SetLayout(RHI_Image_Layout::General, cmd_list);
        cmd_list->FlushBarriers();

        intel::params_execute                              = {};
        intel::params_execute.pColorTexture                = static_cast<ID3D12Resource*>(tex_color->GetRhiResource());
        intel::params_execute.pVelocityTexture             = static_cast<ID3D12Resource*>(tex_velocity->GetRhiResource());
        intel::params_execute.pDepthTexture                = static_cast<ID3D12Resource*>(tex_depth->GetRhiResource());
        intel::params_execute.pExposureScaleTexture        = static_cast<ID3D12Resource*>(tex_exposure->GetRhiResource());
        intel::params_execute.pResponsivePixelMaskTexture  = static_cast<ID3D12Resource*>(Renderer::GetStandardTexture(Renderer_StandardTexture::Black)->GetRhiResource());
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
        SP_ASSERT(result == XESS_RESULT_SUCCESS);
    #endif
    }

    void RHI_VendorTechnology::FSR3_GenerateJitterSample(float* x, float* y)
    {
        // not implemented on d3d12, no jitter
        if (x) *x = 0.0f;
        if (y) *y = 0.0f;
    }

    void RHI_VendorTechnology::FSR3_Dispatch
    (
        RHI_CommandList* cmd_list,
        Camera* camera,
        const float delta_time_sec,
        const float sharpness,
        RHI_Texture* tex_color,
        RHI_Texture* tex_depth,
        RHI_Texture* tex_velocity,
        RHI_Texture* tex_output
    )
    {
        // not implemented on d3d12, fall back to a passthrough blit so the renderer still produces an image
        static bool warned = false;
        if (!warned)
        {
            SP_LOG_WARNING("FSR3 on D3D12 not implemented, falling back to passthrough");
            warned = true;
        }

        if (cmd_list && tex_color && tex_output)
        {
            cmd_list->Blit(tex_color, tex_output, false);
        }
    }

}
