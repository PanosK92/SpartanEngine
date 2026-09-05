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
#include "../../animation/AnimationClip.h"
#include "../../animation/AnimationEvaluate.h"
#include "../../animation/AnimationIk.h"
#include "../../animation/Skeleton.h"
#include "../../animation/SkeletalMeshBinding.h"
#include "../../core/Engine.h"
#include "../../core/ProgressTracker.h"
#include "../../core/Timer.h"
#include "../../geometry/Mesh.h"
#include "../../physics/PhysicsWorld.h"
#include "../../rendering/GeometryBuffer.h"
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
    namespace
    {
        // every authoring tool names leg joints differently: thigh_l, thigh.L, UpperLeg.L,
        // LeftUpLeg, mixamorig:LeftUpLeg. strip the namespace, case, separators and trailing
        // numbering so one matcher covers all of them
        string normalize_joint_name(const string& name)
        {
            size_t start = name.rfind(':');
            start = (start == string::npos) ? 0 : start + 1;

            string out;
            out.reserve(name.size());
            for (size_t i = start; i < name.size(); ++i)
            {
                const unsigned char c = static_cast<unsigned char>(name[i]);
                if (c == '_' || c == '.' || c == '-' || c == ' ')
                {
                    continue;
                }
                out.push_back(static_cast<char>(::tolower(c)));
            }

            // thigh_l_01 and thigh_l are the same joint
            while (!out.empty() && out.back() >= '0' && out.back() <= '9')
            {
                out.pop_back();
            }

            return out;
        }

        // -1 unknown, 0 right, 1 left. runs on the raw name because an isolated side token, as in
        // the biped style "Bip01 L Thigh", becomes invisible once separators are stripped
        int32_t joint_side_from_tokens(const string& name)
        {
            string token;
            token.reserve(name.size());

            for (size_t i = 0; i <= name.size(); ++i)
            {
                const bool at_end = i == name.size();
                const unsigned char c = at_end ? 0 : static_cast<unsigned char>(name[i]);
                if (!at_end && c != '_' && c != '.' && c != '-' && c != ':' && c != ' ')
                {
                    token.push_back(static_cast<char>(::tolower(c)));
                    continue;
                }

                if (token == "l" || token == "left")
                {
                    return 1;
                }
                if (token == "r" || token == "right")
                {
                    return 0;
                }
                token.clear();
            }

            return -1;
        }

        // true when the joint belongs to the requested side
        bool joint_matches_side(
            const string& raw,
            const string& normalized,
            const bool want_left
        )
        {
            if (const int32_t side = joint_side_from_tokens(raw); side >= 0)
            {
                return want_left == (side == 1);
            }

            if (normalized.find("left") != string::npos)
            {
                return want_left;
            }
            if (normalized.find("right") != string::npos)
            {
                return !want_left;
            }
            if (normalized.empty())
            {
                return false;
            }

            // glued side tag, thighl or lthigh
            const char tag = normalized.back() == 'l' || normalized.back() == 'r'
                ? normalized.back()
                : normalized.front();
            if (tag != 'l' && tag != 'r')
            {
                return false;
            }

            return want_left ? tag == 'l' : tag == 'r';
        }

        // first keyword that hits wins, so order the list most specific first
        int32_t find_joint_by_role(
            const Skeleton& skeleton,
            const vector<const char*>& include,
            const vector<const char*>& exclude,
            const bool want_left
        )
        {
            if (skeleton.joint_names.size() != skeleton.joint_count)
            {
                return -1;
            }

            vector<string> normalized(skeleton.joint_count);
            for (uint32_t i = 0; i < skeleton.joint_count; ++i)
            {
                normalized[i] = normalize_joint_name(skeleton.joint_names[i]);
            }

            for (const char* keyword : include)
            {
                for (uint32_t i = 0; i < skeleton.joint_count; ++i)
                {
                    const string& candidate = normalized[i];
                    if (candidate.find(keyword) == string::npos)
                    {
                        continue;
                    }

                    bool rejected = false;
                    for (const char* banned : exclude)
                    {
                        if (candidate.find(banned) != string::npos)
                        {
                            rejected = true;
                            break;
                        }
                    }
                    if (rejected ||
                        !joint_matches_side(skeleton.joint_names[i], candidate, want_left))
                    {
                        continue;
                    }

                    return static_cast<int32_t>(i);
                }
            }

            return -1;
        }
    }

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
        m_anim_pose.clear();
        m_anim_pose_previous.clear();
        m_inertializer.Reset();
        m_global_matrices.clear();
        m_skin_matrices.clear();
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
        m_foot_ik_blend = 0.0f;
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

        // generic fallback so foot ik survives any rig, not just the two naming conventions above.
        // twist joints are excluded, they sit on the same limb and would win the name match
        {
            static const vector<const char*> thigh_words = { "upperleg", "upleg", "thigh" };
            static const vector<const char*> calf_words  = { "lowerleg", "calf", "shin", "leg" };
            static const vector<const char*> foot_words  = { "foot", "ankle" };
            static const vector<const char*> ball_words  = { "toebase", "ball", "toe" };
            static const vector<const char*> no_twist    = { "twist" };
            static const vector<const char*> no_upper    = { "twist", "upperleg", "upleg", "thigh" };
            static const vector<const char*> no_toe      = { "twist", "toe", "ball" };
            static const vector<const char*> no_end      = { "twist", "end" };

            struct RoleLookup
            {
                int32_t* target;
                const vector<const char*>* include;
                const vector<const char*>* exclude;
                bool left;
            };

            const RoleLookup lookups[] =
            {
                { &m_foot_ik_l.thigh, &thigh_words, &no_twist, true  },
                { &m_foot_ik_l.calf,  &calf_words,  &no_upper, true  },
                { &m_foot_ik_l.foot,  &foot_words,  &no_toe,   true  },
                { &m_foot_ik_l.ball,  &ball_words,  &no_end,   true  },
                { &m_foot_ik_r.thigh, &thigh_words, &no_twist, false },
                { &m_foot_ik_r.calf,  &calf_words,  &no_upper, false },
                { &m_foot_ik_r.foot,  &foot_words,  &no_toe,   false },
                { &m_foot_ik_r.ball,  &ball_words,  &no_end,   false }
            };

            for (const RoleLookup& lookup : lookups)
            {
                if (*lookup.target < 0)
                {
                    *lookup.target = find_joint_by_role(
                        skeleton, *lookup.include, *lookup.exclude, lookup.left);
                }
            }
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

        m_foot_ik_resolved =
            m_foot_ik_l.thigh >= 0 && m_foot_ik_l.calf >= 0 && m_foot_ik_l.foot >= 0 &&
            m_foot_ik_r.thigh >= 0 && m_foot_ik_r.calf >= 0 && m_foot_ik_r.foot >= 0;

        // the bind sole is the clip floor, the mesh knows it best, the ankle joint is the fallback
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
        if (m_foot_ik_resolved && !have_sole)
        {
            const float foot_l = bind_matrix(static_cast<uint32_t>(m_foot_ik_l.foot)).GetTranslation().y;
            const float foot_r = bind_matrix(static_cast<uint32_t>(m_foot_ik_r.foot)).GetTranslation().y;
            min_sole_y = min(foot_l, foot_r) - 0.11f;
            have_sole = true;
        }

        auto cache_bind = [&](FootIkLeg& leg)
        {
            if (leg.foot < 0)
            {
                return;
            }

            const Vector3 foot_pos = bind_matrix(static_cast<uint32_t>(leg.foot)).GetTranslation();

            // flat footed ankle height above the sole, the clip raising the ankle past this is a lift
            leg.ankle_height = clamp(foot_pos.y - min_sole_y, 0.05f, 0.25f);

            // knee bend side from bind: offset of calf from thigh->foot axis
            leg.knee_pole_bind = Vector3(0.0f, 0.0f, -1.0f);
            if (leg.thigh >= 0 && leg.calf >= 0)
            {
                const Vector3 hip = bind_matrix(static_cast<uint32_t>(leg.thigh)).GetTranslation();
                const Vector3 knee = bind_matrix(static_cast<uint32_t>(leg.calf)).GetTranslation();
                Vector3 axis = foot_pos - hip;
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

        // offset so bind soles sit on y=0, never lower the character
        m_foot_ik_ground_offset = have_sole ? max(0.0f, -min_sole_y) : 0.0f;
    }

    void Animator::UpdateFootIkLegTarget(
        const vector<Matrix>& globals,
        FootIkLeg& leg,
        const Matrix& model_to_world,
        const Matrix& world_to_model,
        Entity* ignore_entity
    )
    {
        leg.ground_hit = false;
        if (leg.thigh < 0 || leg.calf < 0 || leg.foot < 0)
        {
            return;
        }

        const float dt = static_cast<float>(Timer::GetDeltaTimeSec());
        const float dt_clamped = dt > 0.0f ? dt : 0.016f;
        const float blend = 1.0f - expf(-10.0f * dt_clamped);

        const uint32_t foot_i = static_cast<uint32_t>(leg.foot);
        const Vector3 foot_model = globals[foot_i].GetTranslation();
        const Vector3 foot_world = model_to_world * foot_model;

        // the clip was authored on a flat floor, the bind sole marks where that floor sits in model
        // space, the root is raised by the same offset so on level ground the two coincide
        const float clip_floor_y = -m_foot_ik_ground_offset;

        // how planted the clip has this foot, read off its height above the flat footed ankle. heel
        // strike, heel off and the toe drag after it all sit inside the first band and stay planted,
        // a clear swing is past the second. this is a clip fact, no ray involved, so a foot swinging
        // over a step never gets pulled down onto it
        constexpr float plant_band = 0.06f;
        constexpr float swing_band = 0.18f;
        const float ankle_rise = (foot_model.y - clip_floor_y) - leg.ankle_height;
        const float plant = 1.0f - clamp(
            (ankle_rise - plant_band) / (swing_band - plant_band),
            0.0f,
            1.0f
        );

        constexpr float ray_up = 0.8f;
        constexpr float ray_down = 1.5f;
        constexpr float max_lift = 0.5f;
        constexpr float max_drop = 0.35f;

        float lift = 0.0f;
        Vector3 normal_model = Vector3::Up;

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

            // the clip already puts the foot on its own floor, the ik adds only the difference to the
            // real ground under it. zero on level ground, so nothing lags, slides or flattens there
            const float ground_model_y = (world_to_model * hit.position).y;
            lift = clamp(ground_model_y - clip_floor_y, -max_drop, max_lift);

            Vector3 n = (world_to_model * ground_n) - (world_to_model * Vector3::Zero);
            if (n.LengthSquared() > 1.0e-10f)
            {
                normal_model = n.Normalized();
            }
        }

        // no ground, nothing to adapt to, let the clip through
        const float plant_target = leg.ground_hit ? plant : 0.0f;

        if (!leg.has_smooth)
        {
            leg.smooth_lift = lift;
            leg.smooth_normal = normal_model;
            leg.smooth_plant = 0.0f;
            leg.has_smooth = true;
        }

        // only the vertical lift is smoothed, a scalar with no horizontal part, so the foot follows
        // the clip exactly on the ground plane and the old world space target lag is gone
        leg.smooth_lift += (lift - leg.smooth_lift) * blend;
        leg.smooth_normal = Vector3::Lerp(leg.smooth_normal, normal_model, blend).Normalized();
        leg.smooth_plant += (plant_target - leg.smooth_plant) * blend;

        // sampled before the pelvis moves, the foot must end up here no matter what the pelvis does
        leg.target_model = foot_model + Vector3::Up * leg.smooth_lift;
    }

    bool Animator::SolveFootIkLeg(
        const Skeleton& skeleton,
        vector<Matrix>& local_matrices,
        const vector<Matrix>& globals,
        FootIkLeg& leg
    )
    {
        const float weight = leg.smooth_plant * m_foot_ik_weight * m_foot_ik_blend;
        if (leg.thigh < 0 || leg.calf < 0 || leg.foot < 0 || weight <= 0.001f)
        {
            return false;
        }

        const uint32_t foot_i = static_cast<uint32_t>(leg.foot);
        const uint32_t calf_i = static_cast<uint32_t>(leg.calf);
        const uint32_t thigh_i = static_cast<uint32_t>(leg.thigh);
        const Vector3 foot_model = globals[foot_i].GetTranslation();
        const Vector3 knee_model = globals[calf_i].GetTranslation();
        const Vector3 thigh_model = globals[thigh_i].GetTranslation();

        // level ground and a still pelvis, the clip already has the foot where it must be, solving
        // anyway would only re-derive the leg through the pole and nudge the knee
        const Vector3 correction = leg.target_model - foot_model;
        const bool flat_normal = leg.smooth_normal.Dot(Vector3::Up) > 0.99999f;
        if (correction.LengthSquared() < 1.0e-6f && flat_normal)
        {
            return false;
        }

        Vector3 hip_to_target = leg.target_model - thigh_model;
        if (hip_to_target.LengthSquared() < 1.0e-6f)
        {
            hip_to_target = foot_model - thigh_model;
        }
        if (hip_to_target.LengthSquared() < 1.0e-6f)
        {
            return false;
        }
        const Vector3 axis = hip_to_target.Normalized();

        // bend the knee where the clip bends it, the bind side only decides for a straight leg
        Vector3 prefer = knee_model - thigh_model;
        prefer = prefer - axis * prefer.Dot(axis);
        if (prefer.LengthSquared() < 4.0e-4f)
        {
            prefer = leg.knee_pole_bind - axis * leg.knee_pole_bind.Dot(axis);
        }
        if (prefer.LengthSquared() < 1.0e-6f)
        {
            prefer = Vector3::Up.Cross(axis);
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
            leg.target_model,
            pole_model,
            weight))
        {
            return false;
        }

        animation_ik::PlantFoot(
            skeleton,
            local_matrices,
            foot_i,
            leg.smooth_normal,
            weight
        );
        return true;
    }

    void Animator::ApplyFootIk(
        const Skeleton& skeleton,
        vector<Matrix>& local_matrices
    )
    {
        const float ik_dt = static_cast<float>(Timer::GetDeltaTimeSec());
        const float ik_dt_clamped = ik_dt > 0.0f ? ik_dt : 0.016f;

        // ease the master weight, the lua toggles ik on every takeoff and landing and a hard switch
        // drops the leg corrections in a single frame
        const float want = (m_foot_ik_enabled && m_foot_ik_weight > 0.0f) ? 1.0f : 0.0f;
        m_foot_ik_blend += (want - m_foot_ik_blend) * (1.0f - expf(-8.0f * ik_dt_clamped));

        if (m_foot_ik_blend <= 0.002f)
        {
            m_foot_ik_blend = 0.0f;
            m_foot_ik_has_support = false;
            // next enable re-seeds the smoothing from the live foot instead of a stale target
            m_foot_ik_l.has_smooth = false;
            m_foot_ik_r.has_smooth = false;
            m_foot_ik_l.smooth_plant = 0.0f;
            m_foot_ik_r.smooth_plant = 0.0f;

            const float blend = 1.0f - expf(-6.0f * ik_dt_clamped);
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

        // both legs read the same pose, walk the hierarchy once instead of once per leg
        m_ik_globals.resize(skeleton.joint_count);
        skeleton.ComputeGlobalPose(local_matrices, m_ik_globals);

        UpdateFootIkLegTarget(
            m_ik_globals, m_foot_ik_l, model_to_world, world_to_model, root);
        UpdateFootIkLegTarget(
            m_ik_globals, m_foot_ik_r, model_to_world, world_to_model, root);

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

        // the pelvis follows the lower planted foot, a foot the clip is lifting hands its say over to
        // the other one gradually so the body never jumps when a foot leaves the ground
        const float lift_l = m_foot_ik_l.smooth_lift;
        const float lift_r = m_foot_ik_r.smooth_lift;
        const float ask_l = lift_r + (lift_l - lift_r) * m_foot_ik_l.smooth_plant;
        const float ask_r = lift_l + (lift_r - lift_l) * m_foot_ik_r.smooth_plant;
        const float pelvis_target = min(ask_l, ask_r) * m_foot_ik_weight * m_foot_ik_blend;

        const float blend = 1.0f - expf(-5.0f * ik_dt_clamped);
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

        // the legs are sibling chains, so one refresh after the pelvis edit serves both solves
        skeleton.ComputeGlobalPose(local_matrices, m_ik_globals);
        SolveFootIkLeg(skeleton, local_matrices, m_ik_globals, m_foot_ik_l);
        SolveFootIkLeg(skeleton, local_matrices, m_ik_globals, m_foot_ik_r);
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

    void Animator::SetBoneEntitiesEnabled(bool enabled)
    {
        if (m_bone_entities_enabled == enabled)
        {
            return;
        }

        m_bone_entities_enabled = enabled;

        if (enabled)
        {
            // resolve lazily on the next tick
            m_joints_resolve_attempted = false;
            m_hands_attach_attempted   = false;
            return;
        }

        RestoreHands();
        RestoreBindEntityPoses();
        m_joint_entities.clear();
        m_bind_entity_poses.clear();
        m_joints_resolved = false;

        // mark both as done so nothing retries the lookups every frame
        m_joints_resolve_attempted = true;
        m_hands_attach_attempted   = true;
    }

    void Animator::ResolveJointEntities(const Skeleton& skeleton)
    {
        if (!m_bone_entities_enabled)
        {
            m_joints_resolved          = false;
            m_joints_resolve_attempted = true;
            m_hands_attach_attempted   = true;
            return;
        }

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

        uint32_t count    = 0;
        uint32_t excluded = 0;
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

            if (render->HasFlag(RenderFlags::ExcludeFromRayTracing))
            {
                excluded++;
                continue;
            }

            render->SetAllowBlasUpdate(true);
            render->InvalidateAccelerationStructure();
            ++count;
        }

        if (count == 0)
        {
            if (excluded > 0)
            {
                m_dynamic_blas_ready = true;
            }
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

            if (render->HasFlag(RenderFlags::ExcludeFromRayTracing))
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
        m_skinned_vertices = m_bind_vertices;
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

        // build the lookup here, play is main thread only so sampling never has to write to the clip
        animation_evaluate::EnsureSampleIndex(clips[found]);

        if (m_clip_index != found || m_current_clip != clips[found].name)
        {
            // inertialize out of whatever is on screen: a running clip, a held one shot, the bind
            // pose, or a transition still in flight. the source is a pose, so none of those need a
            // case of their own
            const shared_ptr<Skeleton>& skeleton = mesh->GetSkeleton();
            const float dt = static_cast<float>(Timer::GetDeltaTimeSec());
            const float dt_clamped = dt > 1.0e-5f ? dt : 0.016f;

            bool transitioned = false;
            if (skeleton && m_blend_duration > 0.0f && !m_anim_pose.empty())
            {
                vector<Matrix> dst;
                vector<Matrix> dst_next;

                // m_loop already describes the incoming clip, the caller sets it before Play
                if (animation_evaluate::SampleLocals(
                        clips[found], *skeleton, 0.0f, dst, m_loop) &&
                    animation_evaluate::SampleLocals(
                        clips[found], *skeleton, dt_clamped * m_speed, dst_next, m_loop))
                {
                    m_inertializer.Transition(
                        m_anim_pose,
                        m_anim_pose_previous,
                        dst,
                        dst_next,
                        dt_clamped
                    );
                    transitioned = true;
                }
            }

            if (!transitioned)
            {
                m_inertializer.Reset();
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
        m_inertializer.Reset();
        m_anim_pose_previous.clear();
        m_playing         = false;
        m_time            = 0.0f;
        m_clip_index      = -1;
        m_current_clip.clear();
        m_foot_ik_blend   = 0.0f;
        m_foot_ik_pelvis_offset = 0.0f;
        m_foot_ik_has_support = false;
        m_foot_ik_l.has_smooth = false;
        m_foot_ik_r.has_smooth = false;
        m_foot_ik_l.smooth_plant = 0.0f;
        m_foot_ik_r.smooth_plant = 0.0f;
        // bind pose after clearing clip state
        ApplyBindPose();
    }

    void Animator::ApplyBindPose()
    {
        Mesh* mesh = ResolveMesh();
        if (!mesh || !mesh->GetSkeleton())
        {
            RestoreBindEntityPoses();
            return;
        }

        if (!m_bind_captured)
        {
            CaptureBindPose();
        }

        const shared_ptr<Skeleton>& skeleton = mesh->GetSkeleton();
        if (!skeleton || skeleton->bind_local_matrices.empty())
        {
            RestoreBindEntityPoses();
            return;
        }

        RestoreBindEntityPoses();
        SkinFromLocalPose(mesh, *skeleton, skeleton->bind_local_matrices);
        // keep bind as the transition source, a later Play eases out of it instead of cutting
        m_last_local_matrices = skeleton->bind_local_matrices;
        m_anim_pose = skeleton->bind_local_matrices;
        m_anim_pose_previous.clear();
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
        // ragdoll owns the pose now, a later Play eases out of wherever the body ended up
        m_anim_pose = local_pose;
        m_anim_pose_previous.clear();
        m_inertializer.Reset();
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

        m_global_matrices.resize(local_matrices.size());
        skeleton.ComputeGlobalPose(
            local_matrices,
            m_global_matrices
        );

        EnsureDynamicBlas(mesh);

        if (animation_evaluate::SkinMesh(
            *binding,
            m_global_matrices,
            skeleton.bind_inverse_global_matrices,
            m_bind_vertices,
            m_skinned_vertices,
            m_skin_matrices))
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

        const float dt_real = static_cast<float>(Timer::GetDeltaTimeSec());
        const float dt = dt_real * m_speed;
        m_time += dt;

        if (!m_loop && clip.duration_seconds > 0.0f && m_time >= clip.duration_seconds)
        {
            m_time = clip.duration_seconds;
            m_playing = false;
        }
        else if (m_loop && clip.duration_seconds > 0.0f)
        {
            // wrap here, an unbounded clock loses float resolution and quantizes the sampled frame
            m_time = fmodf(m_time, clip.duration_seconds);
            if (m_time < 0.0f)
            {
                m_time += clip.duration_seconds;
            }
        }

        // member scratch, a local vector would heap allocate the whole pose every frame
        vector<Matrix>& local_matrices = m_tick_locals;
        if (!animation_evaluate::SampleLocals(clip, *skeleton, m_time, local_matrices, m_loop))
        {
            return;
        }

        // the transition offset decays on top of the live clip, blend duration is wall clock so it
        // uses real dt, not the clip scaled one
        m_inertializer.Apply(
            local_matrices,
            dt_real,
            max(0.01f, m_blend_duration * 0.25f)
        );

        if (!m_hands_attach_attempted)
        {
            AttachHandsToArms();
        }

        // rotate the two buffers instead of moving, a move empties the target and the next assign
        // has to allocate the pose again
        m_anim_pose.swap(m_anim_pose_previous);
        m_anim_pose = local_matrices;

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
