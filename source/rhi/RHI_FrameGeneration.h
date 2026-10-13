/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ===============
#include "../math/Rectangle.h"
#include "RHI_Definitions.h"
//==========================

namespace spartan
{
    // amd fsr 3.1 frame generation through the ffx api runtime (amd_fidelityfx_dx12.dll / amd_fidelityfx_vk.dll), vendor agnostic
    // the main swapchain becomes the ffx frame generation swapchain while r.frame_generation is on, it presents one generated frame between every two rendered ones
    class RHI_FrameGeneration
    {
    public:
        static void Shutdown();

        // the runtime dll loaded and the device can host the frame generation swapchain
        static bool IsSupported();

        // the main swapchain is the frame generation swapchain
        static bool IsActive();

        // records the frame inputs, depth and velocity are the render resolution targets the upscaler consumed
        static void Prepare(RHI_Texture* tex_depth, RHI_Texture* tex_velocity);

        // backbuffer copy without ui, ffx diffs it against the backbuffer to keep ui pixels out of interpolation
        static RHI_Texture* GetHudlessTarget(uint32_t width, uint32_t height, RHI_Format format);
        static void SetHudlessRecorded();

        // backbuffer area that holds the 3d view, zero size means the whole backbuffer
        static void SetGenerationRect(const math::Rectangle& rect);

        // runs on the frame command list before it is submitted for presentation
        static void EndFrame(RHI_CommandList* cmd_list, RHI_SwapChain* swapchain);

        // runs before a swapchain recreation that can change the size or format
        static void OnSwapChainRecreate();

        // frames ffx interpolated since startup, counted when its generation callback succeeds, may run on the ffx present thread
        static uint64_t GetGeneratedFrameCount();
        static void OnFramesGenerated(uint32_t count);

        // shared state, implemented in RHI_FrameGeneration.cpp
        static RHI_Texture* GetHudless();
        static RHI_Texture* ConsumeHudlessRecorded();
        static math::Rectangle GetGenerationRect(uint32_t backbuffer_width, uint32_t backbuffer_height);
        static void ReleaseHudless();
    };
}
