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
#include "../Math/Matrix.h"
#include "../RHI/RHI_Vertex.h"
#include <vector>
//=============================

namespace spartan
{
    struct AnimationClip;
    struct Skeleton;
    struct SkeletalMeshBinding;

    namespace animation_evaluate
    {
        // sample clip into joint local matrices (bind for unanimated joints)
        bool SampleLocals(
            const AnimationClip& clip,
            const Skeleton& skeleton,
            float time_seconds,
            std::vector<math::Matrix>& out_local_matrices
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

        bool HasCurve(const AnimationClip& clip, uint32_t bone_index);

        // skin with skin = global * inverse(bind_global), bind pose is identity
        bool SkinMesh(
            const SkeletalMeshBinding& binding,
            const std::vector<math::Matrix>& global_matrices,
            const std::vector<math::Matrix>& bind_global_matrices,
            const std::vector<RHI_Vertex_PosTexNorTan>& bind_vertices,
            std::vector<RHI_Vertex_PosTexNorTan>& out_vertices
        );
    }
}
