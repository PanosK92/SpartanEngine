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
        Vector3 sample_vector_stream(
            const PositionTrackStream& stream,
            const uint32_t bone_index,
            const float frame,
            const Vector3& fallback
        )
        {
            for (const ConstantPosition& constant : stream.constants)
            {
                if (constant.bone_index == bone_index)
                {
                    return constant.value;
                }
            }

            for (const AnimChannel& channel : stream.channels)
            {
                if (channel.bone_index != bone_index || channel.sample_count == 0)
                {
                    continue;
                }

                if (channel.sample_count == 1)
                {
                    return stream.values[channel.first_sample];
                }

                const float clamped = clamp(
                    frame,
                    0.0f,
                    static_cast<float>(channel.sample_count - 1)
                );
                const uint32_t i0 = static_cast<uint32_t>(clamped);
                const uint32_t i1 = min(i0 + 1u, channel.sample_count - 1u);
                const float t = clamped - static_cast<float>(i0);

                return Vector3::Lerp(
                    stream.values[channel.first_sample + i0],
                    stream.values[channel.first_sample + i1],
                    t
                );
            }

            return fallback;
        }

        Quaternion sample_rotation_stream(
            const RotationTrackStream& stream,
            const uint32_t bone_index,
            const float frame,
            const Quaternion& fallback
        )
        {
            for (const ConstantRotation& constant : stream.constants)
            {
                if (constant.bone_index == bone_index)
                {
                    return constant.value;
                }
            }

            for (const AnimChannel& channel : stream.channels)
            {
                if (channel.bone_index != bone_index || channel.sample_count == 0)
                {
                    continue;
                }

                if (channel.sample_count == 1)
                {
                    return stream.values[channel.first_sample];
                }

                const float clamped = clamp(
                    frame,
                    0.0f,
                    static_cast<float>(channel.sample_count - 1)
                );
                const uint32_t i0 = static_cast<uint32_t>(clamped);
                const uint32_t i1 = min(i0 + 1u, channel.sample_count - 1u);
                const float t = clamped - static_cast<float>(i0);

                return Quaternion::Lerp(
                    stream.values[channel.first_sample + i0],
                    stream.values[channel.first_sample + i1],
                    t
                );
            }

            return fallback;
        }

        Vector3 sample_scale_stream(
            const ScaleTrackStream& stream,
            const uint32_t bone_index,
            const float frame,
            const Vector3& fallback
        )
        {
            for (const ConstantScale& constant : stream.constants)
            {
                if (constant.bone_index == bone_index)
                {
                    return constant.value;
                }
            }

            for (const AnimChannel& channel : stream.channels)
            {
                if (channel.bone_index != bone_index || channel.sample_count == 0)
                {
                    continue;
                }

                if (channel.sample_count == 1)
                {
                    return stream.values[channel.first_sample];
                }

                const float clamped = clamp(
                    frame,
                    0.0f,
                    static_cast<float>(channel.sample_count - 1)
                );
                const uint32_t i0 = static_cast<uint32_t>(clamped);
                const uint32_t i1 = min(i0 + 1u, channel.sample_count - 1u);
                const float t = clamped - static_cast<float>(i0);

                return Vector3::Lerp(
                    stream.values[channel.first_sample + i0],
                    stream.values[channel.first_sample + i1],
                    t
                );
            }

            return fallback;
        }

        bool has_any_curve(const AnimationClip& clip, const uint32_t bone_index)
        {
            for (const auto& c : clip.position_stream.constants)
            {
                if (c.bone_index == bone_index) return true;
            }
            for (const auto& c : clip.position_stream.channels)
            {
                if (c.bone_index == bone_index) return true;
            }
            for (const auto& c : clip.rotation_stream.constants)
            {
                if (c.bone_index == bone_index) return true;
            }
            for (const auto& c : clip.rotation_stream.channels)
            {
                if (c.bone_index == bone_index) return true;
            }
            for (const auto& c : clip.scale_stream.constants)
            {
                if (c.bone_index == bone_index) return true;
            }
            for (const auto& c : clip.scale_stream.channels)
            {
                if (c.bone_index == bone_index) return true;
            }
            return false;
        }

        bool is_finite_vector(const Vector3& v)
        {
            return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
        }
    }

    namespace animation_evaluate
    {
        bool HasCurve(const AnimationClip& clip, const uint32_t bone_index)
        {
            return has_any_curve(clip, bone_index);
        }

        bool SampleLocals(
            const AnimationClip& clip,
            const Skeleton& skeleton,
            float time_seconds,
            vector<Matrix>& out_local_matrices
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
                time = fmodf(time, clip.duration_seconds);
                if (time < 0.0f)
                {
                    time += clip.duration_seconds;
                }
            }
            else
            {
                time = 0.0f;
            }

            const float frame = time * clip.sample_rate;
            out_local_matrices = skeleton.bind_local_matrices;

            for (uint32_t i = 0; i < joint_count; ++i)
            {
                if (!has_any_curve(clip, i))
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

                pos = sample_vector_stream(clip.position_stream, i, frame, pos);
                rot = sample_rotation_stream(clip.rotation_stream, i, frame, rot);
                scale = sample_scale_stream(clip.scale_stream, i, frame, scale);

                if (!is_finite_vector(pos) || !is_finite_vector(scale))
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

        bool SkinMesh(
            const SkeletalMeshBinding& binding,
            const vector<Matrix>& global_matrices,
            const vector<Matrix>& bind_global_matrices,
            const vector<RHI_Vertex_PosTexNorTan>& bind_vertices,
            vector<RHI_Vertex_PosTexNorTan>& out_vertices
        )
        {
            if (bind_vertices.empty() || global_matrices.empty())
            {
                return false;
            }

            out_vertices = bind_vertices;

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

                for (uint32_t local_v = 0; local_v < section.vertex_count; ++local_v)
                {
                    const uint32_t vertex_index = section.vertex_input_offset + local_v;
                    const SkeletalVertexInfluence& influence = section.influences[local_v];
                    const Vector3 bind_pos = bind_vertices[vertex_index].get_position();

                    Vector3 skinned = Vector3::Zero;
                    float weight_sum = 0.0f;

                    for (uint32_t w = 0; w < 4; ++w)
                    {
                        const float weight = influence.bone_weights[w];
                        if (weight <= 0.0f)
                        {
                            continue;
                        }

                        const uint16_t bone_index = influence.bone_indices[w];
                        if (bone_index >= global_matrices.size())
                        {
                            continue;
                        }

                        Matrix skin;
                        if (use_assimp_offset)
                        {
                            skin = section.inverse_bind_matrices[bone_index] *
                                global_matrices[bone_index];
                        }
                        else if (bone_index < bind_global_matrices.size())
                        {
                            skin = global_matrices[bone_index] *
                                bind_global_matrices[bone_index].Inverted();
                        }
                        else
                        {
                            continue;
                        }

                        skinned += (skin * bind_pos) * weight;
                        weight_sum += weight;
                    }

                    if (weight_sum <= 0.0f || !is_finite_vector(skinned) || skinned.LengthSquared() > 1.0e12f)
                    {
                        continue;
                    }

                    out_vertices[vertex_index].set_position(skinned);
                }
            }

            return true;
        }
    }
}
