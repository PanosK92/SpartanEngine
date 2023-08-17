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

//= INCLUDES ==================================
#include "pch.h"
#include "../RHI_AMD_FidelityFX.h"
#include "../RHI_Implementation.h"
#include "../RHI_CommandList.h"
#include "../RHI_Texture.h"
#include "../../World/Components/Camera.h"
SP_WARNINGS_OFF
#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_spd.h>
#include <FidelityFX/host/ffx_fsr2.h>
SP_WARNINGS_ON
//=============================================

//= NAMESPACES ===============
using namespace Spartan::Math;
//============================

namespace Spartan
{
    namespace
    {
        // common
        FfxInterface common_interface = {};

        // spd
        FfxSpdContext            spd_context             = {};
        FfxSpdContextDescription spd_context_description = {};
        bool                     spd_context_created     = false;

        // fsr 2
        FfxFsr2Context fsr2_context                          = {};
        FfxFsr2ContextDescription fsr2_context_description   = {};
        FfxFsr2DispatchDescription fsr2_dispatch_description = {};
        bool fsr2_reset_history                              = false;
        bool fsr2_context_created                            = false;
        uint32_t fsr2_jitter_index                           = 0;

        static void ffx_message_callback(FfxMsgType type, const wchar_t* message)
        {
            if (type == FFX_MESSAGE_TYPE_ERROR)
            {
                SP_LOG_ERROR("AMD FidelityFX: %ls", message);
            }
            else if (type == FFX_MESSAGE_TYPE_WARNING)
            {
                SP_LOG_WARNING("AMD FidelityFX: %ls", message);
            }
        }

        static FfxSurfaceFormat to_ffx_surface_format(const RHI_Format format)
        {
            switch (format)
            {
            case RHI_Format::R32G32B32A32_Float:
                return FFX_SURFACE_FORMAT_R32G32B32A32_FLOAT;
            case RHI_Format::R16G16B16A16_Float:
                return FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT;
            case RHI_Format::R32G32_Float:
                return FFX_SURFACE_FORMAT_R32G32_FLOAT;
            case RHI_Format::R8_Uint:
                return FFX_SURFACE_FORMAT_R8_UINT;
            case RHI_Format::R32_Uint:
                return FFX_SURFACE_FORMAT_R32_UINT;
            case RHI_Format::R8G8B8A8_Unorm:
                return FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;
            case RHI_Format::R11G11B10_Float:
                return FFX_SURFACE_FORMAT_R11G11B10_FLOAT;
            case RHI_Format::R16G16_Float:
                return FFX_SURFACE_FORMAT_R16G16_FLOAT;
            case RHI_Format::R16_Uint:
                return FFX_SURFACE_FORMAT_R16_UINT;
            case RHI_Format::R16_Float:
                return FFX_SURFACE_FORMAT_R16_FLOAT;
            case RHI_Format::R16_Unorm:
                return FFX_SURFACE_FORMAT_R16_UNORM;
            case RHI_Format::R8_Unorm:
                return FFX_SURFACE_FORMAT_R8_UNORM;
            case RHI_Format::R8G8_Unorm:
                return FFX_SURFACE_FORMAT_R8G8_UNORM;
            case RHI_Format::R32_Float:
            case RHI_Format::D32_Float:
                return FFX_SURFACE_FORMAT_R32_FLOAT;
            case RHI_Format::Undefined:
                return FFX_SURFACE_FORMAT_UNKNOWN;
            default:
                SP_ASSERT_MSG(false, "Unsupported format");
                return FFX_SURFACE_FORMAT_UNKNOWN;
            }
        }

        static FfxResource to_ffx_resource(RHI_Texture* texture, const wchar_t* name, FfxResourceUsage usage_additional = static_cast<FfxResourceUsage>(0))
        {
            FfxResourceDescription resource_description = {};
            resource_description.type                   = FfxResourceType::FFX_RESOURCE_TYPE_TEXTURE2D;
            resource_description.width                  = texture->GetWidth();
            resource_description.height                 = texture->GetHeight();
            resource_description.mipCount               = texture->GetMipCount();
            resource_description.depth                  = texture->GetArrayLength(); // depth or array length
            resource_description.format                 = to_ffx_surface_format(texture->GetFormat());
            resource_description.flags                  = FfxResourceFlags::FFX_RESOURCE_FLAGS_NONE;
            resource_description.usage                  = texture->IsDepthFormat() ? FFX_RESOURCE_USAGE_DEPTHTARGET : FFX_RESOURCE_USAGE_READ_ONLY;
            resource_description.usage                  = static_cast<FfxResourceUsage>(resource_description.usage | (texture->IsUav() ? FFX_RESOURCE_USAGE_UAV : 0));
            resource_description.usage                  = static_cast<FfxResourceUsage>(resource_description.usage | usage_additional);

            bool is_shader_read_only_optimal = texture->GetLayout(0) == RHI_Image_Layout::Shader_Read_Only_Optimal;
            FfxResourceStates current_state  = is_shader_read_only_optimal ? FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ : FFX_RESOURCE_STATE_UNORDERED_ACCESS;

            return ffxGetResourceVK(
                static_cast<VkImage>(texture->GetRhiResource()),
                resource_description,
                const_cast<wchar_t*>(name),
                current_state
            );
        }
    }

