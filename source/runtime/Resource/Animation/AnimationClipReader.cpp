/*
Copyright(c) 2015-2026 Panos Karabelas
*/

//= INCLUDES ==============================
#include "pch.h"
#include "AnimationClipReader.h"
#include "AnimationAssetValidation.h"
#include "AnimationFileFormat.h"
#include "AnimationLimits.h"
#include "BinaryIO.h"
//=========================================

namespace
{
    using namespace spartan::BinaryIO;
    using namespace spartan::animation_format;
    using namespace spartan::animation_limits;

    template <typename TValue, typename TConstant>
    bool read_track_stream(
        BoundedReader& reader,
        spartan::AnimationTrackStream<TValue, TConstant>& stream,
        const uint32_t channel_count,
        const uint32_t constant_count,
        const uint32_t sample_count)
    {
        return read_array(reader, stream.channels, channel_count) &&
            read_array(reader, stream.constants, constant_count) &&
            read_array(reader, stream.values, sample_count);
    }

    template <typename TValue, typename TConstant>
    bool account_track_stream(
        ByteBudget& budget,
        const uint32_t channel_count,
        const uint32_t constant_count,
        const uint32_t sample_count)
    {
        return budget.AddArray(channel_count, sizeof(spartan::AnimChannel)) &&
            budget.AddArray(constant_count, sizeof(TConstant)) &&
            budget.AddArray(sample_count, sizeof(TValue));
    }
}

namespace spartan
{
    bool AnimationClipReader::ReadFromFile(const std::string& path, AnimationClip& clip)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream.is_open())
            return false;

        uint64_t file_size = 0;
        if (!try_get_file_size(stream, file_size))
            return false;

        BoundedReader reader(stream, file_size);

        ClipHeader header = {};
        if (!read_pod(reader, header))
        {
            return false;
        }

        if (header.magic != clip_magic || header.version != clip_version)
            return false;

        if (header.joint_count > animation_limits::joint_count ||
            header.position_channel_count > animation_limits::channel_count ||
            header.rotation_channel_count > animation_limits::channel_count ||
            header.scale_channel_count > animation_limits::channel_count ||
            header.constant_position_count > animation_limits::constant_count ||
            header.constant_rotation_count > animation_limits::constant_count ||
            header.constant_scale_count > animation_limits::constant_count ||
            header.position_sample_count > animation_limits::sample_count ||
            header.rotation_sample_count > animation_limits::sample_count ||
            header.scale_sample_count > animation_limits::sample_count ||
            header.base_local_pose_count > animation_limits::joint_count ||
            header.sampled_bone_count > animation_limits::joint_count)
        {
            return false;
        }

        ByteBudget budget = {};
        budget.limit = animation_limits::max_clip_bytes;

        if (!account_track_stream<math::Vector3, ConstantPosition>(
                budget,
                header.position_channel_count,
                header.constant_position_count,
                header.position_sample_count) ||
            !account_track_stream<math::Quaternion, ConstantRotation>(
                budget,
                header.rotation_channel_count,
                header.constant_rotation_count,
                header.rotation_sample_count) ||
            !account_track_stream<math::Vector3, ConstantScale>(
                budget,
                header.scale_channel_count,
                header.constant_scale_count,
                header.scale_sample_count) ||
            !budget.AddArray(header.base_local_pose_count, sizeof(math::Vector3)) ||
            !budget.AddArray(header.base_local_pose_count, sizeof(math::Quaternion)) ||
            !budget.AddArray(header.base_local_pose_count, sizeof(math::Vector3)) ||
            !budget.AddArray(header.sampled_bone_count, sizeof(uint32_t)))
        {
            return false;
        }

        clip = {};
        clip.joint_count = header.joint_count;
        clip.duration_seconds = header.duration_seconds;
        clip.sample_rate = header.sample_rate;

        if (!read_track_stream(reader, clip.position_stream, header.position_channel_count, header.constant_position_count, header.position_sample_count) ||
            !read_track_stream(reader, clip.rotation_stream, header.rotation_channel_count, header.constant_rotation_count, header.rotation_sample_count) ||
            !read_track_stream(reader, clip.scale_stream, header.scale_channel_count, header.constant_scale_count, header.scale_sample_count) ||
            !read_array(reader, clip.base_local_positions, header.base_local_pose_count) ||
            !read_array(reader, clip.base_local_rotations, header.base_local_pose_count) ||
            !read_array(reader, clip.base_local_scales, header.base_local_pose_count) ||
            !read_array(reader, clip.sampled_bones, header.sampled_bone_count))
        {
            return false;
        }

        if (reader.remaining != 0)
            return false;

        if (!ValidateClip(clip, nullptr))
            return false;

        return true;
    }
}
