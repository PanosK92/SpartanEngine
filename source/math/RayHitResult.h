/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =======
#include <memory>
#include "Vector3.h"
//==================

namespace spartan
{
    class Entity;

    namespace math
    {
        class RayHitResult
        {
        public:
            RayHitResult(Entity* entity, const Vector3& position, float distance, bool is_inside)
            {
                m_entity   = entity;
                m_position = position;
                m_distance = distance;
                m_inside   = is_inside;
            };

            Entity* m_entity;
            Vector3 m_position;
            float m_distance;
            bool m_inside;
        };
    }
}
