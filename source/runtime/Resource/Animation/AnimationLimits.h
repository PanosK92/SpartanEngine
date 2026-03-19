/*
Copyright(c) 2015-2026 Panos Karabelas
*/

#pragma once

//= INCLUDES ==================
#include <cstdint>
//=============================

namespace spartan::animation_limits
{
    inline constexpr uint32_t joint_count = 1024;
    inline constexpr uint32_t channel_count = 100000;
    inline constexpr uint32_t constant_count = 100000;
    inline constexpr uint32_t sample_count = 30000000;
    inline constexpr uint32_t samples_per_channel = 10000000;

    inline constexpr uint64_t max_clip_bytes = 512ull * 1024ull * 1024ull;
    inline constexpr uint64_t max_skeleton_bytes = 256ull * 1024ull * 1024ull;
}
