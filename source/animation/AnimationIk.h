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
#include "../math/Quaternion.h"
#include "../math/Vector3.h"
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

        // tilt the foot by the ground slope only, model up onto the ground normal, so the heel and
        // toe roll authored in the clip survive. flat ground is a no op
        // toe locals are left alone, the toes ride the tilt as part of the same sole
        bool PlantFoot(
            const Skeleton& skeleton,
            std::vector<math::Matrix>& local_matrices,
            uint32_t end_index,
            const math::Vector3& ground_normal_model,
            float weight
        );
    }
}
