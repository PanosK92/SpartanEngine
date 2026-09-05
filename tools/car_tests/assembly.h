#pragma once
#include "car/CarDebug.h"

void assembly_frame_checks(PxPhysics* physics, PxScene* scene, const car::car_preset& preset)
{
    PxVec3 reference_extents[2];
    PxMat33 reference_inertia[2];
    for (int rotation = 0; rotation < 3; ++rotation)
    {
        car::Simulation sim; sim.get_spec() = preset;
        car::setup_params params; params.physics = physics; params.scene = scene;
        check(sim.setup(params), "assembly frame setup");
        PxQuat orientation = rotation == 0 ? PxQuat(PxIdentity)
            : rotation == 1 ? PxQuat(PxHalfPi, PxVec3(0, 1, 0))
            : PxQuat(0.7f, PxVec3(1, 2, 3).getNormalized());
        PxTransform frame(PxVec3(0, 10, 0), orientation);
        sim.get_body()->setGlobalPose(frame);
        check(sim.rebuild_multibody(false), "rebuild at rotated chassis pose");
        for (int arm = 0; arm < 2; ++arm)
        {
            const auto* actor = sim.get_multibody_state().corners[0].members[arm * 2].actor;
            PxShape* shape = nullptr; actor->getShapes(&shape, 1);
            PxVec3 extents = static_cast<const PxBoxGeometry&>(shape->getGeometry()).halfExtents;
            PxMat33 inertia = car::actor_inertia_about(*actor, frame, frame.transformInv(actor->getGlobalPose().p));
            if (!rotation) { reference_extents[arm] = extents; reference_inertia[arm] = inertia; }
            else
            {
                check((extents - reference_extents[arm]).magnitude() < 1e-5f, "wishbone shape is independent of world heading");
                for (int axis = 0; axis < 3; ++axis)
                    check((inertia[axis] - reference_inertia[arm][axis]).magnitude() < 1e-4f, "wishbone inertia rotates with chassis");
            }
        }
    }
}

void engine_reaction_checks(PxPhysics* physics, PxScene* scene, const car::car_preset& preset)
{
    car::Simulation sim; sim.get_spec() = preset;
    car::setup_params params; params.physics = physics; params.scene = scene; params.create_mechanisms = false;
    check(sim.setup(params), "free engine reaction setup");
    auto* body = sim.get_body();
    body->setGlobalPose(PxTransform(PxVec3(0, 10, 0)));
    body->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, true);
    body->setAngularDamping(0);
    sim.set_manual_transmission(true); sim.shift_to_neutral();
    sim.set_throttle(1); sim.update_input(1);
    const float before = sim.get_current_engine_rpm() * PxTwoPi / 60;
    const float gearbox_before = sim.get_gearbox_input_angular_velocity();
    sim.integrate_powertrain(0.005f);
    const float delta_momentum = preset.engine_inertia * (sim.get_current_engine_rpm() * PxTwoPi / 60 - before)
        + preset.driveline_inertia * (sim.get_gearbox_input_angular_velocity() - gearbox_before);
    check(delta_momentum > 0.01f, "neutral throttle accelerates engine");
    PxMat33 inertia = car::actor_inertia_about(*body, PxTransform(PxIdentity), body->getGlobalPose().transform(body->getCMassLocalPose().p));
    scene->simulate(0.005f); scene->fetchResults(true);
    PxVec3 crank(preset.engine_crank_axis_x, preset.engine_crank_axis_y, preset.engine_crank_axis_z);
    PxVec3 momentum = inertia * body->getAngularVelocity();
    check((momentum + crank.getNormalized() * delta_momentum).magnitude() < 0.001f,
        "engine and chassis conserve angular momentum with an open clutch");
}

void debug_geometry_checks(PxPhysics* physics, PxScene* scene, const car::car_preset& preset)
{
    car::Simulation sim; sim.get_spec() = preset;
    car::setup_params params; params.physics = physics; params.scene = scene;
    check(sim.setup(params), "debug geometry setup");
    const auto& corner = sim.get_multibody_state().corners[2];
    const PxTransform upright = corner.upright->getGlobalPose();
    // Deliberately separate and yaw the wheel without moving the upright.
    // The debug anchors and tread queries must expose this, not hide it.
    PxTransform wheel = corner.wheel_body->getGlobalPose();
    wheel.p.x += 0.02f;
    wheel.p.y = sim.get_config().wheel_radius_for(2) - 0.025f;
    wheel.q = PxQuat(0.2f, PxVec3(0, 1, 0)) * wheel.q;
    corner.wheel_body->setGlobalPose(wheel);
    PxTransform a, b;
    check(car::joint_world_frames(corner.wheel_joint, a, b), "debug joint frames available");
    check((a.p - upright.p).magnitude() < 1e-6f && (b.p - wheel.p).magnitude() < 1e-6f,
        "debug exposes each actual bearing anchor");
    check((a.q.rotate(PxVec3(1, 0, 0)) - b.q.rotate(PxVec3(1, 0, 0))).magnitude() > 0.1f,
        "debug exposes wheel bearing angular error");
    sim.update_suspension(scene, 0.005f, false);
    const auto& state = sim.get_wheel_state(2);
    check(state.row_count >= 2 && state.contacts[0].hit && state.contacts[state.row_count - 1].hit,
        "yawed wheel tread spans multiple loaded rows");
    PxVec3 span = state.contacts[state.row_count - 1].point - state.contacts[0].point;
    span.y = 0; span.normalize();
    PxVec3 axis = wheel.q.rotate(PxVec3(1, 0, 0)); axis.y = 0; axis.normalize();
    check(span.dot(axis) > 0.9999f, "contact rows follow actual wheel plane, not upright plane");
    sim.clear_force_accumulators();

    // A fully commanded caliper on a stationary unloaded wheel performs no
    // braking work; the debug display must report actual torque, not capacity.
    for (int i = 0; i < 4; ++i)
    {
        auto& w = sim.get_wheel_state(i);
        w.grounded = false; w.tire_load = 0; w.drive_torque = 0;
        w.brake_torque = 1000;
        sim.set_wheel_angular_velocity(i, 0);
        w.force_debug.longitudinal = w.force_debug.lateral = PxVec3(123);
    }
    sim.apply_tire_forces(0.005f);
    for (int i = 0; i < 4; ++i)
    {
        const auto& debug = sim.get_wheel_state(i).force_debug;
        check(debug.brake_torque == 0 && debug.longitudinal.isZero() && debug.lateral.isZero(),
            "debug clears unsupported tire forces and reports actual caliper torque");
    }
    printf("assembly rotation, engine reaction, and debug geometry checks passed\n");
}
