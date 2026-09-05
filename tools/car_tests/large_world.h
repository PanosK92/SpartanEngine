#pragma once
#include "physics/PhysicsSceneOrigin.h"
#include <physx/characterkinematic/PxControllerManager.h>
#include <physx/characterkinematic/PxCapsuleController.h>

void origin_scene_checks(PxPhysics* physics, PxScene* scene)
{
    // Finite geometry (the car fixture uses an infinite plane), a kinematic
    // target, a world-anchored joint, and CCT query caches must move together.
    PxMaterial* material = physics->createMaterial(0.8f, 0.7f, 0);
    const PxVec3 location(6276, 5, -2823);
    auto* road = PxCreateStatic(*physics, PxTransform(location), PxBoxGeometry(4, 0.5f, 4), *material);
    auto* platform = PxCreateDynamic(*physics, PxTransform(PxIdentity), PxBoxGeometry(1, 1, 1), *material, 100);
    platform->setGlobalPose(PxTransform(location + PxVec3(10, 0, 0)));
    platform->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
    auto* tethered = PxCreateDynamic(*physics, PxTransform(PxIdentity), PxSphereGeometry(0.2f), *material, 100);
    tethered->setGlobalPose(PxTransform(location + PxVec3(15, 2, 0)));
    scene->addActor(*road); scene->addActor(*platform); scene->addActor(*tethered);
    auto* anchor = PxFixedJointCreate(*physics, nullptr, tethered->getGlobalPose(), tethered, PxTransform(PxIdentity));
    PxControllerManager* controllers = PxCreateControllerManager(*scene);
    PxCapsuleControllerDesc desc; desc.radius = 0.3f; desc.height = 1; desc.material = material;
    desc.position = PxExtendedVec3(0, 7, 0);
    PxController* controller = controllers->createController(desc);
    check(controller != nullptr, "origin controller setup");
    controller->setPosition(PxExtendedVec3(location.x, 7, location.z));
    PxControllerFilters filters;
    controller->move(PxVec3(0, -0.2f, 0), 0.001f, 0.005f, filters);
    const PxExtendedVec3 controller_before = controller->getPosition();
    const PxVec3 target = location + PxVec3(11, 0, 0);
    platform->setKinematicTarget(PxTransform(target));
    spartan::PhysicsSceneOrigin origin;
    const PxVec3 shift = origin.Update(*scene, location);
    controllers->shiftOrigin(shift);
    const PxExtendedVec3 controller_after = controller->getPosition();
    check(fabs(controller_after.x + shift.x - controller_before.x) < 1e-6 && fabs(controller_after.z + shift.z - controller_before.z) < 1e-6,
        "controller origin shift preserves map position exactly once");
    PxRaycastBuffer hit;
    check(scene->raycast(location - shift + PxVec3(2, 3, 0), PxVec3(0, -1, 0), 4, hit) && hit.block.actor == road,
        "queries find finite road geometry after origin shift");
    check(fabsf(hit.block.position.y - 5.5f) < 1e-4f, "query hit retains world elevation");
    scene->simulate(0.005f); scene->fetchResults(true);
    check((platform->getGlobalPose().p + shift - target).magnitude() < 1e-4f, "kinematic target survives origin shift");
    PxTransform a, b; check(car::joint_world_frames(anchor, a, b), "origin joint debug frames available");
    check((a.p - b.p).magnitude() < 1e-4f, "world-anchored joint survives origin shift");
    controller->move(PxVec3(0, -2, 0), 0.001f, 0.005f, filters);
    check(controller->getPosition().y > 6.2, "controller still collides with road after cache shift");
    controller->release(); controllers->release(); anchor->release(); tethered->release(); platform->release(); road->release(); material->release();
    scene->shiftOrigin(-origin.offset);
    printf("origin scene queries, kinematic target, world joint, and controller checks passed\n");
}

