/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =========================
#include "pch.h"
#include "../RHI_FrameGeneration.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_CommandList.h"
#include "../RHI_SwapChain.h"
#include "../RHI_Texture.h"
#include "../../display/Display.h"
#include "../../rendering/Renderer_Internal.h"
#include "../../world/World.h"
#include "../../world/Entity.h"
#include "../../world/components/Camera.h"
#ifdef _WIN32
SP_WARNINGS_OFF
#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_api_loader.h>
#include <ffx_api/ffx_framegeneration.h>
#include <ffx_api/vk/ffx_api_vk.h>
SP_WARNINGS_ON
#endif
//====================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

// forward declarations from vulkan_device.cpp
namespace spartan::frame_generation_vk_queues
{
    bool available();
    void* present_queue();
    uint32_t present_family();
    void* acquire_queue();
    uint32_t acquire_family();
}

namespace spartan
{
    #ifdef _WIN32
    namespace
    {
        HMODULE module      = nullptr;
        bool load_attempted = false;
        ffxFunctions ffx    = {};

        ffxContext context_swapchain = nullptr;
        ffxContext context_fg        = nullptr;
        VkSwapchainKHR swapchain_fg  = VK_NULL_HANDLE;
        ffxQueryDescSwapchainReplacementFunctionsVK functions = {};

        uint64_t frame_id          = 0;
        bool prepared_this_frame   = false;
        bool logged_create_error   = false;
        bool logged_dispatch_error = false;

        struct context_params
        {
            uint32_t display_width  = 0;
            uint32_t display_height = 0;
            uint32_t render_width   = 0;
            uint32_t render_height  = 0;
            uint32_t format         = 0;
            bool hdr                = false;

            bool operator==(const context_params& other) const = default;
        };
        context_params created;

        bool load()
        {
            if (load_attempted)
            {
                return module != nullptr;
            }
            load_attempted = true;

            if (!frame_generation_vk_queues::available())
            {
                return false;
            }

            module = LoadLibraryW(L"amd_fidelityfx_vk.dll");
            if (module)
            {
                ffxLoadFunctions(&ffx, module);
            }

            if (!module || !ffx.CreateContext || !ffx.DestroyContext || !ffx.Configure || !ffx.Query || !ffx.Dispatch)
            {
                SP_LOG_WARNING("FSR frame generation is unavailable, amd_fidelityfx_vk.dll is missing, run the setup to download it");
                if (module)
                {
                    FreeLibrary(module);
                    module = nullptr;
                }
                return false;
            }

            SP_LOG_INFO("FSR frame generation runtime loaded (amd_fidelityfx_vk.dll)");
            return true;
        }

        uint32_t to_ffx_format(const RHI_Format format)
        {
            switch (format)
            {
                case RHI_Format::R8G8B8A8_Unorm:     return FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
                case RHI_Format::B8R8G8A8_Unorm:     return FFX_API_SURFACE_FORMAT_B8G8R8A8_UNORM;
                case RHI_Format::R10G10B10A2_Unorm:  return FFX_API_SURFACE_FORMAT_R10G10B10A2_UNORM;
                case RHI_Format::R11G11B10_Float:    return FFX_API_SURFACE_FORMAT_R11G11B10_FLOAT;
                case RHI_Format::R16G16B16A16_Float: return FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT;
                case RHI_Format::R32G32B32A32_Float: return FFX_API_SURFACE_FORMAT_R32G32B32A32_FLOAT;
                case RHI_Format::R16G16_Float:       return FFX_API_SURFACE_FORMAT_R16G16_FLOAT;
                case RHI_Format::R32G32_Float:       return FFX_API_SURFACE_FORMAT_R32G32_FLOAT;
                case RHI_Format::R32_Float:          return FFX_API_SURFACE_FORMAT_R32_FLOAT;
                case RHI_Format::D32_Float:          return FFX_API_SURFACE_FORMAT_R32_FLOAT;
                case RHI_Format::R16_Float:          return FFX_API_SURFACE_FORMAT_R16_FLOAT;
                default:                             return FFX_API_SURFACE_FORMAT_UNKNOWN;
            }
        }

        uint32_t to_transfer_function(const RHI_Format format)
        {
            if (format == RHI_Format::R16G16B16A16_Float)
            {
                return FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SCRGB;
            }

            if (format == RHI_Format::R10G10B10A2_Unorm)
            {
                return FFX_API_BACKBUFFER_TRANSFER_FUNCTION_PQ;
            }

            return FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
        }

