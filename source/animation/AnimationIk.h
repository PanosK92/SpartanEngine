/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
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

        // give the foot back its clip rotation, which the knee solve changed by rotating the calf, then
        // tilt it by the ground slope only (model up onto the ground normal), so the heel and toe roll
        // authored in the clip survive. toe locals are left alone, the toes ride the foot as one sole
        bool PlantFoot(
            const Skeleton& skeleton,
            std::vector<math::Matrix>& local_matrices,
            uint32_t end_index,
            const math::Quaternion& clip_rotation_model,
            const math::Vector3& ground_normal_model,
            float tilt_weight
        );
    }
}