    void RHI_AMD_FidelityFX::Initialize()
    {
        // common interface
        {
            const size_t size =
                //FFX_SPD_CONTEXT_COUNT +
                FFX_FSR2_CONTEXT_COUNT;

            VkDeviceContext device_context  = {};
            device_context.vkDevice         = RHI_Context::device;
            device_context.vkPhysicalDevice = RHI_Context::device_physical;

            const size_t scratch_buffer_size = ffxGetScratchMemorySizeVK(RHI_Context::device_physical, size);
            void* scratch_buffer             = malloc(scratch_buffer_size);

            SP_ASSERT(ffxGetInterfaceVK(&common_interface, ffxGetDeviceVK(&device_context), scratch_buffer, scratch_buffer_size, size) == FFX_OK);
        }

        // spd
        {
            // interface
            spd_context_description.backendInterface = common_interface;

            // context
            spd_context_description.flags = FFX_SPD_SAMPLER_LINEAR | FFX_SPD_WAVE_INTEROP_WAVE_OPS | FFX_SPD_MATH_PACKED;
            //ffxSpdContextCreate(&spd_context, &spd_context_description);
            //spd_context_created = true;
        }

        // fsr 2
        {
            // interface
            fsr2_context_description.backendInterface = common_interface;

            // context
            // the context is (re)created below, in FSR2_Resize()
        }
    }

    void RHI_AMD_FidelityFX::Destroy()
    {
        // spd
        if (spd_context_created)
        {
            ffxSpdContextDestroy(&spd_context);
            spd_context_created = false;
        }

        // fsr 2
        if (fsr2_context_created)
        {
            ffxFsr2ContextDestroy(&fsr2_context);
            fsr2_context_created = false;
        }

        // common
        if (common_interface.scratchBuffer != nullptr)
        {
            free(common_interface.scratchBuffer);
        }
    }

    void RHI_AMD_FidelityFX::SPD_Dispatch(RHI_CommandList* cmd_list, RHI_Texture* texture)
    {
        FfxSpdDispatchDescription dispatch_description = {};
        dispatch_description.commandList               = ffxGetCommandListVK(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()));
        dispatch_description.resource                  = to_ffx_resource(texture, L"spd_texture", FFX_RESOURCE_USAGE_ARRAYVIEW);