        // the engine keeps sampled and storage images in the general layout, which ffx calls common
        FfxApiResource to_ffx_resource(RHI_Texture* texture)
        {
            FfxApiResource resource       = {};
            resource.resource             = texture->GetRhiResource();
            resource.state                = FFX_API_RESOURCE_STATE_COMMON;
            resource.description.type     = FFX_API_RESOURCE_TYPE_TEXTURE2D;
            resource.description.format   = to_ffx_format(texture->GetFormat());
            resource.description.width    = texture->GetWidth();
            resource.description.height   = texture->GetHeight();
            resource.description.depth    = 1;
            resource.description.mipCount = 1;
            resource.description.flags    = FFX_API_RESOURCE_FLAGS_NONE;
            resource.description.usage    = texture->IsDepthStencilFormat() ? FFX_API_RESOURCE_USAGE_DEPTHTARGET : FFX_API_RESOURCE_USAGE_READ_ONLY;
            return resource;
        }

        ffxReturnCode_t frame_generation_callback(ffxDispatchDescFrameGeneration* params, void* user_context)
        {
            if (RHI_SwapChain* swapchain = RHI_Device::GetSwapChain())
            {
                params->backbufferTransferFunction = to_transfer_function(swapchain->GetFormat());
                params->minMaxLuminance[0]         = 0.0f;
                params->minMaxLuminance[1]         = Display::GetLuminanceMax();
            }

            ffxReturnCode_t result = ffx.Dispatch(static_cast<ffxContext*>(user_context), &params->header);
            if (result == FFX_API_RETURN_OK)
            {
                RHI_FrameGeneration::OnFramesGenerated(params->numGeneratedFrames);
            }
            return result;
        }

        bool generation_enabled = false;

        void configure(const bool enabled, RHI_Texture* hudless, const math::Rectangle& rect)
        {
            // Disabling an already disabled context can still wait on the present queue.
            if (!enabled && !generation_enabled) return;

            ffxConfigureDescFrameGeneration config     = {};
            config.header.type                         = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
            config.swapChain                           = swapchain_fg;
            config.frameGenerationCallback             = enabled ? frame_generation_callback : nullptr;
            config.frameGenerationCallbackUserContext  = enabled ? &context_fg : nullptr;
            config.frameGenerationEnabled              = enabled;
            config.allowAsyncWorkloads                 = false;
            config.HUDLessColor                        = hudless ? to_ffx_resource(hudless) : FfxApiResource{};
            config.generationRect.left                 = static_cast<int32_t>(rect.x);
            config.generationRect.top                  = static_cast<int32_t>(rect.y);
            config.generationRect.width                = static_cast<int32_t>(rect.width);
            config.generationRect.height               = static_cast<int32_t>(rect.height);
            config.frameID                             = frame_id;

            ffxReturnCode_t result = ffx.Configure(&context_fg, &config.header);
            if (result == FFX_API_RETURN_OK) generation_enabled = enabled;
            if (result != FFX_API_RETURN_OK && !logged_dispatch_error)
            {
                SP_LOG_WARNING("FSR frame generation configure failed: %u", static_cast<uint32_t>(result));
                logged_dispatch_error = true;
            }
        }

        void context_fg_destroy()
        {
            if (!context_fg)
            {
                return;
            }

            RHI_Device::QueueWaitAll();
            configure(false, nullptr, math::Rectangle());
            ffx.DestroyContext(&context_fg, nullptr);
            generation_enabled = false;
            context_fg          = nullptr;
            prepared_this_frame = false;
        }

