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
//= INCLUDES =============================
#include "pch.h"
#include "../RHI_FSR2.h"
#include "fsr2/ffx_fsr2.h"
#include "fsr2/vk/ffx_fsr2_vk.h"
#include "../RHI_Implementation.h"
#include "../RHI_CommandList.h"
#include "../../Math/Vector2.h"
#include "../../World/Components/Camera.h"
#include "../Rendering/Renderer.h"
//========================================

//= NAMESPACES ===============
using namespace Spartan::Math;
//============================

namespace Spartan
{
    FfxFsr2Context RHI_FSR2::m_ffx_fsr2_context;
    FfxFsr2ContextDescription RHI_FSR2::m_ffx_fsr2_context_description;
    FfxFsr2DispatchDescription RHI_FSR2::m_ffx_fsr2_dispatch_description;

    static void on_fsr2_message(FfxFsr2MsgType type, const wchar_t* message)
    {
        if (type == FFX_FSR2_MESSAGE_TYPE_ERROR)
        {
            SP_LOG_ERROR("FSR 2: %ls", message);
        }
        else if (type == FFX_FSR2_MESSAGE_TYPE_WARNING)
        {
            SP_LOG_WARNING("FSR 2: %ls", message);
        }
    }

    static FfxResource to_ffx_resource(FfxFsr2Context& fsr2_context, RHI_Texture* texture, const wchar_t* name)
    {
        return ffxGetTextureResourceVK(
            &fsr2_context,
            static_cast<VkImage>(texture->GetRhiResource()),
            static_cast<VkImageView>(texture->GetRhiSrv()),
            texture->GetWidth(),
            texture->GetHeight(),
            vulkan_format[rhi_format_to_index(texture->GetFormat())],
            name,
            texture->GetLayout(0) == RHI_Image_Layout::Shader_Read_Only_Optimal ? FFX_RESOURCE_STATE_COMPUTE_READ : FFX_RESOURCE_STATE_UNORDERED_ACCESS
        );
    }

    void RHI_FSR2::GenerateJitterSample(float* x, float* y)
    {
        // Get jitter sample count
        uint32_t resolution_render_x      = static_cast<uint32_t>(m_ffx_fsr2_context_description.maxRenderSize.width);
        uint32_t resolution_output_x      = static_cast<uint32_t>(m_ffx_fsr2_context_description.displaySize.width);
        const int32_t jitter_sample_count = ffxFsr2GetJitterPhaseCount(resolution_render_x, resolution_output_x);

        // Generate jitter sample
        static uint32_t index = 0; index++;
        SP_ASSERT(ffxFsr2GetJitterOffset(&m_ffx_fsr2_dispatch_description.jitterOffset.x, &m_ffx_fsr2_dispatch_description.jitterOffset.y, index, jitter_sample_count) == FFX_OK);

        // Out jitter offset
        *x = m_ffx_fsr2_dispatch_description.jitterOffset.x;
        *y = m_ffx_fsr2_dispatch_description.jitterOffset.y;
    }

    void RHI_FSR2::OnResolutionChange(const Math::Vector2& resolution_render, const Math::Vector2& resolution_output)
    {
        VkDevice device                  = RHI_Context::device;
        VkPhysicalDevice device_physical = RHI_Context::device_physical;

        // Set callbacks
        if (m_ffx_fsr2_context_description.callbacks.scratchBuffer == nullptr)
        {
            const size_t scratch_buffer_size = ffxFsr2GetScratchMemorySizeVK(device_physical);
            void* scratch_buffer = malloc(scratch_buffer_size);
            SP_ASSERT(ffxFsr2GetInterfaceVK(&m_ffx_fsr2_context_description.callbacks, scratch_buffer, scratch_buffer_size, device_physical, vkGetDeviceProcAddr) == FFX_OK);
        }

        // create context
        {
            m_ffx_fsr2_context_description.device               = ffxGetDeviceVK(device);
            m_ffx_fsr2_context_description.maxRenderSize.width  = static_cast<uint32_t>(resolution_render.x);
            m_ffx_fsr2_context_description.maxRenderSize.height = static_cast<uint32_t>(resolution_render.y);
            m_ffx_fsr2_context_description.displaySize.width    = static_cast<uint32_t>(resolution_output.x);
            m_ffx_fsr2_context_description.displaySize.height   = static_cast<uint32_t>(resolution_output.y);
            m_ffx_fsr2_context_description.flags                = FFX_FSR2_ENABLE_DEPTH_INVERTED     |
                                                                  FFX_FSR2_ENABLE_AUTO_EXPOSURE      |
                                                                  FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE;

            // Debug check
            #ifdef DEBUG
            m_ffx_fsr2_context_description.flags    |= FFX_FSR2_ENABLE_DEBUG_CHECKING;
            m_ffx_fsr2_context_description.fpMessage = &on_fsr2_message;
            #endif

            ffxFsr2ContextCreate(&m_ffx_fsr2_context, &m_ffx_fsr2_context_description);
        }
    }