        //SP_ASSERT(ffxSpdContextDispatch(&spd_context, &dispatch_description) == FFX_OK);
    }

    void RHI_AMD_FidelityFX::FSR2_ResetHistory()
    {
        fsr2_reset_history = true;
    }

    void RHI_AMD_FidelityFX::FSR2_GenerateJitterSample(float* x, float* y)
    {
        // Get jitter sample count
        uint32_t resolution_render_x      = static_cast<uint32_t>(fsr2_context_description.maxRenderSize.width);
        uint32_t resolution_output_x      = static_cast<uint32_t>(fsr2_context_description.displaySize.width);
        const int32_t jitter_sample_count = ffxFsr2GetJitterPhaseCount(resolution_render_x, resolution_output_x);

        // Generate jitter sample
        SP_ASSERT(ffxFsr2GetJitterOffset(&fsr2_dispatch_description.jitterOffset.x, &fsr2_dispatch_description.jitterOffset.y, fsr2_jitter_index++, jitter_sample_count) == FFX_OK);

        // Out jitter offset
        *x = fsr2_dispatch_description.jitterOffset.x;
        *y = fsr2_dispatch_description.jitterOffset.y;
    }

    void RHI_AMD_FidelityFX::FSR2_Resize(const Math::Vector2& resolution_render, const Math::Vector2& resolution_output)
    {
        // destroy context
        if (fsr2_context_created)
        {
            ffxFsr2ContextDestroy(&fsr2_context);
            fsr2_context_created = false;
        }

        // create context
        {
            // the maximum size that rendering will be performed at
            fsr2_context_description.maxRenderSize.width  = static_cast<uint32_t>(resolution_render.x);
            fsr2_context_description.maxRenderSize.height = static_cast<uint32_t>(resolution_render.y);

            // the size of the presentation resolution targeted by the upscaling process
            fsr2_context_description.displaySize.width  = static_cast<uint32_t>(resolution_output.x);
            fsr2_context_description.displaySize.height = static_cast<uint32_t>(resolution_output.y);

            // flags
            {
                fsr2_context_description.flags      = FFX_FSR2_ENABLE_DEPTH_INVERTED | FFX_FSR2_ENABLE_AUTO_EXPOSURE | FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE;
                #ifdef DEBUG
                fsr2_context_description.flags     |= FFX_FSR2_ENABLE_DEBUG_CHECKING;
                fsr2_context_description.fpMessage  = &ffx_message_callback;
                #endif
            }

            // context
            ffxFsr2ContextCreate(&fsr2_context, &fsr2_context_description);
            fsr2_context_created = true;
        }

        // reset jitter index
        fsr2_jitter_index = 0;
    }

    void RHI_AMD_FidelityFX::FSR2_Dispatch
    (
        RHI_CommandList* cmd_list,
        RHI_Texture* tex_input,
        RHI_Texture* tex_depth,
        RHI_Texture* tex_velocity,
        RHI_Texture* tex_mask_reactive,
        RHI_Texture* tex_mask_transparency,
        RHI_Texture* tex_output,
        Camera* camera,
        float delta_time_sec,
        float sharpness
    )
    {
        // transition to the appropriate layouts (will only happen if needed)
        tex_input->SetLayout(RHI_Image_Layout::Shader_Read_Only_Optimal, cmd_list);
        tex_depth->SetLayout(RHI_Image_Layout::Shader_Read_Only_Optimal, cmd_list);
        tex_velocity->SetLayout(RHI_Image_Layout::Shader_Read_Only_Optimal, cmd_list);
        tex_mask_reactive->SetLayout(RHI_Image_Layout::Shader_Read_Only_Optimal, cmd_list);
        tex_mask_transparency->SetLayout(RHI_Image_Layout::Shader_Read_Only_Optimal, cmd_list);
        tex_output->SetLayout(RHI_Image_Layout::General, cmd_list);

        // dispatch description
        {
            // resources
            fsr2_dispatch_description.color                      = to_ffx_resource(tex_input,             L"fsr2_color");
            fsr2_dispatch_description.depth                      = to_ffx_resource(tex_depth,             L"fsr2_depth");
            fsr2_dispatch_description.motionVectors              = to_ffx_resource(tex_velocity,          L"fsr2_velocity");
            fsr2_dispatch_description.reactive                   = to_ffx_resource(tex_mask_reactive,     L"fsr2_mask_reactive");
            fsr2_dispatch_description.transparencyAndComposition = to_ffx_resource(tex_mask_transparency, L"fsr2_mask_transparency_and_composition");
            fsr2_dispatch_description.output                     = to_ffx_resource(tex_output,            L"fsr2_output");
            fsr2_dispatch_description.commandList                = ffxGetCommandListVK(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()));

            // configuration
            fsr2_dispatch_description.motionVectorScale.x    = -static_cast<float>(tex_velocity->GetWidth());
            fsr2_dispatch_description.motionVectorScale.y    = -static_cast<float>(tex_velocity->GetHeight());
            fsr2_dispatch_description.reset                  = fsr2_reset_history;       // a boolean value which when set to true, indicates the camera has moved discontinuously
            fsr2_dispatch_description.enableSharpening       = sharpness != 0.0f;
            fsr2_dispatch_description.sharpness              = sharpness;
            fsr2_dispatch_description.frameTimeDelta         = delta_time_sec * 1000.0f; // seconds to milliseconds
            fsr2_dispatch_description.preExposure            = 1.0f;                     // the exposure value if not using FFX_FSR2_ENABLE_AUTO_EXPOSURE
            fsr2_dispatch_description.renderSize.width       = tex_input->GetWidth();    // the resolution that was used for rendering the input resources
            fsr2_dispatch_description.renderSize.height      = tex_input->GetHeight();   // the resolution that was used for rendering the input resources
            fsr2_dispatch_description.cameraNear             = camera->GetFarPlane();    // far as near because we are using reverse-z
            fsr2_dispatch_description.cameraFar              = camera->GetNearPlane();   // near as far because we are using reverse-z
            fsr2_dispatch_description.cameraFovAngleVertical = camera->GetFovVerticalRad();
        }

        // dispatch
        SP_ASSERT(ffxFsr2ContextDispatch(&fsr2_context, &fsr2_dispatch_description) == FFX_OK);
        fsr2_reset_history = false;
    }
}