        bool context_fg_create(const context_params& params)
        {
            ffxCreateBackendVKDesc backend_desc = {};
            backend_desc.header.type            = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
            backend_desc.vkDevice               = RHI_Context::device;
            backend_desc.vkPhysicalDevice       = RHI_Context::device_physical;
            backend_desc.vkDeviceProcAddr       = vkGetDeviceProcAddr;

            ffxCreateContextDescFrameGeneration desc = {};
            desc.header.type                         = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
            desc.header.pNext                        = &backend_desc.header;
            desc.flags                               = FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED;
            desc.flags                              |= params.hdr ? FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE : 0;
            desc.displaySize                         = { params.display_width, params.display_height };
            desc.maxRenderSize                       = { params.render_width, params.render_height };
            desc.backBufferFormat                    = params.format;

            ffxReturnCode_t result = ffx.CreateContext(&context_fg, &desc.header, nullptr);
            if (result != FFX_API_RETURN_OK)
            {
                context_fg = nullptr;
                if (!logged_create_error)
                {
                    SP_LOG_ERROR("FSR frame generation context creation failed: %u", static_cast<uint32_t>(result));
                    logged_create_error = true;
                }
                return false;
            }

            created = params;
            SP_LOG_INFO("FSR frame generation context created, display %ux%u, max render %ux%u", params.display_width, params.display_height, params.render_width, params.render_height);
            return true;
        }
    }

    // used by vulkan_swapchain.cpp and vulkan_queue.cpp
    namespace frame_generation_vk
    {
        bool is_active()
        {
            return context_swapchain != nullptr;
        }

        bool owns(void* swapchain)
        {
            return context_swapchain != nullptr && swapchain != nullptr && static_cast<VkSwapchainKHR>(swapchain) == swapchain_fg;
        }

        // hands a native swapchain to ffx, which destroys it and returns the frame generation one in its place
        bool swapchain_replace(void** swapchain, const VkSwapchainCreateInfoKHR& create_info)
        {
            if (!load())
            {
                return false;
            }

            VkSwapchainKHR vk_swapchain = static_cast<VkSwapchainKHR>(*swapchain);

            ffxCreateContextDescFrameGenerationSwapChainVK desc = {};
            desc.header.type                   = FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_VK;
            desc.physicalDevice                = RHI_Context::device_physical;
            desc.device                        = RHI_Context::device;
            desc.swapchain                     = &vk_swapchain;
            desc.allocator                     = nullptr;
            desc.createInfo                    = create_info;
            desc.createInfo.oldSwapchain       = VK_NULL_HANDLE;
            desc.gameQueue.queue               = static_cast<VkQueue>(RHI_Device::GetQueueRhiResource(RHI_Queue_Type::Graphics));
            desc.gameQueue.familyIndex         = RHI_Device::GetQueueIndex(RHI_Queue_Type::Graphics);
            desc.presentQueue.queue            = static_cast<VkQueue>(frame_generation_vk_queues::present_queue());
            desc.presentQueue.familyIndex      = frame_generation_vk_queues::present_family();
            desc.imageAcquireQueue.queue       = static_cast<VkQueue>(frame_generation_vk_queues::acquire_queue());
            desc.imageAcquireQueue.familyIndex = frame_generation_vk_queues::acquire_family();

            ffxReturnCode_t result = ffx.CreateContext(&context_swapchain, &desc.header, nullptr);
            *swapchain             = vk_swapchain;
            if (result != FFX_API_RETURN_OK)
            {
                SP_LOG_ERROR("FSR frame generation swapchain creation failed: %u", static_cast<uint32_t>(result));
                context_swapchain = nullptr;
                return false;
            }

            functions             = {};
            functions.header.type = FFX_API_QUERY_DESC_TYPE_FGSWAPCHAIN_FUNCTIONS_VK;
            ffx.Query(&context_swapchain, &functions.header);

            swapchain_fg = vk_swapchain;
            frame_id     = 0;
            SP_LOG_INFO("FSR frame generation swapchain created");
            return true;
        }

        // recreation while the swapchain context lives, size, vsync and hdr changes
        VkResult swapchain_recreate(void** swapchain, const VkSwapchainCreateInfoKHR& create_info)
        {
            functions.pOutDestroySwapchainFFXAPI(RHI_Context::device, swapchain_fg, nullptr, context_swapchain);
            swapchain_fg = VK_NULL_HANDLE;

            VkSwapchainCreateInfoKHR info = create_info;
            info.oldSwapchain             = VK_NULL_HANDLE;
            VkResult result               = functions.pOutCreateSwapchainFFXAPI(RHI_Context::device, &info, nullptr, &swapchain_fg, context_swapchain);
            *swapchain                    = swapchain_fg;
            return result;
        }

