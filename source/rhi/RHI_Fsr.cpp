/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "RHI_VendorTechnology.h"
#include "RHI_Implementation.h"
#include "RHI_Device.h"
#include "RHI_CommandList.h"
#include "RHI_Texture.h"
#include "../rendering/Renderer_Internal.h"
#include "../world/components/Camera.h"

#ifdef _WIN32
SP_WARNINGS_OFF
// AMD's loader checks PLATFORM_WINDOWS rather than the compiler's _WIN32.
#define PLATFORM_WINDOWS
#include <api/include/ffx_api_loader.h>
#undef PLATFORM_WINDOWS
#include <upscalers/include/ffx_upscale.h>
#if defined(API_GRAPHICS_D3D12)
#include <api/include/dx12/ffx_api_dx12.h>
#else
#include <api/include/vk/ffx_api_vk.h>
#endif
SP_WARNINGS_ON
#endif

namespace spartan
{
#ifdef _WIN32
    namespace
    {
        HMODULE module = nullptr;
        ffxFunctions api = {};
        ffxContext context = nullptr;
        bool load_attempted = false;
        bool reset_history = true;
        bool failed = false;
        uint32_t render_width = 0, render_height = 0, output_width = 0, output_height = 0;
        uint32_t jitter_index = 0;

        void destroy_context()
        {
            if (context)
            {
                RHI_Device::QueueWaitAll();
                api.DestroyContext(&context, nullptr);
                context = nullptr;
            }
            reset_history = true;
        }

        uint32_t format(RHI_Format value)
        {
            switch (value)
            {
                case RHI_Format::R8_Unorm: return FFX_API_SURFACE_FORMAT_R8_UNORM;
                case RHI_Format::R16_Float: return FFX_API_SURFACE_FORMAT_R16_FLOAT;
                case RHI_Format::R32_Float:
                case RHI_Format::D32_Float: return FFX_API_SURFACE_FORMAT_R32_FLOAT;
                case RHI_Format::R16G16_Float: return FFX_API_SURFACE_FORMAT_R16G16_FLOAT;
                case RHI_Format::R11G11B10_Float: return FFX_API_SURFACE_FORMAT_R11G11B10_FLOAT;
                case RHI_Format::R16G16B16A16_Float: return FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT;
                case RHI_Format::R8G8B8A8_Unorm: return FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
                default: return FFX_API_SURFACE_FORMAT_UNKNOWN;
            }
        }

#if defined(API_GRAPHICS_VULKAN)
        // The ML provider requires shader-read-only inputs. Engine textures otherwise stay
        // in GENERAL; keep these temporary layouts entirely inside the external pass.
        void transition_inputs(RHI_CommandList* cmd, std::initializer_list<RHI_Texture*> inputs, bool restore)
        {
            std::vector<VkImageMemoryBarrier> barriers;
            for (RHI_Texture* texture : inputs)
            {
                VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                barrier.srcAccessMask = restore ? VK_ACCESS_SHADER_READ_BIT : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
                barrier.dstAccessMask = restore ? VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;
                barrier.oldLayout = restore ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
                barrier.newLayout = restore ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = static_cast<VkImage>(texture->GetRhiResource());
                barrier.subresourceRange = { static_cast<VkImageAspectFlags>(texture->IsDepthStencilFormat() ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 1, 0, 1 };
                barriers.push_back(barrier);
            }
            vkCmdPipelineBarrier(static_cast<VkCommandBuffer>(cmd->GetRhiResource()),
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
                0, nullptr, 0, nullptr, static_cast<uint32_t>(barriers.size()), barriers.data());
        }
#endif

        FfxApiResource resource(RHI_Texture* texture, bool output = false)
        {
#if defined(API_GRAPHICS_D3D12)
            FfxApiResource result = ffxApiGetResourceDX12(static_cast<ID3D12Resource*>(texture->GetRhiResource()),
                output ? FFX_API_RESOURCE_STATE_UNORDERED_ACCESS : FFX_API_RESOURCE_STATE_COMPUTE_READ);
            result.description.format = format(texture->GetFormat());
#else
            FfxApiResource result = {};
            result.resource = texture->GetRhiResource();
            result.state = output ? FFX_API_RESOURCE_STATE_UNORDERED_ACCESS : FFX_API_RESOURCE_STATE_COMPUTE_READ;
            result.description.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
            result.description.format = format(texture->GetFormat());
            result.description.width = texture->GetWidth();
            result.description.height = texture->GetHeight();
            result.description.depth = 1;
            result.description.mipCount = 1;
            result.description.usage = output ? FFX_API_RESOURCE_USAGE_UAV :
                (texture->IsDepthStencilFormat() ? FFX_API_RESOURCE_USAGE_DEPTHTARGET : FFX_API_RESOURCE_USAGE_READ_ONLY);
#endif
            return result;
        }
    }
#endif

