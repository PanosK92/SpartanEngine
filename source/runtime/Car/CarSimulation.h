/*
Copyright(c) 2015-2026 Panos Karabelas
*/
#pragma once
#include "CarState.h"
#include "CarAssists.h"
#include "CarPacejka.h"
#include "CarAero.h"
#include "CarMultibody.h"
#include "CarSuspension.h"
#include "CarDrivetrain.h"
#include "CarTires.h"
#include "CarValidation.h"

namespace car
{

    struct setup_params
    {
        PxPhysics*              physics      = nullptr;
        PxScene*                scene        = nullptr;
        PxConvexMesh*           chassis_mesh = nullptr;  // convex hull for collision
        std::vector<PxVec3>     vertices;                // original mesh verts for aero calculation
        config                  car_config;
        bool                    multibody_enabled = true;
    };

    class Simulation
    {
        #include "CarSimulationCore.h"
    };
}