        void release()
        {
            if (!context_swapchain)
            {
                return;
            }

            context_fg_destroy();
            ffx.DestroyContext(&context_swapchain, nullptr);
            context_swapchain = nullptr;
            swapchain_fg      = VK_NULL_HANDLE;
            functions         = {};
            RHI_FrameGeneration::ReleaseHudless();
            SP_LOG_INFO("FSR frame generation swapchain destroyed");
        }

        VkResult get_swapchain_images(void* swapchain, uint32_t* count, VkImage* images)
        {
            return functions.pOutGetSwapchainImagesKHR(RHI_Context::device, static_cast<VkSwapchainKHR>(swapchain), count, images);
        }

        VkResult acquire_next_image(void* swapchain, uint64_t timeout, VkSemaphore semaphore, uint32_t* image_index)
        {
            return functions.pOutAcquireNextImageKHR(RHI_Context::device, static_cast<VkSwapchainKHR>(swapchain), timeout, semaphore, VK_NULL_HANDLE, image_index);
        }

        VkResult queue_present(VkQueue queue, const VkPresentInfoKHR* present_info)
        {
            return functions.pOutQueuePresentKHR(queue, present_info);
        }

        void set_hdr_metadata(void* swapchain, const VkHdrMetadataEXT* metadata)
        {
            VkSwapchainKHR vk_swapchain = static_cast<VkSwapchainKHR>(swapchain);
            functions.pOutSetHdrMetadataEXT(RHI_Context::device, 1, &vk_swapchain, metadata);
        }
    }
    #else
    namespace frame_generation_vk
    {
        bool is_active() { return false; }
        bool owns(void*) { return false; }
        bool swapchain_replace(void**, const VkSwapchainCreateInfoKHR&) { return false; }
        VkResult swapchain_recreate(void**, const VkSwapchainCreateInfoKHR&) { return VK_ERROR_INITIALIZATION_FAILED; }
        void release() {}
        VkResult get_swapchain_images(void*, uint32_t*, VkImage*) { return VK_ERROR_INITIALIZATION_FAILED; }
        VkResult acquire_next_image(void*, uint64_t, VkSemaphore, uint32_t*) { return VK_ERROR_INITIALIZATION_FAILED; }
        VkResult queue_present(VkQueue, const VkPresentInfoKHR*) { return VK_ERROR_INITIALIZATION_FAILED; }
        void set_hdr_metadata(void*, const VkHdrMetadataEXT*) {}
    }
    #endif

    void RHI_FrameGeneration::Shutdown()
    {
    #ifdef _WIN32
        context_fg_destroy();
        ReleaseHudless();
        if (module && !context_swapchain)
        {
            FreeLibrary(module);
            module         = nullptr;
            load_attempted = false;
            ffx            = {};
        }
    #endif
    }

    bool RHI_FrameGeneration::IsSupported()
    {
    #ifdef _WIN32
        return load();
    #else
        return false;
    #endif
    }

    bool RHI_FrameGeneration::IsActive()
    {
    #ifdef _WIN32
        return context_swapchain != nullptr;
    #else
        return false;
    #endif
    }

    void RHI_FrameGeneration::OnSwapChainRecreate()
    {
    #ifdef _WIN32
        context_fg_destroy();
    #endif
    }

