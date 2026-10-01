/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

namespace spartan
{
    // wheel indices for vehicles
    enum class WheelIndex
    {
        FrontLeft  = 0,
        FrontRight = 1,
        RearLeft   = 2,
        RearRight  = 3,
        Count      = 4
    };

    // full = multibody tires, cheap = arcade chassis plus visual wheels
    enum class VehicleSimMode
    {
        Full,
        Cheap
    };

    struct VehiclePhysicsState;
    struct TireDeformationBatch;
    struct TireVisualState;
    struct VehiclePhysicsDeleter
    {
        void operator()(VehiclePhysicsState* state) const;
    };

    // Install the game-owned contact observer before simulation begins.
    void RegisterVehicleContacts();
}
