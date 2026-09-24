/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ==================
#include <cstdint>
#include <type_traits>
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

namespace spartan::animation_format
{
    constexpr uint32_t clip_magic = 0x50494C43; // CLIP
    constexpr uint32_t clip_version = 4;
    constexpr uint32_t skeleton_magic = 0x4C454B53; // SKEL
    constexpr uint32_t skeleton_version = 2;

    struct ClipHeader
    {
        uint32_t magic = clip_magic;
        uint32_t version = clip_version;

        uint32_t joint_count = 0;
        float duration_seconds = 0.0f;
        float sample_rate = 0.0f;

        uint32_t position_channel_count = 0;
        uint32_t rotation_channel_count = 0;
        uint32_t scale_channel_count = 0;

        uint32_t constant_position_count = 0;
        uint32_t constant_rotation_count = 0;
        uint32_t constant_scale_count = 0;

        uint32_t position_sample_count = 0;
        uint32_t rotation_sample_count = 0;
        uint32_t scale_sample_count = 0;

        uint32_t base_local_pose_count = 0;
        uint32_t sampled_bone_count = 0;
    };

    static_assert(std::is_trivially_copyable_v<ClipHeader>);
    static_assert(sizeof(ClipHeader) == 64);

    struct SkeletonHeader
    {
        uint32_t magic = skeleton_magic;
        uint32_t version = skeleton_version;
        uint16_t joint_count = 0;
        uint16_t pad = 0;
    };

    static_assert(std::is_trivially_copyable_v<SkeletonHeader>);
    static_assert(sizeof(SkeletonHeader) == 12);
}
