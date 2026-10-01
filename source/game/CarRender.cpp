/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#ifdef SP_GAME
#include "../car/CarPhysics.h"
#endif
#include "CarRender.h"
#include "CarWeather.h"
#include "../car/CarRain.h"
#include "../world/Weather.h"
#include "../car/Car.h"
#include "../car/CarSimulation.h"
#include "../physics/PhysicsWorld.h"
#include "../world/World.h"
#include "../world/Entity.h"
#include "../world/components/Physics.h"
#include "../world/components/Camera.h"
#include "../rendering/Renderer.h"
#include "../rendering/SurfaceInteraction.h"
using namespace std;
using namespace spartan::math;
namespace spartan::game
{
    void SubmitCarRendering()
    {
        // Match the renderer's world gate: loader workers may still be assembling cars.
        if (World::IsLoadingFromFile())
        {
            Renderer::SetSurfaceInteraction({});
            Renderer::SetSurfaceWater({});
            return;
        }
        SurfaceWater water;
        water.active = CarRain::IsActive();
        FillCarWeather(water);
        water.version = CarRain::GetVersion();
        water.width = CarRain::GetAtlasWidth();
        water.height = CarRain::GetAtlasHeight();
        water.texel_size = CarRain::GetTexelSize();
        water.box_min = CarRain::GetBoxMin();
        water.clock = CarRain::GetClock();
        water.micro_life = CarRain::GetMicroLife();
        water.residue_mass = CarRain::GetResidueMass();
        water.surface = CarRain::GetSurface();
        water.micro = CarRain::GetMicro();
        water.drops = CarRain::GetDrops();
        water.texels = CarRain::GetTexels();
        for (uint32_t face = 0; face < 6; ++face) water.faces[face] = CarRain::GetFaceRect(face);
        Renderer::SetSurfaceWater(water);
        SurfaceInteraction result;
        Camera* camera = World::GetCamera();
        if (!camera) { Renderer::SetSurfaceInteraction(result); return; }
        const Vector3 camera_position = camera->GetEntity()->GetPosition();
        const bool paused = Engine::IsFlagSet(EngineMode::Paused);
        // The occupied car owns the local field. On foot, use the nearest drivable car.
        Car* vehicle = nullptr;
        float nearest = numeric_limits<float>::max();
        for (Car* candidate : Car::GetAll())
        {
            Entity* root = candidate->GetRootEntity();
            if (!candidate->IsDrivable() || !root || !root->IsActive())
                continue;
            const float distance = Vector3::DistanceSquared(root->GetPosition(), camera_position);
            if (candidate->IsViewed() || distance < nearest)
            {
                vehicle = candidate;
                nearest = distance;
                if (candidate->IsViewed())
                    break;
            }
        }
        Entity* root = vehicle ? vehicle->GetRootEntity() : nullptr;
        Physics* physics = root ? root->GetComponent<Physics>() : nullptr;
        auto* simulation = physics ? CarPhysics::Get(*physics).GetVehicleSimulation() : nullptr;
        if (!simulation) { Renderer::SetSurfaceInteraction(result); return; }
        result.id = root->GetObjectId();
        const Vector3 center = root->GetPosition();
        result.center = center;
            // Keep the fitted hulls separate: their combined box includes empty
            // space around bumpers, sills and mirrors and would clear a rectangle.
            lock_guard<recursive_mutex> physx_lock(PhysicsWorld::GetMutex());
            if (physx::PxRigidDynamic* chassis = simulation->get_body())
            {
                physx::PxBounds3 bounds = physx::PxBounds3::empty();
                auto& data = result.body;
                uint32_t hull_count = 0;
                for (uint32_t i = 0; i < chassis->getNbShapes(); ++i)
                {
                    physx::PxShape* shape = nullptr;
                    chassis->getShapes(&shape, 1, i);
                    if (!shape || !(shape->getFlags() & physx::PxShapeFlag::eSIMULATION_SHAPE))
                        continue;
                    if (hull_count >= data.hulls.size())
                        break;
                    const auto& geometry = shape->getGeometry();
                    const auto pose = shape->getLocalPose();
                    physx::PxBounds3 shape_bounds;
                    if (!physx::PxGeometryQuery::computeGeomBounds(shape_bounds, geometry, pose))
                        continue;
                    bounds.include(shape_bounds);
                    const uint32_t first = hull_count * 48;
                    uint32_t count = 0;
                    if (geometry.getType() == physx::PxGeometryType::eCONVEXMESH)
                    {
                        const auto& convex = static_cast<const physx::PxConvexMeshGeometry&>(geometry);
                        const auto* mesh = convex.convexMesh;
                        if (mesh && mesh->getNbPolygons() <= 48)
                        {
                            const auto normal_matrix = convex.scale.toMat33().getInverse().getTranspose();
                            for (uint32_t face = 0; face < mesh->getNbPolygons(); ++face)
                            {
                                physx::PxHullPolygon polygon;
                                if (!mesh->getPolygonData(face, polygon))
                                    continue;
                                const auto n = pose.q.rotate(normal_matrix * physx::PxVec3(polygon.mPlane[0], polygon.mPlane[1], polygon.mPlane[2])).getNormalized();
                                const auto vertex = mesh->getVertices()[mesh->getIndexBuffer()[polygon.mIndexBase]];
                                const auto point = pose.transform(convex.scale.transform(vertex));
                                data.planes[first + count++] = Vector4(n.x, n.y, n.z, -n.dot(point));
                            }
                        }
                    }
                    if (count == 0)
                    {
                        // Box primitives and unusually complex imported hulls.
                        for (uint32_t axis = 0; axis < 3; ++axis)
                        {
                            Vector4 positive(axis == 0 ? 1.0f : 0.0f, axis == 1 ? 1.0f : 0.0f, axis == 2 ? 1.0f : 0.0f, -shape_bounds.maximum[axis]);
                            Vector4 negative(-positive.x, -positive.y, -positive.z, shape_bounds.minimum[axis]);
                            data.planes[first + count++] = positive;
                            data.planes[first + count++] = negative;
                        }
                    }
                    data.hulls[hull_count++] = Vector4(static_cast<float>(first), static_cast<float>(count), 0, 0);
                }
                if (!bounds.isEmpty() && bounds.isFinite())
                {
                    // TickVehicle extrapolates the visual chassis between fixed
                    // steps. Follow that rendered pose so contact cannot lag the
                    // visible panels at speed. Entity positions are already world-space.
                    const Quaternion rotation = root->GetRotation();
                    const physx::PxVec3 e = bounds.minimum.abs().maximum(bounds.maximum.abs());
                    const auto axis = [&](const physx::PxVec3& v, float extent)
                    {
                        const Vector3 a = rotation * Vector3(v.x, v.y, v.z);
                        return Vector4(a.x, a.y, a.z, extent);
                    };
                    data.center = Vector4(center, static_cast<float>(hull_count));
                    data.right = axis(physx::PxVec3(1, 0, 0), e.x);
                    data.up = axis(physx::PxVec3(0, 1, 0), e.y);
                    data.forward = axis(physx::PxVec3(0, 0, 1), e.z);
                }
            }

        for (uint32_t i = 0; i < result.contacts.size(); ++i)
        {
            const auto& wheel = simulation->get_wheel_state(i);
            auto* body = simulation->get_multibody_state().corners[i].wheel_body;
            if (paused || !wheel.grounded || !body || !isfinite(wheel.tire_load) || wheel.tire_load <= 80.0f ||
                !wheel.contact_point.isFinite() || !wheel.contact_normal.isFinite())
            {
                continue;
            }
            // Moving receivers are not part of the world-space terrain field.
            if (const auto* ground = wheel.contact_actor ? wheel.contact_actor->is<physx::PxRigidDynamic>() : nullptr)
            {
                if (ground->getLinearVelocity().magnitudeSquared() > 0.01f || ground->getAngularVelocity().magnitudeSquared() > 0.01f)
                {
                        continue;
                }
            }
            const auto pose = body->getGlobalPose();
            const auto normal_px = wheel.contact_normal.getNormalized();
            const auto hub = pose.p - normal_px * (pose.p - wheel.contact_point).dot(normal_px);
            const Vector3 position = PhysicsWorld::ToWorldPosition(Vector3(hub.x, hub.y, hub.z));
            const Vector3 normal(normal_px.x, normal_px.y, normal_px.z);
            const auto axle = pose.q.rotate(physx::PxVec3(1, 0, 0));
            Vector3 forward = Vector3::Cross(Vector3(axle.x, axle.y, axle.z), normal);
            if (!position.IsFinite() || forward.LengthSquared() < 0.001f || normal.y < 0.2f)
            {
                continue;
            }
            forward.Normalize();
            if (Vector3::Dot(forward, root->GetForward()) < 0.0f)
                forward = -forward;
            const auto velocity_px = body->getLinearVelocity();
            Vector3 velocity(velocity_px.x, velocity_px.y, velocity_px.z);
            velocity -= normal * Vector3::Dot(velocity, normal);
            // Follow travel during a slide and reverse; a resting wheel uses its steered heading.
            Vector3 direction = velocity.LengthSquared() > 0.04f ? velocity.Normalized() : forward;
            const float width = CarPhysics::Get(*physics).GetWheelWidth(static_cast<WheelIndex>(i));
            if (!isfinite(width) || width <= 0.0f) continue;
            result.contacts[i] = {position, normal, velocity, direction, width, clamp(wheel.tire_load / 1500.0f, 0.0f, 1.0f), true};
        }
        Renderer::SetSurfaceInteraction(result);
    }
}
