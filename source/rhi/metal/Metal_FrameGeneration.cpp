/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==================
#include "pch.h"
#include "../RHI_FrameGeneration.h"
//=============================

namespace spartan
{
    // fsr frame generation is a dxgi/vulkan swapchain replacement, metal has no equivalent here
    void RHI_FrameGeneration::Shutdown()
    {
    }

    bool RHI_FrameGeneration::IsSupported()
    {
        return false;
    }

    bool RHI_FrameGeneration::IsActive()
    {
        return false;
    }

    void RHI_FrameGeneration::OnSwapChainRecreate()
    {
    }

    void RHI_FrameGeneration::Prepare(RHI_Texture* tex_depth, RHI_Texture* tex_velocity)
    {
    }

    void RHI_FrameGeneration::EndFrame(RHI_CommandList* cmd_list, RHI_SwapChain* swapchain)
    {
    }
}
