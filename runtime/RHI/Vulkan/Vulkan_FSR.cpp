/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "fsr/ffx_fsr2.h"
#include "fsr/vk/ffx_fsr2_vk.h"
#include "../RHI_Implementation.h"
#include "../RHI_CommandList.h"
#include "../../Math/Vector2.h"
#include "../../World/Components/Camera.h"
//========================================

//= NAMESPACES ===============
using namespace Spartan::Math;
//============================

namespace Spartan
{
    FfxFsr2Context RHI_FSR2::m_ffx_fsr2_context;
    FfxFsr2ContextDescription RHI_FSR2::m_ffx_fsr2_context_description;
    FfxFsr2DispatchDescription RHI_FSR2::m_ffx_fsr2_dispatch_description;

    void RHI_FSR2::GenerateJitterSample(float* x, float* y)
    {
        // Get render and output resolution from the context description (safe to do as we are not using dynamic resolution)
        uint32_t resolution_render_x = static_cast<uint32_t>(m_ffx_fsr2_context_description.maxRenderSize.width);
        uint32_t resolution_output_x = static_cast<uint32_t>(m_ffx_fsr2_context_description.displaySize.width);

        // Generate jitter sample
        static uint32_t index = 0; index++;
        const int32_t jitter_phase_count = ffxFsr2GetJitterPhaseCount(resolution_render_x, resolution_output_x);
        ffxFsr2GetJitterOffset(&m_ffx_fsr2_dispatch_description.jitterOffset.x, &m_ffx_fsr2_dispatch_description.jitterOffset.y, index, jitter_phase_count);

        // Out
        *x = m_ffx_fsr2_dispatch_description.jitterOffset.x;
        *y = m_ffx_fsr2_dispatch_description.jitterOffset.y;
    }

    void RHI_FSR2::OnResolutionChange(RHI_Device* rhi_device, const Math::Vector2& resolution_render, const Math::Vector2& resolution_output)
    {
        VkDevice device                  = rhi_device->GetRhiContext()->device;
        VkPhysicalDevice device_physical = rhi_device->GetRhiContext()->device_physical;

        // Callbacks
        if (m_ffx_fsr2_context_description.callbacks.fpCreateDevice == nullptr)
        {
            const size_t scratch_buffer_size = ffxFsr2GetScratchMemorySizeVK(device_physical);
            void* scratch_buffer             = malloc(scratch_buffer_size);
            SP_ASSERT(ffxFsr2GetInterfaceVK(&m_ffx_fsr2_context_description.callbacks, scratch_buffer, scratch_buffer_size, device_physical, vkGetDeviceProcAddr) == FFX_OK);
        }

        // Context
        m_ffx_fsr2_context_description.device               = ffxGetDeviceVK(device);
        m_ffx_fsr2_context_description.maxRenderSize.width  = static_cast<uint32_t>(resolution_render.x);
        m_ffx_fsr2_context_description.maxRenderSize.height = static_cast<uint32_t>(resolution_render.y);
        m_ffx_fsr2_context_description.displaySize.width    = static_cast<uint32_t>(resolution_output.x);
        m_ffx_fsr2_context_description.displaySize.height   = static_cast<uint32_t>(resolution_output.y);
        m_ffx_fsr2_context_description.flags                = FFX_FSR2_ENABLE_DEPTH_INVERTED     |
                                                              FFX_FSR2_ENABLE_AUTO_EXPOSURE      |
                                                              FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE;

        ffxFsr2ContextCreate(&m_ffx_fsr2_context, &m_ffx_fsr2_context_description);
    }