    bool RHI_VendorTechnology::FSR_IsAvailable()
    {
#ifdef _WIN32
        if (!load_attempted)
        {
            load_attempted = true;
#if defined(API_GRAPHICS_D3D12)
            module = LoadLibraryW(L"amd_fidelityfx_loader_dx12.dll");
#else
            module = LoadLibraryW(L"amd_fidelityfx_loader_vk.dll");
#endif
            if (module) ffxLoadFunctions(&api, module);
            if (!api.CreateContext || !api.DestroyContext || !api.Query || !api.Dispatch || !api.Configure)
            {
                if (module) FreeLibrary(module);
                module = nullptr;
                api = {};
                SP_LOG_WARNING("FSR upscaling runtime unavailable; run dependency setup");
            }
        }
        return module != nullptr && !failed;
#else
        return false;
#endif
    }

    void RHI_VendorTechnology::FSR_Shutdown()
    {
#ifdef _WIN32
        destroy_context();
        if (module) FreeLibrary(module);
        module = nullptr;
        api = {};
        load_attempted = false;
        failed = false;
        jitter_index = 0;
#endif
    }

    void RHI_VendorTechnology::FSR_ResetHistory()
    {
#ifdef _WIN32
        reset_history = true;
        failed = false;
        jitter_index = 0;
#endif
    }

    void RHI_VendorTechnology::FSR_GenerateJitterSample(float* x, float* y)
    {
        *x = *y = 0.0f;
#ifdef _WIN32
        if (!FSR_IsAvailable()) return;
        const auto& resolution = Renderer::GetResolutionRender();
        const float scale = Renderer::GetResolutionScale();
        const uint32_t width = Renderer::GetScaledDimension(static_cast<uint32_t>(resolution.x), scale);
        const uint32_t height = Renderer::GetScaledDimension(static_cast<uint32_t>(resolution.y), scale);
        if (!width || !height) return;
        int32_t phases = 8;
        ffxQueryDescUpscaleGetJitterPhaseCount phase = {};
        phase.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
        phase.renderWidth = width;
        phase.displayWidth = static_cast<uint32_t>(Renderer::GetResolutionOutput().x);
        phase.pOutPhaseCount = &phases;
        api.Query(nullptr, &phase.header);
        if (phases <= 0) phases = 8;
        ffxQueryDescUpscaleGetJitterOffset offset = {};
        offset.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET;
        offset.index = static_cast<int32_t>(jitter_index++ % static_cast<uint32_t>(phases));
        offset.phaseCount = phases;
        offset.pOutX = x;
        offset.pOutY = y;
        api.Query(nullptr, &offset.header);
        *x *= -2.0f / static_cast<float>(width);
        *y *=  2.0f / static_cast<float>(height);
#endif
    }

