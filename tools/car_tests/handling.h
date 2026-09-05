#pragma once

void handling_checks(PxPhysics* physics, PxScene* scene, PxRigidStatic* plane, const car::car_preset& preset, float dt)
{
    float skid_g[2] = {};
    for (int scenario = 0; scenario < 7; ++scenario)
    {
        car::Simulation sim; sim.get_spec() = preset;
        car::setup_params params; params.physics = physics; params.scene = scene;
        check(sim.setup(params), "handling setup");
        PxShape* shape; sim.get_body()->getShapes(&shape, 1); shape->setGeometry(PxBoxGeometry(0.8f, 0.15f, 2));
        sim.set_telemetry_path("binaries/car_tests/handling_" + std::to_string(scenario) + "_" + std::to_string(static_cast<int>(1 / dt)) + ".csv");
        sim.set_log_to_file(true);
        auto step = [&]() {
            sim.tick(dt); scene->simulate(dt); scene->fetchResults(true);
            check(sim.get_body()->getGlobalPose().isValid(), "handling finite pose");
            check(sim.get_body()->getGlobalPose().q.rotate(PxVec3(0, 1, 0)).y > 0.6f, "handling remains upright");
            for (int j = 0; j < sim.get_multibody_state().actor_count; ++j)
                check(sim.get_multibody_state().actors[j]->getLinearVelocity().isFinite(), "handling finite mechanisms");
        };
        for (int j = 0; j < static_cast<int>(4 / dt); ++j) step();
        for (int i = 0; i < 4; ++i) { auto& t = sim.get_wheel_state(i).thermal; t.core = 40; for (float& v : t.surface) v = 80; }
        sim.set_validation_speed(scenario == 6 ? 100 / 3.6f : 16);
        PxRigidStatic* split[2] = {}; PxMaterial* split_material[2] = {};
        if (scenario == 6)
        {
            sim.set_manual_transmission(true);
            scene->removeActor(*plane);
            for (int side = 0; side < 2; ++side)
            {
                split_material[side] = physics->createMaterial(side ? 0.8f : 0.16f, side ? 0.7f : 0.14f, 0);
                split[side] = PxCreateStatic(*physics, PxTransform(PxVec3(side ? 50.0f : -50.0f, -0.5f, 100)), PxBoxGeometry(50, 0.5f, 200), *split_material[side]);
                scene->addActor(*split[side]);
            }
        }
        float g_sum = 0, yaw_sum = 0, max_yaw = 0, distance = 0; int samples = 0;
        PxVec3 previous = sim.get_body()->getGlobalPose().p;
        for (int j = 0; j < static_cast<int>(8 / dt); ++j)
        {
            float time = j * dt;
            float steer = scenario == 0 ? 0.35f : scenario == 1 ? -0.35f
                : scenario == 2 ? (time > 1 ? 0.25f : 0) : scenario == 3 ? 0.35f * sinf(time * PxTwoPi / 2.5f)
                : scenario == 6 ? 0 : 0.25f;
            float speed_error = 16 - sim.get_speed_kmh() / 3.6f;
            float throttle = PxClamp(0.15f + speed_error * 0.2f, 0.0f, 1.0f);
            if (scenario == 4) throttle = time < 4 ? 0.6f : 0; // lift-off
            if (scenario == 5) throttle = time < 2 ? 0.15f : 1; // power-on
            sim.set_throttle(scenario == 6 ? 0 : throttle);
            sim.set_brake(scenario == 6 ? 1 : PxClamp(-speed_error * 0.1f, 0.0f, 0.3f));
            sim.set_steering(steer); step();
            PxVec3 position = sim.get_body()->getGlobalPose().p;
            distance += (position - previous).magnitude(); previous = position;
            float yaw = sim.get_body()->getAngularVelocity().y;
            max_yaw = PxMax(max_yaw, fabsf(yaw));
            if (time > 5) { g_sum += fabsf(sim.get_lateral_accel()) / 9.81f; yaw_sum += yaw; ++samples; }
            if (scenario == 6 && sim.get_speed_kmh() < 1) break;
        }
        printf("handling %d mean_g=%.3f mean_yaw=%.3f peak_yaw=%.3f distance=%.2f speed=%.1f\n", scenario, samples ? g_sum/samples : 0, samples ? yaw_sum/samples : 0, max_yaw, distance, sim.get_speed_kmh());
        check(max_yaw < 3.0f, "bounded handling yaw rate");
        if (scenario < 2)
        {
            skid_g[scenario] = g_sum / samples;
            check(yaw_sum * (scenario ? -1.0f : 1.0f) > 0, "steering sign in both directions");
            check(skid_g[scenario] >= preset.validation.skidpad_g_min && skid_g[scenario] <= preset.validation.skidpad_g_max, "skidpad envelope");
        }
        if (scenario == 6)
        {
            check(distance > 30 && distance < 90 && sim.get_speed_kmh() < 1, "split friction stopping envelope");
            for (int side = 0; side < 2; ++side) { split[side]->release(); split_material[side]->release(); }
            scene->addActor(*plane);
        }
    }
    check(fabsf(skid_g[0] - skid_g[1]) < 0.15f, "left/right skidpad symmetry");
}
