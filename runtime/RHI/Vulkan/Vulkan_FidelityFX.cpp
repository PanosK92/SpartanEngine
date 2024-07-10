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

//= INCLUDES ==================================
#include "pch.h"
#include "../RHI_FidelityFX.h"
#include "../RHI_Implementation.h"
#include "../RHI_CommandList.h"
#include "../RHI_Texture.h"
#include "../../World/Components/Camera.h"
SP_WARNINGS_OFF
#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_fsr3.h>
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
        FfxInterface ffx_interface = {};

        // fsr 3
        FfxFsr3UpscalerContext fsr3_context                          = {};
        FfxFsr3UpscalerContextDescription fsr3_description_context   = {};
        FfxFsr3UpscalerDispatchDescription fsr3_description_dispatch = {};
        bool fsr3_context_created                                    = false;
        uint32_t fsr3_jitter_index                                   = 0;

        void ffx_message_callback(FfxMsgType type, const wchar_t* message)
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

        FfxSurfaceFormat to_ffx_surface_format(const RHI_Format format)
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
            case RHI_Format::Max:
                return FFX_SURFACE_FORMAT_UNKNOWN;
            default:
                SP_ASSERT_MSG(false, "Unsupported format");
                return FFX_SURFACE_FORMAT_UNKNOWN;
            }
        }

        FfxResource to_ffx_resource(RHI_Texture* texture, const wchar_t* name, FfxResourceUsage usage_additional = static_cast<FfxResourceUsage>(0))
        {
            FfxResourceDescription resource_description = {};
            resource_description.type                   = FfxResourceType::FFX_RESOURCE_TYPE_TEXTURE2D;
            resource_description.width                  = texture->GetWidth();
            resource_description.height                 = texture->GetHeight();
            resource_description.mipCount               = texture->GetMipCount();
            resource_description.depth                  = texture->GetArrayLength(); // depth or array length
            resource_description.format                 = to_ffx_surface_format(texture->GetFormat());
            resource_description.flags                  = FfxResourceFlags::FFX_RESOURCE_FLAGS_NONE;
            resource_description.usage                  = FFX_RESOURCE_USAGE_READ_ONLY;
            resource_description.usage                  = static_cast<FfxResourceUsage>(resource_description.usage | (texture->IsDepthFormat() ? FFX_RESOURCE_USAGE_DEPTHTARGET : 0));
            resource_description.usage                  = static_cast<FfxResourceUsage>(resource_description.usage | (texture->IsUav() ? FFX_RESOURCE_USAGE_UAV : 0));
            resource_description.usage                  = static_cast<FfxResourceUsage>(resource_description.usage | usage_additional);

            bool is_shader_read_only_optimal = texture->GetLayout(0) == RHI_Image_Layout::Shader_Read;
            FfxResourceStates current_state  = is_shader_read_only_optimal ? FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ : FFX_RESOURCE_STATE_UNORDERED_ACCESS;

            return ffxGetResourceVK(
                static_cast<VkImage>(texture->GetRhiResource()),
                resource_description,
                const_cast<wchar_t*>(name),
                current_state
            );
        }
    }

    void RHI_FidelityFX::Initialize()
    {
        // ffx interface
        {
            const size_t countext_count = FFX_FSR3_CONTEXT_COUNT;

            VkDeviceContext device_context  = {};
            device_context.vkDevice         = RHI_Context::device;
            device_context.vkPhysicalDevice = RHI_Context::device_physical;
            device_context.vkDeviceProcAddr = vkGetDeviceProcAddr;

            const size_t scratch_buffer_size = ffxGetScratchMemorySizeVK(RHI_Context::device_physical, countext_count);
            void* scratch_buffer             = calloc(1, scratch_buffer_size);

            FfxErrorCode result = ffxGetInterfaceVK(&ffx_interface, ffxGetDeviceVK(&device_context), scratch_buffer, scratch_buffer_size, countext_count);
            SP_ASSERT(result == FFX_OK);
        }

        // fsr 3
        {
            // interface
            fsr3_description_context.backendInterface = ffx_interface;

            // context
            // the context is created in FSR3_Resize()
        }
    }

    void RHI_FidelityFX::Shutdown()
    {
        // fsr 3
        if (fsr3_context_created)
        {
            ffxFsr3UpscalerContextDestroy(&fsr3_context);
            fsr3_context_created = false;
        }

        // ffx interface
        if (ffx_interface.scratchBuffer != nullptr)
        {
            free(ffx_interface.scratchBuffer);
        }
    }

    void RHI_FidelityFX::FSR3_ResetHistory()
    {
        fsr3_description_dispatch.reset = true;
    }

    void RHI_FidelityFX::FSR3_GenerateJitterSample(float* x, float* y)
    {
        // get jitter phase count
        const uint32_t resolution_render_x = static_cast<uint32_t>(fsr3_description_context.maxRenderSize.width);
        const uint32_t resolution_render_y = static_cast<uint32_t>(fsr3_description_context.maxRenderSize.height);
        const int32_t jitter_phase_count   = ffxFsr3GetJitterPhaseCount(resolution_render_x, resolution_render_x);

        // ensure fsr3_jitter_index is properly wrapped around the jitter_phase_count
        fsr3_jitter_index = (fsr3_jitter_index + 1) % jitter_phase_count;

        // generate jitter sample
        FfxErrorCode result = ffxFsr3GetJitterOffset(&fsr3_description_dispatch.jitterOffset.x, &fsr3_description_dispatch.jitterOffset.y, fsr3_jitter_index, jitter_phase_count);
        SP_ASSERT(result == FFX_OK);

        // adjust the jitter offset for the projection matrix, based on the function comments
        *x =  2.0f * fsr3_description_dispatch.jitterOffset.x / resolution_render_x;
        *y = -2.0f * fsr3_description_dispatch.jitterOffset.y / resolution_render_y;
    }

    void RHI_FidelityFX::FSR3_Resize(const Vector2& resolution_render, const Vector2& resolution_output)
    {
        // destroy context
        if (fsr3_context_created)
        {
            ffxFsr3UpscalerContextDestroy(&fsr3_context);
            fsr3_context_created = false;
        }

        // create context
        {
            // max render size
            fsr3_description_context.maxRenderSize.width  = static_cast<uint32_t>(resolution_render.x);
            fsr3_description_context.maxRenderSize.height = static_cast<uint32_t>(resolution_render.y);

            // max output size
            fsr3_description_context.maxUpscaleSize.width  = static_cast<uint32_t>(resolution_output.x);
            fsr3_description_context.maxUpscaleSize.height = static_cast<uint32_t>(resolution_output.y);

            // flags
            {
                fsr3_description_context.flags      = 0;
                fsr3_description_context.flags     |= FFX_FSR3_ENABLE_UPSCALING_ONLY | FFX_FSR3_ENABLE_DEPTH_INVERTED | FFX_FSR3_ENABLE_DYNAMIC_RESOLUTION;
                fsr3_description_context.flags     |= FFX_FSR3_ENABLE_HIGH_DYNAMIC_RANGE; // hdr input
                #ifdef DEBUG
                fsr3_description_context.flags     |= FFX_FSR3_ENABLE_DEBUG_CHECKING;
                fsr3_description_context.fpMessage  = &ffx_message_callback;
                #endif
            }

            // context
            ffxFsr3UpscalerContextCreate(&fsr3_context, &fsr3_description_context);
            fsr3_context_created = true;
        }

        // reset jitter index
        fsr3_jitter_index = 0;
    }

    void RHI_FidelityFX::FSR3_Dispatch
    (
        RHI_CommandList* cmd_list,
        RHI_Texture* tex_color,
        RHI_Texture* tex_depth,
        RHI_Texture* tex_velocity,
        RHI_Texture* tex_color_opaque,
        RHI_Texture* tex_reactive,
        RHI_Texture* tex_output,
        Camera* camera,
        const float delta_time_sec,
        const float sharpness,
        const float exposure,
        const float resolution_scale
    )
    {
        // transition to the appropriate layouts (will only happen if needed)
        {
            tex_color->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
            tex_color_opaque->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
            tex_depth->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
            tex_velocity->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
            tex_output->SetLayout(RHI_Image_Layout::General, cmd_list);
            cmd_list->InsertPendingBarrierGroup();
        }

        // generate reactive mask
        {
            FfxFsr3UpscalerGenerateReactiveDescription fsr3_reactive_mask_description = {};
            fsr3_reactive_mask_description.commandList                                = ffxGetCommandListVK(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()));
            fsr3_reactive_mask_description.colorOpaqueOnly                            = to_ffx_resource(tex_color_opaque, L"fsr3_color_opaque");
            fsr3_reactive_mask_description.colorPreUpscale                            = to_ffx_resource(tex_color, L"fsr3_color");
            fsr3_reactive_mask_description.outReactive                                = to_ffx_resource(tex_reactive, L"fsr3_reactive");
            fsr3_reactive_mask_description.renderSize.width                           = static_cast<uint32_t>(tex_velocity->GetWidth() * resolution_scale);
            fsr3_reactive_mask_description.renderSize.height                          = static_cast<uint32_t>(tex_velocity->GetHeight() * resolution_scale);
            fsr3_reactive_mask_description.scale                                      = 1.0f; // global multiplier for reactivity, higher values increase overall reactivity
            fsr3_reactive_mask_description.cutoffThreshold                            = 0.8f; // difference threshold, lower values make more pixels reactive
            fsr3_reactive_mask_description.binaryValue                                = 1.0f; // value assigned to reactive pixels in the mask (typically 1.0)
            fsr3_reactive_mask_description.flags                                      = 0;

            SP_ASSERT(ffxFsr3UpscalerContextGenerateReactiveMask(&fsr3_context, &fsr3_reactive_mask_description) == FFX_OK);
        }

        // upscale
        {
            // set resources
            {
                fsr3_description_dispatch.color           = to_ffx_resource(tex_color,    L"fsr3_color");
                fsr3_description_dispatch.depth           = to_ffx_resource(tex_depth,    L"fsr3_depth");
                fsr3_description_dispatch.motionVectors   = to_ffx_resource(tex_velocity, L"fsr3_velocity");
                fsr3_description_dispatch.reactive        = to_ffx_resource(tex_reactive, L"fsr3_reactive");
                fsr3_description_dispatch.output          = to_ffx_resource(tex_output,   L"fsr3_output");
                fsr3_description_dispatch.commandList     = ffxGetCommandListVK(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()));
            }

            // configure
            fsr3_description_dispatch.motionVectorScale.x    = -static_cast<float>(tex_velocity->GetWidth());
            fsr3_description_dispatch.motionVectorScale.y    = -static_cast<float>(tex_velocity->GetHeight());
            fsr3_description_dispatch.enableSharpening       = sharpness != 0.0f;
            fsr3_description_dispatch.sharpness              = sharpness;
            fsr3_description_dispatch.frameTimeDelta         = delta_time_sec * 1000.0f;                                            // seconds to milliseconds
            fsr3_description_dispatch.preExposure            = exposure;                                                            // the exposure value if not using FFX_FSR3_ENABLE_AUTO_EXPOSURE
            fsr3_description_dispatch.renderSize.width       = static_cast<uint32_t>(tex_velocity->GetWidth() * resolution_scale);  // viewport size
            fsr3_description_dispatch.renderSize.height      = static_cast<uint32_t>(tex_velocity->GetHeight() * resolution_scale); // viewport size
            fsr3_description_dispatch.cameraNear             = camera->GetFarPlane();                                               // far as near because we are using reverse-z
            fsr3_description_dispatch.cameraFar              = camera->GetNearPlane();                                              // near as far because we are using reverse-z
            fsr3_description_dispatch.cameraFovAngleVertical = camera->GetFovVerticalRad();

            // dispatch
            SP_ASSERT(ffxFsr3UpscalerContextDispatch(&fsr3_context, &fsr3_description_dispatch) == FFX_OK);
            fsr3_description_dispatch.reset = false;
        }
    }
}
