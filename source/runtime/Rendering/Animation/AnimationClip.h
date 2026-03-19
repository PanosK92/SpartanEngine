/*
Copyright(c) 2015-2026 Panos Karabelas

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

#pragma once

//= INCLUDES ====================
#include "../../Math/Quaternion.h"
#include <cstdint>
#include <vector>
//===============================

namespace spartan
{
    struct AnimChannel
    {
        uint32_t bone_index = 0;
        uint32_t first_sample = 0;
        uint32_t sample_count = 0;
    };

    struct ConstantChannel
    {
        uint32_t bone_index = 0;
    };

    struct ConstantPosition : ConstantChannel
    {
        math::Vector3 value = math::Vector3::Zero;
    };

    struct ConstantRotation : ConstantChannel
    {
        math::Quaternion value = math::Quaternion::Identity;
    };

    struct ConstantScale : ConstantChannel
    {
        math::Vector3 value = math::Vector3::One;
    };

    template <typename TValue, typename TConstant>
    struct AnimationTrackStream
    {
        std::vector<AnimChannel> channels;
        std::vector<TConstant> constants;
        std::vector<TValue> values;
    };

    using PositionTrackStream = AnimationTrackStream<math::Vector3, ConstantPosition>;
    using RotationTrackStream = AnimationTrackStream<math::Quaternion, ConstantRotation>;
    using ScaleTrackStream    = AnimationTrackStream<math::Vector3, ConstantScale>;

    struct AnimationClip
    {
        float duration_seconds = 0.0f;
        float sample_rate      = 30.0f;
        uint32_t joint_count   = 0;

        // Cooked base local pose (bind pose with constant channels already applied).
        std::vector<math::Vector3> base_local_positions;
        std::vector<math::Quaternion> base_local_rotations;
        std::vector<math::Vector3> base_local_scales;

        // Cooked runtime subsets.
        // sampled_bones: Indices of bones that have animation channels in this clip.
        std::vector<uint32_t> sampled_bones;

        PositionTrackStream position_stream;
        RotationTrackStream rotation_stream;
        ScaleTrackStream scale_stream;
    };
}
