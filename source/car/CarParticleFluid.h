// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.

#pragma once
#include "../car/Car.h"
#include "../car/CarPhysics.h"
#include "../car/CarSimulation.h"
#include "../physics/PhysicsWorld.h"
#include "../world/World.h"
#include "../world/Entity.h"
#include "../world/components/Camera.h"
#include "../world/components/Physics.h"
#include "../world/ParticleFluid.h"
#include "../world/VegetationInteraction.h"
#include <algorithm>
#include <vector>

namespace spartan::vehicle_particle_fluid
{
    using namespace math;

    // a car further than this from the focus is outside the box of air the renderer solves
    constexpr float reach        = 14.0f;
    // without a viewed car the box goes to the nearest one, if it is close enough to be worth it
    constexpr float camera_reach = 45.0f;

    // the last ground the focus stood on, held while it is airborne
    inline Vector4 ground_plane = Vector4(0.0f, 1.0f, 0.0f, 0.0f);
    inline bool ground_known    = false;

    inline void Clear()
    {
        particle_fluid::scene = {};
        ground_known          = false;
    }

    // the chassis as a box and the wheels as spinning cylinders
    inline bool add_car(Car* car, particle_fluid::Scene& scene)
    {
        if (scene.box_count + 1 > particle_fluid::box_capacity || scene.wheel_count + 4 > particle_fluid::wheel_capacity)
            return false;

        Entity* root     = car->GetRootEntity();
        Physics* physics = root->GetComponent<Physics>();
        if (!physics)
            return false;

        SurfaceInteractor shape;
        float mass = 0.0f;
        if (!vegetation_interaction::car_body(car, shape, mass))
            return false;

        CarPhysics& car_physics  = CarPhysics::Get(*physics);
        Vector3 angular_velocity = Vector3::Zero;
        Vector3 pivot            = shape.center;
        {
            std::lock_guard<std::recursive_mutex> physx_lock(PhysicsWorld::GetMutex());
            auto* simulation = car_physics.GetVehicleSimulation();
            physx::PxRigidDynamic* chassis = simulation ? simulation->get_body() : nullptr;
            if (chassis)
            {
                const physx::PxVec3 w = chassis->getAngularVelocity();
                const physx::PxVec3 c = chassis->getCMassLocalPose().p;
                angular_velocity = Vector3(w.x, w.y, w.z);
                pivot            = root->GetPosition() + root->GetRotation() * Vector3(c.x, c.y, c.z);
            }
        }
        if (!angular_velocity.IsFinite() || !pivot.IsFinite())
            return false;

        const float radius = car_physics.GetWheelRadius();
        for (int i = 0; i < static_cast<int>(WheelIndex::Count); ++i)
        {
            const WheelIndex index = static_cast<WheelIndex>(i);
            Entity* wheel_entity   = car_physics.GetWheelEntity(index);
            if (!wheel_entity || radius <= 0.0f)
                continue;

            particle_fluid::Wheel& wheel = scene.wheels[scene.wheel_count++];
            wheel.center     = wheel_entity->GetPosition();
            wheel.axis       = shape.right;
            wheel.radius     = radius;
            wheel.half_width = std::max(car_physics.GetWheelWidth(index) * 0.5f, 0.08f);
            wheel.spin       = car_physics.GetWheelAngularVelocity(index);
            wheel.velocity   = shape.velocity + Vector3::Cross(angular_velocity, wheel.center - pivot);
        }

        // the whole body as one box, the solver hollows the arches out of it around the wheels
        particle_fluid::Box& box = scene.boxes[scene.box_count++];
        box.center           = shape.center;
        box.right            = shape.right;
        box.up               = shape.up;
        box.forward          = shape.forward;
        box.extents          = Vector3(shape.extents.x + shape.radius, shape.extents.y, shape.extents.z + shape.radius);
        box.velocity         = shape.velocity;
        box.angular_velocity = angular_velocity;
        box.pivot            = pivot;
        return true;
    }

    // the plane the focus car stands on, from its tire contacts
    inline void update_ground(Car* car)
    {
        Entity* root     = car->GetRootEntity();
        Physics* physics = root->GetComponent<Physics>();
        if (!physics)
            return;

        CarPhysics& car_physics = CarPhysics::Get(*physics);
        Vector3 point  = Vector3::Zero;
        Vector3 normal = Vector3::Zero;
        float grounded = 0.0f;
        for (int i = 0; i < static_cast<int>(WheelIndex::Count); ++i)
        {
            const WheelIndex index = static_cast<WheelIndex>(i);
            if (!car_physics.IsWheelGrounded(index))
                continue;
            point    += car_physics.GetWheelContactPoint(index);
            normal   += car_physics.GetWheelContactNormal(index);
            grounded += 1.0f;
        }

        if (grounded > 0.0f && point.IsFinite() && normal.IsFinite() && normal.LengthSquared() > 0.01f)
        {
            point = point / grounded;
            normal.Normalize();
            // a wall the car leans on is not the ground the smoke lies on
            if (normal.y < 0.5f)
                normal = Vector3::Up;
            ground_plane = Vector4(normal.x, normal.y, normal.z, -Vector3::Dot(normal, point));
            ground_known = true;
        }
        else if (!ground_known)
        {
            // never seen it on the ground, the underside of the tires is the best guess there is
            const float bottom = root->GetPosition().y - car_physics.GetSuspensionHeight() - car_physics.GetWheelRadius();
            ground_plane = Vector4(0.0f, 1.0f, 0.0f, -bottom);
        }
    }

    inline void Tick()
    {
        particle_fluid::Scene& scene = particle_fluid::scene;
        scene = {};

        Camera* camera = World::GetCamera();
        if (!camera)
        {
            ground_known = false;
            return;
        }

        // the air belongs to the car being watched, driven or spectated, otherwise to the nearest one
        auto usable = [](Car* car)
        {
            Entity* root = car ? car->GetRootEntity() : nullptr;
            return root && root->GetActive();
        };
        const auto all = Car::GetAll();
        Car* focus     = Car::GetViewed();
        if (!usable(focus))
        {
            focus = nullptr;
            const Vector3 viewer = camera->GetEntity()->GetPosition();
            float nearest = camera_reach * camera_reach;
            for (Car* car : all)
            {
                if (!usable(car))
                    continue;
                const float distance = Vector3::DistanceSquared(car->GetRootEntity()->GetPosition(), viewer);
                if (distance < nearest)
                {
                    nearest = distance;
                    focus   = car;
                }
            }
        }
        if (!focus)
        {
            ground_known = false;
            return;
        }

        // the focus first, then whoever else is close enough to stir the same air
        const Vector3 center = focus->GetRootEntity()->GetPosition();
        std::vector<std::pair<float, Car*>> cars;
        for (Car* car : all)
        {
            if (!usable(car))
                continue;
            const float distance = car == focus ? -1.0f : Vector3::DistanceSquared(car->GetRootEntity()->GetPosition(), center);
            if (distance <= reach * reach)
                cars.emplace_back(distance, car);
        }
        std::sort(cars.begin(), cars.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        bool focus_added = false;
        for (const auto& [distance, car] : cars)
        {
            const bool added = add_car(car, scene);
            if (car == focus)
                focus_added = added;
        }

        update_ground(focus);
        const Vector3 normal(ground_plane.x, ground_plane.y, ground_plane.z);
        scene.ground_plane   = ground_plane;
        scene.focus          = center - normal * (Vector3::Dot(normal, center) + ground_plane.w);
        scene.focus_velocity = focus_added ? scene.boxes[0].velocity : Vector3::Zero;
        scene.valid          = scene.focus.IsFinite();
    }
}