    void RHI_FrameGeneration::Prepare(RHI_Texture* tex_depth, RHI_Texture* tex_velocity)
    {
    #ifdef _WIN32
        RHI_CommandList* cmd_list = RHI_Device::Cmd();
        RHI_SwapChain* swapchain  = RHI_Device::GetSwapChain();
        Camera* camera           = World::GetCamera();
        if (!HasPresentationSource() || !IsActive() || !swapchain_fg || !cmd_list || !tex_depth || !tex_velocity || !swapchain || !camera || !camera->GetEntity())
        {
            return;
        }

        const Vector2& resolution_render = Renderer::GetResolutionRender();
        const float resolution_scale     = Renderer::GetResolutionScale();

        context_params params;
        params.display_width  = swapchain->GetWidth();
        params.display_height = swapchain->GetHeight();
        params.render_width   = static_cast<uint32_t>(resolution_render.x);
        params.render_height  = static_cast<uint32_t>(resolution_render.y);
        params.format         = to_ffx_format(swapchain->GetFormat());
        params.hdr            = swapchain->IsHdr();

        if (context_fg && !(params == created))
        {
            context_fg_destroy();
        }

        if (!context_fg && !context_fg_create(params))
        {
            return;
        }

        const uint32_t render_width  = Renderer::GetScaledDimension(params.render_width, resolution_scale);
        const uint32_t render_height = Renderer::GetScaledDimension(params.render_height, resolution_scale);

        frame_id++;
        // allocate before configuring, including the first frame and the first frame after resize
        configure(true, GetHudlessTarget(params.display_width, params.display_height, swapchain->GetFormat()), GetGenerationRect(params.display_width, params.display_height));

        tex_depth->SetLayout(RHI_Image_Layout::General, cmd_list);
        tex_velocity->SetLayout(RHI_Image_Layout::General, cmd_list);
        RHI_CommandList::RenderPassEnd();
        cmd_list->FlushBarriers();

        // the projection jitter is in ndc, ffx wants the pixel offset with the sign the fsr upscaler uses
        const Vector2& jitter = Renderer::view().jitter_offset;
        const Cb_Frame& frame = Renderer::view().frame;
        Entity* entity        = camera->GetEntity();

        ffxDispatchDescFrameGenerationPrepareCameraInfo camera_info = {};
        camera_info.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO;
        const Vector3 position  = entity->GetPosition();
        const Vector3 up        = entity->GetUp();
        const Vector3 right     = entity->GetRight();
        const Vector3 forward   = entity->GetForward();
        memcpy(camera_info.cameraPosition, &position, sizeof(float) * 3);
        memcpy(camera_info.cameraUp, &up, sizeof(float) * 3);
        memcpy(camera_info.cameraRight, &right, sizeof(float) * 3);
        memcpy(camera_info.cameraForward, &forward, sizeof(float) * 3);

        ffxDispatchDescFrameGenerationPrepare prepare = {};
        prepare.header.type             = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;
        prepare.header.pNext            = &camera_info.header;
        prepare.frameID                 = frame_id;
        prepare.commandList             = cmd_list->GetRhiResource();
        prepare.renderSize              = { render_width, render_height };
        prepare.jitterOffset.x          = -jitter.x * 0.5f * static_cast<float>(render_width);
        prepare.jitterOffset.y          =  jitter.y * 0.5f * static_cast<float>(render_height);
        // engine velocity is unjittered ndc curr minus prev with y up, ffx wants prev minus curr in pixels with y down
        prepare.motionVectorScale.x     = -0.5f * static_cast<float>(render_width);
        prepare.motionVectorScale.y     =  0.5f * static_cast<float>(render_height);
        prepare.frameTimeDelta          = frame.delta_time * 1000.0f;
        // reverse z, ffx takes the plane that maps to depth 0 as near
        prepare.cameraNear              = camera->GetFarPlane();
        prepare.cameraFar               = camera->GetNearPlane();
        prepare.cameraFovAngleVertical  = camera->GetFovVerticalRad();
        prepare.viewSpaceToMetersFactor = 1.0f;
        prepare.depth                   = to_ffx_resource(tex_depth);
        prepare.motionVectors           = to_ffx_resource(tex_velocity);

        ffxReturnCode_t result = ffx.Dispatch(&context_fg, &prepare.header);
        if (result != FFX_API_RETURN_OK && !logged_dispatch_error)
        {
            SP_LOG_WARNING("FSR frame generation prepare failed: %u", static_cast<uint32_t>(result));
            logged_dispatch_error = true;
        }

        cmd_list->AdoptComputeShaderResource(tex_depth);
        cmd_list->AdoptComputeShaderResource(tex_velocity);
        cmd_list->restore_after_external_pass();
        prepared_this_frame = result == FFX_API_RETURN_OK;
    #endif
    }

    void RHI_FrameGeneration::EndFrame(RHI_CommandList* cmd_list, RHI_SwapChain* swapchain)
    {
    #ifdef _WIN32
        if (!swapchain || swapchain != RHI_Device::GetSwapChain())
        {
            return;
        }

        RHI_Texture* hudless = ConsumeHudlessRecorded();
        if (hudless)
        {
            hudless->SetLayout(RHI_Image_Layout::General, cmd_list);
            cmd_list->FlushBarriers();
        }

        // never interpolate a frame without current scene inputs and a current ui-free backbuffer
        if (context_fg && (!prepared_this_frame || !hudless))
        {
            configure(false, nullptr, math::Rectangle());
        }
        prepared_this_frame = false;
    #endif
    }
}