    void RHI_FSR2::Dispatch
    (
        RHI_CommandList* cmd_list,
        RHI_Texture* tex_input,
        RHI_Texture* tex_depth,
        RHI_Texture* tex_velocity,
        RHI_Texture* tex_mask_reactive,
        RHI_Texture* tex_mask_transparency,
        RHI_Texture* tex_output,
        Camera* camera,
        float delta_time,
        float sharpness,
        bool reset
    )
    {
        // Get render and output resolution from the context description (safe to do as we are not using dynamic resolution)
        uint32_t resolution_render_x = static_cast<uint32_t>(m_ffx_fsr2_context_description.maxRenderSize.width);
        uint32_t resolution_render_y = static_cast<uint32_t>(m_ffx_fsr2_context_description.maxRenderSize.height);
        uint32_t resolution_output_x = static_cast<uint32_t>(m_ffx_fsr2_context_description.displaySize.width);
        uint32_t resolution_output_y = static_cast<uint32_t>(m_ffx_fsr2_context_description.displaySize.height);

        // Transition to the appropriate texture layouts (will only happen if needed)
        tex_input->SetLayout(RHI_Image_Layout::Shader_Read_Only_Optimal, cmd_list);
        tex_depth->SetLayout(RHI_Image_Layout::Shader_Read_Only_Optimal, cmd_list);
        tex_velocity->SetLayout(RHI_Image_Layout::Shader_Read_Only_Optimal, cmd_list);
        tex_mask_reactive->SetLayout(RHI_Image_Layout::Shader_Read_Only_Optimal, cmd_list);
        tex_mask_transparency->SetLayout(RHI_Image_Layout::Shader_Read_Only_Optimal, cmd_list);
        tex_output->SetLayout(RHI_Image_Layout::General, cmd_list);

        // Dispatch description
        {
            // Resources
            m_ffx_fsr2_dispatch_description.color                      = to_ffx_resource(m_ffx_fsr2_context, tex_input,             L"fsr2_color");
            m_ffx_fsr2_dispatch_description.depth                      = to_ffx_resource(m_ffx_fsr2_context, tex_depth,             L"fsr2_depth");
            m_ffx_fsr2_dispatch_description.motionVectors              = to_ffx_resource(m_ffx_fsr2_context, tex_velocity,          L"fsr2_velocity");
            m_ffx_fsr2_dispatch_description.reactive                   = to_ffx_resource(m_ffx_fsr2_context, tex_mask_reactive,     L"fsr2_mask_reactive");
            m_ffx_fsr2_dispatch_description.transparencyAndComposition = to_ffx_resource(m_ffx_fsr2_context, tex_mask_transparency, L"fsr2_mask_transparency_and_composition");
            m_ffx_fsr2_dispatch_description.output                     = to_ffx_resource(m_ffx_fsr2_context, tex_output,            L"fsr2_output");
            m_ffx_fsr2_dispatch_description.exposure                   = ffxGetTextureResourceVK(&m_ffx_fsr2_context, nullptr, nullptr, 1, 1, VK_FORMAT_UNDEFINED, L"fsr2_exposure");
            m_ffx_fsr2_dispatch_description.commandList                = ffxGetCommandListVK(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()));

            // Configuration
            m_ffx_fsr2_dispatch_description.motionVectorScale.x    = -static_cast<float>(resolution_render_x);
            m_ffx_fsr2_dispatch_description.motionVectorScale.y    = -static_cast<float>(resolution_render_y);
            m_ffx_fsr2_dispatch_description.reset                  = reset;                  // A boolean value which when set to true, indicates the camera has moved discontinuously.
            m_ffx_fsr2_dispatch_description.enableSharpening       = sharpness != 0.0f;      
            m_ffx_fsr2_dispatch_description.sharpness              = sharpness;              
            m_ffx_fsr2_dispatch_description.frameTimeDelta         = delta_time * 1000.0f;   // Seconds to milliseconds.
            m_ffx_fsr2_dispatch_description.preExposure            = 1.0f;                   // The exposure value if not using FFX_FSR2_ENABLE_AUTO_EXPOSURE.
            m_ffx_fsr2_dispatch_description.renderSize.width       = resolution_render_x;    
            m_ffx_fsr2_dispatch_description.renderSize.height      = resolution_render_y;    
            m_ffx_fsr2_dispatch_description.cameraNear             = camera->GetFarPlane();  // far for near because we are using reverse-z.
            m_ffx_fsr2_dispatch_description.cameraFar              = camera->GetNearPlane(); // near for far because we are using reverse-z.
            m_ffx_fsr2_dispatch_description.cameraFovAngleVertical = camera->GetFovVerticalRad();
        }

        SP_ASSERT(ffxFsr2ContextDispatch(&m_ffx_fsr2_context, &m_ffx_fsr2_dispatch_description) == FFX_OK);
    }

    void RHI_FSR2::Destroy()
    {
        ffxFsr2ContextDestroy(&m_ffx_fsr2_context);
    }
}
