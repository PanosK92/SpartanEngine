// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
// https://github.com/PanosK92/SpartanEngine/blob/master/license.md
// Commercial use requires written permission and negotiated payment terms.
#pragma once

#include "NavigationMesh.h"
#include "../math/Vector3.h"
#include <memory>

namespace spartan
{
    // Streams navigation independently of render/physics distance. Recreate after editing static
    // geometry (play/stop does this automatically). Workers only ever see copied triangle data.
    class NavigationWorld
    {
    public:
        NavigationWorld();
        ~NavigationWorld();
        void Tick(const math::Vector3& focus, float delta_time, bool debug_draw);
        bool Project(math::Vector3& position) const;
        navigation::NavigationMesh& GetMesh();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
