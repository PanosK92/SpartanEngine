/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ============================
#include "pch.h"
#include "../../profiling/WorldWork.h"
#include "../../profiling/Profiler.h"
#include <cctype>
#include <unordered_map>
#include "Animator.h"
#include "Camera.h"
#include "Render.h"
#include "../Entity.h"
#include "../World.h"
#include "../../animation/AnimationClip.h"
#include "../../animation/AnimationEvaluate.h"
#include "../../animation/AnimationIk.h"
#include "../../animation/Skeleton.h"
#include "../../animation/SkeletalMeshBinding.h"
#include "../../core/Engine.h"
#include "../../core/ProgressTracker.h"
#include "../../core/Timer.h"
#include "../../core/ThreadPool.h"
#include "../../geometry/Mesh.h"
#include "../../physics/PhysicsWorld.h"
#include "../../rendering/GeometryBuffer.h"
//=======================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        struct SkinningJob
        {
            Animator* animator;
            shared_ptr<Mesh> mesh;
            bool succeeded = false;
        };
        bool batch_skinning = false;
        vector<SkinningJob> skinning_jobs;
        uint32_t skinning_frame = 0;

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

    Animator::~Animator()
    {
        skinning_jobs.erase(remove_if(skinning_jobs.begin(), skinning_jobs.end(),
            [this](const SkinningJob& job) { return job.animator == this; }), skinning_jobs.end());
    }

    void Animator::BeginSkinningBatch()
    {
        batch_skinning = true;
        ++skinning_frame;
    }

    void Animator::FlushSkinningBatch()
    {
        batch_skinning = false;
        if (skinning_jobs.empty()) return;
        SP_PROFILE_CPU_START("animation_skin_batch");
        // Character meshes have very different vertex counts. Feed expensive jobs
        // first and let workers claim the next mesh instead of fixing equal job counts.
        static vector<uint32_t> order;
        order.resize(skinning_jobs.size());
        for (uint32_t i = 0; i < order.size(); ++i) order[i] = i;
        sort(order.begin(), order.end(), [](uint32_t a, uint32_t b)
        {
            return skinning_jobs[a].animator->m_bind_vertices.size() > skinning_jobs[b].animator->m_bind_vertices.size();
        });
        auto skin_job = [&](uint32_t i)
        {
            SkinningJob& job = skinning_jobs[order[i]];
            Animator* animator = job.animator;
            const auto& skeleton = job.mesh->GetSkeleton();
            const auto* binding = job.mesh->GetSkeletalMeshBinding();
            if (skeleton && binding)
            {
                job.succeeded = animation_evaluate::SkinMesh(*binding, animator->m_global_matrices,
                    skeleton->bind_inverse_global_matrices, animator->m_bind_vertices,
                    animator->m_skinned_vertices, animator->m_skin_matrices);
                if (job.succeeded)
                {
                    animator->StepSkinnedCloth(job.mesh.get(), *skeleton, animator->m_cloth_dt);
                    animator->StepProceduralEyes(job.mesh.get(), *skeleton, animator->m_cloth_dt);
                }
            }
        };
        // A nested ParallelLoop runs inline. With only a few characters due,
        // let their vertex loops use the pool instead of serializing each mesh.
        if (order.size() <= ThreadPool::GetThreadCount() / 2)
        {
            for (uint32_t i = 0; i < order.size(); ++i) skin_job(i);
        }
        else
        {
            atomic<uint32_t> next_job = 0;
            ThreadPool::ParallelLoop([&](uint32_t, uint32_t)
            {
                for (uint32_t i = next_job.fetch_add(1, memory_order_relaxed); i < order.size();
                    i = next_job.fetch_add(1, memory_order_relaxed)) skin_job(i);
            }, static_cast<uint32_t>(order.size()));
        }
        SP_PROFILE_CPU_END();
        SP_PROFILE_CPU_START("animation_publish_batch");
        for (SkinningJob& job : skinning_jobs)
        {
            CountWorldWork(WorldWork::animation_jobs);
            if (job.succeeded) CountWorldWork(WorldWork::animation_vertices, job.animator->m_skinned_vertices.size());
            if (!job.succeeded) continue;
            Animator* animator = job.animator;
            animator->m_skin_published = true;
            // Keep both allocations: the old mesh storage becomes next frame's
            // skinning destination, removing one full-mesh copy from the main thread.
            job.mesh->GetVertices().swap(animator->m_skinned_vertices);
            const auto& vertices = job.mesh->GetVertices();
            GeometryBuffer::UpdateVertices(vertices.data(), job.mesh->GetGlobalVertexOffset(),
                static_cast<uint32_t>(vertices.size()), true);
            animator->MarkBlasNeedsRefit(job.mesh.get());
            animator->PublishPoseBounds(job.mesh.get());
        }
        skinning_jobs.clear();
        SP_PROFILE_CPU_END();
    }

    void Animator::Remove()
    {
        Stop();
        // Stop can queue the bind pose while a world animation batch is open.
        skinning_jobs.erase(remove_if(skinning_jobs.begin(), skinning_jobs.end(),
            [this](const SkinningJob& job) { return job.animator == this; }), skinning_jobs.end());
        // the renders go back to the box of their own mesh, nothing poses them from here on
        if (m_mesh && m_skin_published)
        {
            for (Render* render : ResolveSkinRenders(m_mesh))
            {
                render->ClearBoundingBoxOverride();
            }
        }
        m_mesh = nullptr;
        m_skin_published = false;
        m_skin_render_ids.clear();
        m_skin_renders_mesh = nullptr;
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
        for (LimbIk& target : m_limb_ik)
        {
            target = {};
        }
        m_limb_ik_resolved = false;
        ResetSkinnedCloth();
        ResetProceduralEyes();
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

                    // bind legs are near straight so the knee offset is mostly noise, the kneecap faces
                    // the toes, which the bind pose states clearly
                    leg.has_kneecap = false;
                    Vector3 kneecap = leg.knee_pole_bind;
                    if (leg.ball >= 0)
                    {
                        Vector3 toe = bind_matrix(static_cast<uint32_t>(leg.ball)).GetTranslation() - foot_pos;
                        toe = toe - axis * toe.Dot(axis);
                        if (toe.LengthSquared() > 1.0e-6f)
                        {
                            kneecap = toe.Normalized();
                        }
                    }
                    const Quaternion thigh_rotation = bind_matrix(static_cast<uint32_t>(leg.thigh)).GetRotation();
                    leg.kneecap_thigh_local = (thigh_rotation.Inverse() * kneecap).Normalized();
                    leg.has_kneecap = true;
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
        // past the ball joint to the tip of the shoe
        constexpr float toe_reach = 0.07f;
        // toe hit this far off the heel plane is a step edge, not a slope
        constexpr float edge_tolerance = 0.03f;

        auto cast_down = [&](const Vector3& from_world, PhysicsRaycastHit& out) -> bool
        {
            if (!PhysicsWorld::RaycastStatic(from_world + Vector3::Up * ray_up, -Vector3::Up, ray_up + ray_down, out, ignore_entity))
            {
                return false;
            }
            if (out.normal.Dot(Vector3::Up) < 0.0f)
            {
                out.normal = -out.normal;
            }
            return true;
        };

        // the sole spans heel to toe, one ray under the ankle lets the toes sink into a step the heel
        // is still in front of, so probe both ends
        PhysicsRaycastHit heel_hit;
        const bool heel_ok = cast_down(foot_world, heel_hit);

        PhysicsRaycastHit toe_hit;
        bool toe_ok = false;
        if (leg.ball >= 0)
        {
            const Vector3 ball_world = model_to_world * globals[static_cast<uint32_t>(leg.ball)].GetTranslation();
            Vector3 forward = ball_world - foot_world;
            forward.y = 0.0f;
            if (forward.LengthSquared() > 1.0e-6f)
            {
                toe_ok = cast_down(ball_world + forward.Normalized() * toe_reach, toe_hit);
            }
        }

        float ground_y = 0.0f;
        Vector3 ground_n = Vector3::Up;
        if (heel_ok && toe_ok)
        {
            // on one plane (flat or slope) the heel carries the ankle and the tilt brings the toes
            // along, across a step edge the sole rests on whichever end is higher and stays level
            const Vector3 n = heel_hit.normal;
            const float dx = toe_hit.position.x - heel_hit.position.x;
            const float dz = toe_hit.position.z - heel_hit.position.z;
            const float plane_y = n.y > 0.2f ? heel_hit.position.y - (n.x * dx + n.z * dz) / n.y : heel_hit.position.y;
            if (fabsf(toe_hit.position.y - plane_y) <= edge_tolerance)
            {
                ground_y = heel_hit.position.y;
                ground_n = heel_hit.normal;
            }
            else if (toe_hit.position.y > heel_hit.position.y)
            {
                ground_y = toe_hit.position.y;
                ground_n = toe_hit.normal;
            }
            else
            {
                ground_y = heel_hit.position.y;
                ground_n = heel_hit.normal;
            }
        }
        else if (heel_ok || toe_ok)
        {
            const PhysicsRaycastHit& hit = heel_ok ? heel_hit : toe_hit;
            ground_y = hit.position.y;
            ground_n = hit.normal;
        }

        leg.ground_hit = heel_ok || toe_ok;
        if (leg.ground_hit)
        {
            leg.ground_y_world = ground_y;
        }
        else
        {
            // nothing under the foot, ease back to the clip floor so the leg plays untouched
            ground_y = (model_to_world * Vector3(0.0f, clip_floor_y, 0.0f)).y;
            ground_n = Vector3::Up;
        }

        // no ground, nothing to adapt to, let the clip through
        const float plant_target = leg.ground_hit ? plant : 0.0f;

        if (!leg.has_smooth)
        {
            leg.smooth_ground_y_world = ground_y;
            leg.smooth_normal = ground_n;
            leg.smooth_plant = 0.0f;
            leg.has_smooth = true;
        }

        // the ground is smoothed in world space, a planted foot keeps its ground while the root climbs,
        // smoothing a root relative lift instead let the foot float off the step for a tenth of a second.
        // rising ground is taken faster so a swinging toe clears the step nose
        const float ground_rate = ground_y > leg.smooth_ground_y_world ? 24.0f : 12.0f;
        leg.smooth_ground_y_world += (ground_y - leg.smooth_ground_y_world) * (1.0f - expf(-ground_rate * dt_clamped));
        leg.smooth_normal = Vector3::Lerp(leg.smooth_normal, ground_n, blend).Normalized();
        leg.smooth_plant += (plant_target - leg.smooth_plant) * blend;

        // the clip already puts the foot on its own floor, the ik adds only the difference to the real
        // ground under it. zero on level ground, so nothing lags, slides or flattens there
        const Vector3 ground_world = Vector3(foot_world.x, leg.smooth_ground_y_world, foot_world.z);
        leg.smooth_lift = clamp((world_to_model * ground_world).y - clip_floor_y, -max_drop, max_lift);

        Vector3 n = (world_to_model * leg.smooth_normal) - (world_to_model * Vector3::Zero);
        leg.normal_model = n.LengthSquared() > 1.0e-10f ? n.Normalized() : Vector3::Up;

        // a swinging foot keeps any lift that raises it, so it clears higher ground instead of passing
        // through the riser, but is only pulled down onto lower ground as it plants
        const float lift = leg.smooth_lift >= 0.0f ? leg.smooth_lift : leg.smooth_lift * leg.smooth_plant;

        // sampled before the pelvis moves, the foot must end up here no matter what the pelvis does
        leg.target_model = foot_model + Vector3::Up * lift;
    }

    bool Animator::SolveFootIkLeg(
        const Skeleton& skeleton,
        vector<Matrix>& local_matrices,
        const vector<Matrix>& globals,
        FootIkLeg& leg
    )
    {
        // the target already fades a swinging foot's downward lift, so the chain itself runs at full
        // weight, only the slope tilt of the sole scales with how planted the foot is
        const float weight = m_foot_ik_weight * m_foot_ik_blend;
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
        // the pelvis edit only translates, so this is the clip's sole orientation
        const Quaternion clip_foot_rotation = globals[foot_i].GetRotation();

        // level ground and a still pelvis, the clip already has the foot where it must be, solving
        // anyway would only re-derive the leg through the pole and nudge the knee
        const Vector3 correction = leg.target_model - foot_model;
        const bool flat_normal = leg.normal_model.Dot(Vector3::Up) > 0.99999f;
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

        // anatomy: the knee is a hinge that tracks over the toes. the thigh's kneecap (it carries the
        // clip's hip rotation) and the live toe direction agree on a real leg, and together they keep a
        // raised knee from collapsing inward. a clearly bent clip knee already states its plane, so it
        // takes over as it bends
        Vector3 anatomical = Vector3::Zero;
        if (leg.has_kneecap)
        {
            anatomical += globals[thigh_i].GetRotation() * leg.kneecap_thigh_local;
        }
        if (leg.ball >= 0)
        {
            Vector3 toe = globals[static_cast<uint32_t>(leg.ball)].GetTranslation() - foot_model;
            toe = toe - axis * toe.Dot(axis);
            if (toe.LengthSquared() > 1.0e-6f)
            {
                anatomical += toe.Normalized();
            }
        }
        anatomical = anatomical - axis * anatomical.Dot(axis);

        Vector3 clip_bend = knee_model - thigh_model;
        clip_bend = clip_bend - axis * clip_bend.Dot(axis);
        const float clip_bend_length = clip_bend.Length();

        Vector3 prefer = Vector3::Zero;
        if (anatomical.LengthSquared() > 1.0e-6f)
        {
            prefer = anatomical.Normalized();
            // 3 cm of knee offset is noise, 8 cm is a knee the clip means to bend
            const float clip_trust = clamp((clip_bend_length - 0.03f) / 0.05f, 0.0f, 1.0f);
            if (clip_trust > 0.0f && clip_bend.Dot(prefer) > 0.0f)
            {
                prefer = Vector3::Lerp(prefer, clip_bend / clip_bend_length, clip_trust);
            }
        }
        else if (clip_bend_length > 0.02f)
        {
            prefer = clip_bend / clip_bend_length;
        }
        else
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
            clip_foot_rotation,
            leg.normal_model,
            leg.smooth_plant * weight
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
            m_foot_ik_root_previous_valid = false;
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

        // the root climbs steps at its own rate, carry the offset across that move so the body keeps its
        // world height and only eases toward the feet, a teleport is not carried
        // while ik fades out for a jump the body must leave with the root, so only carry when enabled
        const Vector3 root_position = root->GetPosition();
        if (m_foot_ik_root_previous_valid && m_foot_ik_enabled)
        {
            const float root_rise = -(world_to_model * m_foot_ik_root_previous).y;
            if (fabsf(root_rise) < 0.5f)
            {
                m_foot_ik_pelvis_offset = clamp(m_foot_ik_pelvis_offset - root_rise, -0.6f, 0.6f);
            }
        }
        m_foot_ik_root_previous = root_position;
        m_foot_ik_root_previous_valid = true;

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

    void Animator::SetLimbIk(IkLimb limb, const Vector3& position, const Quaternion& rotation, float weight)
    {
        if (limb >= IkLimb::Count)
        {
            return;
        }

        LimbIk& target  = m_limb_ik[static_cast<size_t>(limb)];
        target.position = position;
        target.rotation = rotation;
        target.weight   = clamp(weight, 0.0f, 1.0f);
    }

    float Animator::GetClipDuration(const string& name)
    {
        Mesh* mesh = ResolveMesh();
        if (!mesh)
        {
            return 0.0f;
        }
        for (const AnimationClip& clip : mesh->GetAnimationClips())
        {
            if (clip.name == name)
            {
                return clip.duration_seconds;
            }
        }
        return 0.0f;
    }

    void Animator::SetTime(float seconds)
    {
        float duration = 0.0f;
        Mesh* mesh     = ResolveMesh();
        if (mesh && m_clip_index >= 0 && m_clip_index < static_cast<int32_t>(mesh->GetAnimationClipCount()))
        {
            duration = mesh->GetAnimationClips()[m_clip_index].duration_seconds;
        }
        // a one shot stops when it reaches its end, a scrubbed clip has to stay alive on its last frame
        const float last = m_loop ? duration : max(duration - 1.0e-4f, 0.0f);
        m_time           = duration > 0.0f ? clamp(seconds, 0.0f, last) : max(seconds, 0.0f);
    }

    void Animator::SetLimbIkPole(IkLimb limb, const Vector3& pole)
    {
        if (limb >= IkLimb::Count)
        {
            return;
        }

        LimbIk& target  = m_limb_ik[static_cast<size_t>(limb)];
        target.pole     = pole;
        target.has_pole = true;
    }

    void Animator::ClearLimbIk()
    {
        for (LimbIk& target : m_limb_ik)
        {
            target.weight   = 0.0f;
            target.has_pole = false;
        }
    }

    void Animator::ResolveLimbIkJoints(const Skeleton& skeleton)
    {
        m_limb_ik_resolved = true;

        // twist and roll joints share the limb's name, fingers hang off the hand and carry its side tag
        static const vector<const char*> upper_words = { "upperarm", "uparm", "shoulder", "arm" };
        static const vector<const char*> lower_words = { "lowerarm", "forearm", "elbow" };
        static const vector<const char*> hand_words  = { "hand", "wrist" };
        static const vector<const char*> no_upper    = { "twist", "roll", "lower", "fore", "clavicle", "collar" };
        static const vector<const char*> no_lower    = { "twist", "roll" };
        static const vector<const char*> no_finger   = { "twist", "roll", "thumb", "index", "middle", "ring", "pinky", "finger", "end" };

        for (const bool left : { true, false })
        {
            LimbIk& arm = m_limb_ik[static_cast<size_t>(left ? IkLimb::HandLeft : IkLimb::HandRight)];
            arm.upper   = find_joint_by_role(skeleton, upper_words, no_upper, left);
            arm.lower   = find_joint_by_role(skeleton, lower_words, no_lower, left);
            arm.end     = find_joint_by_role(skeleton, hand_words, no_finger, left);
        }

        // the legs are the ones the foot ik already found
        if (!m_foot_ik_resolved)
        {
            ResolveFootIkJoints(skeleton);
        }
        LimbIk& leg_l = m_limb_ik[static_cast<size_t>(IkLimb::FootLeft)];
        LimbIk& leg_r = m_limb_ik[static_cast<size_t>(IkLimb::FootRight)];
        leg_l.upper   = m_foot_ik_l.thigh;
        leg_l.lower   = m_foot_ik_l.calf;
        leg_l.end     = m_foot_ik_l.foot;
        leg_r.upper   = m_foot_ik_r.thigh;
        leg_r.lower   = m_foot_ik_r.calf;
        leg_r.end     = m_foot_ik_r.foot;
    }

    void Animator::ApplyLimbIk(const Skeleton& skeleton, vector<Matrix>& local_matrices)
    {
        bool any = false;
        for (const LimbIk& target : m_limb_ik)
        {
            any = any || target.weight > 0.001f;
        }
        if (!any || local_matrices.size() != skeleton.joint_count)
        {
            return;
        }

        if (!m_limb_ik_resolved)
        {
            ResolveLimbIkJoints(skeleton);
        }

        m_ik_globals.resize(skeleton.joint_count);
        for (const LimbIk& target : m_limb_ik)
        {
            if (target.weight <= 0.001f || target.upper < 0 || target.lower < 0 || target.end < 0)
            {
                continue;
            }

            const uint32_t upper = static_cast<uint32_t>(target.upper);
            const uint32_t lower = static_cast<uint32_t>(target.lower);
            const uint32_t end   = static_cast<uint32_t>(target.end);

            // the limbs are separate chains, but a hand's parents can be another limb's joints on odd rigs, so each
            // solve reads the pose the one before it left
            skeleton.ComputeGlobalPose(local_matrices, m_ik_globals);

            // the clip's own elbow or knee is the pole unless the script names one, so the limb bends the way it was animated
            const Vector3 pole = target.has_pole ? target.pole : m_ik_globals[lower].GetTranslation();
            animation_ik::SolveTwoBone(skeleton, local_matrices, upper, lower, end, target.position, pole, target.weight);

            // the chain put the end joint in place, its rotation is the target's whatever the forearm or shin did
            skeleton.ComputeGlobalPose(local_matrices, m_ik_globals);
            const int16_t parent        = skeleton.parent_indices[end];
            const Matrix parent_global  = parent < 0 ? Matrix::Identity : m_ik_globals[static_cast<uint32_t>(parent)];
            const Quaternion rotation   = Quaternion::Lerp(m_ik_globals[end].GetRotation(), target.rotation, target.weight);
            const Matrix end_global     = Matrix(m_ik_globals[end].GetTranslation(), rotation, m_ik_globals[end].GetScale());
            local_matrices[end]         = end_global * parent_global.Inverted();
        }
    }

    bool Animator::ResolveJointGlobal(const string& name, Matrix& out_global)
    {
        Mesh* mesh = ResolveMesh();
        const Skeleton* skeleton = mesh ? mesh->GetSkeleton().get() : nullptr;
        if (!skeleton)
        {
            return false;
        }

        const int32_t index = FindJointIndex(*skeleton, name);
        if (index < 0)
        {
            return false;
        }

        const vector<Matrix>& locals = m_last_local_matrices.size() == skeleton->joint_count ? m_last_local_matrices : skeleton->bind_local_matrices;
        if (locals.size() != skeleton->joint_count)
        {
            return false;
        }

        // walk up the parents, a script asks for a joint or two a frame and the whole pose is not worth computing for that
        Matrix global = locals[static_cast<uint32_t>(index)];
        for (int16_t parent = skeleton->parent_indices[static_cast<uint32_t>(index)]; parent >= 0; parent = skeleton->parent_indices[static_cast<uint32_t>(parent)])
        {
            global = global * locals[static_cast<uint32_t>(parent)];
        }
        out_global = global;
        return true;
    }

    Vector3 Animator::GetJointPosition(const string& name)
    {
        Matrix global;
        return ResolveJointGlobal(name, global) ? global.GetTranslation() : Vector3::Zero;
    }

    Quaternion Animator::GetJointRotation(const string& name)
    {
        Matrix global;
        return ResolveJointGlobal(name, global) ? global.GetRotation() : Quaternion::Identity;
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

        for (Render* render : ResolveSkinRenders(mesh))
        {
            if (!render->HasFlag(RenderFlags::ExcludeFromRayTracing))
            {
                render->SetNeedsBlasRefit(true);
            }
        }
    }

    void Animator::PublishPoseBounds(Mesh* mesh)
    {
        if (!mesh || m_global_matrices.empty())
        {
            return;
        }

        // a skinned mesh is culled by a box, and the box of its bind pose does not hold a pose that sits, reaches or lies down,
        // so the box follows the joints: a seated driver's arms and legs stay drawn when the standing box falls behind the camera
        const Matrix model_to_world = GetEntity()->GetMatrix();
        Vector3 box_min = Vector3::Infinity;
        Vector3 box_max = Vector3::InfinityNeg;
        for (const Matrix& global : m_global_matrices)
        {
            const Vector3 joint = model_to_world * global.GetTranslation();
            box_min = Vector3::Min(box_min, joint);
            box_max = Vector3::Max(box_max, joint);
        }
        if (!box_min.IsFinite() || !box_max.IsFinite())
        {
            return;
        }

        // the flesh around the outermost joints: a skull over the head joint, a palm past the wrist, a toe past the ball
        const Vector3 scale  = GetEntity()->GetScale().Abs();
        const Vector3 margin = Vector3(0.3f * max(scale.x, max(scale.y, scale.z)));
        const BoundingBox box(box_min - margin, box_max + margin);
        for (Render* render : ResolveSkinRenders(mesh))
        {
            render->SetBoundingBoxOverride(box);
        }
    }

    const vector<Render*>& Animator::ResolveSkinRenders(Mesh* mesh)
    {
        m_skin_renders.clear();
        bool valid = m_skin_renders_mesh == mesh && !m_skin_render_ids.empty();
        for (uint64_t id : m_skin_render_ids)
        {
            if (!valid)
            {
                break;
            }
            Entity* entity = World::GetEntityById(id);
            Render* render = entity ? entity->GetComponent<Render>() : nullptr;
            valid = render && render->GetMesh() == mesh;
            if (valid)
            {
                m_skin_renders.push_back(render);
            }
        }
        if (valid)
        {
            return m_skin_renders;
        }

        // the hierarchy walk allocates and visits every joint, so it only runs when the binding changes
        m_skin_renders.clear();
        m_skin_render_ids.clear();
        m_skin_renders_mesh = mesh;
        vector<Entity*> nodes;
        nodes.push_back(GetEntity());
        GetEntity()->GetDescendants(&nodes);
        for (Entity* node : nodes)
        {
            Render* render = node ? node->GetComponent<Render>() : nullptr;
            if (render && render->GetMesh() == mesh)
            {
                m_skin_renders.push_back(render);
                m_skin_render_ids.push_back(node->GetObjectId());
            }
        }
        return m_skin_renders;
    }

    bool Animator::IsSkinDue(Mesh* mesh)
    {
        // The first pose always publishes. Afterwards distant and off-screen characters
        // skin at a reduced rate; off-screen ones still refresh their shadow and reflection
        // shapes. The per-entity phase spreads the reduced-rate work across frames.
        if (!m_skin_published || m_skin_renders_mesh != mesh)
        {
            return true;
        }

        bool visible = false;
        float distance_squared = numeric_limits<float>::max();
        for (Render* render : ResolveSkinRenders(mesh))
        {
            visible = visible || render->IsVisible();
        }
        if (Camera* camera = World::GetCamera())
        {
            distance_squared = Vector3::DistanceSquared(camera->GetEntity()->GetPosition(), GetEntity()->GetPosition());
        }

        uint32_t interval = 1;
        if (!visible)
        {
            // Skip CPU skinning completely for offscreen characters past 10 m once their initial pose is published.
            if (distance_squared > 10.0f * 10.0f)
            {
                return false;
            }
            interval = 16;
        }
        else if (distance_squared > 55.0f * 55.0f)
        {
            interval = 8;
        }
        else if (distance_squared > 25.0f * 25.0f)
        {
            interval = 4;
        }
        else if (distance_squared > 10.0f * 10.0f)
        {
            interval = 2;
        }
        const uint32_t phase = static_cast<uint32_t>(GetEntity()->GetObjectId() % 16);
        return (skinning_frame + phase) % interval == 0;
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

            entity->SetTransformLocalDeferred(local.GetTranslation(), local.GetRotation(), scale);
        }
        // Each joint setter used to propagate through all descendants, repeatedly
        // recomputing the same bones. Publish the complete pose in one traversal.
        root->UpdateTransform();
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
        m_foot_ik_root_previous_valid = false;
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
        const vector<Matrix>& local_matrices,
        bool sample_clip
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

        SP_PROFILE_CPU_START("animation_hierarchy");
        ApplyHierarchy(skeleton, local_matrices);
        SP_PROFILE_CPU_END();

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

        if (mesh->HasCrowdPoses() && sample_clip)
        {
            const auto& renders = ResolveSkinRenders(mesh);
            sample_clip = all_of(renders.begin(), renders.end(), [](const Render* render)
            {
                return render->HasFlag(RenderFlags::ExcludeFromRayTracing);
            });
        }
        if (mesh->SelectCrowdPose(sample_clip ? m_clip_index : -1, m_time))
        {
            m_skin_published = true;
            PublishPoseBounds(mesh);
            return;
        }
        EnsureDynamicBlas(mesh);

        if (m_cloth_enabled)
        {
            ResolveSkinnedCloth(mesh, skeleton);
            if (!m_cloth_state.surfaces.empty() && GetEntity())
            {
                m_cloth_state.cached_world = GetEntity()->GetMatrix();
            }
        }
        if (m_procedural_eyes_enabled)
        {
            ResolveProceduralEyes(mesh, skeleton);
        }

        if (batch_skinning)
        {
            // A script may replace the pose again this frame; only publish its final pose.
            for (SkinningJob& job : skinning_jobs)
            {
                if (job.animator == this)
                {
                    job.mesh = static_pointer_cast<Mesh>(mesh->shared_from_this());
                    return;
                }
            }
            skinning_jobs.push_back({this, static_pointer_cast<Mesh>(mesh->shared_from_this()), false});
            return;
        }
        SP_PROFILE_CPU_START("animation_skin");
        if (animation_evaluate::SkinMesh(
            *binding,
            m_global_matrices,
            skeleton.bind_inverse_global_matrices,
            m_bind_vertices,
            m_skinned_vertices,
            m_skin_matrices))
        {
            StepSkinnedCloth(mesh, skeleton, m_cloth_dt);
            StepProceduralEyes(mesh, skeleton, m_cloth_dt);
            SP_PROFILE_CPU_END();
            SP_PROFILE_CPU_START("animation_upload");
            mesh->GetVertices() = m_skinned_vertices;
            GeometryBuffer::UpdateVertices(
                m_skinned_vertices.data(),
                mesh->GetGlobalVertexOffset(),
                static_cast<uint32_t>(m_skinned_vertices.size()), true
            );

            MarkBlasNeedsRefit(mesh);
            PublishPoseBounds(mesh);
        }
        SP_PROFILE_CPU_END();
    }

    void Animator::ResetSkinnedCloth()
    {
        m_cloth_state = {};
    }

    void Animator::ResolveSkinnedCloth(Mesh* mesh, const Skeleton& skeleton)
    {
        if (!mesh || m_cloth_state.built_mesh == mesh)
        {
            return;
        }

        m_cloth_state = {};
        m_cloth_state.built_mesh = mesh;

        auto to_lower = [](string value)
        {
            for (char& ch : value)
            {
                ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
            }
            return value;
        };

        auto is_cloth_garment = [&](const string& entity_name, const string& mat_name) -> bool
        {
            const string combined = to_lower(entity_name + " " + mat_name);
            if (combined.find("skin") != string::npos || combined.find("face") != string::npos ||
                combined.find("hair") != string::npos || combined.find("eye") != string::npos ||
                combined.find("lash") != string::npos || combined.find("brow") != string::npos ||
                combined.find("beard") != string::npos || combined.find("shoe") != string::npos ||
                combined.find("boot") != string::npos || combined.find("pant") != string::npos ||
                combined.find("jean") != string::npos || combined.find("trouser") != string::npos ||
                combined.find("caruncle") != string::npos || combined.find("teeth") != string::npos)
            {
                return false;
            }
            return combined.find("shirt") != string::npos || combined.find("top") != string::npos ||
                   combined.find("jacket") != string::npos || combined.find("coat") != string::npos ||
                   combined.find("cape") != string::npos || combined.find("cloth") != string::npos ||
                   combined.find("dress") != string::npos || combined.find("skirt") != string::npos ||
                   combined.find("sweater") != string::npos || combined.find("hoodie") != string::npos ||
                   combined.find("tunic") != string::npos;
        };

        const vector<Render*>& renders = ResolveSkinRenders(mesh);
        const vector<uint32_t>& indices = mesh->GetIndices();
        if (renders.empty() || indices.empty() || m_bind_vertices.empty())
        {
            return;
        }

        // Compute character bind-pose anatomical axes and key joint positions
        const size_t joint_count = skeleton.bind_inverse_global_matrices.size();
        vector<Matrix> bind_globals(joint_count, Matrix::Identity);
        for (size_t i = 0; i < joint_count; ++i)
        {
            bind_globals[i] = skeleton.bind_inverse_global_matrices[i].Inverted();
        }

        auto find_any_joint = [&](initializer_list<const char*> names) -> int32_t
        {
            for (const char* candidate : names)
            {
                int32_t idx = FindJointIndex(skeleton, candidate);
                if (idx >= 0)
                {
                    return idx;
                }
            }
            return -1;
        };

        const int32_t j_pelvis     = find_any_joint({ "pelvis", "hips", "mixamorig:Hips" });
        const int32_t j_spine1     = find_any_joint({ "spine_01", "spine", "mixamorig:Spine" });
        const int32_t j_spine2     = find_any_joint({ "spine_02", "spine1", "mixamorig:Spine1" });
        const int32_t j_spine3     = find_any_joint({ "spine_03", "spine2", "chest", "mixamorig:Spine2" });
        const int32_t j_neck       = find_any_joint({ "neck_01", "neck", "mixamorig:Neck" });
        const int32_t j_upperarm_l = find_any_joint({ "upperarm_l", "upperarm.l", "leftarm", "mixamorig:LeftArm" });
        const int32_t j_lowerarm_l = find_any_joint({ "lowerarm_l", "lowerarm.l", "leftforearm", "mixamorig:LeftForeArm" });
        const int32_t j_upperarm_r = find_any_joint({ "upperarm_r", "upperarm.r", "rightarm", "mixamorig:RightArm" });
        const int32_t j_lowerarm_r = find_any_joint({ "lowerarm_r", "lowerarm.r", "rightforearm", "mixamorig:RightForeArm" });

        Vector3 up_axis = Vector3::Up;
        Vector3 right_axis = Vector3::Right;
        Vector3 neck_bind = Vector3::Zero;
        bool has_neck = false;
        if (j_pelvis >= 0 && j_neck >= 0)
        {
            Vector3 p_pelvis = bind_globals[j_pelvis].GetTranslation();
            neck_bind = bind_globals[j_neck].GetTranslation();
            has_neck = true;
            Vector3 spine_dir = neck_bind - p_pelvis;
            if (spine_dir.LengthSquared() > 1e-4f)
            {
                up_axis = spine_dir.Normalized();
            }
        }
        if (j_upperarm_l >= 0 && j_upperarm_r >= 0)
        {
            Vector3 span = bind_globals[j_upperarm_l].GetTranslation() - bind_globals[j_upperarm_r].GetTranslation();
            span -= up_axis * Vector3::Dot(span, up_axis);
            if (span.LengthSquared() > 1e-4f)
            {
                right_axis = span.Normalized();
            }
        }

        // Collect all cloth surfaces
        for (Render* render : renders)
        {
            if (!render || !render->GetEntity())
            {
                continue;
            }
            const string entity_name = render->GetEntity()->GetObjectName();
            const string mat_name = render->GetMaterialName();
            if (!is_cloth_garment(entity_name, mat_name))
            {
                continue;
            }

            const uint32_t sub_idx = render->GetSubMeshIndex();
            if (sub_idx >= mesh->GetSubMeshCount())
            {
                continue;
            }
            const GeometryRange& sub_range = mesh->GetSubMesh(sub_idx).root;
            const uint32_t v_off = sub_range.vertex_offset;
            const uint32_t v_cnt = sub_range.vertex_count;
            if (v_cnt == 0 || v_off + v_cnt > m_bind_vertices.size())
            {
                continue;
            }

            ClothSurface surf;
            surf.vertex_offset = v_off;
            surf.vertex_count  = v_cnt;

            // Build a clean single-sheet 3.0 cm spatial proxy cage over the garment so double-walled shells
            // (outer + inner fabric layers) and split UV seams deform together smoothly with zero tearing or poke-through.
            struct GridKey
            {
                int32_t x, y, z;
                bool operator==(const GridKey& o) const { return x == o.x && y == o.y && z == o.z; }
            };
            struct GridHash
            {
                size_t operator()(const GridKey& k) const
                {
                    size_t h = static_cast<size_t>(k.x) * 73856093u;
                    h ^= static_cast<size_t>(k.y) * 19349663u;
                    h ^= static_cast<size_t>(k.z) * 83492791u;
                    return h;
                }
            };

            // First compute garment bind centroid & vertical span
            float y_min = numeric_limits<float>::max();
            float y_max = -numeric_limits<float>::max();
            Vector3 centroid = Vector3::Zero;
            for (uint32_t v = 0; v < v_cnt; ++v)
            {
                const RHI_Vertex_PosTexNorTan& bv = m_bind_vertices[v_off + v];
                const Vector3 p(bv.pos[0], bv.pos[1], bv.pos[2]);
                const float h = Vector3::Dot(p, up_axis);
                y_min = min(y_min, h);
                y_max = max(y_max, h);
                centroid += p;
            }
            centroid /= static_cast<float>(v_cnt);

            constexpr float cell_size = 0.032f;
            unordered_map<GridKey, uint32_t, GridHash> cell_map;
            cell_map.reserve(512);
            vector<float> best_outward;

            for (uint32_t v = 0; v < v_cnt; ++v)
            {
                const RHI_Vertex_PosTexNorTan& bv = m_bind_vertices[v_off + v];
                const Vector3 p(bv.pos[0], bv.pos[1], bv.pos[2]);
                const Vector3 n = bv.get_normal();
                Vector3 radial = p - centroid;
                radial -= up_axis * Vector3::Dot(radial, up_axis);
                const float outward_score = Vector3::Dot(n, radial.LengthSquared() > 1e-6f ? radial.Normalized() : n);

                const GridKey key
                {
                    static_cast<int32_t>(floorf(p.x / cell_size)),
                    static_cast<int32_t>(floorf(p.y / cell_size)),
                    static_cast<int32_t>(floorf(p.z / cell_size))
                };
                auto it = cell_map.find(key);
                if (it == cell_map.end())
                {
                    const uint32_t pid = static_cast<uint32_t>(surf.positions.size());
                    cell_map.emplace(key, pid);
                    surf.positions.push_back(p);
                    surf.prev_positions.push_back(p);
                    surf.particle_rep_vertex.push_back(v);
                    best_outward.push_back(outward_score);
                }
                else
                {
                    // Prefer outermost wall vertex as the proxy node's reference vertex so proxy normals always face outward
                    const uint32_t pid = it->second;
                    if (outward_score > best_outward[pid])
                    {
                        best_outward[pid] = outward_score;
                        surf.particle_rep_vertex[pid] = v;
                        surf.positions[pid] = p;
                        surf.prev_positions[pid] = p;
                    }
                }
            }

            const uint32_t p_cnt = static_cast<uint32_t>(surf.positions.size());
            if (p_cnt < 12)
            {
                continue;
            }

            surf.skinned_positions.resize(p_cnt);
            surf.skinned_normals.resize(p_cnt);
            surf.displacements.resize(p_cnt, Vector3::Zero);
            surf.inv_mass.resize(p_cnt, 1.0f);
            surf.max_distance.resize(p_cnt, 0.03f);
            surf.anim_drive.resize(p_cnt, 0.08f);

            const float height_span = max(y_max - y_min, 0.10f);
            const Vector3 neck_center = has_neck ? neck_bind : (centroid + up_axis * (y_max - Vector3::Dot(centroid, up_axis)));

            for (uint32_t p = 0; p < p_cnt; ++p)
            {
                const Vector3& pos = surf.positions[p];
                const Vector3 nor = m_bind_vertices[v_off + surf.particle_rep_vertex[p]].get_normal();
                const float h_from_top = clamp((y_max - Vector3::Dot(pos, up_axis)) / height_span, 0.0f, 1.0f);

                Vector3 rel_neck = pos - neck_center;
                rel_neck -= up_axis * Vector3::Dot(rel_neck, up_axis);
                const float neck_radial = rel_neck.Length();
                const float lateral = fabsf(Vector3::Dot(pos - centroid, right_axis));
                const float up_facing = Vector3::Dot(nor, up_axis);

                // Collar ring & top deltoid shoulder ridge stay cleanly seated on the body so fabric never slips or opens gaps
                if ((h_from_top < 0.11f && neck_radial < 0.165f) || (h_from_top < 0.17f && up_facing > 0.30f))
                {
                    surf.inv_mass[p]     = 0.0f;
                    surf.max_distance[p] = 0.0f;
                    surf.anim_drive[p]   = 1.0f;
                    continue;
                }

                // Chest, ribs, waist, sleeves & bottom hem drape smoothly downward under gravity & hug body capsules
                const float shoulder_top = clamp((up_facing - 0.15f) / 0.45f, 0.0f, 1.0f) * clamp(1.0f - h_from_top / 0.32f, 0.0f, 1.0f);
                const float sleeve_mask  = clamp((lateral - 0.19f) / 0.08f, 0.0f, 1.0f);
                const float drape_t      = clamp((h_from_top - 0.12f) / 0.80f, 0.0f, 1.0f);

                float max_d = 0.008f + 0.036f * drape_t;
                float drive = 0.24f - 0.19f * drape_t;

                max_d = max_d * (1.0f - 0.65f * shoulder_top) * (1.0f - 0.30f * sleeve_mask);
                drive = drive + 0.24f * shoulder_top + 0.08f * sleeve_mask;

                surf.inv_mass[p]     = 1.0f;
                surf.max_distance[p] = clamp(max_d, 0.004f, 0.044f);
                surf.anim_drive[p]   = clamp(drive, 0.035f, 0.50f);
            }

            // Connect nearby proxy nodes into a smooth unified stretch + bending cloth cage (~300-450 nodes)
            const float stretch_r2 = (cell_size * 1.75f) * (cell_size * 1.75f);
            const float bend_r2    = (cell_size * 3.00f) * (cell_size * 3.00f);
            for (uint32_t a = 0; a < p_cnt; ++a)
            {
                for (uint32_t b = a + 1; b < p_cnt; ++b)
                {
                    const float d2 = (surf.positions[b] - surf.positions[a]).LengthSquared();
                    if (d2 <= stretch_r2 && d2 > 1e-6f)
                    {
                        surf.stretch_constraints.push_back({ a, b, sqrtf(d2) });
                    }
                    else if (d2 <= bend_r2 && d2 > stretch_r2)
                    {
                        surf.bend_constraints.push_back({ a, b, sqrtf(d2) });
                    }
                }
            }

            // Bind every high-res render vertex (outer wall, inner wall, collar, hem) smoothly to its 3 nearest proxy nodes
            surf.vertex_bindings.resize(v_cnt);
            const float inv_sigma2 = 1.0f / (2.0f * cell_size * cell_size);
            for (uint32_t v = 0; v < v_cnt; ++v)
            {
                const RHI_Vertex_PosTexNorTan& bv = m_bind_vertices[v_off + v];
                const Vector3 p(bv.pos[0], bv.pos[1], bv.pos[2]);

                uint32_t best_id[3] = { 0, 0, 0 };
                float best_d2[3] = { numeric_limits<float>::max(), numeric_limits<float>::max(), numeric_limits<float>::max() };
                for (uint32_t k = 0; k < p_cnt; ++k)
                {
                    const float d2 = (p - surf.positions[k]).LengthSquared();
                    if (d2 < best_d2[0])
                    {
                        best_d2[2] = best_d2[1]; best_id[2] = best_id[1];
                        best_d2[1] = best_d2[0]; best_id[1] = best_id[0];
                        best_d2[0] = d2;         best_id[0] = k;
                    }
                    else if (d2 < best_d2[1])
                    {
                        best_d2[2] = best_d2[1]; best_id[2] = best_id[1];
                        best_d2[1] = d2;         best_id[1] = k;
                    }
                    else if (d2 < best_d2[2])
                    {
                        best_d2[2] = d2;         best_id[2] = k;
                    }
                }

                float w0 = expf(-best_d2[0] * inv_sigma2);
                float w1 = expf(-best_d2[1] * inv_sigma2);
                float w2 = expf(-best_d2[2] * inv_sigma2);
                const float w_sum = max(w0 + w1 + w2, 1e-6f);
                surf.vertex_bindings[v] =
                {
                    { static_cast<uint16_t>(best_id[0]), static_cast<uint16_t>(best_id[1]), static_cast<uint16_t>(best_id[2]) },
                    { w0 / w_sum, w1 / w_sum, w2 / w_sum }
                };
            }

            m_cloth_state.surfaces.push_back(move(surf));
        }

        if (m_cloth_state.surfaces.empty())
        {
            return;
        }

        // Auto-calibrate skeleton body collision capsules in bind pose from the garment vertices
        auto add_capsule = [&](int32_t ja, int32_t jb, const Vector3& off_a, const Vector3& off_b, float min_r, float max_r, float inset)
        {
            if (ja < 0 || jb < 0)
            {
                return;
            }
            const Vector3 pa = bind_globals[ja].GetTranslation() + off_a;
            const Vector3 pb = bind_globals[jb].GetTranslation() + off_b;
            const Vector3 ab = pb - pa;
            const float ab_len_sq = ab.LengthSquared();
            if (ab_len_sq < 1e-5f)
            {
                return;
            }

            float min_dist = numeric_limits<float>::max();
            for (const ClothSurface& surf : m_cloth_state.surfaces)
            {
                for (const Vector3& pt : surf.positions)
                {
                    const float t = clamp(Vector3::Dot(pt - pa, ab) / ab_len_sq, 0.0f, 1.0f);
                    const Vector3 closest = pa + ab * t;
                    const float d = (pt - closest).Length();
                    if (d < min_dist)
                    {
                        min_dist = d;
                    }
                }
            }

            const float radius = clamp(min_dist - inset, min_r, max_r);
            // Convert offsets into each joint's local frame so they follow joint rotation accurately
            const Vector3 local_off_a = bind_globals[ja].Inverted().GetRotation() * off_a;
            const Vector3 local_off_b = bind_globals[jb].Inverted().GetRotation() * off_b;
            m_cloth_state.capsules.push_back({ ja, jb, local_off_a, local_off_b, radius });
        };

        const int32_t torso_top = (j_spine3 >= 0) ? j_spine3 : ((j_spine2 >= 0) ? j_spine2 : j_neck);
        const int32_t torso_mid = (j_spine2 >= 0) ? j_spine2 : j_spine1;
        add_capsule(j_pelvis, torso_mid, Vector3::Zero, Vector3::Zero, 0.07f, 0.135f, 0.008f);
        add_capsule(torso_mid, torso_top, Vector3::Zero, Vector3::Zero, 0.075f, 0.145f, 0.008f);
        add_capsule(torso_mid, torso_top, right_axis * 0.055f, right_axis * 0.055f, 0.055f, 0.115f, 0.007f);
        add_capsule(torso_mid, torso_top, -right_axis * 0.055f, -right_axis * 0.055f, 0.055f, 0.115f, 0.007f);
        add_capsule(j_upperarm_l, j_lowerarm_l, Vector3::Zero, Vector3::Zero, 0.036f, 0.068f, 0.006f);
        add_capsule(j_upperarm_r, j_lowerarm_r, Vector3::Zero, Vector3::Zero, 0.036f, 0.068f, 0.006f);
    }

    void Animator::StepSkinnedCloth(Mesh* mesh, const Skeleton& skeleton, float delta_time)
    {
        if (!m_cloth_enabled || m_cloth_state.surfaces.empty() || m_cloth_state.built_mesh != mesh)
        {
            return;
        }
        if (m_skinned_vertices.empty() || m_global_matrices.size() < skeleton.bind_inverse_global_matrices.size())
        {
            return;
        }

        // Build live body collision capsules in character-local space
        struct LiveCapsule
        {
            Vector3 a;
            Vector3 b;
            float radius;
        };
        LiveCapsule live_capsules[12];
        uint32_t capsule_count = 0;
        for (const ClothCapsuleSpec& spec : m_cloth_state.capsules)
        {
            if (capsule_count >= 12)
            {
                break;
            }
            if (spec.joint_a < 0 || spec.joint_b < 0 ||
                spec.joint_a >= static_cast<int32_t>(m_global_matrices.size()) ||
                spec.joint_b >= static_cast<int32_t>(m_global_matrices.size()))
            {
                continue;
            }
            const Matrix& ma = m_global_matrices[spec.joint_a];
            const Matrix& mb = m_global_matrices[spec.joint_b];
            live_capsules[capsule_count++] =
            {
                ma.GetTranslation() + ma.GetRotation() * spec.offset_a,
                mb.GetTranslation() + mb.GetRotation() * spec.offset_b,
                spec.radius
            };
        }

        // Transform world gravity (-9.81 Y) into character-local space and compute inertial frame carry
        const Quaternion world_rot = m_cloth_state.cached_world.GetRotation().Normalized();
        const Vector3 gravity_local = world_rot.Conjugate() * Vector3(0.0f, -9.81f, 0.0f);

        Matrix frame_carry = Matrix::Identity;
        bool use_carry = false;
        if (m_cloth_state.has_prev_world)
        {
            frame_carry = m_cloth_state.prev_world * m_cloth_state.cached_world.Inverted();
            const float jump_sq = frame_carry.GetTranslation().LengthSquared();
            use_carry = jump_sq < 2.25f; // ignore teleports
        }
        m_cloth_state.prev_world = m_cloth_state.cached_world;
        m_cloth_state.has_prev_world = true;

        const float clamped_dt = clamp(delta_time > 0.0f ? delta_time : (1.0f / 60.0f), 1.0f / 240.0f, 1.0f / 30.0f);
        m_cloth_state.time += clamped_dt;

        for (ClothSurface& surf : m_cloth_state.surfaces)
        {
            const uint32_t p_cnt = static_cast<uint32_t>(surf.positions.size());
            if (surf.vertex_offset + surf.vertex_count > m_skinned_vertices.size())
            {
                continue;
            }

            // Read freshly skinned target positions and normals for each welded cloth particle
            for (uint32_t p = 0; p < p_cnt; ++p)
            {
                const RHI_Vertex_PosTexNorTan& sv = m_skinned_vertices[surf.vertex_offset + surf.particle_rep_vertex[p]];
                surf.skinned_positions[p] = Vector3(sv.pos[0], sv.pos[1], sv.pos[2]);
                surf.skinned_normals[p]   = sv.get_normal();
            }

            const bool warm_start = !surf.settled;
            if (warm_start)
            {
                surf.positions      = surf.skinned_positions;
                surf.prev_positions = surf.skinned_positions;
                surf.settled        = true;
            }
            else if (use_carry)
            {
                // Carry simulated cloth smoothly across world rotation/translation for genuine inertial swing
                for (uint32_t p = 0; p < p_cnt; ++p)
                {
                    if (surf.inv_mass[p] <= 0.0f)
                    {
                        continue;
                    }
                    const Vector3 carried_pos  = surf.positions[p] * frame_carry;
                    const Vector3 carried_prev = surf.prev_positions[p] * frame_carry;
                    surf.positions[p]      = Vector3::Lerp(surf.positions[p], carried_pos, 0.65f);
                    surf.prev_positions[p] = Vector3::Lerp(surf.prev_positions[p], carried_prev, 0.65f);
                }
            }

            const int substeps   = warm_start ? 16 : 2;
            const int iterations = warm_start ? 4 : 4;
            const float sub_dt   = warm_start ? (1.0f / 60.0f) : (clamped_dt / static_cast<float>(substeps));
            const float sub_dt2  = sub_dt * sub_dt;
            const float damping  = warm_start ? 0.78f : 0.84f;

            const float alpha_stretch = 0.00025f / sub_dt2;
            const float alpha_bend    = 0.0035f / sub_dt2;

            for (int step = 0; step < substeps; ++step)
            {
                // 1. Predict particle positions under calm weighty gravity (zero artificial vibration/jitter!)
                for (uint32_t p = 0; p < p_cnt; ++p)
                {
                    if (surf.inv_mass[p] <= 0.0f)
                    {
                        surf.positions[p]      = surf.skinned_positions[p];
                        surf.prev_positions[p] = surf.skinned_positions[p];
                        continue;
                    }

                    const Vector3 cur = surf.positions[p];
                    Vector3 vel = (cur - surf.prev_positions[p]) * damping;
                    surf.prev_positions[p] = cur;

                    Vector3 next_pos = cur + vel + gravity_local * (0.95f * sub_dt2);

                    // Smooth animation drive keeps garment following torso & breathing motion with calm gravity drape
                    const float drive = surf.anim_drive[p] * (warm_start ? 0.28f : 0.38f);
                    next_pos = Vector3::Lerp(next_pos, surf.skinned_positions[p], drive);

                    surf.positions[p] = next_pos;
                }

                // 2. XPBD constraint & collision solver iterations (pose-relative rest lengths prevent T-pose -> idle buckling!)
                for (int iter = 0; iter < iterations; ++iter)
                {
                    // Structural stretch constraints using live posed edge length (eliminates T-pose armpit crumpling)
                    for (const ClothConstraint& c : surf.stretch_constraints)
                    {
                        const float wa = surf.inv_mass[c.a];
                        const float wb = surf.inv_mass[c.b];
                        const float w_sum = wa + wb;
                        if (w_sum <= 0.0f)
                        {
                            continue;
                        }
                        const float posed_len = (surf.skinned_positions[c.b] - surf.skinned_positions[c.a]).Length();
                        if (posed_len < 1e-5f)
                        {
                            continue;
                        }
                        Vector3 diff = surf.positions[c.b] - surf.positions[c.a];
                        const float dist = diff.Length();
                        if (dist < 1e-6f)
                        {
                            continue;
                        }
                        const float corr = (dist - posed_len) / (w_sum + alpha_stretch);
                        const Vector3 dir = diff / dist;
                        if (wa > 0.0f)
                        {
                            surf.positions[c.a] += dir * (corr * wa);
                        }
                        if (wb > 0.0f)
                        {
                            surf.positions[c.b] -= dir * (corr * wb);
                        }
                    }

                    // Bending constraints (broad, silky cotton jersey folds without micro-kinks)
                    for (const ClothConstraint& c : surf.bend_constraints)
                    {
                        const float wa = surf.inv_mass[c.a];
                        const float wb = surf.inv_mass[c.b];
                        const float w_sum = wa + wb;
                        if (w_sum <= 0.0f)
                        {
                            continue;
                        }
                        const float posed_bend = (surf.skinned_positions[c.b] - surf.skinned_positions[c.a]).Length();
                        if (posed_bend < 1e-5f)
                        {
                            continue;
                        }
                        Vector3 diff = surf.positions[c.b] - surf.positions[c.a];
                        const float dist = diff.Length();
                        if (dist < 1e-6f)
                        {
                            continue;
                        }
                        const float corr = (dist - posed_bend) / (w_sum + alpha_bend);
                        const Vector3 dir = diff / dist;
                        if (wa > 0.0f)
                        {
                            surf.positions[c.a] += dir * (corr * wa);
                        }
                        if (wb > 0.0f)
                        {
                            surf.positions[c.b] -= dir * (corr * wb);
                        }
                    }

                    // Body capsule collisions + skinned backstop + max-distance tether
                    for (uint32_t p = 0; p < p_cnt; ++p)
                    {
                        if (surf.inv_mass[p] <= 0.0f)
                        {
                            surf.positions[p] = surf.skinned_positions[p];
                            continue;
                        }

                        Vector3 pos = surf.positions[p];

                        // Skeleton body capsules
                        for (uint32_t ci = 0; ci < capsule_count; ++ci)
                        {
                            const LiveCapsule& cap = live_capsules[ci];
                            const Vector3 ab = cap.b - cap.a;
                            const float ab_len_sq = ab.LengthSquared();
                            const float t = (ab_len_sq > 1e-6f) ? clamp(Vector3::Dot(pos - cap.a, ab) / ab_len_sq, 0.0f, 1.0f) : 0.0f;
                            const Vector3 closest = cap.a + ab * t;
                            const Vector3 delta = pos - closest;
                            const float dist_sq = delta.LengthSquared();
                            if (dist_sq < cap.radius * cap.radius && dist_sq > 1e-10f)
                            {
                                const float dist = sqrtf(dist_sq);
                                pos = closest + delta * (cap.radius / dist);
                            }
                        }

                        // Strict outward backstop prevents fabric from ever clipping into underlying torso/arm skin
                        const Vector3& ref_pos = surf.skinned_positions[p];
                        const Vector3& ref_nor = surf.skinned_normals[p];
                        const Vector3 offset = pos - ref_pos;
                        const float signed_normal_dist = Vector3::Dot(offset, ref_nor);
                        if (signed_normal_dist < 0.0f)
                        {
                            pos -= ref_nor * signed_normal_dist;
                        }

                        // Max-distance tether prevents explosive stretching during extreme motion
                        const Vector3 final_off = pos - ref_pos;
                        const float max_d = surf.max_distance[p];
                        const float off_sq = final_off.LengthSquared();
                        if (off_sq > max_d * max_d && off_sq > 1e-10f)
                        {
                            pos = ref_pos + final_off * (max_d / sqrtf(off_sq));
                        }

                        surf.positions[p] = pos;
                    }
                }
            }

            // 3. Compute C1-smooth proxy node displacements and apply seamlessly across all outer/inner render vertices
            for (uint32_t p = 0; p < p_cnt; ++p)
            {
                surf.displacements[p] = surf.positions[p] - surf.skinned_positions[p];
            }

            for (uint32_t v = 0; v < surf.vertex_count; ++v)
            {
                const ClothVertexBinding& bind = surf.vertex_bindings[v];
                const Vector3 disp =
                    surf.displacements[bind.nodes[0]] * bind.weights[0] +
                    surf.displacements[bind.nodes[1]] * bind.weights[1] +
                    surf.displacements[bind.nodes[2]] * bind.weights[2];

                RHI_Vertex_PosTexNorTan& dst = m_skinned_vertices[surf.vertex_offset + v];
                dst.pos[0] += disp.x;
                dst.pos[1] += disp.y;
                dst.pos[2] += disp.z;
            }
        }
    }

    void Animator::ResetProceduralEyes()
    {
        m_eye_state = {};
    }

    void Animator::ResolveProceduralEyes(Mesh* mesh, const Skeleton& skeleton)
    {
        if (!mesh || m_eye_state.built_mesh == mesh)
        {
            return;
        }

        m_eye_state = {};
        m_eye_state.built_mesh = mesh;

        auto to_lower = [](string value)
        {
            for (char& ch : value)
            {
                ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
            }
            return value;
        };

        // Resolve head joint index
        const char* head_candidates[] = { "head", "Head", "mixamorig:Head", "neck_01" };
        for (const char* cand : head_candidates)
        {
            int32_t idx = FindJointIndex(skeleton, cand);
            if (idx >= 0)
            {
                m_eye_state.head_joint = idx;
                break;
            }
        }
        if (m_eye_state.head_joint < 0 || m_bind_vertices.empty())
        {
            return;
        }

        const vector<Render*>& renders = ResolveSkinRenders(mesh);
        if (renders.empty())
        {
            return;
        }

        // Seed per-entity PRNG so multiple characters never blink or glance in robotic unison
        uint32_t seed = 0x5A17E3u;
        if (GetEntity())
        {
            uint64_t id = GetEntity()->GetObjectId();
            seed ^= static_cast<uint32_t>(id ^ (id >> 32));
        }
        m_eye_state.rng_state = seed != 0u ? seed : 0x5A17E3u;

        // 1. Locate eyeball submesh(es) (specifically mh_eyes / cornea — excluding tearline & eyeshell)
        vector<Render*> eyeball_renders;
        vector<Render*> head_skin_renders;
        vector<Render*> upper_lash_renders;
        vector<Render*> lower_lash_renders;
        vector<Render*> eyeshell_renders;

        for (Render* render : renders)
        {
            if (!render || !render->GetEntity())
            {
                continue;
            }
            const string ename = to_lower(render->GetEntity()->GetObjectName());
            const string mname = to_lower(render->GetMaterialName());
            const string both  = ename + " " + mname;

            if (mname.find("tearline") != string::npos || mname.find("eyeedge") != string::npos)
            {
                continue;
            }
            if (mname.find("eyeshell") != string::npos)
            {
                eyeshell_renders.push_back(render);
            }
            else if (mname.find("eyelashes_upper") != string::npos || (mname.find("eyelash") != string::npos && mname.find("lower") == string::npos))
            {
                upper_lash_renders.push_back(render);
            }
            else if (mname.find("eyelashes_lower") != string::npos)
            {
                lower_lash_renders.push_back(render);
            }
            else if (mname.find("mh_eyes") != string::npos || mname.find("cornea") != string::npos || (mname.find("eye") != string::npos && mname.find("brow") == string::npos && mname.find("lash") == string::npos && mname.find("shell") == string::npos))
            {
                eyeball_renders.push_back(render);
            }
            else if (mname.find("mh_face") != string::npos || (both.find("head") != string::npos && mname.find("teeth") == string::npos && mname.find("caruncle") == string::npos && mname.find("hair") == string::npos && mname.find("brow") == string::npos && mname.find("beard") == string::npos))
            {
                head_skin_renders.push_back(render);
            }
        }

        if (eyeball_renders.empty())
        {
            return;
        }

        // Compute exact geometric sphere centers via bounding-box midpoint (unaffected by front-heavy iris tessellation!)
        Vector3 min_l(numeric_limits<float>::max());
        Vector3 max_l(-numeric_limits<float>::max());
        Vector3 min_r(numeric_limits<float>::max());
        Vector3 max_r(-numeric_limits<float>::max());
        uint32_t cnt_l = 0;
        uint32_t cnt_r = 0;

        auto get_submesh_range = [&](Render* r, uint32_t& out_off, uint32_t& out_cnt) -> bool
        {
            const uint32_t sub_idx = r->GetSubMeshIndex();
            if (sub_idx >= mesh->GetSubMeshCount())
            {
                return false;
            }
            const GeometryRange& range = mesh->GetSubMesh(sub_idx).root;
            out_off = range.vertex_offset;
            out_cnt = range.vertex_count;
            return out_cnt > 0 && out_off + out_cnt <= m_bind_vertices.size();
        };

        // First find overall eye midpoint X so left/right split works even if mesh is slightly offset
        float mid_x = 0.0f;
        uint32_t total_eye_verts = 0;
        for (Render* r : eyeball_renders)
        {
            uint32_t v_off = 0;
            uint32_t v_cnt = 0;
            if (!get_submesh_range(r, v_off, v_cnt))
            {
                continue;
            }
            for (uint32_t v = 0; v < v_cnt; ++v)
            {
                mid_x += m_bind_vertices[v_off + v].pos[0];
                ++total_eye_verts;
            }
        }
        if (total_eye_verts < 16)
        {
            return;
        }
        mid_x /= static_cast<float>(total_eye_verts);

        for (Render* r : eyeball_renders)
        {
            uint32_t v_off = 0;
            uint32_t v_cnt = 0;
            if (!get_submesh_range(r, v_off, v_cnt))
            {
                continue;
            }
            for (uint32_t v = 0; v < v_cnt; ++v)
            {
                const uint32_t gidx = v_off + v;
                const RHI_Vertex_PosTexNorTan& bv = m_bind_vertices[gidx];
                const Vector3 p(bv.pos[0], bv.pos[1], bv.pos[2]);
                if (p.x >= mid_x)
                {
                    min_l = Vector3::Min(min_l, p);
                    max_l = Vector3::Max(max_l, p);
                    ++cnt_l;
                    m_eye_state.left_eye_vertices.push_back(gidx);
                }
                else
                {
                    min_r = Vector3::Min(min_r, p);
                    max_r = Vector3::Max(max_r, p);
                    ++cnt_r;
                    m_eye_state.right_eye_vertices.push_back(gidx);
                }
            }
        }
        if (cnt_l < 8 || cnt_r < 8)
        {
            return;
        }

        const Vector3 center_l = (min_l + max_l) * 0.5f;
        const Vector3 center_r = (min_r + max_r) * 0.5f;
        const Vector3 eye_mid  = (center_l + center_r) * 0.5f;

        // Average eyeball radius
        float rad_sum = 0.0f;
        for (uint32_t gidx : m_eye_state.left_eye_vertices)
        {
            const RHI_Vertex_PosTexNorTan& bv = m_bind_vertices[gidx];
            rad_sum += (Vector3(bv.pos[0], bv.pos[1], bv.pos[2]) - center_l).Length();
        }
        const float eye_radius = clamp(rad_sum / static_cast<float>(m_eye_state.left_eye_vertices.size()), 0.009f, 0.018f);

        // Compute orthonormal anatomical eye basis (eye_right, eye_up, eye_forward) in bind pose
        const Matrix head_bind = skeleton.bind_inverse_global_matrices[m_eye_state.head_joint].Inverted();
        const Vector3 head_pos = head_bind.GetTranslation();

        Vector3 eye_right = (center_r - center_l);
        if (eye_right.LengthSquared() < 1e-6f)
        {
            return;
        }
        eye_right = eye_right.Normalized();

        Vector3 eye_up = Vector3::Up;
        const int32_t j_pelvis = FindJointIndex(skeleton, "pelvis");
        if (j_pelvis >= 0)
        {
            const Vector3 p_pelvis = skeleton.bind_inverse_global_matrices[j_pelvis].Inverted().GetTranslation();
            Vector3 spine = head_pos - p_pelvis;
            spine -= eye_right * Vector3::Dot(spine, eye_right);
            if (spine.LengthSquared() > 1e-4f)
            {
                eye_up = spine.Normalized();
            }
        }

        Vector3 eye_forward = eye_mid - head_pos;
        eye_forward -= eye_right * Vector3::Dot(eye_forward, eye_right);
        eye_forward -= eye_up * Vector3::Dot(eye_forward, eye_up);
        if (eye_forward.LengthSquared() < 1e-6f)
        {
            eye_forward = Vector3::Cross(eye_right, eye_up);
        }
        eye_forward = eye_forward.Normalized();
        // Re-orthogonalize eye_up strictly orthogonal to eye_right and eye_forward
        eye_up = (eye_up - eye_right * Vector3::Dot(eye_up, eye_right) - eye_forward * Vector3::Dot(eye_up, eye_forward)).Normalized();

        m_eye_state.eye_center_l = center_l;
        m_eye_state.eye_center_r = center_r;
        m_eye_state.eye_right    = eye_right;
        m_eye_state.eye_up       = eye_up;
        m_eye_state.eye_forward  = eye_forward;
        m_eye_state.eye_radius   = eye_radius;

        // 2. Sculpt smooth anatomical eyelid weights on head skin, upper/lower eyelashes, and eyeshell
        auto classify_eye_side = [&](const Vector3& p, uint8_t& out_side, Vector3& out_center)
        {
            const float dl = (p - center_l).LengthSquared();
            const float dr = (p - center_r).LengthSquared();
            if (dl <= dr)
            {
                out_side = 0;
                out_center = center_l;
            }
            else
            {
                out_side = 1;
                out_center = center_r;
            }
        };

        const float pi_half = 1.57079632679f;

        // Head skin eyelids (smooth cosine envelope peaking right at the upper lash margin, zero at brow & nose bridge!)
        for (Render* r : head_skin_renders)
        {
            uint32_t v_off = 0;
            uint32_t v_cnt = 0;
            if (!get_submesh_range(r, v_off, v_cnt))
            {
                continue;
            }
            for (uint32_t v = 0; v < v_cnt; ++v)
            {
                const uint32_t gidx = v_off + v;
                const RHI_Vertex_PosTexNorTan& bv = m_bind_vertices[gidx];
                const Vector3 p(bv.pos[0], bv.pos[1], bv.pos[2]);

                uint8_t side = 0;
                Vector3 c = center_l;
                classify_eye_side(p, side, c);

                const Vector3 rel = p - c;
                const float dx = Vector3::Dot(rel, eye_right);
                const float dy = Vector3::Dot(rel, eye_up);
                const float dz = Vector3::Dot(rel, eye_forward);

                // Strictly over the front eye socket (never back of socket, temple, or nose bridge)
                if (dz < 0.20f * eye_radius || dz > 2.20f * eye_radius)
                {
                    continue;
                }
                const float ax = fabsf(dx) / (1.48f * eye_radius);
                if (ax >= 1.0f)
                {
                    continue;
                }

                const float hx = powf(cosf(ax * pi_half), 0.90f);
                const float hz = clamp((dz - 0.20f * eye_radius) / (0.65f * eye_radius), 0.0f, 1.0f);

                float uw = 0.0f;
                float lw = 0.0f;

                // Upper eyelid: peaks along upper margin (dy ~ 0.15..0.48 R) and tapers cleanly to 0 before the brow (1.08 R)
                if (dy > -0.10f * eye_radius && dy < 1.12f * eye_radius)
                {
                    const float ny = dy / eye_radius;
                    float vy = 0.0f;
                    if (ny < 0.25f)
                    {
                        vy = clamp((ny + 0.10f) / 0.35f, 0.0f, 1.0f);
                    }
                    else
                    {
                        const float t = clamp((ny - 0.25f) / 0.87f, 0.0f, 1.0f);
                        vy = cosf(t * pi_half);
                        vy = vy * vy;
                    }
                    uw = hx * hz * vy;
                }

                // Lower eyelid: gentle subtle lift (0.16 max) when blinking
                if (dy < -0.08f * eye_radius && dy > -0.92f * eye_radius)
                {
                    const float ny = -dy / eye_radius;
                    float vy = 0.0f;
                    if (ny < 0.30f)
                    {
                        vy = clamp((ny - 0.08f) / 0.22f, 0.0f, 1.0f);
                    }
                    else
                    {
                        const float t = clamp((ny - 0.30f) / 0.62f, 0.0f, 1.0f);
                        vy = cosf(t * pi_half);
                        vy = vy * vy;
                    }
                    lw = hx * hz * vy;
                }

                if (uw > 0.015f || lw > 0.015f)
                {
                    m_eye_state.eyelid_vertices.push_back({ gidx, side, uw, lw });
                }
            }
        }

        // Upper eyelashes move in 100% lockstep with the upper eyelid margin
        for (Render* r : upper_lash_renders)
        {
            uint32_t v_off = 0;
            uint32_t v_cnt = 0;
            if (!get_submesh_range(r, v_off, v_cnt))
            {
                continue;
            }
            for (uint32_t v = 0; v < v_cnt; ++v)
            {
                const uint32_t gidx = v_off + v;
                const RHI_Vertex_PosTexNorTan& bv = m_bind_vertices[gidx];
                const Vector3 p(bv.pos[0], bv.pos[1], bv.pos[2]);
                uint8_t side = 0;
                Vector3 c = center_l;
                classify_eye_side(p, side, c);
                const float ax = clamp(fabsf(Vector3::Dot(p - c, eye_right)) / (1.72f * eye_radius), 0.0f, 0.96f);
                const float hx = powf(cosf(ax * pi_half), 0.72f);
                m_eye_state.eyelid_vertices.push_back({ gidx, side, hx, 0.0f });
            }
        }

        // Lower eyelashes lift softly with the lower lid
        for (Render* r : lower_lash_renders)
        {
            uint32_t v_off = 0;
            uint32_t v_cnt = 0;
            if (!get_submesh_range(r, v_off, v_cnt))
            {
                continue;
            }
            for (uint32_t v = 0; v < v_cnt; ++v)
            {
                const uint32_t gidx = v_off + v;
                const RHI_Vertex_PosTexNorTan& bv = m_bind_vertices[gidx];
                const Vector3 p(bv.pos[0], bv.pos[1], bv.pos[2]);
                uint8_t side = 0;
                Vector3 c = center_l;
                classify_eye_side(p, side, c);
                const float ax = clamp(fabsf(Vector3::Dot(p - c, eye_right)) / (1.72f * eye_radius), 0.0f, 0.96f);
                const float hx = powf(cosf(ax * pi_half), 0.72f);
                m_eye_state.eyelid_vertices.push_back({ gidx, side, 0.0f, hx });
            }
        }

        // Upper-eyelid occlusion shell follows upper lid cleanly
        for (Render* r : eyeshell_renders)
        {
            uint32_t v_off = 0;
            uint32_t v_cnt = 0;
            if (!get_submesh_range(r, v_off, v_cnt))
            {
                continue;
            }
            for (uint32_t v = 0; v < v_cnt; ++v)
            {
                const uint32_t gidx = v_off + v;
                const RHI_Vertex_PosTexNorTan& bv = m_bind_vertices[gidx];
                const Vector3 p(bv.pos[0], bv.pos[1], bv.pos[2]);
                uint8_t side = 0;
                Vector3 c = center_l;
                classify_eye_side(p, side, c);
                const float dy = Vector3::Dot(p - c, eye_up);
                if (dy > 0.0f)
                {
                    const float ax = clamp(fabsf(Vector3::Dot(p - c, eye_right)) / (1.60f * eye_radius), 0.0f, 0.96f);
                    const float hx = powf(cosf(ax * pi_half), 0.80f);
                    const float vy = clamp(dy / (0.65f * eye_radius), 0.0f, 1.0f);
                    m_eye_state.eyelid_vertices.push_back({ gidx, side, hx * vy, 0.0f });
                }
            }
        }

        m_eye_state.valid = true;
    }

    void Animator::StepProceduralEyes(Mesh* mesh, const Skeleton& skeleton, float delta_time)
    {
        if (!m_procedural_eyes_enabled || !m_eye_state.valid || m_eye_state.built_mesh != mesh)
        {
            return;
        }
        if (m_eye_state.head_joint < 0 ||
            m_eye_state.head_joint >= static_cast<int32_t>(m_global_matrices.size()) ||
            m_eye_state.head_joint >= static_cast<int32_t>(skeleton.bind_inverse_global_matrices.size()))
        {
            return;
        }

        const float dt = clamp(delta_time > 0.0f ? delta_time : (1.0f / 60.0f), 0.0f, 0.05f);
        m_eye_state.clock += dt;

        // Deterministic fast xorshift32 PRNG in [0, 1]
        auto next_rand = [&]() -> float
        {
            uint32_t x = m_eye_state.rng_state;
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            m_eye_state.rng_state = x;
            return static_cast<float>(x & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
        };

        // 1. Update organic human gaze saccades (crisp ~65ms shifts + calm steady fixation + micro-saccades)
        m_eye_state.gaze_timer -= dt;
        if (m_eye_state.gaze_timer <= 0.0f)
        {
            m_eye_state.gaze_start_yaw   = m_eye_state.gaze_yaw;
            m_eye_state.gaze_start_pitch = m_eye_state.gaze_pitch;
            m_eye_state.gaze_move_t      = 0.0f;

            const float r_type = next_rand();
            if (r_type < 0.42f)
            {
                // Calm attentive forward gaze with subtle natural offset
                m_eye_state.gaze_target_yaw   = (next_rand() * 2.0f - 1.0f) * 0.085f; // +/- 4.9 deg
                m_eye_state.gaze_target_pitch = (next_rand() * 2.0f - 1.0f) * 0.042f; // +/- 2.4 deg
                m_eye_state.gaze_timer        = 1.2f + next_rand() * 1.6f;
            }
            else
            {
                // Natural side/up/down glance (clearly alive, never extreme wall-eyed)
                const float dir = (next_rand() < 0.5f) ? -1.0f : 1.0f;
                m_eye_state.gaze_target_yaw   = dir * (0.11f + next_rand() * 0.11f); // +/- 6.3..12.6 deg
                m_eye_state.gaze_target_pitch = (next_rand() * 2.0f - 1.0f) * 0.085f; // +/- 4.9 deg
                m_eye_state.gaze_timer        = 1.0f + next_rand() * 1.4f;

                // Humans naturally trigger a blink on ~45% of larger gaze shifts
                const float jump = fabsf(m_eye_state.gaze_target_yaw - m_eye_state.gaze_start_yaw);
                if (jump > 0.11f && m_eye_state.blink_phase < 0.0f && next_rand() < 0.45f)
                {
                    m_eye_state.blink_timer = 0.0f;
                }
            }
            m_eye_state.gaze_move_duration = 0.062f + next_rand() * 0.028f;
        }

        if (m_eye_state.gaze_move_t < 1.0f)
        {
            m_eye_state.gaze_move_t = min(1.0f, m_eye_state.gaze_move_t + dt / max(m_eye_state.gaze_move_duration, 0.02f));
            const float u = m_eye_state.gaze_move_t;
            const float smooth = u * u * (3.0f - 2.0f * u);
            m_eye_state.gaze_yaw   = m_eye_state.gaze_start_yaw + (m_eye_state.gaze_target_yaw - m_eye_state.gaze_start_yaw) * smooth;
            m_eye_state.gaze_pitch = m_eye_state.gaze_start_pitch + (m_eye_state.gaze_target_pitch - m_eye_state.gaze_start_pitch) * smooth;
        }

        // Tiny physiological fixation micro-saccade drift so eyes feel genuinely alive even while fixating
        const float micro_yaw   = sinf(m_eye_state.clock * 1.7f) * 0.0055f + sinf(m_eye_state.clock * 4.3f) * 0.0025f;
        const float micro_pitch = cosf(m_eye_state.clock * 1.3f) * 0.0038f + sinf(m_eye_state.clock * 3.1f) * 0.0018f;
        const float live_yaw    = m_eye_state.gaze_yaw + micro_yaw;
        const float live_pitch  = m_eye_state.gaze_pitch + micro_pitch;

        // 2. Update organic human eyelid blinking (fast 55ms close, 20ms hold, silky 110ms open, occasional double blink!)
        float blink_amount = 0.0f;
        if (m_eye_state.blink_phase < 0.0f)
        {
            m_eye_state.blink_timer -= dt;
            if (m_eye_state.blink_timer <= 0.0f)
            {
                m_eye_state.blink_phase    = 0.0f;
                m_eye_state.blink_duration = 0.175f + next_rand() * 0.030f;
            }
        }
        else
        {
            m_eye_state.blink_phase += dt;
            const float t_norm = m_eye_state.blink_phase / max(m_eye_state.blink_duration, 0.08f);
            if (t_norm >= 1.0f)
            {
                m_eye_state.blink_phase = -1.0f;
                if (m_eye_state.pending_double_blink)
                {
                    m_eye_state.pending_double_blink = false;
                    m_eye_state.blink_timer = 0.14f + next_rand() * 0.08f;
                }
                else
                {
                    m_eye_state.pending_double_blink = (next_rand() < 0.16f);
                    m_eye_state.blink_timer = 2.6f + next_rand() * 2.8f;
                }
            }
            else if (t_norm < 0.30f)
            {
                // Crisp fast close
                const float c = t_norm / 0.30f;
                blink_amount = c * c * (3.0f - 2.0f * c);
            }
            else if (t_norm < 0.42f)
            {
                // Brief natural full closure
                blink_amount = 1.0f;
            }
            else
            {
                // Smooth relaxed opening
                const float o = (t_norm - 0.42f) / 0.58f;
                const float inv = 1.0f - o;
                blink_amount = inv * inv * (3.0f - 2.0f * inv);
            }
        }

        // 3. Compute exact live posed left/right eyeball centers & orthonormal head eye basis directly from skinned vertices
        // (100% exact across any head bone rotation or glTF inverse-bind convention, zero pivot drift or lid twitch!)
        Vector3 min_l(numeric_limits<float>::max());
        Vector3 max_l(-numeric_limits<float>::max());
        Vector3 min_r(numeric_limits<float>::max());
        Vector3 max_r(-numeric_limits<float>::max());
        for (uint32_t gidx : m_eye_state.left_eye_vertices)
        {
            if (gidx < m_skinned_vertices.size())
            {
                const RHI_Vertex_PosTexNorTan& sv = m_skinned_vertices[gidx];
                const Vector3 p(sv.pos[0], sv.pos[1], sv.pos[2]);
                min_l = Vector3::Min(min_l, p);
                max_l = Vector3::Max(max_l, p);
            }
        }
        for (uint32_t gidx : m_eye_state.right_eye_vertices)
        {
            if (gidx < m_skinned_vertices.size())
            {
                const RHI_Vertex_PosTexNorTan& sv = m_skinned_vertices[gidx];
                const Vector3 p(sv.pos[0], sv.pos[1], sv.pos[2]);
                min_r = Vector3::Min(min_r, p);
                max_r = Vector3::Max(max_r, p);
            }
        }

        const Vector3 center_l = (min_l + max_l) * 0.5f;
        const Vector3 center_r = (min_r + max_r) * 0.5f;
        const Vector3 eye_mid  = (center_l + center_r) * 0.5f;

        Vector3 e_right = center_r - center_l;
        if (e_right.LengthSquared() < 1e-6f)
        {
            return;
        }
        e_right = e_right.Normalized();

        const Matrix head_rot_delta = m_global_matrices[m_eye_state.head_joint] * skeleton.bind_inverse_global_matrices[m_eye_state.head_joint];
        const Vector3 head_origin   = m_global_matrices[m_eye_state.head_joint].GetTranslation();
        Vector3 e_up = (head_rot_delta.GetRotation() * m_eye_state.eye_up);
        e_up -= e_right * Vector3::Dot(e_up, e_right);
        e_up = (e_up.LengthSquared() > 1e-6f) ? e_up.Normalized() : Vector3::Up;

        Vector3 e_fwd = eye_mid - head_origin;
        e_fwd -= e_right * Vector3::Dot(e_fwd, e_right);
        e_fwd -= e_up * Vector3::Dot(e_fwd, e_up);
        if (e_fwd.LengthSquared() < 1e-6f)
        {
            e_fwd = head_rot_delta.GetRotation() * m_eye_state.eye_forward;
            e_fwd -= e_right * Vector3::Dot(e_fwd, e_right);
            e_fwd -= e_up * Vector3::Dot(e_fwd, e_up);
        }
        e_fwd = e_fwd.Normalized();
        e_up  = (e_up - e_right * Vector3::Dot(e_up, e_right) - e_fwd * Vector3::Dot(e_up, e_fwd)).Normalized();

        // Handedness-independent basis rotation helper (pitch downward around e_right, then yaw around e_up)
        auto rotate_in_eye_basis = [&](const Vector3& v, float cos_p, float sin_p, float cos_y, float sin_y) -> Vector3
        {
            const float rx = Vector3::Dot(v, e_right);
            const float ry = Vector3::Dot(v, e_up);
            const float rz = Vector3::Dot(v, e_fwd);
            const float ry1 = ry * cos_p - rz * sin_p;
            const float rz1 = ry * sin_p + rz * cos_p;
            const float rx2 = rx * cos_y + rz1 * sin_y;
            const float rz2 = -rx * sin_y + rz1 * cos_y;
            return e_right * rx2 + e_up * ry1 + e_fwd * rz2;
        };

        // 4. Rotate left and right eyeballs around their exact anatomical centers
        const float cos_ep = cosf(live_pitch);
        const float sin_ep = sinf(live_pitch);
        const float cos_ey = cosf(live_yaw);
        const float sin_ey = sinf(live_yaw);

        auto apply_eyeball_rotation = [&](const vector<uint32_t>& verts, const Vector3& center)
        {
            for (uint32_t gidx : verts)
            {
                if (gidx >= m_skinned_vertices.size())
                {
                    continue;
                }
                RHI_Vertex_PosTexNorTan& v = m_skinned_vertices[gidx];
                const Vector3 rel = Vector3(v.pos[0], v.pos[1], v.pos[2]) - center;
                const Vector3 p_new = center + rotate_in_eye_basis(rel, cos_ep, sin_ep, cos_ey, sin_ey);
                v.pos[0] = p_new.x;
                v.pos[1] = p_new.y;
                v.pos[2] = p_new.z;
                v.set_normal(rotate_in_eye_basis(v.get_normal(), cos_ep, sin_ep, cos_ey, sin_ey).Normalized());
                v.set_tangent(rotate_in_eye_basis(v.get_tangent(), cos_ep, sin_ep, cos_ey, sin_ey).Normalized());
            }
        };
        apply_eyeball_rotation(m_eye_state.left_eye_vertices, center_l);
        apply_eyeball_rotation(m_eye_state.right_eye_vertices, center_r);

        // 5. Spherical eyelid blinking + subtle natural upper-eyelid gaze tracking (hugs eyeball curvature cleanly!)
        const float lid_gaze_follow = clamp(live_pitch * 0.45f, -0.06f, 0.09f);
        const float upper_angle = blink_amount * 0.78f + lid_gaze_follow * (1.0f - blink_amount);
        const float lower_angle = -blink_amount * 0.14f;
        const float corneal_clearance = sinf(blink_amount * 3.14159265f) * 0.00140f;

        if (fabsf(upper_angle) > 1e-4f || fabsf(lower_angle) > 1e-4f)
        {
            for (const EyelidVertex& ev : m_eye_state.eyelid_vertices)
            {
                if (ev.vertex_index >= m_skinned_vertices.size())
                {
                    continue;
                }
                const float angle = upper_angle * ev.upper_weight + lower_angle * ev.lower_weight;
                if (fabsf(angle) < 1e-5f)
                {
                    continue;
                }

                const Vector3& center = (ev.eye_side == 0) ? center_l : center_r;
                RHI_Vertex_PosTexNorTan& v = m_skinned_vertices[ev.vertex_index];
                const Vector3 rel = Vector3(v.pos[0], v.pos[1], v.pos[2]) - center;

                const float cp = cosf(angle);
                const float sp = sinf(angle);
                Vector3 rot_rel = rotate_in_eye_basis(rel, cp, sp, 1.0f, 0.0f);
                // Tiny outward spherical clearance over the convex cornea during mid-blink so lid skin never clips eyeball
                if (ev.upper_weight > 0.0f && corneal_clearance > 0.0f)
                {
                    rot_rel += e_fwd * (corneal_clearance * ev.upper_weight);
                }

                const Vector3 p_new = center + rot_rel;
                v.pos[0] = p_new.x;
                v.pos[1] = p_new.y;
                v.pos[2] = p_new.z;
                v.set_normal(rotate_in_eye_basis(v.get_normal(), cp, sp, 1.0f, 0.0f).Normalized());
            }
        }
    }

    void Animator::Tick()
    {
        SP_PROFILE_CPU();
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

        m_cloth_dt = static_cast<float>(Timer::GetDeltaTimeSec());

        if (m_external_pose_active)
        {
            if (!batch_skinning || IsSkinDue(mesh))
            {
                SkinFromLocalPose(mesh, *skeleton, m_external_local_matrices);
            }
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

        // Skip clip sampling, IK raycasts, and pose copies completely on frames when this
        // character is not due for skinning (unless an inertial transition is still decaying).
        const bool skin_due = !batch_skinning || IsSkinDue(mesh);
        if (!skin_due && !m_inertializer.IsActive())
        {
            return;
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

        if (!skin_due)
        {
            return;
        }

        if (!m_hands_attach_attempted)
        {
            AttachHandsToArms();
        }

        // rotate the two buffers instead of moving, a move empties the target and the next assign
        // has to allocate the pose again
        m_anim_pose.swap(m_anim_pose_previous);
        m_anim_pose = local_matrices;

        bool run_foot_ik = m_foot_ik_enabled;
        if (run_foot_ik)
        {
            if (Camera* camera = World::GetCamera())
            {
                run_foot_ik = Vector3::DistanceSquared(camera->GetEntity()->GetPosition(), GetEntity()->GetPosition()) <= 20.0f * 20.0f;
            }
        }
        if (run_foot_ik)
        {
            ApplyFootIk(*skeleton, local_matrices);
        }
        ApplyLimbIk(*skeleton, local_matrices);
        m_last_local_matrices = local_matrices;
        const bool unmodified_clip = m_loop && !run_foot_ik && !m_inertializer.IsActive() &&
            none_of(begin(m_limb_ik), end(m_limb_ik), [](const LimbIk& limb) { return limb.weight > 0.0f; });
        SkinFromLocalPose(mesh, *skeleton, local_matrices, unmodified_clip);
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
}
