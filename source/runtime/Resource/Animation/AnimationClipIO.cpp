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

//= INCLUDES ==============================
#include "pch.h"
#include "AnimationClipIO.h"
#include "AnimationAssetValidation.h"
#include "AnimationFileFormat.h"
#include "BinaryIO.h"
//=========================================

//= NAMESPACES ================================
using namespace spartan::BinaryIO;
using namespace spartan::animation_format;
using namespace spartan::animation_limits;
//=============================================

namespace
{
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

    template <typename TValue, typename TConstant>
    bool write_track_stream(
        std::ofstream& stream,
        const spartan::AnimationTrackStream<TValue, TConstant>& track_stream)
    {
        return write_array(stream, track_stream.channels) &&
            write_array(stream, track_stream.constants) &&
            write_array(stream, track_stream.values);
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

    bool AnimationClipWriter::WriteToFile(const AnimationClip& clip, const std::string& path)
    {
        if (!ValidateClip(clip, nullptr))
            return false;

        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream.is_open())
            return false;

        ClipHeader header = {};
        header.joint_count = clip.joint_count;
        header.duration_seconds = clip.duration_seconds;
        header.sample_rate = clip.sample_rate;
        header.position_channel_count = static_cast<uint32_t>(clip.position_stream.channels.size());
        header.rotation_channel_count = static_cast<uint32_t>(clip.rotation_stream.channels.size());
        header.scale_channel_count = static_cast<uint32_t>(clip.scale_stream.channels.size());
        header.constant_position_count = static_cast<uint32_t>(clip.position_stream.constants.size());
        header.constant_rotation_count = static_cast<uint32_t>(clip.rotation_stream.constants.size());
        header.constant_scale_count = static_cast<uint32_t>(clip.scale_stream.constants.size());
        header.position_sample_count = static_cast<uint32_t>(clip.position_stream.values.size());
        header.rotation_sample_count = static_cast<uint32_t>(clip.rotation_stream.values.size());
        header.scale_sample_count = static_cast<uint32_t>(clip.scale_stream.values.size());
        header.base_local_pose_count = static_cast<uint32_t>(clip.base_local_positions.size());
        header.sampled_bone_count = static_cast<uint32_t>(clip.sampled_bones.size());

        if (!write_pod(stream, header))
        {
            return false;
        }

        if (!write_track_stream(stream, clip.position_stream) ||
            !write_track_stream(stream, clip.rotation_stream) ||
            !write_track_stream(stream, clip.scale_stream) ||
            !write_array(stream, clip.base_local_positions) ||
            !write_array(stream, clip.base_local_rotations) ||
            !write_array(stream, clip.base_local_scales) ||
            !write_array(stream, clip.sampled_bones))
        {
            return false;
        }

        return stream.good();
    }
}
