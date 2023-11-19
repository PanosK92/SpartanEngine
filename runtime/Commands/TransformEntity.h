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

//= INCLUDES =============================
#include "Definitions.h"
#include "../Commands/Command.h"
#include "../World/Components/Transform.h"
#include "../World/Entity.h"
//========================================

namespace Spartan
{
    /**
     * Baseclass for commands to be used in a CommandStack.
     */
    class SP_CLASS TransformEntity : public Spartan::Command
    {
    public:
        TransformEntity(Spartan::Entity* entity, Math::Vector3 old_position, Math::Quaternion old_rotation, Math::Vector3 old_scale);

        virtual void OnApply() override;
        virtual void OnRevert() override;

    protected:

        uint64_t m_entity_id{ UINT64_MAX };

        Math::Vector3 m_new_position;
        Math::Quaternion m_new_rotation;
        Math::Vector3 m_new_scale;

        Math::Vector3 m_old_position;
        Math::Quaternion m_old_rotation;
        Math::Vector3 m_old_scale;

    };
}
