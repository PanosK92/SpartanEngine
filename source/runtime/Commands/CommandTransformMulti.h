/*
Copyright(c) 2023 Fredrik Svantesson

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

//= INCLUDES ===================
#include "Definitions.h"
#include "../Commands/Command.h"
#include "../World/Entity.h"
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

