/*
Copyright(c) 2015-2026 Panos Karabelas
*/
#pragma once
#include "CarState.h"
#include "CarPacejka.h"
#include "CarMultibody.h"

namespace car
{

    struct setup_params
    {
        PxPhysics*              physics      = nullptr;
        PxScene*                scene        = nullptr;
        PxConvexMesh*           chassis_mesh = nullptr;  // convex hull for collision
        std::vector<PxVec3>     vertices;                // original mesh verts for aero calculation
        config                  car_config;
    };

    class Simulation
    {
        #include "CarSimulationCore.h"
    };
}
