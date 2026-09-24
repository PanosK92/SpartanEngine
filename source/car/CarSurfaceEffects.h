/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
#include <memory>
namespace spartan
{
    class Entity;
    // GPU spray and a bounded ballistic deposition simulation share the wheel's launch conditions.
    class CarSurfaceEffects
    {
    public:
        CarSurfaceEffects();
        ~CarSurfaceEffects();
        void Tick(Entity* vehicle, float delta_time, bool playing);
    private:
        struct State;
        std::unique_ptr<State> m_state;
    };
}