    bool RHI_VendorTechnology::FSR_Dispatch(RHI_Texture* color, RHI_Texture* depth, RHI_Texture* velocity,
                                           RHI_Texture* output, RHI_Texture* reactive)
    {
#ifdef _WIN32
        if (!FSR_IsAvailable()) return false;
        Camera* camera = Renderer::GetViewCamera();
        if (!camera) return false;
        if (render_width != color->GetWidth() || render_height != color->GetHeight() ||
            output_width != output->GetWidth() || output_height != output->GetHeight())
        {
            destroy_context();
            render_width = color->GetWidth();
            render_height = color->GetHeight();
            output_width = output->GetWidth();
            output_height = output->GetHeight();
        }
        if (!context)
        {
#if defined(API_GRAPHICS_D3D12)
            ffxCreateBackendDX12Desc backend = {};
            backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
            backend.device = RHI_Context::device;
#else
            ffxCreateBackendVKDesc backend = {};
            backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
            backend.deviceContext = { RHI_Context::device, RHI_Context::device_physical, vkGetDeviceProcAddr };
#endif
            ffxCreateContextDescUpscaleVersion version = {};
            version.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
            version.header.pNext = &backend.header;
            version.version = FFX_UPSCALER_VERSION;
            ffxCreateContextDescUpscale create = {};
            create.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
            create.header.pNext = &version.header;
            create.flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_DEPTH_INVERTED |
                           FFX_UPSCALE_ENABLE_AUTO_EXPOSURE | FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION;
            create.maxRenderSize = { render_width, render_height };
            create.maxUpscaleSize = { output_width, output_height };
            const auto result = api.CreateContext(&context, &create.header, nullptr);
            if (result != FFX_API_RETURN_OK)
            {
                SP_LOG_WARNING("FSR upscaling context creation failed (%u); using TAAU", static_cast<uint32_t>(result));
                context = nullptr;
                failed = true;
                return false;
            }
            ffxQueryGetProviderVersion provider = {};
            provider.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
            if (api.Query(&context, &provider.header) == FFX_API_RETURN_OK && provider.versionName)
                SP_LOG_INFO("FSR upscaling selected: %s", provider.versionName);
        }

        RHI_CommandList* cmd = RHI_Device::Cmd();
        RHI_CommandList::RenderPassEnd();
        for (RHI_Texture* input : { color, depth, velocity, reactive })
        {
#if defined(API_GRAPHICS_D3D12)
            cmd->EnsureComputeShaderResource(input);
#else
            cmd->PrepareForExternalRead(input);
#endif
        }
        cmd->PrepareForExternalWrite(output);
        cmd->FlushBarriers();
#if defined(API_GRAPHICS_VULKAN)
        transition_inputs(cmd, { color, depth, velocity, reactive }, false);
#endif

        const float scale = Renderer::GetResolutionScale();
        const uint32_t width = Renderer::GetScaledDimension(render_width, scale);
        const uint32_t height = Renderer::GetScaledDimension(render_height, scale);
        const auto& jitter = Renderer::view().jitter_offset;
        ffxDispatchDescUpscale dispatch = {};
        dispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
        dispatch.commandList = cmd->GetRhiResource();
        dispatch.color = resource(color);
        dispatch.depth = resource(depth);
        dispatch.motionVectors = resource(velocity);
        dispatch.reactive = resource(reactive);
        dispatch.output = resource(output, true);
        dispatch.renderSize = { width, height };
        dispatch.upscaleSize = { output_width, output_height };
        dispatch.jitterOffset = { -jitter.x * 0.5f * width, jitter.y * 0.5f * height };
        dispatch.motionVectorScale = { -0.5f * width, 0.5f * height };
        dispatch.frameTimeDelta = Renderer::view().frame.delta_time_real * 1000.0f;
        dispatch.preExposure = 1.0f;
        dispatch.reset = reset_history;
        dispatch.cameraNear = camera->GetFarPlane();
        dispatch.cameraFar = camera->GetNearPlane();
        dispatch.cameraFovAngleVertical = camera->GetFovVerticalRad();
        dispatch.viewSpaceToMetersFactor = 1.0f;
        const auto result = api.Dispatch(&context, &dispatch.header);
#if defined(API_GRAPHICS_VULKAN)
        transition_inputs(cmd, { color, depth, velocity, reactive }, true);
#endif
        for (RHI_Texture* input : { color, depth, velocity, reactive }) cmd->AdoptComputeShaderResource(input);
        cmd->AdoptUnorderedAccess(output);
        cmd->restore_after_external_pass();
        reset_history = result != FFX_API_RETURN_OK;
        if (result != FFX_API_RETURN_OK)
        {
            SP_LOG_WARNING("FSR upscaling dispatch failed (%u); using TAAU", static_cast<uint32_t>(result));
            failed = true;
            return false;
        }
        return true;
#else
        return false;
#endif
    }
}
