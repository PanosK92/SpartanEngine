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

//= INCLUDES ============================
#include "pch.h"
#include <cctype>
#include "Animator.h"
#include "Render.h"
#include "../Entity.h"
#include "../../Animation/AnimationClip.h"
#include "../../Animation/AnimationEvaluate.h"
#include "../../Animation/AnimationIk.h"
#include "../../Animation/Skeleton.h"
#include "../../Animation/SkeletalMeshBinding.h"
#include "../../Core/Engine.h"
#include "../../Core/ProgressTracker.h"
#include "../../Core/Timer.h"
#include "../../Geometry/Mesh.h"
#include "../../Physics/PhysicsWorld.h"
#include "../../Rendering/GeometryBuffer.h"
SP_WARNINGS_OFF
#include <sol/sol.hpp>
SP_WARNINGS_ON
//=======================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    Animator::Animator(Entity* entity) : Component(entity)
    {
    }

    void Animator::Initialize()
    {
        CaptureBindPose();
    }

    void Animator::Remove()
    {
        Stop();
        m_mesh = nullptr;
        m_bind_vertices.clear();
        m_skinned_vertices.clear();
        m_joint_entities.clear();
        m_bind_entity_poses.clear();
        m_hand_l = {};
        m_hand_r = {};
        m_bind_captured = false;
        m_joints_resolved = false;
        m_joints_resolve_attempted = false;
        m_hands_attached = false;
        m_hands_attach_attempted = false;
        m_dynamic_blas_ready = false;
        m_foot_ik_resolved = false;
        m_foot_ik_ground_offset = 0.0f;
        m_foot_ik_pelvis_offset = 0.0f;
        m_foot_ik_support_ground_y = 0.0f;
        m_foot_ik_has_support = false;
        m_foot_ik_l = {};
        m_foot_ik_r = {};
    }

    Mesh* Animator::ResolveMesh()
    {
        if (m_mesh && m_mesh->GetSkeleton() && m_mesh->GetAnimationClipCount() > 0)
        {
            return m_mesh;
        }

        vector<Entity*> nodes;
        nodes.push_back(GetEntity());
        GetEntity()->GetDescendants(&nodes);

        for (Entity* node : nodes)
        {
            if (!node)
            {
                continue;
            }

            Render* render = node->GetComponent<Render>();
            if (!render)
            {
                continue;
            }

            Mesh* mesh = render->GetMesh();
            if (mesh && mesh->GetSkeleton() && mesh->GetAnimationClipCount() > 0)
            {
                m_mesh = mesh;
                return m_mesh;
            }
        }

        return nullptr;
    }

    static int score_joint_entity(Entity* entity)
    {
        if (!entity)
        {
            return -1000;
        }

        int score = static_cast<int>(entity->GetChildrenCount()) * 10;
        // mesh leaves are usually renderables with no children
        if (entity->GetComponent<Render>() && entity->GetChildrenCount() == 0)
        {
            score -= 50;
        }
        return score;
    }

    Entity* Animator::FindJointEntity(Entity* parent, const string& name) const
    {
        if (!parent || name.empty())
        {
            return nullptr;
        }

        vector<Entity*> matches;
        for (Entity* child : parent->GetChildren())
        {
            if (child && child->GetObjectName() == name)
            {
                matches.push_back(child);
            }
        }

        if (matches.empty())
        {
            vector<Entity*> descendants;
            parent->GetDescendants(&descendants);
            for (Entity* node : descendants)
            {
                if (node && node->GetObjectName() == name)
                {
                    matches.push_back(node);
                }
            }
        }

        if (matches.empty())
        {
            return nullptr;
        }

        Entity* best = matches[0];
        int best_score = score_joint_entity(best);
        for (size_t i = 1; i < matches.size(); ++i)
        {
            const int score = score_joint_entity(matches[i]);
            if (score > best_score)
            {
                best_score = score;
                best = matches[i];
            }
        }

        return best;
    }

    Entity* Animator::FindJointByName(const Skeleton& skeleton, const string& name) const
    {
        const int32_t index = FindJointIndex(skeleton, name);
        if (index < 0 || static_cast<size_t>(index) >= m_joint_entities.size())
        {
            return nullptr;
        }

        return m_joint_entities[static_cast<size_t>(index)];
    }

    int32_t Animator::FindJointIndex(const Skeleton& skeleton, const string& name) const
    {
        if (name.empty() || skeleton.joint_names.size() != skeleton.joint_count)
        {
            return -1;
        }

        for (uint32_t i = 0; i < skeleton.joint_count; ++i)
        {
            if (skeleton.joint_names[i] == name)
            {
                return static_cast<int32_t>(i);
            }
        }

        string needle = name;
        transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
        for (uint32_t i = 0; i < skeleton.joint_count; ++i)
        {
            string hay = skeleton.joint_names[i];
            transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
            if (hay == needle)
            {
                return static_cast<int32_t>(i);
            }
        }

        return -1;
    }

    void Animator::ResolveFootIkJoints(const Skeleton& skeleton)
    {
        m_foot_ik_l.thigh = FindJointIndex(skeleton, "thigh.l");
        m_foot_ik_l.calf  = FindJointIndex(skeleton, "calf.l");
        m_foot_ik_l.foot  = FindJointIndex(skeleton, "foot.l");
        m_foot_ik_l.ball  = FindJointIndex(skeleton, "ball.l");
        m_foot_ik_r.thigh = FindJointIndex(skeleton, "thigh.r");
        m_foot_ik_r.calf  = FindJointIndex(skeleton, "calf.r");
        m_foot_ik_r.foot  = FindJointIndex(skeleton, "foot.r");
        m_foot_ik_r.ball  = FindJointIndex(skeleton, "ball.r");

        if (m_foot_ik_l.thigh < 0)
        {
            m_foot_ik_l.thigh = FindJointIndex(skeleton, "UpperLeg.L");
        }
        if (m_foot_ik_l.calf < 0)
        {
            m_foot_ik_l.calf = FindJointIndex(skeleton, "LowerLeg.L");
        }
        if (m_foot_ik_l.foot < 0)
        {
            m_foot_ik_l.foot = FindJointIndex(skeleton, "Foot.L");
        }
        if (m_foot_ik_r.thigh < 0)
        {
            m_foot_ik_r.thigh = FindJointIndex(skeleton, "UpperLeg.R");
        }
        if (m_foot_ik_r.calf < 0)
        {
            m_foot_ik_r.calf = FindJointIndex(skeleton, "LowerLeg.R");
        }
        if (m_foot_ik_r.foot < 0)
        {
            m_foot_ik_r.foot = FindJointIndex(skeleton, "Foot.R");
        }

        // bind pose in skeleton space (same space as animated globals).
        // do not take the first skin section ibm blindly: multi-primitive meshes
        // leave unused bones as identity, which makes toes map to up.
        auto bind_matrix = [&](const uint32_t bone_index) -> Matrix
        {
            if (m_mesh && m_mesh->GetSkeletalMeshBinding())
            {
                for (const SkeletalMeshSection& section : m_mesh->GetSkeletalMeshBinding()->GetSections())
                {
                    if (bone_index >= section.inverse_bind_matrices.size())
                    {
                        continue;
                    }

                    const Matrix& ibm = section.inverse_bind_matrices[bone_index];
                    if (ibm == Matrix::Identity)
                    {
                        continue;
                    }

                    return ibm.Inverted();
                }
            }

            if (bone_index < skeleton.bind_global_matrices.size())
            {
                return skeleton.bind_global_matrices[bone_index];
            }

            return Matrix::Identity;
        };

        auto direction_to_local = [](const Matrix& bind, const Vector3& model_dir) -> Vector3
        {
            const Matrix bind_inv = bind.Inverted();
            Vector3 local = (bind_inv * model_dir) - (bind_inv * Vector3::Zero);
            if (local.LengthSquared() < 1.0e-10f)
            {
                return Vector3::Zero;
            }
            return local.Normalized();
        };

        auto cache_bind = [&](FootIkLeg& leg)
        {
            if (leg.foot < 0)
            {
                return;
            }

            const uint32_t foot_i = static_cast<uint32_t>(leg.foot);
            const Matrix bind = bind_matrix(foot_i);
            const Vector3 foot_pos = bind.GetTranslation();

            leg.ankle_height = clamp(fabsf(foot_pos.y), 0.08f, 0.20f);

            // sole normal = model up expressed in foot local, works for 45 deg bones
            leg.sole_up_local = direction_to_local(bind, Vector3::Up);
            if (leg.sole_up_local.LengthSquared() < 1.0e-8f)
            {
                leg.sole_up_local = Vector3::Up;
            }

            Vector3 toe_model = Vector3::Zero;
            if (leg.ball >= 0)
            {
                toe_model = bind_matrix(static_cast<uint32_t>(leg.ball)).GetTranslation() - foot_pos;
            }
            toe_model = toe_model - Vector3::Up * toe_model.Dot(Vector3::Up);
            if (toe_model.LengthSquared() < 1.0e-8f)
            {
                // mannequiny faces -z
                toe_model = Vector3(0.0f, 0.0f, -1.0f);
            }
            toe_model.Normalize();
            leg.toe_fwd_local = direction_to_local(bind, toe_model);
            if (leg.toe_fwd_local.LengthSquared() < 1.0e-8f)
            {
                leg.toe_fwd_local = Vector3(0.0f, 0.0f, -1.0f);
            }

            // knee bend side from bind: offset of calf from thigh->foot axis
            leg.knee_pole_bind = Vector3(0.0f, 0.0f, -1.0f);
            leg.bind_hip_foot_y = 0.85f;
            if (leg.thigh >= 0 && leg.calf >= 0)
            {
                const Vector3 hip = bind_matrix(static_cast<uint32_t>(leg.thigh)).GetTranslation();
                const Vector3 knee = bind_matrix(static_cast<uint32_t>(leg.calf)).GetTranslation();
                const Vector3 ankle = foot_pos;
                leg.bind_hip_foot_y = max(0.35f, hip.y - ankle.y);
                Vector3 axis = ankle - hip;
                if (axis.LengthSquared() > 1.0e-8f)
                {
                    axis.Normalize();
                    Vector3 knee_off = knee - hip;
                    knee_off = knee_off - axis * knee_off.Dot(axis);
                    if (knee_off.LengthSquared() > 1.0e-8f)
                    {
                        leg.knee_pole_bind = knee_off.Normalized();
                    }
                }
            }
        };

        cache_bind(m_foot_ik_l);
        cache_bind(m_foot_ik_r);

        m_foot_ik_resolved =
            m_foot_ik_l.thigh >= 0 && m_foot_ik_l.calf >= 0 && m_foot_ik_l.foot >= 0 &&
            m_foot_ik_r.thigh >= 0 && m_foot_ik_r.calf >= 0 && m_foot_ik_r.foot >= 0;

        // offset so bind soles sit on y=0, never lower the character
        m_foot_ik_ground_offset = 0.0f;
        float min_sole_y = 0.0f;
        bool have_sole = false;
        if (!m_bind_vertices.empty())
        {
            min_sole_y = m_bind_vertices[0].pos[1];
            for (size_t i = 1; i < m_bind_vertices.size(); ++i)
            {
                min_sole_y = min(min_sole_y, m_bind_vertices[i].pos[1]);
            }
            have_sole = true;
        }
        if (m_foot_ik_resolved)
        {
            const float sole_l = bind_matrix(static_cast<uint32_t>(m_foot_ik_l.foot)).GetTranslation().y -
                m_foot_ik_l.ankle_height;
            const float sole_r = bind_matrix(static_cast<uint32_t>(m_foot_ik_r.foot)).GetTranslation().y -
                m_foot_ik_r.ankle_height;
            const float joint_sole = min(sole_l, sole_r);
            min_sole_y = have_sole ? min(min_sole_y, joint_sole) : joint_sole;
            have_sole = true;
        }
        if (have_sole)
        {
            m_foot_ik_ground_offset = max(0.0f, -min_sole_y);
        }
    }

    void Animator::UpdateFootIkLegTarget(
        const Skeleton& skeleton,
        const vector<Matrix>& local_matrices,
        FootIkLeg& leg,
        const Matrix& model_to_world,
        const Matrix& world_to_model,
        Entity* ignore_entity
    )
    {
        leg.contact_dy = 0.0f;
        leg.contact_active = false;
        leg.ground_hit = false;
        leg.use_for_pelvis = false;
        if (leg.thigh < 0 || leg.calf < 0 || leg.foot < 0)
        {
            return;
        }

        const float dt = static_cast<float>(Timer::GetDeltaTimeSec());
        const float dt_clamped = dt > 0.0f ? dt : 0.016f;
        const float blend = 1.0f - expf(-6.0f * dt_clamped);

        vector<Matrix> globals(skeleton.joint_count);
        skeleton.ComputeGlobalPose(local_matrices, globals);

        const uint32_t foot_i = static_cast<uint32_t>(leg.foot);
        const uint32_t thigh_i = static_cast<uint32_t>(leg.thigh);
        const Vector3 foot_model = globals[foot_i].GetTranslation();
        const Vector3 thigh_model = globals[thigh_i].GetTranslation();
        const Vector3 foot_world = model_to_world * foot_model;

        constexpr float ray_up = 0.8f;
        constexpr float ray_down = 1.5f;
        constexpr float max_lift = 0.5f;
        // allow reach-down to a lower step; swing feet are filtered separately
        constexpr float max_drop = 0.35f;
        constexpr float contact_band = 0.14f;
        constexpr float swing_band = 0.40f;

        float target_weight = 0.0f;
        Vector3 target_model = foot_model;
        Vector3 normal_model = Vector3::Up;
        Vector3 forward_model = Vector3(0.0f, 0.0f, -1.0f);

        if (leg.ball >= 0)
        {
            forward_model = globals[static_cast<uint32_t>(leg.ball)].GetTranslation() - foot_model;
            forward_model.y = 0.0f;
            if (forward_model.LengthSquared() > 1.0e-8f)
            {
                forward_model.Normalize();
            }
            else
            {
                forward_model = Vector3(0.0f, 0.0f, -1.0f);
            }
        }

        PhysicsRaycastHit hit;
        if (PhysicsWorld::RaycastStatic(
            foot_world + Vector3::Up * ray_up,
            -Vector3::Up,
            ray_up + ray_down,
            hit,
            ignore_entity))
        {
            Vector3 ground_n = hit.normal;
            if (ground_n.Dot(Vector3::Up) < 0.0f)
            {
                ground_n = -ground_n;
            }

            leg.ground_hit = true;
            leg.ground_y_world = hit.position.y;

            const float above_ground = foot_world.y - hit.position.y;
            const Vector3 ankle_world = hit.position + ground_n * leg.ankle_height;
            const Vector3 ankle_model = world_to_model * ankle_world;
            const float ankle_lift = ankle_model.y - foot_model.y;
            const float stand_lift = (ankle_model.y + leg.bind_hip_foot_y) - thigh_model.y;
            leg.contact_dy = max(ankle_lift, stand_lift);

            // true walk swing: high above its own ground and needing a big pull-down
            const bool true_swing = above_ground > leg.ankle_height + swing_band && ankle_lift < -0.12f;
            leg.use_for_pelvis = !true_swing;

            const bool airborne = above_ground > leg.ankle_height + contact_band;
            if (!airborne && ankle_lift <= max_lift && ankle_lift >= -max_drop)
            {
                target_weight = m_foot_ik_weight;
                target_model = ankle_model;
                leg.contact_active = true;

                Vector3 n = (world_to_model * ground_n) - (world_to_model * Vector3::Zero);
                if (n.LengthSquared() > 1.0e-10f)
                {
                    normal_model = n.Normalized();
                }
            }
        }

        if (!leg.has_smooth)
        {
            leg.smooth_target = foot_model;
            leg.smooth_normal = Vector3::Up;
            leg.smooth_forward = forward_model;
            leg.smooth_weight = 0.0f;
            leg.has_smooth = true;
        }

        leg.smooth_target = Vector3::Lerp(leg.smooth_target, target_model, blend);
        leg.smooth_normal = Vector3::Lerp(leg.smooth_normal, normal_model, blend).Normalized();
        leg.smooth_forward = Vector3::Lerp(leg.smooth_forward, forward_model, blend).Normalized();
        leg.smooth_weight = leg.smooth_weight + (target_weight - leg.smooth_weight) * blend;
    }

    bool Animator::SolveFootIkLeg(
        const Skeleton& skeleton,
        vector<Matrix>& local_matrices,
        FootIkLeg& leg
    )
    {
        if (leg.thigh < 0 || leg.calf < 0 || leg.foot < 0 || leg.smooth_weight <= 0.001f)
        {
            return false;
        }

        vector<Matrix> globals(skeleton.joint_count);
        skeleton.ComputeGlobalPose(local_matrices, globals);

        const uint32_t foot_i = static_cast<uint32_t>(leg.foot);
        const uint32_t calf_i = static_cast<uint32_t>(leg.calf);
        const uint32_t thigh_i = static_cast<uint32_t>(leg.thigh);
        const Vector3 foot_model = globals[foot_i].GetTranslation();
        const Vector3 thigh_model = globals[thigh_i].GetTranslation();

        Vector3 hip_to_target = leg.smooth_target - thigh_model;
        if (hip_to_target.LengthSquared() < 1.0e-6f)
        {
            hip_to_target = foot_model - thigh_model;
        }
        Vector3 prefer = leg.knee_pole_bind;
        if (prefer.LengthSquared() < 1.0e-6f)
        {
            prefer = leg.smooth_forward;
        }
        if (hip_to_target.LengthSquared() > 1.0e-6f)
        {
            const Vector3 axis = hip_to_target.Normalized();
            prefer = prefer - axis * prefer.Dot(axis);
        }
        if (prefer.LengthSquared() < 1.0e-6f)
        {
            prefer = Vector3::Up.Cross(hip_to_target);
        }
        if (prefer.LengthSquared() < 1.0e-6f)
        {
            prefer = Vector3(1.0f, 0.0f, 0.0f);
        }
        prefer.Normalize();
        const Vector3 pole_model = thigh_model + prefer * 0.5f;

        if (!animation_ik::SolveTwoBone(
            skeleton,
            local_matrices,
            thigh_i,
            calf_i,
            foot_i,
            leg.smooth_target,
            pole_model,
            leg.smooth_weight))
        {
            return false;
        }

        animation_ik::PlantFoot(
            skeleton,
            local_matrices,
            foot_i,
            leg.smooth_normal,
            leg.smooth_forward,
            leg.sole_up_local,
            leg.toe_fwd_local,
            leg.smooth_weight
        );
        return true;
    }

    void Animator::ApplyFootIk(
        const Skeleton& skeleton,
        vector<Matrix>& local_matrices
    )
    {
        if (!m_foot_ik_enabled || m_foot_ik_weight <= 0.0f)
        {
            const float dt = static_cast<float>(Timer::GetDeltaTimeSec());
            const float dt_clamped = dt > 0.0f ? dt : 0.016f;
            const float blend = 1.0f - expf(-6.0f * dt_clamped);
            m_foot_ik_pelvis_offset += (0.0f - m_foot_ik_pelvis_offset) * blend;
            if (fabsf(m_foot_ik_pelvis_offset) > 1.0e-4f && !local_matrices.empty())
            {
                const Vector3 t = local_matrices[0].GetTranslation();
                local_matrices[0] = Matrix(
                    Vector3(t.x, t.y + m_foot_ik_pelvis_offset, t.z),
                    local_matrices[0].GetRotation(),
                    local_matrices[0].GetScale()
                );
            }
            return;
        }

        if (!m_foot_ik_resolved)
        {
            ResolveFootIkJoints(skeleton);
            if (!m_foot_ik_resolved)
            {
                return;
            }
        }

        Entity* root = GetEntity();
        if (!root)
        {
            return;
        }

        const Matrix model_to_world = root->GetMatrix();
        const Matrix world_to_model = model_to_world.Inverted();

        UpdateFootIkLegTarget(
            skeleton, local_matrices, m_foot_ik_l, model_to_world, world_to_model, root);
        UpdateFootIkLegTarget(
            skeleton, local_matrices, m_foot_ik_r, model_to_world, world_to_model, root);

        // support = lowest ground under either foot (split steps stay on the lower one)
        m_foot_ik_has_support = false;
        m_foot_ik_support_ground_y = 0.0f;
        if (m_foot_ik_l.ground_hit || m_foot_ik_r.ground_hit)
        {
            if (m_foot_ik_l.ground_hit && m_foot_ik_r.ground_hit)
            {
                m_foot_ik_support_ground_y = min(
                    m_foot_ik_l.ground_y_world,
                    m_foot_ik_r.ground_y_world
                );
            }
            else
            {
                m_foot_ik_support_ground_y = m_foot_ik_l.ground_hit
                    ? m_foot_ik_l.ground_y_world
                    : m_foot_ik_r.ground_y_world;
            }
            m_foot_ik_has_support = true;
        }

        // pelvis only rises as much as the lower stance foot allows
        float pelvis_target = 0.0f;
        bool has_pelvis = false;
        if (m_foot_ik_l.use_for_pelvis)
        {
            pelvis_target = m_foot_ik_l.contact_dy;
            has_pelvis = true;
        }
        if (m_foot_ik_r.use_for_pelvis)
        {
            if (has_pelvis)
            {
                pelvis_target = min(pelvis_target, m_foot_ik_r.contact_dy);
            }
            else
            {
                pelvis_target = m_foot_ik_r.contact_dy;
                has_pelvis = true;
            }
        }
        if (!has_pelvis)
        {
            pelvis_target = 0.0f;
        }

        const float dt = static_cast<float>(Timer::GetDeltaTimeSec());
        const float dt_clamped = dt > 0.0f ? dt : 0.016f;
        const float blend = 1.0f - expf(-5.0f * dt_clamped);
        m_foot_ik_pelvis_offset += (pelvis_target - m_foot_ik_pelvis_offset) * blend;

        if (!local_matrices.empty() && fabsf(m_foot_ik_pelvis_offset) > 1.0e-5f)
        {
            const Vector3 t = local_matrices[0].GetTranslation();
            local_matrices[0] = Matrix(
                Vector3(t.x, t.y + m_foot_ik_pelvis_offset, t.z),
                local_matrices[0].GetRotation(),
                local_matrices[0].GetScale()
            );
        }

        SolveFootIkLeg(skeleton, local_matrices, m_foot_ik_l);
        SolveFootIkLeg(skeleton, local_matrices, m_foot_ik_r);
    }

    bool Animator::AttachHand(Entity* hand, Entity* arm, HandAttach& out_attach)
    {
        if (!hand || !arm)
        {
            return false;
        }

        out_attach.hand = hand;
        out_attach.original_parent = hand->GetParent();
        out_attach.original_local.position = hand->GetPositionLocal();
        out_attach.original_local.rotation = hand->GetRotationLocal();
        out_attach.original_local.scale    = hand->GetScaleLocal();

        const Vector3 world_pos = hand->GetPosition();
        const Quaternion world_rot = hand->GetRotation();
        const Vector3 world_scale = hand->GetScale();

        hand->SetParent(arm);
        hand->SetPosition(world_pos);
        hand->SetRotation(world_rot);
        hand->SetScale(world_scale);
        out_attach.attached = true;
        return true;
    }

    void Animator::AttachHandsToArms()
    {
        if (m_hands_attached || m_hands_attach_attempted || !m_mesh || !m_mesh->GetSkeleton())
        {
            return;
        }

        m_hands_attach_attempted = true;

        const Skeleton& skeleton = *m_mesh->GetSkeleton();
        Entity* root = GetEntity();

        Entity* arm_l = FindJointByName(skeleton, "LowerArm.L");
        Entity* arm_r = FindJointByName(skeleton, "LowerArm.R");
        Entity* hand_l = root ? root->GetDescendantByName("Hand.L") : nullptr;
        Entity* hand_r = root ? root->GetDescendantByName("Hand.R") : nullptr;

        if (!hand_l && root)
        {
            hand_l = root->GetChildByName("Hand.L");
        }
        if (!hand_r && root)
        {
            hand_r = root->GetChildByName("Hand.R");
        }

        bool any = false;
        any |= AttachHand(hand_l, arm_l, m_hand_l);
        any |= AttachHand(hand_r, arm_r, m_hand_r);
        m_hands_attached = any;
    }

    void Animator::RestoreHands()
    {
        auto restore_one = [](HandAttach& attach)
        {
            if (!attach.attached || !attach.hand)
            {
                return;
            }

            attach.hand->SetParent(attach.original_parent);
            attach.hand->SetPositionLocal(attach.original_local.position);
            attach.hand->SetRotationLocal(attach.original_local.rotation);
            attach.hand->SetScaleLocal(attach.original_local.scale);
            attach = {};
        };

        restore_one(m_hand_l);
        restore_one(m_hand_r);
        m_hands_attached = false;
        m_hands_attach_attempted = false;
    }

    void Animator::ResolveJointEntities(const Skeleton& skeleton)
    {
        m_joint_entities.assign(skeleton.joint_count, nullptr);
        m_bind_entity_poses.assign(skeleton.joint_count, {});

        Entity* root = GetEntity();

        if (!root || skeleton.joint_names.size() != skeleton.joint_count)
        {
            m_joints_resolved = false;
            return;
        }

        uint32_t resolved = 0;
        for (uint32_t i = 0; i < skeleton.joint_count; ++i)
        {
            const string& name = skeleton.joint_names[i];
            const int16_t parent_index = skeleton.parent_indices[i];

            Entity* entity = nullptr;
            if (parent_index < 0)
            {
                // root joint maps to mesh root (often renamed in lua)
                if (name == "RootNode" || name == root->GetObjectName())
                {
                    entity = root;
                }
                else
                {
                    entity = FindJointEntity(root, name);
                }
            }
            else
            {
                Entity* parent_entity = m_joint_entities[static_cast<uint32_t>(parent_index)];
                if (parent_entity)
                {
                    entity = FindJointEntity(parent_entity, name);
                }
            }

            m_joint_entities[i] = entity;
            if (entity)
            {
                m_bind_entity_poses[i].position = entity->GetPositionLocal();
                m_bind_entity_poses[i].rotation = entity->GetRotationLocal();
                m_bind_entity_poses[i].scale    = entity->GetScaleLocal();
                ++resolved;
            }
        }

        // skinned-only models may have 0 bone entities, still mark attempted
        m_joints_resolve_attempted = true;
        m_joints_resolved = resolved > 0;

        if (m_joints_resolved)
        {
            AttachHandsToArms();
        }
        else
        {
            m_hands_attach_attempted = true;
        }
    }

    void Animator::RestoreBindEntityPoses()
    {
        const uint32_t count = static_cast<uint32_t>(min(m_joint_entities.size(), m_bind_entity_poses.size()));
        for (uint32_t i = 0; i < count; ++i)
        {
            Entity* entity = m_joint_entities[i];
            if (!entity)
            {
                continue;
            }

            entity->SetPositionLocal(m_bind_entity_poses[i].position);
            entity->SetRotationLocal(m_bind_entity_poses[i].rotation);
            entity->SetScaleLocal(m_bind_entity_poses[i].scale);
        }
    }

    void Animator::EnsureDynamicBlas(Mesh* mesh)
    {
        if (m_dynamic_blas_ready || !mesh)
        {
            return;
        }

        // rebuild blas with allow_update so refit can track skinned verts (same as cloth)
        vector<Entity*> nodes;
        nodes.push_back(GetEntity());
        GetEntity()->GetDescendants(&nodes);

        uint32_t count = 0;
        for (Entity* node : nodes)
        {
            if (!node)
            {
                continue;
            }

            Render* render = node->GetComponent<Render>();
            if (!render || render->GetMesh() != mesh)
            {
                continue;
            }

            render->SetAllowBlasUpdate(true);
            render->InvalidateAccelerationStructure();
            ++count;
        }

        if (count == 0)
        {
            return;
        }

        m_dynamic_blas_ready = true;
    }

    void Animator::MarkBlasNeedsRefit(Mesh* mesh)
    {
        if (!mesh)
        {
            return;
        }

        vector<Entity*> nodes;
        nodes.push_back(GetEntity());
        GetEntity()->GetDescendants(&nodes);

        for (Entity* node : nodes)
        {
            if (!node)
            {
                continue;
            }

            Render* render = node->GetComponent<Render>();
            if (!render || render->GetMesh() != mesh)
            {
                continue;
            }

            render->SetNeedsBlasRefit(true);
        }
    }

    void Animator::ApplyHierarchy(
        const Skeleton& skeleton,
        const vector<Matrix>& local_matrices
    )
    {
        if (!m_joints_resolved || local_matrices.size() != skeleton.joint_count)
        {
            return;
        }

        Entity* root = GetEntity();

        for (uint32_t i = 0; i < skeleton.joint_count; ++i)
        {
            Entity* entity = m_joint_entities[i];
            if (!entity)
            {
                continue;
            }

            // never overwrite the instance root (lua/ai sets world pos/scale there)
            if (entity == root)
            {
                continue;
            }

            const Matrix& local = local_matrices[i];
            Vector3 scale = local.GetScale();
            if (fabsf(scale.x) < 1.0e-6f || fabsf(scale.y) < 1.0e-6f || fabsf(scale.z) < 1.0e-6f)
            {
                scale = Vector3::One;
            }

            entity->SetPositionLocal(local.GetTranslation());
            entity->SetRotationLocal(local.GetRotation());
            entity->SetScaleLocal(scale);
        }
    }

    void Animator::CaptureBindPose()
    {
        Mesh* mesh = ResolveMesh();
        if (!mesh)
        {
            m_bind_captured = false;
            return;
        }

        m_bind_vertices = mesh->GetVertices();
        m_skinned_vertices.resize(m_bind_vertices.size());
        m_bind_captured = !m_bind_vertices.empty();

        if (mesh->GetSkeleton())
        {
            ResolveJointEntities(*mesh->GetSkeleton());
        }
    }

    bool Animator::Play(const string& clip_name)
    {
        Mesh* mesh = ResolveMesh();
        if (!mesh)
        {
            SP_LOG_WARNING("Animator::Play: no animated mesh with clips on '%s'", GetEntity()->GetObjectName().c_str());
            return false;
        }

        if (!m_bind_captured)
        {
            CaptureBindPose();
        }

        if (!m_joints_resolve_attempted && mesh->GetSkeleton())
        {
            ResolveJointEntities(*mesh->GetSkeleton());
        }

        const vector<AnimationClip>& clips = mesh->GetAnimationClips();
        int32_t found = -1;
        for (uint32_t i = 0; i < static_cast<uint32_t>(clips.size()); ++i)
        {
            if (clips[i].name == clip_name)
            {
                found = static_cast<int32_t>(i);
                break;
            }
        }

        if (found < 0)
        {
            string needle = clip_name;
            transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
            for (uint32_t i = 0; i < static_cast<uint32_t>(clips.size()); ++i)
            {
                string hay = clips[i].name;
                transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
                if (hay.find(needle) != string::npos || needle.find(hay) != string::npos)
                {
                    found = static_cast<int32_t>(i);
                    break;
                }
            }
        }

        if (found < 0)
        {
            SP_LOG_WARNING("Animator::Play: clip '%s' not found (%u clips)", clip_name.c_str(), mesh->GetAnimationClipCount());
            return false;
        }

        if (m_clip_index != found || m_current_clip != clips[found].name)
        {
            // crossfade from current clip into the new one
            if (m_playing && m_clip_index >= 0 && m_blend_duration > 0.0f)
            {
                m_prev_clip_index = m_clip_index;
                m_prev_time       = m_time;
                m_blend_weight    = 0.0f;
                m_blending        = true;
            }
            else
            {
                m_prev_clip_index = -1;
                m_prev_time       = 0.0f;
                m_blend_weight    = 1.0f;
                m_blending        = false;
            }

            m_clip_index   = found;
            m_current_clip = clips[found].name;
            m_time         = 0.0f;
        }

        ClearExternalPose();
        m_playing = true;
        EnsureDynamicBlas(mesh);

        if (m_joints_resolved && !m_hands_attached)
        {
            AttachHandsToArms();
        }

        return true;
    }

    void Animator::Stop()
    {
        RestoreHands();
        ClearExternalPose();
        RestoreBindEntityPoses();
        m_playing         = false;
        m_blending        = false;
        m_time            = 0.0f;
        m_prev_time       = 0.0f;
        m_prev_clip_index = -1;
        m_blend_weight    = 1.0f;
    }

    void Animator::Pause()
    {
        if (!m_external_pose_active)
        {
            m_playing = false;
        }
    }

    void Animator::Resume()
    {
        if (m_external_pose_active)
        {
            return;
        }

        if (m_clip_index >= 0)
        {
            m_playing = true;
        }
    }

    const Skeleton* Animator::GetSkeleton() const
    {
        Mesh* mesh = const_cast<Animator*>(this)->ResolveMesh();
        if (!mesh)
        {
            return nullptr;
        }

        return mesh->GetSkeleton().get();
    }

    void Animator::SetExternalPose(const vector<Matrix>& local_pose)
    {
        m_external_local_matrices = local_pose;
        m_last_local_matrices = local_pose;
        m_external_pose_active = !m_external_local_matrices.empty();
        if (m_external_pose_active)
        {
            m_playing = false;
            m_foot_ik_enabled = false;
        }
    }

    void Animator::ClearExternalPose()
    {
        m_external_pose_active = false;
        m_external_local_matrices.clear();
    }

    void Animator::SkinFromLocalPose(
        Mesh* mesh,
        const Skeleton& skeleton,
        const vector<Matrix>& local_matrices
    )
    {
        if (!mesh || local_matrices.size() != skeleton.joint_count)
        {
            return;
        }

        if (!m_joints_resolve_attempted)
        {
            ResolveJointEntities(skeleton);
        }

        ApplyHierarchy(skeleton, local_matrices);

        SkeletalMeshBinding* binding = mesh->GetSkeletalMeshBinding();
        if (!binding || !m_bind_captured || m_bind_vertices.empty())
        {
            return;
        }

        vector<Matrix> global_matrices(local_matrices.size());
        skeleton.ComputeGlobalPose(local_matrices, global_matrices);

        EnsureDynamicBlas(mesh);

        if (animation_evaluate::SkinMesh(
            *binding,
            global_matrices,
            skeleton.bind_global_matrices,
            m_bind_vertices,
            m_skinned_vertices))
        {
            mesh->GetVertices() = m_skinned_vertices;
            GeometryBuffer::UpdateVertices(
                m_skinned_vertices.data(),
                mesh->GetGlobalVertexOffset(),
                static_cast<uint32_t>(m_skinned_vertices.size())
            );

            MarkBlasNeedsRefit(mesh);
        }
    }

    void Animator::Tick()
    {
        if (ProgressTracker::IsLoading())
        {
            return;
        }

        if (!Engine::IsFlagSet(EngineMode::Playing))
        {
            return;
        }

        // paused pedestrians skip mesh resolve and bind work entirely
        if (!m_playing && !m_external_pose_active)
        {
            return;
        }

        Mesh* mesh = ResolveMesh();
        if (!mesh || !mesh->GetSkeleton())
        {
            return;
        }

        if (!m_bind_captured)
        {
            CaptureBindPose();
            if (!m_bind_captured)
            {
                return;
            }
        }

        const shared_ptr<Skeleton>& skeleton = mesh->GetSkeleton();
        if (!skeleton)
        {
            return;
        }

        if (m_external_pose_active)
        {
            SkinFromLocalPose(mesh, *skeleton, m_external_local_matrices);
            return;
        }

        if (!m_playing)
        {
            return;
        }

        if (m_clip_index < 0 || m_clip_index >= static_cast<int32_t>(mesh->GetAnimationClipCount()))
        {
            return;
        }

        const vector<AnimationClip>& clips = mesh->GetAnimationClips();
        const AnimationClip& clip = clips[m_clip_index];

        if (!m_joints_resolve_attempted)
        {
            ResolveJointEntities(*skeleton);
        }

        const float dt = static_cast<float>(Timer::GetDeltaTimeSec()) * m_speed;
        m_time += dt;

        if (!m_loop && clip.duration_seconds > 0.0f && m_time >= clip.duration_seconds)
        {
            m_time = clip.duration_seconds;
            m_playing = false;
        }

        if (m_blending)
        {
            m_prev_time += dt;
            if (m_blend_duration <= 0.0f)
            {
                m_blend_weight = 1.0f;
                m_blending = false;
            }
            else
            {
                m_blend_weight += dt / m_blend_duration;
                if (m_blend_weight >= 1.0f)
                {
                    m_blend_weight = 1.0f;
                    m_blending = false;
                    m_prev_clip_index = -1;
                }
            }
        }

        vector<Matrix> local_matrices;
        if (!animation_evaluate::SampleLocals(clip, *skeleton, m_time, local_matrices))
        {
            return;
        }

        if (m_blending &&
            m_prev_clip_index >= 0 &&
            m_prev_clip_index < static_cast<int32_t>(clips.size()))
        {
            vector<Matrix> prev_locals;
            if (animation_evaluate::SampleLocals(
                clips[m_prev_clip_index],
                *skeleton,
                m_prev_time,
                prev_locals))
            {
                // smoothstep ease for less linear-looking transitions
                const float w = m_blend_weight;
                const float eased = w * w * (3.0f - 2.0f * w);
                vector<Matrix> blended;
                if (animation_evaluate::BlendLocals(
                    prev_locals,
                    local_matrices,
                    eased,
                    blended))
                {
                    local_matrices = move(blended);
                }
            }
        }

        if (!m_hands_attach_attempted)
        {
            AttachHandsToArms();
        }

        ApplyFootIk(*skeleton, local_matrices);
        m_last_local_matrices = local_matrices;
        SkinFromLocalPose(mesh, *skeleton, local_matrices);
    }

    float Animator::GetFootIkGroundOffset()
    {
        if (!m_foot_ik_resolved)
        {
            if (!m_bind_captured)
            {
                CaptureBindPose();
            }

            Mesh* mesh = ResolveMesh();
            if (mesh && mesh->GetSkeleton())
            {
                ResolveFootIkJoints(*mesh->GetSkeleton());
            }
        }

        return m_foot_ik_ground_offset;
    }

    void Animator::RegisterForScripting(sol::state_view state)
    {
        state.new_usertype<Animator>("Animator",
            sol::base_classes, sol::bases<Component>(),
            "Play",               &Animator::Play,
            "Stop",               &Animator::Stop,
            "Pause",              &Animator::Pause,
            "Resume",             &Animator::Resume,
            "IsPlaying",          &Animator::IsPlaying,
            "GetSpeed",           &Animator::GetSpeed,
            "SetSpeed",           &Animator::SetSpeed,
            "GetLoop",            &Animator::GetLoop,
            "SetLoop",            &Animator::SetLoop,
            "GetBlendDuration",   &Animator::GetBlendDuration,
            "SetBlendDuration",   &Animator::SetBlendDuration,
            "GetCurrentClip",     &Animator::GetCurrentClip,
            "GetFootIkEnabled",   &Animator::GetFootIkEnabled,
            "SetFootIkEnabled",   &Animator::SetFootIkEnabled,
            "GetFootIkWeight",    &Animator::GetFootIkWeight,
            "SetFootIkWeight",    &Animator::SetFootIkWeight,
            "GetFootIkGroundOffset", &Animator::GetFootIkGroundOffset,
            "HasFootIkSupportGround", &Animator::HasFootIkSupportGround,
            "GetFootIkSupportGroundY", &Animator::GetFootIkSupportGroundY
        );
    }

    sol::reference Animator::AsLua(sol::state_view state)
    {
        return sol::make_reference(state, this);
    }
}
