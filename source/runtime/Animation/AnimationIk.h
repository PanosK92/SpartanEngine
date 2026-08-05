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
#include "../Math/Quaternion.h"
#include "../Math/Vector3.h"
#include <vector>
//=============================

namespace spartan
{
    struct Skeleton;

    namespace animation_ik
    {
        bool SolveTwoBone(
            const Skeleton& skeleton,
            std::vector<math::Matrix>& local_matrices,
            uint32_t root_index,
            uint32_t mid_index,
            uint32_t end_index,
            const math::Vector3& target_model,
            const math::Vector3& pole_model,
            float weight
        );

        // pitch/roll sole_up_local onto ground normal, then yaw toe_fwd_local onto toe forward
        // local vectors come from bind pose (filled ibm or skeleton bind globals)
        // ball_index: child toe joint, -1 to skip. plant changes foot world rot, so ball local
        // is compensated to keep the clip toe world rot (no bind lock, no l/r freeze)
        bool PlantFoot(
            const Skeleton& skeleton,
            std::vector<math::Matrix>& local_matrices,
            uint32_t end_index,
            const math::Vector3& ground_normal_model,
            const math::Vector3& toe_forward_model,
            const math::Vector3& sole_up_local,
            const math::Vector3& toe_fwd_local,
            float weight,
            int32_t ball_index = -1
        );
    }
}
