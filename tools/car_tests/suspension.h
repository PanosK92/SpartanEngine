#pragma once

// Measure solved bodies, not telemetry sampled before the PhysX step or the
// rendered wheel rings.
void suspension_checks(PxPhysics* physics, PxScene* scene, const car::car_preset& preset, float dt)
{
    check(std::isfinite(dt) && dt > 0 && dt <= 0.02f, "suspension timestep is finite and within 50 Hz or faster");
    car::Simulation sim; sim.get_spec() = preset;
    car::setup_params params; params.physics = physics; params.scene = scene;
    check(sim.setup(params), "suspension setup");
    install_bench_chassis(sim, physics);
    const std::string solver = scene->getSolverType() == PxSolverType::eTGS ? "tgs" : "pgs";
    sim.set_telemetry_path("binaries/car_tests/suspension_" + solver + "_" + std::to_string(static_cast<int>(1 / dt)) + ".csv");
    sim.set_log_to_file(true);
    auto step = [&]() { sim.tick(dt); scene->simulate(dt); scene->fetchResults(true); };
    for (int j = 0; j < static_cast<int>(4 / dt); ++j) step();
    for (int i = 0; i < 4; ++i) { auto& t = sim.get_wheel_state(i).thermal; t.core = 40; for (float& v : t.surface) v = 80; }
    sim.set_throttle(1);
    float max_hub_error = 0, max_axis_error = 0, max_pivot_error = 0, max_toe_rate = 0;
    float previous_toe[2] = {};
    int shifts = 0, previous_gear = sim.get_current_gear();
    for (int j = 0; j < static_cast<int>(8 / dt); ++j)
    {
        step();
        int gear = sim.get_current_gear();
        if (gear > previous_gear) ++shifts;
        previous_gear = gear;
        const auto& assembly = sim.get_multibody_state();
        for (int i = 2; i < 4; ++i)
        {
            const auto& corner = assembly.corners[i];
            PxTransform upright = corner.upright->getGlobalPose(), wheel = corner.wheel_body->getGlobalPose();
            check(upright.isValid() && wheel.isValid(), "finite rear suspension poses");
            max_hub_error = PxMax(max_hub_error, (upright.p - wheel.p).magnitude());
            PxVec3 axle = upright.q.rotate(PxVec3(1, 0, 0)), wheel_axis = wheel.q.rotate(PxVec3(1, 0, 0));
            max_axis_error = PxMax(max_axis_error, atan2f(axle.cross(wheel_axis).magnitude(), axle.dot(wheel_axis)));
            PxVec3 local_axis = sim.get_body()->getGlobalPose().q.rotateInv(axle);
            float toe = atan2f(-local_axis.z, local_axis.x);
            if (j) max_toe_rate = PxMax(max_toe_rate, fabsf(toe - previous_toe[i - 2]) / dt);
            previous_toe[i - 2] = toe;
        }
        for (int i = 0; i < assembly.joint_count; ++i)
        {
            PxJoint* joint = assembly.joints[i];
            if (!joint->is<PxSphericalJoint>()) continue;
            PxRigidActor* a; PxRigidActor* b; joint->getActors(a, b);
            if (a != assembly.corners[2].upright && b != assembly.corners[2].upright &&
                a != assembly.corners[3].upright && b != assembly.corners[3].upright) continue;
            PxTransform fa = (a ? a->getGlobalPose() : PxTransform(PxIdentity)) * joint->getLocalPose(PxJointActorIndex::eACTOR0);
            PxTransform fb = (b ? b->getGlobalPose() : PxTransform(PxIdentity)) * joint->getLocalPose(PxJointActorIndex::eACTOR1);
            check(fa.isValid() && fb.isValid(), "finite rear suspension joint frames");
            max_pivot_error = PxMax(max_pivot_error, (fa.p - fb.p).magnitude());
        }
    }
    printf("suspension solver=%s dt=%.4f shifts=%d speed=%.1f hub_mm=%.3f bearing_deg=%.3f pivot_mm=%.3f toe_rate_deg_s=%.3f\n",
        scene->getSolverType() == PxSolverType::eTGS ? "TGS" : "PGS", dt, shifts, sim.get_speed_kmh(),
        max_hub_error * 1000, max_axis_error * 180 / PxPi, max_pivot_error * 1000, max_toe_rate * 180 / PxPi);
    check(max_hub_error < 0.001f, "rear wheel stays on its hub through upshifts");
    check(max_axis_error < PxPi / 180 * 0.25f, "rear wheel bearing holds its axle through upshifts");
    check(max_pivot_error < 0.001f, "suspension ball joints stay connected through upshifts");
    check(shifts >= 3, "suspension fixture exercises multiple loaded upshifts");
}
