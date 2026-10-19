// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
// Everything that pushes vegetation, gathered once per frame. Grass, shrubs, branches and the
// breakable trees all read the same list, so a new kind of body only has to be added here.
#pragma once
#include "../car/Car.h"
#include "../car/CarPhysics.h"
#include "../car/CarSimulation.h"
#include "../core/Engine.h"
#include "../core/Timer.h"
#include "../physics/PhysicsWorld.h"
#include "../world/World.h"
#include "../world/Entity.h"
#include "../world/components/Camera.h"
#include "../world/components/Physics.h"
#include "../rendering/SurfaceInteraction.h"
#include <algorithm>
#include <unordered_map>
#include <vector>

namespace spartan::vegetation_interaction
{
    using namespace math;

    struct Body
    {
        SurfaceInteractor shape;
        Car* car        = nullptr; // set for car bodies, lets a hit slow the car down
        bool is_player  = false;
        float stiffness      = 0.0f; // N/m, how hard the body is where it meets a trunk
        float force_max      = 0.0f; // N, the most a body can push before it gives way itself
        float crush_depth    = 0.0f; // m, how far it gives way at force_max before something hard is behind it
        float core_stiffness = 0.0f; // N/m, that hard part, zero means the body just gives way
    };

    // a car front against a pole loads at roughly 0.6 MN/m, then folds at about 150 kN over the crumple
    // zone, once that is used up the engine and firewall arrive and push an order of magnitude harder
    constexpr float car_stiffness      = 6.0e5f;
    constexpr float car_force_max      = 1.5e5f;
    constexpr float car_crush_depth    = 0.6f;
    constexpr float car_core_stiffness = 5.0e6f;
    // a walking person bumping into something yields and steps around it well under a kilonewton
    constexpr float player_stiffness = 1.5e4f;
    constexpr float player_force_max = 800.0f;

    // the bodies of this frame, field owner first
    inline std::vector<Body> bodies;
    inline std::unordered_map<uint64_t, Vector3> previous_positions;

    // the field follows whoever the player is: the driven car, else the character on foot
    constexpr float field_reach = 40.0f;
    // a character controller is a 1.3 m cylinder on 0.25 m caps, its entity sits at the capsule centre
    constexpr float player_half_height = 0.9f;
    constexpr float player_radius      = 0.3f;
    constexpr float player_mass        = 80.0f;

    inline void Clear()
    {
        bodies.clear();
        previous_positions.clear();
    }

    // the character the camera is mounted on, null while driving or in the editor camera
    // the chase camera stays parented to the character, which waits where the player got in
    inline Entity* GetPlayer()
    {
        for (Car* car : Car::GetAll())
        {
            if (car && car->IsOccupied())
                return nullptr;
        }
        Camera* camera = World::GetCamera();
        Entity* parent = camera ? camera->GetEntity()->GetParent() : nullptr;
        Physics* physics = parent ? parent->GetComponent<Physics>() : nullptr;
        if (!physics || physics->GetBodyType() != BodyType::Controller || !parent->GetActive())
            return nullptr;
        return parent;
    }

    // world velocity from the displacement actually seen, physics lod and teleports included
    inline Vector3 observe_velocity(uint64_t id, const Vector3& position, float dt)
    {
        auto [entry, inserted] = previous_positions.try_emplace(id, position);
        const Vector3 displacement = position - entry->second;
        entry->second = position;
        if (inserted || dt <= 0.0f || displacement.Length() > std::max(10.0f, 100.0f * dt))
            return Vector3::Zero;
        return displacement / dt;
    }

    // the chassis as a box in its own frame, from the simulation shapes, wheels excluded
    inline bool car_body(Car* car, SurfaceInteractor& shape, float& mass)
    {
        Entity* root = car ? car->GetRootEntity() : nullptr;
        Physics* physics = root ? root->GetComponent<Physics>() : nullptr;
        auto* simulation = physics ? CarPhysics::Get(*physics).GetVehicleSimulation() : nullptr;
        if (!simulation)
            return false;

        std::lock_guard<std::recursive_mutex> physx_lock(PhysicsWorld::GetMutex());
        physx::PxRigidDynamic* chassis = simulation->get_body();
        if (!chassis)
            return false;

        physx::PxBounds3 bounds = physx::PxBounds3::empty();
        for (uint32_t i = 0; i < chassis->getNbShapes(); ++i)
        {
            physx::PxShape* px_shape = nullptr;
            chassis->getShapes(&px_shape, 1, i);
            if (!px_shape || !(px_shape->getFlags() & physx::PxShapeFlag::eSIMULATION_SHAPE))
                continue;
            physx::PxBounds3 shape_bounds;
            if (physx::PxGeometryQuery::computeGeomBounds(shape_bounds, px_shape->getGeometry(), px_shape->getLocalPose()))
                bounds.include(shape_bounds);
        }
        if (bounds.isEmpty() || !bounds.isFinite())
            return false;

        const Quaternion rotation = root->GetRotation();
        const physx::PxVec3 c = bounds.getCenter();
        const physx::PxVec3 e = bounds.getExtents();
        shape.center  = root->GetPosition() + rotation * Vector3(c.x, c.y, c.z);
        shape.right   = rotation * Vector3::Right;
        shape.up      = rotation * Vector3::Up;
        shape.forward = rotation * Vector3::Forward;
        // the body tapers to the bumpers, a small rounding reads better than sharp corners
        const float rounding = std::min(0.25f, std::min(e.x, e.z) * 0.5f);
        shape.extents = Vector3(e.x - rounding, e.y, e.z - rounding);
        shape.radius  = rounding;
        const physx::PxVec3 v = chassis->getLinearVelocity();
        shape.velocity = Vector3(v.x, v.y, v.z);
        mass = chassis->getMass();
        return shape.velocity.IsFinite() && shape.center.IsFinite();
    }