void large_world_checks(PxPhysics* physics, PxScene* scene, const car::car_preset& preset, float dt, bool rebasing = true)
{
    origin_scene_checks(physics, scene);
    std::vector<PxVec3> reference_path;
    for (int distant = 0; distant < 2; ++distant)
    {
        spartan::PhysicsSceneOrigin origin;
        const PxVec3 location = distant ? PxVec3(6276, 0, -2823) : PxVec3(0);
        if (rebasing) origin.Update(*scene, location);
        car::Simulation sim; sim.get_spec() = preset;
        car::setup_params params; params.physics = physics; params.scene = scene;
        check(sim.setup(params), "large world setup");
        sim.shift_origin(origin.offset);
        install_bench_chassis(sim, physics);
        PxTransform pose = sim.get_body()->getGlobalPose();
        pose.q = PxQuat(0.63f, PxVec3(0, 1, 0));
        pose.p += location - origin.offset;
        sim.get_body()->setGlobalPose(pose);
        check(sim.rebuild_multibody(false), "large world rebuild");
        if (distant)
        {
            sim.set_telemetry_path("binaries/car_tests/large_world_map_" + std::to_string(static_cast<int>(1 / dt)) + (rebasing ? ".csv" : "_unrebased.csv"));
            sim.set_log_to_file(true);
        }
        int shifts = 0;
        auto step = [&]() {
            if (rebasing)
            {
                const auto& wheel = sim.get_wheel_state(0);
                const PxVec3 contact = wheel.contact_point;
                const PxVec3 tire_point = wheel.force_debug.tire_point;
                const PxVec3 force = wheel.force_debug.lateral;
                const PxVec3 velocity = sim.get_body()->getLinearVelocity();
                const PxVec3 shift = origin.Update(*scene, sim.get_body()->getGlobalPose().p + origin.offset);
                if (!shift.isZero())
                {
                    sim.shift_origin(shift);
                    ++shifts;
                    check((wheel.contact_point + shift - contact).magnitude() < 0.0001f, "rebasing preserves cached road contact");
                    check((wheel.force_debug.tire_point + shift - tire_point).magnitude() < 0.0001f, "rebasing preserves debug force application point");
                    check((sim.get_body()->getLinearVelocity() - velocity).magnitude() == 0 && (wheel.force_debug.lateral - force).magnitude() == 0, "rebasing does not change velocities or forces");
                }
            }
            sim.tick(dt); scene->simulate(dt); scene->fetchResults(true);
        };
        for (int j = 0; j < static_cast<int>(4 / dt); ++j) step();
        const PxVec3 start = sim.get_body()->getGlobalPose().p + origin.offset;
        PxVec3 integrated_velocity(0);
        sim.set_throttle(0.5f);
        for (int j = 0; j < static_cast<int>(5 / dt); ++j)
        {
            step();
            integrated_velocity += sim.get_body()->getLinearVelocity() * dt;
        }
        PxVec3 displacement = sim.get_body()->getGlobalPose().p + origin.offset - start;
        PxVec3 error = displacement - integrated_velocity;
        PxVec3 local = pose.q.rotateInv(displacement);
        PxVec3 axis = sim.get_body()->getGlobalPose().q.rotate(PxVec3(0, 0, 1));
        printf("large_world distant=%d rebasing=%d dt=%.4f displacement=(%.3f,%.3f) integrated=(%.3f,%.3f) error=%.3f lateral=%.3f yaw=%.3f speed=%.2f\n",
            distant, rebasing, dt,
            displacement.x, displacement.z, integrated_velocity.x, integrated_velocity.z,
            PxVec3(error.x, 0, error.z).magnitude(), local.x, atan2f(axis.x, axis.z) - 0.63f, sim.get_speed_kmh());
        check(PxVec3(error.x, 0, error.z).magnitude() < 0.1f, "position follows integrated velocity at recorded map coordinates");
        check(fabsf(local.x) < 0.4f, "straight throttle does not crab at recorded map coordinates");
        check(fabsf(atan2f(axis.x, axis.z) - 0.63f) < 0.02f, "straight throttle preserves heading at recorded map coordinates");
        float max_path_error = 0;
        const double distance_before = sim.get_distance_m();
        for (int j = 0; j < static_cast<int>(8 / dt); ++j)
        {
            const bool braking = j * dt >= 4;
            sim.set_throttle(braking ? 0.0f : 1.0f);
            sim.set_brake(braking ? 1.0f : 0.0f);
            // A gentle steering transient also exercises non-axis-aligned motion
            // through repeated rebases, followed by a stop.
            sim.set_steering(j * dt > 2 && j * dt < 3 ? 0.04f : 0);
            step();
            const PxVec3 point = sim.get_body()->getGlobalPose().p + (origin.offset - location);
            if (!distant) reference_path.push_back(point);
            else max_path_error = PxMax(max_path_error, (point - reference_path[j]).magnitude());
        }
        printf("large_world traversal distant=%d rebases=%d path_error=%.3f distance=%.3f stop_speed=%.3f\n", distant, shifts, max_path_error, sim.get_distance_m() - distance_before, sim.get_speed_kmh());
        check(shifts >= 2 || !rebasing, "traversal crosses multiple physics origin boundaries");
        check(max_path_error < 0.5f, "origin and map launch/steer/brake trajectories agree within half a metre");
        check(sim.get_speed_kmh() < 1, "car brakes to a stop after multiple rebases");
        check(sim.get_distance_m() - distance_before < 220, "origin shifts do not add distance to the odometer");
        scene->shiftOrigin(-origin.offset);
    }
}
