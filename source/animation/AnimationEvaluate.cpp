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

//= INCLUDES =====================
#include "pch.h"
#include "AnimationEvaluate.h"
#include "AnimationClip.h"
#include "Skeleton.h"
#include "SkeletalMeshBinding.h"
#include "../core/ThreadPool.h"
#include <cmath>
//================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        // resolve a frame to the two samples that straddle it, shared by all three stream types
        struct SamplePair
        {
            uint32_t i0 = 0;
            uint32_t i1 = 0;
            float t     = 0.0f;
        };

        SamplePair resolve_samples(const AnimChannel& channel, const float frame)
        {
            SamplePair pair;
            const float clamped = clamp(
                frame,
                0.0f,
                static_cast<float>(channel.sample_count - 1)
            );
            pair.i0 = channel.first_sample + static_cast<uint32_t>(clamped);
            pair.i1 = channel.first_sample + min(
                static_cast<uint32_t>(clamped) + 1u,
                channel.sample_count - 1u
            );
            pair.t = clamped - floorf(clamped);
            return pair;
        }

        template <typename TStream>
        void build_stream_index(
            const TStream& stream,
            const uint32_t joint_count,
            vector<int32_t>& channel_index,
            vector<int32_t>& constant_index
        )
        {
            channel_index.assign(joint_count, -1);
            constant_index.assign(joint_count, -1);

            for (size_t i = 0; i < stream.constants.size(); ++i)
            {
                const uint32_t bone = stream.constants[i].bone_index;
                if (bone < joint_count)
                {
                    constant_index[bone] = static_cast<int32_t>(i);
                }
            }

            for (size_t i = 0; i < stream.channels.size(); ++i)
            {
                const AnimChannel& channel = stream.channels[i];
                if (channel.bone_index < joint_count && channel.sample_count > 0)
                {
                    channel_index[channel.bone_index] = static_cast<int32_t>(i);
                }
            }
        }

        bool has_any_curve(const AnimationClip& clip, const uint32_t bone_index)
        {
            const ClipSampleIndex& index = clip.sample_index;
            if (!index.built || bone_index >= index.position_channel.size())
            {
                return false;
            }

            return index.position_channel[bone_index] >= 0 ||
                   index.position_constant[bone_index] >= 0 ||
                   index.rotation_channel[bone_index] >= 0 ||
                   index.rotation_constant[bone_index] >= 0 ||
                   index.scale_channel[bone_index] >= 0 ||
                   index.scale_constant[bone_index] >= 0;
        }

        bool is_finite_vector(const Vector3& v)
        {
            return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
        }

        // a rotation as axis times angle, this form decays like a plain vector
        Vector3 quaternion_to_rotation_vector(const Quaternion& q_in)
        {
            Quaternion q = q_in.Normalized();
            // shortest path, the offset must never take the long way round
            if (q.w < 0.0f)
            {
                q = -q;
            }

            const Vector3 axis(q.x, q.y, q.z);
            const float length = axis.Length();
            if (length < 1.0e-8f)
            {
                // small angle, sin(a/2) ~ a/2
                return axis * 2.0f;
            }

            const float angle = 2.0f * atan2f(length, q.w);
            return axis * (angle / length);
        }

        Quaternion rotation_vector_to_quaternion(const Vector3& v)
        {
            const float angle = v.Length();
            if (angle < 1.0e-8f)
            {
                return Quaternion::Identity;
            }

            return Quaternion::FromAxisAngle(v / angle, angle);
        }

        // critically damped decay with an exact closed form, never overshoots and needs no clamping
        void decay_spring(Vector3& x, Vector3& v, const float halflife, const float dt)
        {
            const float y = (4.0f * 0.69314718056f / (halflife + 1.0e-5f)) * 0.5f;
            const Vector3 j = v + x * y;
            const float e = expf(-y * dt);

            x = (x + j * dt) * e;
            v = (v - j * (y * dt)) * e;
        }

        // row-vector upper 3x3, skips translation and the w divide, bone matrices are rigid so the
        // plain 3x3 is also correct for normals, no inverse transpose needed
        Vector3 transform_direction(const Matrix& m, const Vector3& v)
        {
            return Vector3(
                v.x * m.m00 + v.y * m.m10 + v.z * m.m20,
                v.x * m.m01 + v.y * m.m11 + v.z * m.m21,
                v.x * m.m02 + v.y * m.m12 + v.z * m.m22
            );
        }
    }

    namespace animation_evaluate
    {
        void EnsureSampleIndex(const AnimationClip& clip)
        {
            ClipSampleIndex& index = clip.sample_index;
            if (index.built)
            {
                return;
            }

            const uint32_t joint_count = clip.joint_count;
            build_stream_index(
                clip.position_stream, joint_count, index.position_channel, index.position_constant);
            build_stream_index(
                clip.rotation_stream, joint_count, index.rotation_channel, index.rotation_constant);
            build_stream_index(
                clip.scale_stream, joint_count, index.scale_channel, index.scale_constant);

            index.built = joint_count > 0;
        }

        bool HasCurve(const AnimationClip& clip, const uint32_t bone_index)
        {
            EnsureSampleIndex(clip);
            return has_any_curve(clip, bone_index);
        }

        bool SampleLocals(
            const AnimationClip& clip,
            const Skeleton& skeleton,
            float time_seconds,
            vector<Matrix>& out_local_matrices,
            const bool loop
        )
        {
            const uint32_t joint_count = skeleton.joint_count;
            if (joint_count == 0 || skeleton.bind_local_matrices.size() != joint_count)
            {
                return false;
            }

            float time = time_seconds;
            if (clip.duration_seconds > 0.0f)
            {
                if (loop)
                {
                    time = fmodf(time, clip.duration_seconds);
                    if (time < 0.0f)
                    {
                        time += clip.duration_seconds;
                    }
                }
                else
                {
                    time = clamp(time, 0.0f, clip.duration_seconds);
                }
            }
            else
            {
                time = 0.0f;
            }

            const float frame = time * clip.sample_rate;

            // assigning into a caller owned buffer reuses its capacity, no allocation per frame
            out_local_matrices = skeleton.bind_local_matrices;

            EnsureSampleIndex(clip);
            const ClipSampleIndex& index = clip.sample_index;
            if (!index.built)
            {
                return true;
            }

            // walk only the bones the clip actually animates, the rest keep their bind local
            const bool have_sampled_list = !clip.sampled_bones.empty();
            const size_t bone_total = have_sampled_list ? clip.sampled_bones.size() : joint_count;

            // the table is sized by the clip, the pose by the skeleton, a bone must be valid in both
            const uint32_t bone_limit = min(joint_count, static_cast<uint32_t>(index.position_channel.size()));

            for (size_t n = 0; n < bone_total; ++n)
            {
                const uint32_t i = have_sampled_list
                    ? clip.sampled_bones[n]
                    : static_cast<uint32_t>(n);
                if (i >= bone_limit)
                {
                    continue;
                }

                const Matrix& bind_local = skeleton.bind_local_matrices[i];
                Vector3 pos = bind_local.GetTranslation();
                Quaternion rot = bind_local.GetRotation();
                Vector3 scale = bind_local.GetScale();
                if (fabsf(scale.x) < 1.0e-6f || fabsf(scale.y) < 1.0e-6f || fabsf(scale.z) < 1.0e-6f)
                {
                    scale = Vector3::One;
                }

                bool animated = false;

                if (const int32_t c = index.position_constant[i]; c >= 0)
                {
                    pos = clip.position_stream.constants[c].value;
                    animated = true;
                }
                else if (const int32_t c = index.position_channel[i]; c >= 0)
                {
                    const AnimChannel& channel = clip.position_stream.channels[c];
                    if (channel.sample_count == 1)
                    {
                        pos = clip.position_stream.values[channel.first_sample];
                    }
                    else
                    {
                        const SamplePair s = resolve_samples(channel, frame);
                        pos = Vector3::Lerp(
                            clip.position_stream.values[s.i0],
                            clip.position_stream.values[s.i1],
                            s.t
                        );
                    }
                    animated = true;
                }

                if (const int32_t c = index.rotation_constant[i]; c >= 0)
                {
                    rot = clip.rotation_stream.constants[c].value;
                    animated = true;
                }
                else if (const int32_t c = index.rotation_channel[i]; c >= 0)
                {
                    const AnimChannel& channel = clip.rotation_stream.channels[c];
                    if (channel.sample_count == 1)
                    {
                        rot = clip.rotation_stream.values[channel.first_sample];
                    }
                    else
                    {
                        const SamplePair s = resolve_samples(channel, frame);
                        rot = Quaternion::Lerp(
                            clip.rotation_stream.values[s.i0],
                            clip.rotation_stream.values[s.i1],
                            s.t
                        );
                    }
                    animated = true;
                }

                if (const int32_t c = index.scale_constant[i]; c >= 0)
                {
                    scale = clip.scale_stream.constants[c].value;
                    animated = true;
                }
                else if (const int32_t c = index.scale_channel[i]; c >= 0)
                {
                    const AnimChannel& channel = clip.scale_stream.channels[c];
                    if (channel.sample_count == 1)
                    {
                        scale = clip.scale_stream.values[channel.first_sample];
                    }
                    else
                    {
                        const SamplePair s = resolve_samples(channel, frame);
                        scale = Vector3::Lerp(
                            clip.scale_stream.values[s.i0],
                            clip.scale_stream.values[s.i1],
                            s.t
                        );
                    }
                    animated = true;
                }

                if (!animated || !is_finite_vector(pos) || !is_finite_vector(scale))
                {
                    continue;
                }

                out_local_matrices[i] = Matrix(pos, rot, scale);
            }

            return true;
        }

        bool SampleGlobals(
            const AnimationClip& clip,
            const Skeleton& skeleton,
            float time_seconds,
            vector<Matrix>& out_global_matrices
        )
        {
            vector<Matrix> local_matrices;
            if (!SampleLocals(clip, skeleton, time_seconds, local_matrices))
            {
                return false;
            }

            out_global_matrices.resize(local_matrices.size());
            skeleton.ComputeGlobalPose(local_matrices, out_global_matrices);
            return true;
        }

        bool BlendLocals(
            const vector<Matrix>& locals_a,
            const vector<Matrix>& locals_b,
            float t,
            vector<Matrix>& out_local_matrices
        )
        {
            if (locals_a.size() != locals_b.size() || locals_a.empty())
            {
                return false;
            }

            t = clamp(t, 0.0f, 1.0f);
            out_local_matrices.resize(locals_a.size());

            for (size_t i = 0; i < locals_a.size(); ++i)
            {
                if (t <= 0.0f)
                {
                    out_local_matrices[i] = locals_a[i];
                    continue;
                }

                if (t >= 1.0f)
                {
                    out_local_matrices[i] = locals_b[i];
                    continue;
                }

                const Vector3 pos = Vector3::Lerp(
                    locals_a[i].GetTranslation(),
                    locals_b[i].GetTranslation(),
                    t
                );
                const Quaternion rot = Quaternion::Lerp(
                    locals_a[i].GetRotation(),
                    locals_b[i].GetRotation(),
                    t
                );
                Vector3 scale = Vector3::Lerp(
                    locals_a[i].GetScale(),
                    locals_b[i].GetScale(),
                    t
                );
                if (fabsf(scale.x) < 1.0e-6f || fabsf(scale.y) < 1.0e-6f || fabsf(scale.z) < 1.0e-6f)
                {
                    scale = Vector3::One;
                }

                out_local_matrices[i] = Matrix(pos, rot, scale);
            }

            return true;
        }

        void PoseInertializer::Reset()
        {
            m_offsets.clear();
            m_active = false;
        }

        void PoseInertializer::Transition(
            const vector<Matrix>& src,
            const vector<Matrix>& src_previous,
            const vector<Matrix>& dst,
            const vector<Matrix>& dst_next,
            const float delta_time
        )
        {
            if (src.empty() || src.size() != dst.size())
            {
                Reset();
                return;
            }

            const float dt = delta_time > 1.0e-5f ? delta_time : 0.016f;
            const float inv_dt = 1.0f / dt;
            const bool has_src_velocity = src_previous.size() == src.size();
            const bool has_dst_velocity = dst_next.size() == dst.size();

            m_offsets.resize(src.size());

            for (size_t i = 0; i < src.size(); ++i)
            {
                JointOffset& offset = m_offsets[i];

                const Vector3 src_p = src[i].GetTranslation();
                const Vector3 dst_p = dst[i].GetTranslation();
                offset.position = src_p - dst_p;

                Vector3 src_velocity = Vector3::Zero;
                if (has_src_velocity)
                {
                    src_velocity = (src_p - src_previous[i].GetTranslation()) * inv_dt;
                }

                Vector3 dst_velocity = Vector3::Zero;
                if (has_dst_velocity)
                {
                    dst_velocity = (dst_next[i].GetTranslation() - dst_p) * inv_dt;
                }

                offset.position_velocity = src_velocity - dst_velocity;

                const Quaternion src_r = src[i].GetRotation();
                const Quaternion dst_r = dst[i].GetRotation();
                offset.rotation = quaternion_to_rotation_vector(src_r * dst_r.Inverse());

                Vector3 src_angular = Vector3::Zero;
                if (has_src_velocity)
                {
                    src_angular = quaternion_to_rotation_vector(
                        src_r * src_previous[i].GetRotation().Inverse()) * inv_dt;
                }

                Vector3 dst_angular = Vector3::Zero;
                if (has_dst_velocity)
                {
                    dst_angular = quaternion_to_rotation_vector(
                        dst_next[i].GetRotation() * dst_r.Inverse()) * inv_dt;
                }

                offset.rotation_velocity = src_angular - dst_angular;
            }

            m_active = true;
        }

        void PoseInertializer::Apply(
            vector<Matrix>& pose,
            const float delta_time,
            const float halflife
        )
        {
            if (!m_active || m_offsets.size() != pose.size())
            {
                return;
            }

            const float dt = delta_time > 0.0f ? delta_time : 0.016f;
            bool any_remaining = false;

            for (size_t i = 0; i < pose.size(); ++i)
            {
                JointOffset& offset = m_offsets[i];

                decay_spring(offset.position, offset.position_velocity, halflife, dt);
                decay_spring(offset.rotation, offset.rotation_velocity, halflife, dt);

                if (!is_finite_vector(offset.position) || !is_finite_vector(offset.rotation))
                {
                    offset = {};
                    continue;
                }

                const bool position_done =
                    offset.position.LengthSquared() < 1.0e-10f &&
                    offset.position_velocity.LengthSquared() < 1.0e-10f;
                const bool rotation_done =
                    offset.rotation.LengthSquared() < 1.0e-10f &&
                    offset.rotation_velocity.LengthSquared() < 1.0e-10f;

                if (position_done && rotation_done)
                {
                    offset = {};
                    continue;
                }

                any_remaining = true;

                pose[i] = Matrix(
                    pose[i].GetTranslation() + offset.position,
                    rotation_vector_to_quaternion(offset.rotation) * pose[i].GetRotation(),
                    pose[i].GetScale()
                );
            }

            m_active = any_remaining;
        }

        bool SkinMesh(
            const SkeletalMeshBinding& binding,
            const vector<Matrix>& global_matrices,
            const vector<Matrix>& bind_inverse_global_matrices,
            const vector<RHI_Vertex_PosTexNorTan>& bind_vertices,
            vector<RHI_Vertex_PosTexNorTan>& out_vertices,
            vector<Matrix>& skin_matrices
        )
        {
            if (bind_vertices.empty() || global_matrices.empty())
            {
                return false;
            }

            if (out_vertices.size() != bind_vertices.size())
            {
                out_vertices = bind_vertices;
            }

            skin_matrices.resize(global_matrices.size());

            for (const SkeletalMeshSection& section : binding.GetSections())
            {
                if (!section.IsValid() || section.vertex_count == 0)
                {
                    continue;
                }

                const uint32_t end = section.vertex_input_offset + section.vertex_count;
                if (end > bind_vertices.size())
                {
                    continue;
                }

                // assimp column skin is G * offset * v; our row-vector form is offset * global
                const bool use_assimp_offset =
                    section.inverse_bind_matrices.size() >= global_matrices.size();
                for (
                    uint32_t bone_index = 0;
                    bone_index <
                        static_cast<uint32_t>(
                            global_matrices.size()
                        );
                    bone_index++
                )
                {
                    if (use_assimp_offset)
                    {
                        skin_matrices[bone_index] =
                            section.inverse_bind_matrices[
                                bone_index
                            ] *
                            global_matrices[bone_index];
                    }
                    else if (
                        bone_index <
                            bind_inverse_global_matrices.size()
                    )
                    {
                        skin_matrices[bone_index] =
                            global_matrices[bone_index] *
                            bind_inverse_global_matrices[
                                bone_index
                            ];
                    }
                }

                auto skin_vertices =
                    [&](
                        const uint32_t vertex_start,
                        const uint32_t vertex_end
                    )
                {
                    for (
                        uint32_t local_v = vertex_start;
                        local_v < vertex_end;
                        local_v++
                    )
                    {
                        const uint32_t vertex_index =
                            section.vertex_input_offset +
                            local_v;
                        const SkeletalVertexInfluence& influence =
                            section.influences[local_v];
                        const RHI_Vertex_PosTexNorTan& bind_vertex =
                            bind_vertices[vertex_index];
                        const Vector3 bind_pos = bind_vertex.get_position();
                        const Vector3 bind_nor = bind_vertex.get_normal();
                        const Vector3 bind_tan = bind_vertex.get_tangent();

                        Vector3 skinned = Vector3::Zero;
                        Vector3 skinned_nor = Vector3::Zero;
                        Vector3 skinned_tan = Vector3::Zero;
                        float weight_sum = 0.0f;

                        for (uint32_t w = 0; w < 4; ++w)
                        {
                            const float weight =
                                influence.bone_weights[w];
                            if (weight <= 0.0f)
                            {
                                continue;
                            }

                            const uint16_t bone_index =
                                influence.bone_indices[w];
                            if (
                                bone_index >=
                                global_matrices.size()
                            )
                            {
                                continue;
                            }

                            if (
                                !use_assimp_offset &&
                                bone_index >=
                                    bind_inverse_global_matrices.size()
                            )
                            {
                                continue;
                            }

                            const Matrix& skin = skin_matrices[bone_index];
                            skinned += (skin * bind_pos) * weight;
                            skinned_nor +=
                                transform_direction(skin, bind_nor) * weight;
                            skinned_tan +=
                                transform_direction(skin, bind_tan) * weight;
                            weight_sum += weight;
                        }

                        if (
                            weight_sum <= 0.0f ||
                            !is_finite_vector(skinned) ||
                            skinned.LengthSquared() >
                                1.0e12f
                        )
                        {
                            out_vertices[vertex_index].set_position(bind_pos);
                            out_vertices[vertex_index].set_normal(bind_nor);
                            out_vertices[vertex_index].set_tangent(bind_tan);
                            continue;
                        }

                        // importers can leave weights summing to less than one, without this the
                        // vertex is dragged toward the model origin and the mesh shrinks
                        if (fabsf(weight_sum - 1.0f) > 1.0e-4f)
                        {
                            skinned = skinned / weight_sum;
                        }

                        out_vertices[vertex_index].set_position(skinned);

                        // lighting used bind pose normals before this, so shading never followed a
                        // rotating limb, the mesh deformed but the light on it did not
                        out_vertices[vertex_index].set_normal(
                            skinned_nor.LengthSquared() > 1.0e-12f
                                ? skinned_nor.Normalized()
                                : bind_nor
                        );
                        out_vertices[vertex_index].set_tangent(
                            skinned_tan.LengthSquared() > 1.0e-12f
                                ? skinned_tan.Normalized()
                                : bind_tan
                        );
                    }
                };

                if (section.vertex_count >= 4096)
                {
                    ThreadPool::ParallelLoop(
                        move(skin_vertices),
                        section.vertex_count
                    );
                }
                else
                {
                    skin_vertices(
                        0,
                        section.vertex_count
                    );
                }
            }

            return true;
        }
    }
}
