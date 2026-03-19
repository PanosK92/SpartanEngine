/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

//= INCLUDES ================================
#include "pch.h"
#include "AnimationAssetValidation.h"
#include "BinaryIO.h"
#include "AnimationFileFormat.h"
//===========================================

//= NAMESPACES ================================
using namespace spartan::BinaryIO;
using namespace spartan::animation_limits;
//=============================================

namespace
{
    constexpr float quaternion_epsilon = 1e-5f;

    bool fail(std::string* error, const std::string& message)
    {
        if (error)
            *error = message;

        return false;
    }

    bool is_finite_quaternion(const spartan::math::Quaternion& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
    }

    bool is_unit_quaternion(const spartan::math::Quaternion& value)
    {
        const float length_squared = value.LengthSquared();
        return std::isfinite(length_squared) && std::abs(length_squared - 1.0f) <= quaternion_epsilon;
    }

    bool is_valid(const spartan::math::Vector3& value)
    {
        return value.IsFinite();
    }

    bool is_valid(const spartan::math::Quaternion& value)
    {
        return is_finite_quaternion(value) && is_unit_quaternion(value);
    }

    bool fail_track(std::string* error, const char* stream_name, const char* message)
    {
        return fail(error, std::string("AnimationClip ") + stream_name + " " + message);
    }

    template <typename TValue>
    bool validate_samples(
        const std::vector<TValue>& values,
        const uint32_t first_sample,
        const uint32_t sample_count)
    {
        if (sample_count == 0)
            return false;

        const uint64_t start = first_sample;
        const uint64_t end = start + sample_count;

        for (uint64_t i = start; i < end; ++i)
        {
            if (!is_valid(values[static_cast<size_t>(i)]))
                return false;
        }

        return true;
    }

    template <typename TConstant>
    bool validate_constants(
        const std::vector<TConstant>& constants,
        const uint32_t bone_count,
        std::vector<uint8_t>& out_bone_mask)
    {
        out_bone_mask.assign(bone_count, 0);

        for (const TConstant& constant : constants)
        {
            if (constant.bone_index >= bone_count)
                return false;

            if (out_bone_mask[constant.bone_index] != 0)
                return false;

            if (!is_valid(constant.value))
                return false;

            out_bone_mask[constant.bone_index] = 1;
        }

        return true;
    }

    template <typename TPositions, typename TRotations, typename TScales>
    bool validate_pose_arrays(
        const TPositions& positions,
        const TRotations& rotations,
        const TScales& scales,
        const uint32_t count,
        const bool require_unit_quaternions)
    {
        if (positions.size() != count ||
            rotations.size() != count ||
            scales.size() != count)
        {
            return false;
        }

        for (uint32_t i = 0; i < count; ++i)
        {
            if (!is_valid(positions[i]) || !is_valid(scales[i]))
                return false;

            if (require_unit_quaternions)
            {
                if (!is_valid(rotations[i]))
                    return false;
            }
            else if (!is_finite_quaternion(rotations[i]))
            {
                return false;
            }
        }

        return true;
    }

    bool has_overlap(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
    {
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (a[i] != 0 && b[i] != 0)
                return true;
        }

        return false;
    }

    template <typename TValue, typename TConstant>
    bool validate_track_stream(
        const spartan::AnimationTrackStream<TValue, TConstant>& stream,
        const uint32_t bone_count,
        std::string* error,
        const char* stream_name)
    {
        if (stream.channels.size() > channel_count)
            return fail_track(error, stream_name, "channel count exceeds limit");

        if (stream.constants.size() > constant_count)
            return fail_track(error, stream_name, "constant count exceeds limit");

        if (stream.values.size() > sample_count)
            return fail_track(error, stream_name, "sample count exceeds limit");

        std::vector<uint8_t> channel_bones;
        channel_bones.assign(bone_count, 0);
        for (const spartan::AnimChannel& channel : stream.channels)
        {
            if (channel.bone_index >= bone_count)
                return fail_track(error, stream_name, "bone index out of range");

            if (channel.sample_count < 1 || channel.sample_count > samples_per_channel)
                return fail_track(error, stream_name, "sample count is invalid");

            const uint64_t first = channel.first_sample;
            const uint64_t count = channel.sample_count;
            uint64_t end = 0;
            if (!checked_add_u64(first, count, end) || end > stream.values.size())
                return fail_track(error, stream_name, "sample range is invalid");

            if (channel_bones[channel.bone_index] != 0)
                return fail_track(error, stream_name, "channels contain duplicate bones");

            channel_bones[channel.bone_index] = 1;

            if (!validate_samples(stream.values, channel.first_sample, channel.sample_count))
                return fail_track(error, stream_name, "samples are invalid");
        }

        std::vector<uint8_t> constant_bones;
        if (!validate_constants(stream.constants, bone_count, constant_bones))
            return fail_track(error, stream_name, "constants are invalid");

        if (has_overlap(channel_bones, constant_bones))
            return fail_track(error, stream_name, "constants overlap variable channels");

        return true;
    }

