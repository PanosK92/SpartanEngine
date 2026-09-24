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
#include <vector>
//==============================

namespace spartan
{
    // Stores transform data for a single entity
    struct EntityTransformData
    {
        uint64_t entity_id = UINT64_MAX;
        math::Vector3 old_position;
        math::Quaternion old_rotation;
        math::Vector3 old_scale;
        math::Vector3 new_position;
        math::Quaternion new_rotation;
        math::Vector3 new_scale;
    };

    // Command for transforming multiple entities at once (single undo/redo operation)
    class CommandTransformMulti : public spartan::Command
    {
    public:
        CommandTransformMulti(
            const std::vector<Entity*>& entities,
            const std::vector<math::Vector3>& old_positions,
            const std::vector<math::Quaternion>& old_rotations,
            const std::vector<math::Vector3>& old_scales
        );

        virtual void OnApply() override;
        virtual void OnRevert() override;

    protected:
        std::vector<EntityTransformData> m_transforms;
    };
}

