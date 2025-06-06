/*
Copyright(c) 2015-2025 Panos Karabelas

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

//= INCLUDES ==========================
#include "pch.h"
#include "Physics.h"
#include "ProgressTracker.h"
#include "../Profiling/Profiler.h"
#include "../Rendering/Renderer.h"
#include "../Input/Input.h"
#include "../World/Components/Camera.h"
#include "../World/World.h"
SP_WARNINGS_OFF
#ifdef DEBUG
    #define _DEBUG 1
    #undef NDEBUG
#else
    #define NDEBUG 1
    #undef _DEBUG
#endif
#define PX_PHYSX_STATIC_LIB
#include <physx/PxPhysicsAPI.h>
SP_WARNINGS_ON
//=====================================

//= NAMESPACES ================
using namespace std;
using namespace spartan::math;
using namespace physx;
//=============================

namespace spartan
{
    namespace
    {
        float gravity = -9.81f;
        float hz      = 60.0f;

        static PxDefaultAllocator allocator;
        static PxDefaultErrorCallback error_callback;
        static PxFoundation* foundation           = nullptr;
        static PxPhysics* physics                 = nullptr;
        static PxScene* scene                     = nullptr;
        static PxDefaultCpuDispatcher* dispatcher = nullptr;
        static PxRigidDynamic* picked_body        = nullptr;
        static PxReal pick_distance               = 0.0f;
        static PxVec3 pick_direction;
    }

    void Physics::Initialize()
    {
        // foundation
        foundation = PxCreateFoundation(PX_PHYSICS_VERSION, allocator, error_callback);
        SP_ASSERT(foundation);

        // physics
        physics = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, PxTolerancesScale(), true, nullptr);
        SP_ASSERT(physics);

        // scene
        PxSceneDesc scene_desc(physics->getTolerancesScale());
        scene_desc.gravity = PxVec3(0.0f, gravity, 0.0f);
        scene_desc.cpuDispatcher = PxDefaultCpuDispatcherCreate(2);
        scene_desc.filterShader = PxDefaultSimulationFilterShader;
        scene = physics->createScene(scene_desc);
        SP_ASSERT(scene);

        // store dispatcher
        dispatcher = static_cast<PxDefaultCpuDispatcher*>(scene_desc.cpuDispatcher);
    }

    void Physics::Shutdown()
    {
        PX_RELEASE(scene);
        PX_RELEASE(dispatcher);
        PX_RELEASE(physics);
        PX_RELEASE(foundation);
    }

    void Physics::Tick()
    {
        SP_PROFILE_CPU();

        if (ProgressTracker::IsLoading())
            return;

        if (Engine::IsFlagSet(EngineMode::Playing))
        {
            // simulate
            scene->simulate(1.0f / hz);
            scene->fetchResults(true);

            // pick
            {
                if (Input::GetKeyDown(KeyCode::Click_Left) && Input::GetMouseIsInViewport())
                {
                    PickBody();
                }
                else if (Input::GetKeyUp(KeyCode::Click_Left))
                {
                    UnpickBody();
                }

                MovePickedBody();
            }

            // debug draw
            if (Renderer::GetOption<bool>(Renderer_Option::Physics))
            {
                const PxRenderBuffer& rb = scene->getRenderBuffer();
                for (PxU32 i = 0; i < rb.getNbLines(); i++)
                {
                    const PxDebugLine& line = rb.getLines()[i];
                    Vector3 start(line.pos0.x, line.pos0.y, line.pos0.z);
                    Vector3 end(line.pos1.x, line.pos1.y, line.pos1.z);
                    Color color(
                        ((line.color0 >> 16) & 0xFF) / 255.0f,
                        ((line.color0 >> 8) & 0xFF) / 255.0f,
                        (line.color0 & 0xFF) / 255.0f
                    );
                    Renderer::DrawLine(start, end, color, color);
                }
            }
        }
    }

    vector<void*> Physics::RayCast(const Vector3& start, const Vector3& end)
    {
        vector<void*> hits;
        PxRaycastBuffer hit;
        PxVec3 origin(start.x, start.y, start.z);
        PxVec3 dir(end.x - start.x, end.y - start.y, end.z - start.z);
        PxReal max_distance = dir.magnitude();
        dir.normalize();

        if (scene->raycast(origin, dir, max_distance, hit))
        {
            for (PxU32 i = 0; i < hit.nbTouches; i++)
            {
                hits.push_back(hit.touches[i].actor);
            }
        }

        return hits;
    }

    math::Vector3 Physics::RayCastFirstHitPosition(const math::Vector3& start, const math::Vector3& end)
    {
        PxRaycastBuffer hit;
        PxVec3 origin(start.x, start.y, start.z);
        PxVec3 dir(end.x - start.x, end.y - start.y, end.z - start.z);
        PxReal max_distance = dir.magnitude();
        dir.normalize();

        if (scene->raycast(origin, dir, max_distance, hit) && hit.hasBlock)
        {
            PxVec3 pos = hit.block.position;
            return Vector3(pos.x, pos.y, pos.z);
        }

        return Vector3::Zero;
    }

    Vector3 Physics::GetGravity()
    {
        PxVec3 g = scene->getGravity();
        return Vector3(g.x, g.y, g.z);
    }

    void Physics::PickBody()
    {
        // get camera
        Camera* camera = World::GetCamera();
        if (!camera)
            return;

        // get picking ray
        Ray picking_ray = camera->ComputePickingRay();
        PxVec3 origin(picking_ray.GetStart().x, picking_ray.GetStart().y, picking_ray.GetStart().z);
        PxVec3 direction(picking_ray.GetDirection().x, picking_ray.GetDirection().y, picking_ray.GetDirection().z);

        // raycast
        PxRaycastBuffer hit;
        if (scene->raycast(origin, direction, 1000.0f, hit) && hit.hasBlock)
        {
            PxRigidActor* actor = hit.block.actor;
            if (actor->is<PxRigidDynamic>())
            {
                picked_body    = actor->is<PxRigidDynamic>();
                pick_distance  = hit.block.distance;
                pick_direction = direction;
                picked_body->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
            }
        }
    }

    void Physics::UnpickBody()
    {
        if (picked_body)
        {
            picked_body->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, false);
            picked_body = nullptr;
        }
    }

    void Physics::MovePickedBody()
    {
        if (!picked_body)
            return;
    
        Camera* camera = World::GetCamera();
        if (!camera)
            return;
    
        Ray picking_ray = camera->ComputePickingRay();
        PxVec3 origin(picking_ray.GetStart().x, picking_ray.GetStart().y, picking_ray.GetStart().z);
        PxVec3 direction(picking_ray.GetDirection().x, picking_ray.GetDirection().y, picking_ray.GetDirection().z);
        PxVec3 target = origin + direction * pick_distance;
        picked_body->setGlobalPose(PxTransform(target));
    }
}