    // fills the interactor list, the field centre and the footstep contacts of the frame
    inline void Gather(SurfaceInteraction& result, Car* nearest_car)
    {
        bodies.clear();
        result.interactor_count = 0;
        Camera* camera = World::GetCamera();
        if (!camera)
            return;

        const bool paused = Engine::IsFlagSet(EngineMode::Paused);
        const float dt    = paused ? 0.0f : static_cast<float>(Timer::GetDeltaTimeSec());

        // the field owner
        Entity* player = GetPlayer();
        Car* viewed    = Car::GetViewed();
        Vector3 center;
        bool has_center = false;
        if (viewed && viewed->GetRootEntity() && viewed->GetRootEntity()->GetActive())
        {
            center     = viewed->GetRootEntity()->GetPosition();
            has_center = true;
        }
        else if (player)
        {
            center     = player->GetPosition();
            has_center = true;
        }
        else if (nearest_car && nearest_car->GetRootEntity())
        {
            center     = nearest_car->GetRootEntity()->GetPosition();
            has_center = true;
        }
        if (!has_center)
            return;
        result.id     = 1;
        result.center = center;

        // the character on foot
        if (player)
        {
            Body body;
            body.is_player      = true;
            body.stiffness      = player_stiffness;
            body.force_max      = player_force_max;
            body.shape.id       = player->GetObjectId();
            body.shape.center   = player->GetPosition();
            body.shape.extents  = Vector3(0.0f, player_half_height, 0.0f);
            body.shape.radius   = player_radius;
            body.shape.mass     = player_mass;
            body.shape.velocity = paused ? Vector3::Zero : observe_velocity(body.shape.id, body.shape.center, dt);
            bodies.push_back(body);

            // footsteps tread the grass down where the character walks
            Vector3 horizontal(body.shape.velocity.x, 0.0f, body.shape.velocity.z);
            Physics* physics = player->GetComponent<Physics>();
            if (!paused && physics && physics->IsGrounded() && horizontal.LengthSquared() > 0.09f)
            {
                SurfaceContact& foot = result.contacts[4];
                foot.position  = body.shape.center - Vector3(0.0f, player_half_height, 0.0f);
                foot.normal    = Vector3::Up;
                foot.velocity  = horizontal;
                foot.direction = horizontal.Normalized();
                foot.width     = 0.32f;
                foot.pressure  = 0.45f;
                foot.valid     = true;
            }
        }

        // every car body within reach of the field, driven or parked, the nearest first
        std::vector<std::pair<float, Car*>> cars;
        for (Car* car : Car::GetAll())
        {
            Entity* root = car ? car->GetRootEntity() : nullptr;
            if (!root || !root->GetActive())
                continue;
            const float distance = Vector3::DistanceSquared(root->GetPosition(), center);
            if (distance <= field_reach * field_reach)
                cars.emplace_back(distance, car);
        }
        std::sort(cars.begin(), cars.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [distance, car] : cars)
        {
            if (bodies.size() >= surface_interactor_max)
                break;
            Body body;
            body.car       = car;
            body.stiffness = car_stiffness;
            body.force_max      = car_force_max;
            body.crush_depth    = car_crush_depth;
            body.core_stiffness = car_core_stiffness;
            body.shape.id  = car->GetRootEntity()->GetObjectId();
            if (!car_body(car, body.shape, body.shape.mass))
                continue;
            if (paused)
                body.shape.velocity = Vector3::Zero;
            bodies.push_back(body);
        }

        for (const Body& body : bodies)
            result.interactors[result.interactor_count++] = body.shape;

        // forget bodies that are gone
        for (auto it = previous_positions.begin(); it != previous_positions.end();)
        {
            const bool alive = std::any_of(bodies.begin(), bodies.end(), [&](const Body& body) { return body.shape.id == it->first; });
            it = alive ? std::next(it) : previous_positions.erase(it);
        }
    }
}
