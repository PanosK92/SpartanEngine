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
#include "AnimationIk.h"
#include "Skeleton.h"
#include <algorithm>
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
        constexpr float k_epsilon = 1.0e-5f;

        Matrix make_trs(const Vector3& t, const Quaternion& r, const Vector3& s)
        {
            return Matrix(t, r, s);
        }

        Matrix to_local(const Matrix& global, const Matrix& parent_global)
        {
            return global * parent_global.Inverted();
        }

        Vector3 flatten_on_plane(const Vector3& v, const Vector3& normal)
        {
            Vector3 flat = v - normal * v.Dot(normal);
            if (flat.LengthSquared() < k_epsilon)
            {
                return Vector3::Zero;
            }
            return flat.Normalized();
        }
    }

    namespace animation_ik
    {
        bool SolveTwoBone(
            const Skeleton& skeleton,
            vector<Matrix>& local_matrices,
            const uint32_t root_index,
            const uint32_t mid_index,
            const uint32_t end_index,
            const Vector3& target_model,
            const Vector3& pole_model,
            const float weight
        )
        {
            if (weight <= 0.0f ||
                local_matrices.size() != skeleton.joint_count ||
                root_index >= skeleton.joint_count ||
                mid_index >= skeleton.joint_count ||
                end_index >= skeleton.joint_count)
            {
                return false;
            }

            const float w = clamp(weight, 0.0f, 1.0f);

            vector<Matrix> globals(skeleton.joint_count);
            skeleton.ComputeGlobalPose(local_matrices, globals);

            const Vector3 root_pos = globals[root_index].GetTranslation();
            const Vector3 mid_pos  = globals[mid_index].GetTranslation();
            const Vector3 end_pos  = globals[end_index].GetTranslation();

            const float len_upper = (mid_pos - root_pos).Length();
            const float len_lower = (end_pos - mid_pos).Length();
            if (len_upper < k_epsilon || len_lower < k_epsilon)
            {
                return false;
            }

            Vector3 to_target = target_model - root_pos;
            float len_target = to_target.Length();
            if (len_target < k_epsilon)
            {
                return false;
            }

            const float max_reach = len_upper + len_lower - k_epsilon;
            const float min_reach = fabsf(len_upper - len_lower) + k_epsilon;
            len_target = clamp(len_target, min_reach, max_reach);
            const Vector3 to_target_dir = to_target.Normalized();
            const Vector3 clamped_target = root_pos + to_target_dir * len_target;

            Vector3 plane_n = (clamped_target - root_pos).Cross(pole_model - root_pos);
            if (plane_n.LengthSquared() < k_epsilon)
            {
                plane_n = (clamped_target - root_pos).Cross(mid_pos - root_pos);
            }
            if (plane_n.LengthSquared() < k_epsilon)
            {
                plane_n = (clamped_target - root_pos).Cross(Vector3::Up);
            }
            if (plane_n.LengthSquared() < k_epsilon)
            {
                return false;
            }
            plane_n.Normalize();

            const float cos_a = clamp(
                (len_upper * len_upper + len_target * len_target - len_lower * len_lower) /
                    (2.0f * len_upper * len_target),
                -1.0f,
                1.0f
            );
            const float sin_a = sqrtf(max(0.0f, 1.0f - cos_a * cos_a));
            Vector3 bend_dir = plane_n.Cross(to_target_dir).Normalized();

            // always bend toward the pole, never follow a flipped animated knee
            Vector3 pole_off = pole_model - root_pos;
            pole_off = pole_off - to_target_dir * pole_off.Dot(to_target_dir);
            if (pole_off.LengthSquared() > k_epsilon && bend_dir.Dot(pole_off) < 0.0f)
            {
                bend_dir = -bend_dir;
            }

            const Vector3 ik_mid =
                root_pos + to_target_dir * (cos_a * len_upper) + bend_dir * (sin_a * len_upper);

            const Vector3 blended_mid = Vector3::Lerp(mid_pos, ik_mid, w);
            const Vector3 blended_end = Vector3::Lerp(end_pos, clamped_target, w);

            const Vector3 old_upper = (mid_pos - root_pos).Normalized();
            const Vector3 new_upper = (blended_mid - root_pos).Normalized();
            const Quaternion root_rot = Quaternion::FromRotation(old_upper, new_upper) *
                globals[root_index].GetRotation();

            const int16_t root_parent = skeleton.parent_indices[root_index];
            const Matrix root_parent_global = root_parent < 0
                ? Matrix::Identity
                : globals[static_cast<uint32_t>(root_parent)];

            const Matrix root_global_ik = make_trs(
                root_pos,
                root_rot,
                globals[root_index].GetScale()
            );
            local_matrices[root_index] = to_local(root_global_ik, root_parent_global);

            const Vector3 old_lower = (end_pos - mid_pos).Normalized();
            const Vector3 new_lower = (blended_end - blended_mid).Normalized();
            if (new_lower.LengthSquared() < k_epsilon)
            {
                return false;
            }

            const Quaternion mid_rot_global = Quaternion::FromRotation(old_lower, new_lower) *
                globals[mid_index].GetRotation();
            const Matrix mid_global_ik = make_trs(
                blended_mid,
                mid_rot_global,
                globals[mid_index].GetScale()
            );
            local_matrices[mid_index] = to_local(mid_global_ik, root_global_ik);
            return true;
        }

        bool PlantFoot(
            const Skeleton& skeleton,
            vector<Matrix>& local_matrices,
            const uint32_t end_index,
            const Vector3& ground_normal_model,
            const Vector3& toe_forward_model,
            const Vector3& sole_up_local,
            const Vector3& toe_fwd_local,
            const float weight,
            const int32_t ball_index
        )
        {
            if (weight <= 0.0f ||
                local_matrices.size() != skeleton.joint_count ||
                end_index >= skeleton.joint_count ||
                ground_normal_model.LengthSquared() < k_epsilon ||
                sole_up_local.LengthSquared() < k_epsilon ||
                toe_fwd_local.LengthSquared() < k_epsilon)
            {
                return false;
            }

            const float w = clamp(weight, 0.0f, 1.0f);
            vector<Matrix> globals(skeleton.joint_count);
            skeleton.ComputeGlobalPose(local_matrices, globals);

            Vector3 w_up = ground_normal_model.Normalized();
            // keep sole facing above the surface
            if (w_up.Dot(Vector3::Up) < 0.0f)
            {
                w_up = -w_up;
            }

            const Quaternion end_rot = globals[end_index].GetRotation();

            // pitch/roll: minimal rotation so bind sole up meets ground normal
            const Vector3 sole_up_model = (end_rot * sole_up_local.Normalized()).Normalized();
            Quaternion planted = Quaternion::FromRotation(sole_up_model, w_up) * end_rot;

            // yaw: align toe on the ground plane without tipping the sole
            Vector3 w_fwd = flatten_on_plane(toe_forward_model, w_up);
            if (w_fwd.LengthSquared() < k_epsilon)
            {
                w_fwd = flatten_on_plane(Vector3(0.0f, 0.0f, -1.0f), w_up);
            }
            Vector3 toe_now = flatten_on_plane(planted * toe_fwd_local.Normalized(), w_up);
            if (w_fwd.LengthSquared() > k_epsilon && toe_now.LengthSquared() > k_epsilon)
            {
                planted = Quaternion::FromRotation(toe_now.Normalized(), w_fwd.Normalized()) * planted;
            }

            const Quaternion end_rot_blend = Quaternion::Lerp(end_rot, planted, w);

            const int16_t parent = skeleton.parent_indices[end_index];
            const Matrix parent_global = parent < 0
                ? Matrix::Identity
                : globals[static_cast<uint32_t>(parent)];

            local_matrices[end_index] = to_local(
                make_trs(
                    globals[end_index].GetTranslation(),
                    end_rot_blend,
                    globals[end_index].GetScale()
                ),
                parent_global
            );

            // planting rotates the foot in world space. ball local is relative to the foot, so
            // without compensation the toes tip with that delta. undo only the plant delta so
            // left/right both keep the clip toe pose instead of freezing to bind.
            if (ball_index >= 0 &&
                static_cast<uint32_t>(ball_index) < skeleton.joint_count &&
                skeleton.parent_indices[static_cast<uint32_t>(ball_index)] ==
                    static_cast<int16_t>(end_index) &&
                w > 0.0f)
            {
                const uint32_t ball_i = static_cast<uint32_t>(ball_index);
                const Matrix& ball_local = local_matrices[ball_i];
                const Quaternion ball_anim = ball_local.GetRotation();

                // row-vector: ball_world = ball_local * foot_world, keep it across plant
                const Quaternion ball_world = ball_anim * end_rot;
                const Quaternion ball_comp = ball_world * end_rot_blend.Inverse();

                local_matrices[ball_i] = Matrix(
                    ball_local.GetTranslation(),
                    ball_comp,
                    ball_local.GetScale()
                );
            }

            return true;
        }
    }
}
