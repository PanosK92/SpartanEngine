/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==================
#include "pch.h"
#include "RHI_FrameGeneration.h"
#include "RHI_Texture.h"
#include "RHI_CommandList.h"
#include <atomic>
//=============================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        shared_ptr<RHI_Texture> hudless;
        shared_ptr<RHI_Texture> scene;
        bool scene_recorded   = false;
        bool hudless_recorded = false; // recorded this frame, needs the transition to a shader read state
        Rectangle generation_rect;
        atomic<uint64_t> generated_frames = 0;
    }

    uint64_t RHI_FrameGeneration::GetGeneratedFrameCount()
    {
        return generated_frames.load(memory_order_relaxed);
    }

    void RHI_FrameGeneration::OnFramesGenerated(uint32_t count)
    {
        generated_frames.fetch_add(count, memory_order_relaxed);
    }

    RHI_Texture* RHI_FrameGeneration::GetHudlessTarget(uint32_t width, uint32_t height, RHI_Format format)
    {
        hudless_recorded = false;
        if (!IsActive() || width == 0 || height == 0)
        {
            return nullptr;
        }

        if (!hudless || hudless->GetWidth() != width || hudless->GetHeight() != height || hudless->GetFormat() != format)
        {
            hudless = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width, height, 1, 1, format, RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit, "frame_generation_hudless");
        }

        return hudless->GetRhiResource() ? hudless.get() : nullptr;
    }

    void RHI_FrameGeneration::SetHudlessRecorded()
    {
        hudless_recorded = hudless != nullptr;
    }

    void RHI_FrameGeneration::CaptureScene(RHI_Texture* texture)
    {
        scene_recorded = false;
        if (!IsActive() || !texture)
        {
            return;
        }

        if (!scene || scene->GetWidth() != texture->GetWidth() || scene->GetHeight() != texture->GetHeight() || scene->GetFormat() != texture->GetFormat())
        {
            scene = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, texture->GetWidth(), texture->GetHeight(), 1, 1, texture->GetFormat(), RHI_Texture_Srv | RHI_Texture_ClearBlit, "frame_generation_scene");
            // copy-only textures have no render-target flags or initial data, so the constructor defers allocation
            scene->PrepareForGpu();
        }

        if (scene->GetRhiResource())
        {
            RHI_CommandList::Copy(texture, scene.get(), false);
            scene_recorded = true;
        }
    }

    RHI_Texture* RHI_FrameGeneration::GetScene()
    {
        return scene_recorded ? scene.get() : nullptr;
    }

    RHI_Texture* RHI_FrameGeneration::ConsumeHudlessRecorded()
    {
        RHI_Texture* texture = hudless_recorded ? hudless.get() : nullptr;
        hudless_recorded     = false;
        scene_recorded       = false;
        return texture;
    }

    void RHI_FrameGeneration::ReleaseHudless()
    {
        hudless          = nullptr;
        scene            = nullptr;
        scene_recorded   = false;
        hudless_recorded = false;
    }

    void RHI_FrameGeneration::SetGenerationRect(const Rectangle& rect)
    {
        generation_rect = rect;
    }

    Rectangle RHI_FrameGeneration::GetGenerationRect(uint32_t backbuffer_width, uint32_t backbuffer_height)
    {
        const float width  = static_cast<float>(backbuffer_width);
        const float height = static_cast<float>(backbuffer_height);

        Rectangle rect;
        rect.x      = clamp(generation_rect.x, 0.0f, width);
        rect.y      = clamp(generation_rect.y, 0.0f, height);
        rect.width  = clamp(generation_rect.width, 0.0f, width - rect.x);
        rect.height = clamp(generation_rect.height, 0.0f, height - rect.y);

        if (rect.width < 1.0f || rect.height < 1.0f)
        {
            return Rectangle(0.0f, 0.0f, width, height);
        }

        return rect;
    }
}
