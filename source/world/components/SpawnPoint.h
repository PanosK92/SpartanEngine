/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include "Component.h"

namespace spartan
{
    class SpawnPoint : public Component
    {
    public:
        SpawnPoint(Entity* entity);
        ~SpawnPoint() = default;

        void Place(Entity* entity) const;
    };
}