    bool validate_base_pose(const spartan::AnimationClip& clip)
    {
        return validate_pose_arrays(
            clip.base_local_positions,
            clip.base_local_rotations,
            clip.base_local_scales,
            clip.joint_count,
            true);
    }
}

namespace spartan
{
    bool ValidateSkeleton(const Skeleton& skeleton, std::string* error)
    {
        const uint32_t bone_count = static_cast<uint32_t>(skeleton.joint_count);
        if (bone_count == 0)
            return fail(error, "Skeleton has no joints");

        if (bone_count > animation_limits::joint_count)
            return fail(error, "Skeleton joint count exceeds limit");

        const auto& bind_positions = skeleton.bind_positions;
        const auto& bind_rotations = skeleton.bind_rotations;
        const auto& bind_scales = skeleton.bind_scales;
        const auto& parents = skeleton.parent_indices;

        if (parents.size() != bone_count)
            return fail(error, "Skeleton arrays have mismatched sizes");

        if (!validate_pose_arrays(bind_positions, bind_rotations, bind_scales, bone_count, true))
            return fail(error, "Skeleton bind pose contains invalid values");

        if (parents[0] != -1)
            return fail(error, "Skeleton root joint must be stored at index 0");

        for (uint32_t bone = 1; bone < bone_count; ++bone)
        {
            const int16_t parent = parents[bone];
            if (parent < 0 || parent >= static_cast<int16_t>(bone))
                return fail(error, "Skeleton parent index is invalid");
        }

        return true;
    }

    bool ValidateClip(const AnimationClip& clip, std::string* error)
    {
        if (!std::isfinite(clip.duration_seconds) || clip.duration_seconds < 0.0f)
            return fail(error, "AnimationClip duration must be finite and >= 0");

        if (!std::isfinite(clip.sample_rate) || clip.sample_rate <= 0.0f)
            return fail(error, "AnimationClip sample_rate must be finite and > 0");

        if (clip.joint_count > animation_limits::joint_count)
            return fail(error, "AnimationClip joint count exceeds limit");

        if (!validate_base_pose(clip))
            return fail(error, "AnimationClip cooked base pose is invalid");

        if (clip.sampled_bones.size() > clip.joint_count)
            return fail(error, "AnimationClip sampled bone count exceeds joint_count");

        if (!clip.sampled_bones.empty())
        {
            std::vector<uint8_t> sampled_mask(clip.joint_count, 0);
            for (const uint32_t bone : clip.sampled_bones)
            {
                if (bone >= clip.joint_count)
                    return fail(error, "AnimationClip sampled bone index out of range");

                if (sampled_mask[bone] != 0)
                    return fail(error, "AnimationClip sampled bones contain duplicates");

                sampled_mask[bone] = 1;
            }
        }

        uint64_t total_samples = 0;
        if (!checked_add_u64(total_samples, clip.position_stream.values.size(), total_samples) ||
            !checked_add_u64(total_samples, clip.rotation_stream.values.size(), total_samples) ||
            !checked_add_u64(total_samples, clip.scale_stream.values.size(), total_samples) ||
            total_samples > sample_count)
        {
            return fail(error, "AnimationClip total sample count exceeds limit");
        }

        if (!validate_track_stream(clip.position_stream, clip.joint_count, error, "position"))
            return false;

        if (!validate_track_stream(clip.rotation_stream, clip.joint_count, error, "rotation"))
            return false;

        if (!validate_track_stream(clip.scale_stream, clip.joint_count, error, "scale"))
            return false;

        return true;
    }

    bool ValidateClip(const AnimationClip& clip, const Skeleton& skeleton, std::string* error)
    {
        if (!ValidateSkeleton(skeleton, error))
            return false;

        if (!ValidateClip(clip, error))
            return false;

        if (clip.joint_count != static_cast<uint32_t>(skeleton.joint_count))
            return fail(error, "AnimationClip joint_count does not match skeleton joint count");

        return true;
    }
}
