#pragma once
#include <fstream>
#include <sstream>
#include "cooking/PxCooking.h"
inline std::string validation_hull_path;

void install_bench_chassis(car::Simulation& sim, PxPhysics* physics)
{
    if (validation_hull_path.empty())
    {
        PxShape* shape; sim.get_body()->getShapes(&shape, 1);
        shape->setGeometry(PxBoxGeometry(0.8f, 0.15f, 2));
        return;
    }
    std::ifstream file(validation_hull_path); std::string line;
    check(static_cast<bool>(std::getline(file, line)) && line == "hull,x,y,z", "cooked hull schema");
    std::vector<std::vector<PxVec3>> regions;
    while (std::getline(file, line))
    {
        for (char& c : line) if (c == ',') c = ' ';
        std::istringstream row(line); int id; PxVec3 p;
        check(static_cast<bool>(row >> id >> p.x >> p.y >> p.z) && id >= 0 && id < 32 && p.isFinite(), "cooked hull vertex");
        if (regions.size() <= static_cast<size_t>(id)) regions.resize(id + 1);
        regions[id].push_back(p);
    }
    std::vector<PxConvexMesh*> meshes; std::vector<PxVec3> vertices;
    for (const auto& region : regions)
    {
        check(region.size() >= 4 && region.size() <= 256, "cooked hull vertex count");
        PxConvexMeshDesc desc; desc.points.count = static_cast<PxU32>(region.size()); desc.points.stride = sizeof(PxVec3); desc.points.data = region.data(); desc.flags = PxConvexFlag::eCOMPUTE_CONVEX;
        auto* mesh = PxCreateConvexMesh(PxCookingParams(physics->getTolerancesScale()), desc, physics->getPhysicsInsertionCallback());
        check(mesh != nullptr, "cook captured chassis hull"); meshes.push_back(mesh);
        vertices.insert(vertices.end(), region.begin(), region.end());
    }
    check(!meshes.empty() && sim.set_chassis(meshes, vertices, physics), "install captured chassis hulls");
    for (auto* mesh : meshes) mesh->release();
}

void contact_checks(PxPhysics* physics, PxScene* scene, PxRigidStatic* plane, const car::car_preset& preset, float dt)
{
    // A free moving support must exchange momentum with the entire assembly.
    scene->removeActor(*plane);
    PxMaterial* material = physics->createMaterial(0.8f, 0.7f, 0);
    auto* platform = PxCreateDynamic(*physics, PxTransform(PxVec3(0, -0.25f, 0)), PxBoxGeometry(10, 0.25f, 10), *material, 1);
    PxRigidBodyExt::setMassAndUpdateInertia(*platform, 2000);
    platform->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, true);
    platform->setRigidDynamicLockFlags(PxRigidDynamicLockFlag::eLOCK_LINEAR_Y | PxRigidDynamicLockFlag::eLOCK_LINEAR_Z | PxRigidDynamicLockFlag::eLOCK_ANGULAR_X | PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y | PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z);
    scene->addActor(*platform);
    {
        car::Simulation sim; sim.get_spec() = preset;
        car::setup_params params; params.physics = physics; params.scene = scene;
        check(sim.setup(params), "moving support setup"); install_bench_chassis(sim, physics);
        for (int j = 0; j < static_cast<int>(4 / dt); ++j) { sim.tick(dt); scene->simulate(dt); scene->fetchResults(true); }
        platform->setLinearVelocity(PxVec3(2, 0, 0));
        for (int j = 0; j < static_cast<int>(3 / dt); ++j) { sim.tick(dt); scene->simulate(dt); scene->fetchResults(true); }
        float car_vx = sim.get_body()->getLinearVelocity().x;
        float momentum = platform->getMass() * platform->getLinearVelocity().x + sim.get_body()->getMass() * car_vx;
        for (int j = 0; j < sim.get_multibody_state().actor_count; ++j) { auto* actor = sim.get_multibody_state().actors[j]; momentum += actor->getMass() * actor->getLinearVelocity().x; }
        printf("moving support car_vx=%.3f platform_vx=%.3f momentum=%.2f / 4000 kg m/s\n", car_vx, platform->getLinearVelocity().x, momentum);
        check(car_vx > 0.3f && fabsf(momentum - 4000) < 160, "moving support traction and momentum balance");
    }
    platform->release(); scene->addActor(*plane);
    std::vector<PxRigidStatic*> kerbs;
    for (int j = 0; j < 8; ++j)
    {
        auto* kerb = PxCreateStatic(*physics, PxTransform(PxVec3(0, 0.025f, 8.0f + j)), PxBoxGeometry(4, 0.025f, 0.08f), *material);
        scene->addActor(*kerb); kerbs.push_back(kerb);
    }
    {
        car::Simulation sim; sim.get_spec() = preset;
        car::setup_params params; params.physics = physics; params.scene = scene;
        check(sim.setup(params), "kerb setup"); install_bench_chassis(sim, physics);
        for (int j = 0; j < static_cast<int>(4 / dt); ++j) { sim.tick(dt); scene->simulate(dt); scene->fetchResults(true); }
        sim.set_validation_speed(12); float peak_vertical = 0;
        for (int j = 0; j < static_cast<int>(2 / dt); ++j)
        {
            sim.tick(dt); scene->simulate(dt); scene->fetchResults(true);
            check(sim.get_body()->getGlobalPose().isValid(), "finite kerb response");
            peak_vertical = PxMax(peak_vertical, fabsf(sim.get_body()->getLinearVelocity().y));
        }
        printf("roughness 50mm kerbs peak_vertical=%.3f m/s\n", peak_vertical);
        check(peak_vertical < 3 && sim.get_body()->getGlobalPose().q.rotate(PxVec3(0, 1, 0)).y > 0.8f, "bounded kerb response");
    }
    for (auto* kerb : kerbs) kerb->release();
    material->release();
}
