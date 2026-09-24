/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ===================
#include "Definitions.h"
#include "../commands/Command.h"
#include "../world/Entity.h"
//==============================

namespace spartan
{
    class CommandTransform : public spartan::Command
    {
    public:
        CommandTransform(spartan::Entity* entity, math::Vector3 old_position, math::Quaternion old_rotation, math::Vector3 old_scale);

        virtual void OnApply() override;
        virtual void OnRevert() override;

    protected:

        uint64_t m_entity_id{ UINT64_MAX };

        math::Vector3 m_new_position;
        math::Quaternion m_new_rotation;
        math::Vector3 m_new_scale;

        math::Vector3 m_old_position;
        math::Quaternion m_old_rotation;
        math::Vector3 m_old_scale;
    };
}
