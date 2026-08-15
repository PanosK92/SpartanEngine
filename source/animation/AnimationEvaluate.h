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

#pragma once

//= INCLUDES ==================
#include "../math/Matrix.h"
#include "../rhi/RHI_Vertex.h"
#include <vector>
//=============================

namespace spartan
{
    struct AnimationClip;
    struct Skeleton;
    struct SkeletalMeshBinding;

    namespace animation_evaluate
    {
        // build the clip's bone to channel lookup if it is missing. sampling calls this itself, but
        // the table is written into the clip, so call it on the main thread before sampling a clip
        // from worker threads
        void EnsureSampleIndex(const AnimationClip& clip);

        // sample clip into joint local matrices (bind for unanimated joints)
        // loop=false holds the last frame past the end instead of wrapping to the start, a one shot
        // clip that wraps mid crossfade blends out of its first frame and snaps
        bool SampleLocals(
            const AnimationClip& clip,
            const Skeleton& skeleton,
            float time_seconds,
            std::vector<math::Matrix>& out_local_matrices,
            bool loop = true
        );

        // sample clip into joint global matrices
        bool SampleGlobals(
            const AnimationClip& clip,
            const Skeleton& skeleton,
            float time_seconds,
            std::vector<math::Matrix>& out_global_matrices
        );

        // blend local poses, t=0 keeps a, t=1 keeps b
        bool BlendLocals(
            const std::vector<math::Matrix>& locals_a,
            const std::vector<math::Matrix>& locals_b,
            float t,
            std::vector<math::Matrix>& out_local_matrices
        );

        // inertialization
        //
        // a transition records the difference between the pose already on screen and where the new
        // clip starts, then decays that difference to zero on top of the live clip. the outgoing
        // clip is never sampled again, so nothing freezes, and because the difference is measured
        // against whatever was displayed, a transition taken mid transition is just another
        // difference, no special case. matches position and velocity, so joints do not kink
        class PoseInertializer
        {
        public:
            void Reset();
            bool IsActive() const { return m_active; }

            // src is the pose displayed last frame, src_previous the frame before it (may be empty
            // on a first transition, that only costs velocity matching). dst is the incoming clip at
            // its start, dst_next the same clip one displayed frame later
            void Transition(
                const std::vector<math::Matrix>& src,
                const std::vector<math::Matrix>& src_previous,
                const std::vector<math::Matrix>& dst,
                const std::vector<math::Matrix>& dst_next,
                float delta_time
            );

            // decay the stored difference and add it on top of pose, in place
            void Apply(std::vector<math::Matrix>& pose, float delta_time, float halflife);

        private:
            struct JointOffset
            {
                math::Vector3 position          = math::Vector3::Zero;
                math::Vector3 position_velocity = math::Vector3::Zero;
                // scaled angle axis, a rotation decays as a plain vector in this form
                math::Vector3 rotation          = math::Vector3::Zero;
                math::Vector3 rotation_velocity = math::Vector3::Zero;
            };

            std::vector<JointOffset> m_offsets;
            bool m_active = false;
        };

        bool HasCurve(const AnimationClip& clip, uint32_t bone_index);

        // skin with skin = global * inverse(bind_global), bind pose is identity
        bool SkinMesh(
            const SkeletalMeshBinding& binding,
            const std::vector<math::Matrix>& global_matrices,
            const std::vector<math::Matrix>& bind_inverse_global_matrices,
            const std::vector<RHI_Vertex_PosTexNorTan>& bind_vertices,
            std::vector<RHI_Vertex_PosTexNorTan>& out_vertices,
            std::vector<math::Matrix>& skin_matrices
        );
    }
}
