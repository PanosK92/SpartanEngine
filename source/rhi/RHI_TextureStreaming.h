/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/
#pragma once

#include <cstdint>
#include <memory>

namespace spartan
{
    class RHI_Texture;

    // Render-thread API. Only immutable, sampled 2D BC textures with a complete CPU
    // mip chain participate. Render targets, UAVs, arrays and CPU-only packing inputs
    // never enter the streamer. Backends only create/upload ordinary textures.
    class RHI_TextureStreaming
    {
    public:
        struct Statistics
        {
            uint64_t resident_bytes = 0;
            uint64_t full_bytes = 0;
            uint64_t budget_bytes = 0;
            uint64_t pending_bytes = 0;
            uint32_t texture_count = 0;
        };

        // Pixels covered by one UV repeat. Multiple users/views merge by maximum demand.
        // Zero requests the always-resident small mip tail; infinity requests full detail.
        static void Request(RHI_Texture* texture, float pixels, uint64_t frame);
        // Before BeginFrame: publishes completed uploads, then schedules at most one
        // replacement. Returns true when bindless descriptors need refreshing.
        static bool Tick(uint64_t frame);
        static void Shutdown();
        static Statistics GetStatistics();

    private:
        friend class RHI_Texture;
        static bool HasStreamableData(const RHI_Texture& texture);
        static bool Prepare(RHI_Texture& texture);
        static bool CanStream(const RHI_Texture& texture);
        static std::shared_ptr<RHI_Texture> CreateReplacement(const RHI_Texture& texture, uint32_t mip);
        static void Publish(RHI_Texture& texture, RHI_Texture& replacement, uint32_t mip);
    };
}
