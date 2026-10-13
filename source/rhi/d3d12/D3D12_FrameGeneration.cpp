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
#include <ffx_api/dx12/ffx_api_dx12.h>
SP_WARNINGS_ON
#endif
//====================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    #ifdef _WIN32
    namespace
    {
        HMODULE module      = nullptr;
        bool load_attempted = false;
        ffxFunctions ffx    = {};

        ffxContext context_swapchain  = nullptr;
        ffxContext context_fg         = nullptr;
        IDXGISwapChain4* swapchain_fg = nullptr;

        uint64_t frame_id         = 0;
        bool prepared_this_frame  = false;
        bool logged_create_error  = false;
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

            module = LoadLibraryW(L"amd_fidelityfx_dx12.dll");
            if (module)
            {
                ffxLoadFunctions(&ffx, module);
            }

            if (!module || !ffx.CreateContext || !ffx.DestroyContext || !ffx.Configure || !ffx.Query || !ffx.Dispatch)
            {
                SP_LOG_WARNING("FSR frame generation is unavailable, amd_fidelityfx_dx12.dll is missing, run the setup to download it");
                if (module)
                {
                    FreeLibrary(module);
                    module = nullptr;
                }
                return false;
            }

            SP_LOG_INFO("FSR frame generation runtime loaded (amd_fidelityfx_dx12.dll)");
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

        // engine formats are typed, the resource itself can be typeless so the format comes from the texture
        FfxApiResource to_ffx_resource(RHI_Texture* texture, const uint32_t state)
        {
            FfxApiResource resource     = ffxApiGetResourceDX12(static_cast<ID3D12Resource*>(texture->GetRhiResource()), state);
            resource.description.format = to_ffx_format(texture->GetFormat());
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

        void configure(const bool enabled, RHI_Texture* hudless, const math::Rectangle& rect)
        {
            ffxConfigureDescFrameGeneration config     = {};
            config.header.type                         = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
            config.swapChain                           = swapchain_fg;
            config.frameGenerationCallback             = enabled ? frame_generation_callback : nullptr;
            config.frameGenerationCallbackUserContext  = enabled ? &context_fg : nullptr;
            config.frameGenerationEnabled              = enabled;
            config.allowAsyncWorkloads                 = false;
            config.HUDLessColor                        = hudless ? to_ffx_resource(hudless, FFX_API_RESOURCE_STATE_COMPUTE_READ) : FfxApiResource{};
            config.generationRect.left                 = static_cast<int32_t>(rect.x);
            config.generationRect.top                  = static_cast<int32_t>(rect.y);
            config.generationRect.width                = static_cast<int32_t>(rect.width);
            config.generationRect.height               = static_cast<int32_t>(rect.height);
            config.frameID                             = frame_id;

            ffxReturnCode_t result = ffx.Configure(&context_fg, &config.header);
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
            context_fg          = nullptr;
            prepared_this_frame = false;
        }

        bool context_fg_create(const context_params& params)
        {
            ffxCreateBackendDX12Desc backend_desc = {};
            backend_desc.header.type              = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
            backend_desc.device                   = RHI_Context::device;

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

    // used by d3d12_swapchain.cpp
    namespace frame_generation_d3d12
    {
        IDXGISwapChain4* swapchain_create(HWND hwnd, DXGI_SWAP_CHAIN_DESC1* desc, IDXGIFactory* factory, ID3D12CommandQueue* queue)
        {
            if (!load())
            {
                return nullptr;
            }

            DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen_desc = {};
            fullscreen_desc.Windowed                        = TRUE;

            IDXGISwapChain4* swapchain = nullptr;
            ffxCreateContextDescFrameGenerationSwapChainForHwndDX12 create_desc = {};
            create_desc.header.type    = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12;
            create_desc.swapchain      = &swapchain;
            create_desc.hwnd           = hwnd;
            create_desc.desc           = desc;
            create_desc.fullscreenDesc = &fullscreen_desc;
            create_desc.dxgiFactory    = factory;
            create_desc.gameQueue      = queue;

            ffxReturnCode_t result = ffx.CreateContext(&context_swapchain, &create_desc.header, nullptr);
            if (result != FFX_API_RETURN_OK || !swapchain)
            {
                SP_LOG_ERROR("FSR frame generation swapchain creation failed: %u", static_cast<uint32_t>(result));
                context_swapchain = nullptr;
                return nullptr;
            }

            swapchain_fg = swapchain;
            frame_id     = 0;
            SP_LOG_INFO("FSR frame generation swapchain created");
            return swapchain;
        }

        void swapchain_release(IDXGISwapChain3* swapchain)
        {
            if (!swapchain)
            {
                return;
            }

            const bool is_frame_generation = swapchain_fg && static_cast<IDXGISwapChain3*>(swapchain_fg) == swapchain;
            if (is_frame_generation)
            {
                context_fg_destroy();
            }

            swapchain->Release();

            if (is_frame_generation)
            {
                ffx.DestroyContext(&context_swapchain, nullptr);
                context_swapchain = nullptr;
                swapchain_fg      = nullptr;
                RHI_FrameGeneration::ReleaseHudless();
                SP_LOG_INFO("FSR frame generation swapchain destroyed");
            }
        }
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
        Camera* camera            = World::GetCamera();
        if (!IsActive() || !cmd_list || !tex_depth || !tex_velocity || !swapchain || !camera || !camera->GetEntity())
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
        configure(true, GetHudless(), GetGenerationRect(params.display_width, params.display_height));

        cmd_list->EnsureComputeShaderResource(tex_depth);
        cmd_list->EnsureComputeShaderResource(tex_velocity);
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
        prepare.depth                   = to_ffx_resource(tex_depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        prepare.motionVectors           = to_ffx_resource(tex_velocity, FFX_API_RESOURCE_STATE_COMPUTE_READ);

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

        if (RHI_Texture* hudless = ConsumeHudlessRecorded())
        {
            cmd_list->EnsureComputeShaderResource(hudless);
            cmd_list->FlushBarriers();
        }

        // a frame that skipped the 3d view has nothing to interpolate from
        if (context_fg && !prepared_this_frame)
        {
            configure(false, nullptr, math::Rectangle());
        }
        prepared_this_frame = false;
    #endif
    }
}
