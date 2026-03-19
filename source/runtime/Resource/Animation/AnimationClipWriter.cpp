/*
Copyright(c) 2015-2026 Panos Karabelas
*/

//= INCLUDES ==============================
#include "pch.h"
#include "AnimationClipWriter.h"
#include "AnimationAssetValidation.h"
#include "AnimationFileFormat.h"
#include "BinaryIO.h"
//=========================================

namespace
{
    using namespace spartan::BinaryIO;
    using namespace spartan::animation_format;

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
