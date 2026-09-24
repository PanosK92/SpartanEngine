/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
#include <physx/PxSceneDesc.h>

namespace spartan
{
    // Keep the engine and headless vehicle fixtures on the same solver. PGS
    // leaves visible joint separation in the stiff, closed suspension loops.
    inline void ConfigurePhysicsScene(physx::PxSceneDesc& desc)
    {
        desc.solverType = physx::PxSolverType::eTGS;
        desc.flags |= physx::PxSceneFlag::eENABLE_CCD;
        desc.ccdMaxPasses = 4;
    }
}