    void RHI_FSR2::Dispatch(RHI_CommandList* cmd_list, RHI_Texture* tex_input, RHI_Texture* tex_depth, RHI_Texture* tex_velocity, RHI_Texture* tex_output, Camera* camera, float delta_time, float sharpness, bool reset)
    {
        // Get render and output resolution from the context description (safe to do as we are not using dynamic resolution)
        uint32_t resolution_render_x = static_cast<uint32_t>(m_ffx_fsr2_context_description.maxRenderSize.width);
        uint32_t resolution_render_y = static_cast<uint32_t>(m_ffx_fsr2_context_description.maxRenderSize.height);
        uint32_t resolution_output_x = static_cast<uint32_t>(m_ffx_fsr2_context_description.displaySize.width);
        uint32_t resolution_output_y = static_cast<uint32_t>(m_ffx_fsr2_context_description.displaySize.height);

        // Define texture names
        wchar_t name_input[]    = L"FSR2_Input";
        wchar_t name_depth[]    = L"FSR2_Depth";
        wchar_t name_velocity[] = L"FSR2_Velocity";
        wchar_t name_exposure[] = L"FSR2_Exposure";
        wchar_t name_output[]   = L"FSR2_Output";

        // Transition to the appropriate texture layouts (will only happen if needed)
        tex_input->SetLayout(RHI_Image_Layout::Shader_Read_Only_Optimal,    cmd_list);
        tex_depth->SetLayout(RHI_Image_Layout::Shader_Read_Only_Optimal,    cmd_list);
        tex_velocity->SetLayout(RHI_Image_Layout::Shader_Read_Only_Optimal, cmd_list);
        tex_output->SetLayout(RHI_Image_Layout::General,                    cmd_list);

        // Fill in the dispatch description
        m_ffx_fsr2_dispatch_description.commandList            = ffxGetCommandListVK(static_cast<VkCommandBuffer>(cmd_list->GetResource()));
        m_ffx_fsr2_dispatch_description.color                  = ffxGetTextureResourceVK(&m_ffx_fsr2_context, static_cast<VkImage>(tex_input->GetRhiResource()),    static_cast<VkImageView>(tex_input->GetRhiSrv()),    resolution_render_x, resolution_render_y, vulkan_format[tex_input->GetFormat()],    name_input, FFX_RESOURCE_STATE_COMPUTE_READ);
        m_ffx_fsr2_dispatch_description.depth                  = ffxGetTextureResourceVK(&m_ffx_fsr2_context, static_cast<VkImage>(tex_depth->GetRhiResource()),    static_cast<VkImageView>(tex_depth->GetRhiSrv()),    resolution_render_x, resolution_render_y, vulkan_format[tex_depth->GetFormat()],    name_depth, FFX_RESOURCE_STATE_COMPUTE_READ);
        m_ffx_fsr2_dispatch_description.motionVectors          = ffxGetTextureResourceVK(&m_ffx_fsr2_context, static_cast<VkImage>(tex_velocity->GetRhiResource()), static_cast<VkImageView>(tex_velocity->GetRhiSrv()), resolution_render_x, resolution_render_y, vulkan_format[tex_velocity->GetFormat()], name_velocity, FFX_RESOURCE_STATE_COMPUTE_READ);
        m_ffx_fsr2_dispatch_description.exposure               = ffxGetTextureResourceVK(&m_ffx_fsr2_context, nullptr,                                              nullptr,                                             1,                   1,                   VK_FORMAT_UNDEFINED,                      name_exposure);
        m_ffx_fsr2_dispatch_description.output                 = ffxGetTextureResourceVK(&m_ffx_fsr2_context, static_cast<VkImage>(tex_output->GetRhiResource()),   static_cast<VkImageView>(tex_output->GetRhiSrv()),   resolution_output_x, resolution_output_y, vulkan_format[tex_output->GetFormat()],   name_output, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        m_ffx_fsr2_dispatch_description.motionVectorScale.x    = -static_cast<float>(resolution_render_x);
        m_ffx_fsr2_dispatch_description.motionVectorScale.y    = -static_cast<float>(resolution_render_y);
        m_ffx_fsr2_dispatch_description.reset                  = reset;                // A boolean value which when set to true, indicates the camera has moved discontinuously.
        m_ffx_fsr2_dispatch_description.enableSharpening       = sharpness != 0.0f;
        m_ffx_fsr2_dispatch_description.sharpness              = sharpness;
        m_ffx_fsr2_dispatch_description.frameTimeDelta         = delta_time * 1000.0f; // Seconds to milliseconds.
        m_ffx_fsr2_dispatch_description.preExposure            = 1.0f;                 // The exposure value if not using FFX_FSR2_ENABLE_AUTO_EXPOSURE.
        m_ffx_fsr2_dispatch_description.renderSize.width       = resolution_render_x;
        m_ffx_fsr2_dispatch_description.renderSize.height      = resolution_render_y;
        m_ffx_fsr2_dispatch_description.cameraNear             = camera->GetNearPlane();
        m_ffx_fsr2_dispatch_description.cameraFar              = camera->GetFarPlane();
        m_ffx_fsr2_dispatch_description.cameraFovAngleVertical = camera->GetFovVerticalRad();

        SP_ASSERT(ffxFsr2ContextDispatch(&m_ffx_fsr2_context, &m_ffx_fsr2_dispatch_description) == FFX_OK);
    }

    void RHI_FSR2::Destroy()
    {
        ffxFsr2ContextDestroy(&m_ffx_fsr2_context);
    }
}
