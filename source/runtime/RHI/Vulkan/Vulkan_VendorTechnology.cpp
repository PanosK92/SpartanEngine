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


//= INCLUDES ==================================
#include "pch.h"
#include "../RHI_VendorTechnology.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_Queue.h"
#include "../RHI_Pipeline.h"
#include "../RHI_Shader.h"
#include "../Rendering/Renderer.h"
#include "../../World/World.h"
#include "../World/Components/Camera.h"
SP_WARNINGS_OFF
#ifdef _WIN32
#include <xess/xess_vk.h>
#endif
SP_WARNINGS_ON
//=============================================

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
        uint32_t resolution_render_max_width  = 0; // unscaled base render resolution, used for context creation
        uint32_t resolution_render_max_height = 0;
        uint32_t resolution_output_width      = 0;
        uint32_t resolution_output_height     = 0;
        bool reset_history                    = false;
        float resolution_scale                = 1.0f;
    }

    namespace intel
    {
        xess_context_handle_t context           = nullptr;
        xess_vk_init_params_t params_init       = {};
        xess_vk_execute_params_t params_execute = {};
        Vector2 jitter                          = Vector2::Zero;
        const float responsive_mask_value_max   = 0.05f;
        xess_quality_settings_t quality         = XESS_QUALITY_SETTING_BALANCED;
        float quality_scale_per_dim             = 2.0f; // xess 1.3+ balanced scale, refreshed on context_create

        xess_quality_settings_t get_quality(const float area_ratio, float* picked_scale_per_dim)
        {
            // xess 1.3+ fixed resolution scaling factors per preset (per dimension)
            struct QualitySetting
            {
                xess_quality_settings_t quality;
                float scale_per_dim;
            };

            const QualitySetting quality_settings[] =
            {
                { XESS_QUALITY_SETTING_ULTRA_PERFORMANCE,  3.0f }, // ~11.1% area
                { XESS_QUALITY_SETTING_PERFORMANCE,        2.3f }, // ~18.9% area
                { XESS_QUALITY_SETTING_BALANCED,           2.0f }, // 25% area
                { XESS_QUALITY_SETTING_QUALITY,            1.7f }, // ~34.6% area
                { XESS_QUALITY_SETTING_ULTRA_QUALITY,      1.5f }, // ~44.4% area
                { XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS, 1.3f }, // ~59.2% area
                { XESS_QUALITY_SETTING_AA,                 1.0f }  // native, no upscaling
            };

            // pick the preset whose area ratio (1 / scale^2) is closest to the requested one
            quality                  = XESS_QUALITY_SETTING_BALANCED;
            float picked             = 2.0f;
            float min_difference     = numeric_limits<float>::max();

            for (const auto& setting : quality_settings)
            {
                float setting_area = 1.0f / (setting.scale_per_dim * setting.scale_per_dim);
                float difference   = abs(area_ratio - setting_area);
                if (difference < min_difference)
                {
                    min_difference = difference;
                    quality        = setting.quality;
                    picked         = setting.scale_per_dim;
                }
            }

            if (picked_scale_per_dim)
                *picked_scale_per_dim = picked;

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

            // create
            context_destroy();
            SP_ASSERT(xessVKCreateContext(RHI_Context::instance, RHI_Context::device_physical, RHI_Context::device, &context) == xess_result_t::XESS_RESULT_SUCCESS);

            // pick the preset that best matches the base render-to-output area ratio
            uint32_t render_area = common::resolution_render_max_width * common::resolution_render_max_height;
            uint32_t output_area = common::resolution_output_width * common::resolution_output_height;
            float area_ratio     = static_cast<float>(render_area) / static_cast<float>(output_area);
            intel::quality       = intel::get_quality(area_ratio, &intel::quality_scale_per_dim);

            // initialize, exposure is provided as a scalar value, no exposure texture flag
            intel::params_init.outputResolution.x = common::resolution_output_width;
            intel::params_init.outputResolution.y = common::resolution_output_height;
            intel::params_init.qualitySetting     = intel::quality;
            intel::params_init.initFlags          = XESS_INIT_FLAG_USE_NDC_VELOCITY | XESS_INIT_FLAG_INVERTED_DEPTH;
            intel::params_init.creationNodeMask   = 0;
            intel::params_init.visibleNodeMask    = 0;
            intel::params_init.tempBufferHeap     = VK_NULL_HANDLE;
            intel::params_init.bufferHeapOffset   = 0;
            intel::params_init.tempTextureHeap    = VK_NULL_HANDLE;
            intel::params_init.textureHeapOffset  = 0;
            intel::params_init.pipelineCache      = VK_NULL_HANDLE;
            SP_ASSERT(xessVKInit(intel::context, &intel::params_init) == xess_result_t::XESS_RESULT_SUCCESS);

            // engine velocity is current_ndc - previous_ndc, xess 1.3+ wants previous - current
            SP_ASSERT(xessSetVelocityScale(intel::context, -1.0f, -1.0f) == xess_result_t::XESS_RESULT_SUCCESS);
            SP_ASSERT(xessSetMaxResponsiveMaskValue(intel::context, intel::responsive_mask_value_max) == xess_result_t::XESS_RESULT_SUCCESS);
        }

        uint32_t get_jitter_phase_count()
        {
            // intel xess developer guide: minimum required phase count is 8 * scale^2
            const float scale = quality_scale_per_dim;
            uint32_t count    = static_cast<uint32_t>(ceil(8.0f * scale * scale));
            return max(count, 8u);
        }

        xess_vk_image_view_info to_xess_image_view(RHI_Texture* texture)
        {
            xess_vk_image_view_info info          = {};
            info.image                            = static_cast<VkImage>(texture->GetRhiResource()); 
            info.imageView                        = static_cast<VkImageView>(texture->GetRhiSrv());
            info.subresourceRange                 = {};
            info.subresourceRange.aspectMask      = texture->IsDepthFormat() ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
            info.subresourceRange.aspectMask     |= texture->IsStencilFormat() ? VK_IMAGE_ASPECT_STENCIL_BIT : 0;
            info.subresourceRange.baseMipLevel    = 0;
            info.subresourceRange.levelCount      = 1;
            info.subresourceRange.baseArrayLayer  = 0;
            info.subresourceRange.layerCount      = 1;
            info.format                           = vulkan_format[static_cast<uint32_t>(texture->GetFormat())];
            info.width                            = texture->GetWidth();
            info.height                           = texture->GetHeight();
        
            return info;
        }
    }
    #endif // _WIN32

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

        // update per-frame scaled render dimensions (used by dispatch)
        common::resolution_render_width  = Renderer::GetScaledDimension(static_cast<uint32_t>(resolution_render.x), resolution_scale);
        common::resolution_render_height = Renderer::GetScaledDimension(static_cast<uint32_t>(resolution_render.y), resolution_scale);

        // check if the base (unscaled) render or output resolution changed
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
        // halton-2,3 sequence built once, length is the maximum xess could ask for
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
            // upper bound matches xess ultra-performance worst case, 8 * 3.0^2 = 72, padded to 128
            const uint32_t halton_max = 128;
            halton_points.reserve(halton_max);
            for (uint32_t i = 1; i <= halton_max; ++i)
            {
                halton_points.emplace_back(get_corput(i, 2) - 0.5f, get_corput(i, 3) - 0.5f);
            }
        }

        // a history reset should restart the jitter sequence so the first accumulated
        // frame samples a known position rather than continuing from an arbitrary phase
        if (common::reset_history)
            halton_index = 0;

        // pixel-space jitter in [-0.5, 0.5], xess consumes this directly
        const auto& jitter = halton_points[halton_index % halton_points.size()];
        intel::jitter.x    = jitter.first;
        intel::jitter.y    = jitter.second;

        // ndc-space jitter for the projection matrix, y is flipped to match dx-style ndc
        *x =  2.0f * jitter.first  / static_cast<float>(common::resolution_render_width);
        *y = -2.0f * jitter.second / static_cast<float>(common::resolution_render_height);

        // phase count is per intel's formula, ensures sub-pixel coverage at the current scale
        const uint32_t phase_count = intel::get_jitter_phase_count();
        halton_index = (halton_index + 1) % phase_count;
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

        tex_color->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
        tex_velocity->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
        tex_depth->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
        tex_output->SetLayout(RHI_Image_Layout::General, cmd_list);
        cmd_list->FlushBarriers();

        // frame_render is unexposed hdr linear, xess wants 1.0 = sdr-white so we
        // hand it the camera/auto-exposure value as a scalar, no texture indirection
        const float camera_exposure = World::GetCamera() ? World::GetCamera()->GetExposure() : 1.0f;
        const float exposure_scale  = max(camera_exposure, 0.000001f);

        intel::params_execute.colorTexture               = intel::to_xess_image_view(tex_color);
        intel::params_execute.depthTexture               = intel::to_xess_image_view(tex_depth);
        intel::params_execute.velocityTexture            = intel::to_xess_image_view(tex_velocity);
        intel::params_execute.outputTexture              = intel::to_xess_image_view(tex_output);
        intel::params_execute.exposureScaleTexture       = {};
        intel::params_execute.responsivePixelMaskTexture = intel::to_xess_image_view(Renderer::GetStandardTexture(Renderer_StandardTexture::Black)); // neutralize and control via float
        intel::params_execute.jitterOffsetX              = intel::jitter.x;
        intel::params_execute.jitterOffsetY              = intel::jitter.y;
        intel::params_execute.exposureScale              = exposure_scale;
        intel::params_execute.inputWidth                 = common::resolution_render_width;
        intel::params_execute.inputHeight                = common::resolution_render_height;
        intel::params_execute.inputColorBase             = { 0, 0 };
        intel::params_execute.inputMotionVectorBase      = { 0, 0 };
        intel::params_execute.inputDepthBase             = { 0, 0 };
        intel::params_execute.inputResponsiveMaskBase    = { 0, 0 };
        intel::params_execute.outputColorBase            = { 0, 0 };
        intel::params_execute.reserved0                  = { 0, 0 };

        intel::params_execute.resetHistory = common::reset_history ? 1u : 0u;
        common::reset_history              = false;

        _xess_result_t result = xessVKExecute(intel::context, static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()), &intel::params_execute);
        SP_ASSERT(result == XESS_RESULT_SUCCESS);
    #endif
    }
}
