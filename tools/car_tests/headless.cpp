// Compile the production vehicle and preset loader without starting the editor.
#include <new>
#include "pch.h"
#include "car/CarSimulation.h"
#include "physics/PhysicsSceneConfig.h"
#include <filesystem>
#include <stdexcept>

namespace spartan
{
    void Log::FormatBuffer(char* buffer, const char*, const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, SP_LOG_BUFFER_SIZE, format, args);
        va_end(args);
    }
    void Log::WriteBuffer(const char* text, LogType type)
    {
        if (type != LogType::Info) fprintf(stderr, "%s\n", text);
    }
    bool FileSystem::Exists(const std::string& path) { return std::filesystem::exists(path); }
    bool FileSystem::IsDirectory(const std::string& path) { return std::filesystem::is_directory(path); }
    std::vector<std::string> FileSystem::GetFilesInDirectory(const std::string& path)
    {
        std::vector<std::string> files;
        for (const auto& entry : std::filesystem::directory_iterator(path))
            if (entry.is_regular_file()) files.push_back(entry.path().string());
        return files;
    }
    std::string FileSystem::GetExtensionFromFilePath(const std::string& path) { return std::filesystem::path(path).extension().string(); }
}

using namespace physx;
void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

#include "chassis.h"
#include "handling.h"
#include "calibration.h"
#include "suspension.h"
#include "assembly.h"
#include "large_world.h"

void regression_checks(car::Simulation& sim, PxPhysics* physics, PxRigidStatic* plane)
{
    PxRigidDynamic* actor = physics->createRigidDynamic(PxTransform(PxVec3(10, 20, 30)));
    actor->setCMassLocalPose(PxTransform(PxVec3(1, 2, 3)));
    actor->setLinearVelocity(PxVec3(4, 5, 6));
    actor->setAngularVelocity(PxVec3(0, 2, 0));
    check((sim.actor_point_velocity(actor, PxVec3(11, 22, 33)) - PxVec3(4, 5, 6)).magnitude() < 1e-5f,
        "point velocity at COM equals COM velocity");
    check((sim.actor_point_velocity(actor, PxVec3(11, 22, 35)) - PxVec3(8, 5, 6)).magnitude() < 1e-5f,
        "point velocity includes rotation around COM");
    actor->release();

    const auto spec = sim.get_spec();
    sim.get_spec().engine_friction = 10;
    for (int i = 0; i < 200; ++i) sim.integrate_powertrain(0.005f);
    check(!sim.get_engine_running(), "engine can stall under mechanical drag");
    sim.get_spec().engine_friction = spec.engine_friction; sim.set_starter(true);
    for (int i = 0; i < 200; ++i) sim.integrate_powertrain(0.005f);
    check(sim.get_engine_running() && sim.get_current_engine_rpm() > spec.engine_stall_rpm, "starter restarts a stalled engine");
    sim.set_starter(false); sim.reset_drivetrain_transients(); sim.clear_force_accumulators();

    float map_x[3] = {0, 1, 2}, map_y[3] = {1, 2, 4};
    check(car::sample_curve(map_x, map_y, 3, 1.5f, 0) == 3 && car::sample_curve(map_x, map_y, 3, 5, 0) == 4, "calibration interpolation and endpoints");
    check(fabsf(car::hot_tire_pressure(spec, spec.tire_pressure_reference_temp, 0) - spec.tire_pressure) < 1e-5f, "cold gauge pressure reference");
    check(car::hot_tire_pressure(spec, 90, 0) > car::hot_tire_pressure(spec, 20, 0), "pressure rises with absolute temperature");
    check(car::water_grip(spec, 50, 2.2f, 0) == 1 && car::water_grip(spec, 50, 2.2f, 0.003f) < 0.2f, "dry and hydroplaning limits");
    car::hybrid_state battery; battery.energy_j = 100000; battery.temperature = 30;
    car::integrate_hybrid(spec, battery, 50000, 0.1f, 30);
    check(fabsf(100000 - battery.energy_j - battery.electrical_power_w * 0.1f) < 0.02f, "battery discharge energy balance");
    check(battery.loss_power_w > 0, "motor losses generate heat");
    battery.energy_j = 0;
    check(car::integrate_hybrid(spec, battery, 1000, 0.1f, 30) == 0, "empty battery cannot propel");
    battery.energy_j = spec.battery_capacity_kwh * 3600000;
    check(car::integrate_hybrid(spec, battery, -1000, 0.1f, 30) == 0, "full battery rejects regeneration");
    PxMat33 assembled = sim.get_assembled_inertia();
    check(fabsf(assembled.column0.x - spec.inertia_xx) < 0.1f && fabsf(assembled.column1.y - spec.inertia_yy) < 0.1f && fabsf(assembled.column2.z - spec.inertia_zz) < 0.1f, "assembled inertia target without double counting");
    FILE* sweep = nullptr; fopen_s(&sweep, "binaries/car_tests/kinematics.csv", "w");
    check(sweep != nullptr, "kinematic report opens");
    fprintf(sweep, "wheel,bump_m,camber_delta_rad,toe_delta_rad,motion_ratio\n");
    for (int i = 0; i < 4; ++i)
    {
        PxVec3 hub = sim.get_wheel_offset(i);
        auto pickups = car::resolve_pickups(i < 2 ? spec.front_geometry : spec.rear_geometry, hub);
        float ratio = car::suspension_motion_ratio(pickups, hub);
        check(ratio > 0.5f && ratio < 1.2f, "kinematic motion ratio");
        const auto& corner = sim.get_multibody_state().corners[i];
        float mass = sim.sprung_mass() * (i < 2 ? sim.get_weight_distribution_front() : 1 - sim.get_weight_distribution_front()) * 0.5f;
        float frequency = sqrtf(corner.shock_stiffness * ratio * ratio / mass) / PxTwoPi;
        check(fabsf(frequency - (i < 2 ? spec.front_spring_freq : spec.rear_spring_freq)) < 0.002f, "shock rate reflects requested wheel frequency");
        for (int j = -8; j <= 8; ++j)
        {
            PxTransform pose; check(car::solve_suspension_pose(pickups, hub, j * 0.01f, pose), "full kinematic sweep converges");
            PxVec3 axis = pose.q.rotate(PxVec3(1, 0, 0));
            fprintf(sweep, "%d,%.3f,%.6f,%.6f,%.6f\n", i, j * 0.01f, asinf(axis.y), atan2f(-axis.z, axis.x), ratio);
        }
    }
    fclose(sweep);
    // Parking caliper work must enter the same heat balance as service braking.
    sim.set_handbrake(1); sim.update_input(0.005f);
    sim.set_wheel_angular_velocity(2, 10);
    sim.get_wheel_state(2).grounded = false;
    float brake_before = sim.get_wheel_state(2).brake_temp;
    sim.update_handbrake(); sim.apply_tire_forces(0.005f);
    check(sim.get_wheel_state(2).brake_temp > brake_before, "handbrake dissipates rotor energy into brake heat");
    sim.set_handbrake(0); sim.update_input(0.005f); sim.set_validation_speed(0); sim.clear_force_accumulators();
    for (int i = 0; i < 4; ++i) { sim.get_wheel_state(i).brake_torque = 0; sim.get_wheel_state(i).net_torque = 0; }


    auto thermal_spec = spec;
    thermal_spec.tire_heat_transfer_static = 0;
    thermal_spec.tire_heat_transfer_airflow = 0;
    car::tire_thermal temperature;
    temperature.surface[0] = 140;
    temperature.surface[1] = 100;
    temperature.surface[2] = 80;
    temperature.core = 50;
    auto energy = [&]() { return temperature.avg_surface() * thermal_spec.tire_surface_heat_capacity + temperature.core * thermal_spec.tire_core_heat_capacity; };
    const float shares[3] = { 0.2f, 0.3f, 0.5f };
    float before = energy();
    car::integrate_tire_thermal(temperature, thermal_spec, shares, 0, 0, 0, 0.005f);
    check(fabsf(energy() - before) < 0.2f, "surface/core heat exchange conserves energy");
    before = energy();
    car::integrate_tire_thermal(temperature, thermal_spec, shares, 10000, 2000, 0, 0.005f);
    check(fabsf(energy() - before - 55.0f) < 0.2f, "temperature rise matches dissipated energy and heat capacity");
    auto brush = car::evaluate_brush_params(spec, 0.34f, 0.265f, 4000, 1);
    for (float slip : {-0.95f, -0.3f, -0.05f, 0.0f, 0.05f, 0.3f, 0.95f})
    {
        for (float angle : {-0.5f, -0.05f, 0.0f, 0.05f, 0.5f})
        {
            float saturation_a, saturation_b;
            auto a = car::evaluate_brush_model(spec, brush, slip, angle, 0, 4000, 6000, 5000, 1, saturation_a, 1);
            auto b = car::evaluate_brush_model(spec, brush, -slip, angle, 0, 4000, 6000, 5000, 1, saturation_b, -1);
            check(fabsf(a.longitudinal + b.longitudinal) < 0.01f && fabsf(a.lateral - b.lateral) < 0.01f,
                "brush force symmetry in reverse");
            check(a.longitudinal * slip >= -0.01f && a.lateral * angle <= 0.01f, "tire resists slip");
            check(powf(a.longitudinal / 6000, 2) + powf(a.lateral / 5000, 2) <= 1.0001f, "combined friction budget");
        }
    }

    const auto& assembly = sim.get_multibody_state();
    const auto& corner = assembly.corners[0];
    PxTransform original = corner.upright->getGlobalPose();
    PxVec3 top = sim.get_body()->getGlobalPose().transform(corner.chassis_shock_anchor);
    PxVec3 bottom = original.transform(corner.upright_shock_anchor);
    float desired_length = corner.shock_rest_length - sim.get_config().suspension_travel * corner.design_motion_ratio * 1.1f;
    PxVec3 new_bottom = top + (bottom - top).getNormalized() * desired_length;
    corner.upright->setGlobalPose(PxTransform(original.p + new_bottom - bottom, original.q));
    sim.get_spec().packer_stiffness = 0;
    sim.update_multibody(0.005f);
    float without_packer = sim.get_wheel_suspension_force(0);
    sim.get_spec().packer_stiffness = spec.packer_stiffness;
    sim.update_multibody(0.005f);
    check(sim.get_wheel_suspension_force(0) > without_packer + 1000, "packer carries load above nominal travel");
    corner.upright->setGlobalPose(original);
    sim.update_multibody(0.005f);
    sim.clear_force_accumulators();

    // Isolate tire torque from the scene solver: no road load, ample brake capacity.
    for (float spin : {-10.0f, 0.0f, 10.0f})
    {
        auto& wheel = sim.get_wheel_state(0);
        wheel.grounded = false;
        wheel.tire_load = 0;
        wheel.drive_torque = 0;
        wheel.brake_torque = 100000;
        sim.set_wheel_angular_velocity(0, spin);
        float inertia = corner.wheel_body->getMassSpaceInertiaTensor().x;
        sim.apply_tire_forces(0.005f);
        check(fabsf(spin + wheel.net_torque * 0.005f / inertia) < 1e-4f, "airborne brakes stop without reversing wheel");
        sim.clear_force_accumulators();
    }
    sim.set_wheel_angular_velocity(0, 0);
    sim.reset_wheel_thermals();
    sim.reset_drivetrain_transients();
    sim.sleep_vehicle_assembly();
    sim.get_body()->wakeUp();
    sim.get_body()->setLinearVelocity(PxVec3(1, 0, 0));
    sim.tick(0.005f);
    check(!sim.get_vehicle_sleeping() && sim.get_body()->getLinearVelocity().x > 0.9f, "external wake preserves collision velocity");
    sim.clear_force_accumulators();
    sim.set_validation_speed(0);
    sim.sleep_vehicle_assembly();
    PxTransform plane_pose = plane->getGlobalPose();
    plane->setGlobalPose(PxTransform(plane_pose.p + PxVec3(0, -5, 0), plane_pose.q));
    plane->getScene()->flushQueryUpdates();
    sim.tick(0.005f);
    check(!sim.get_vehicle_sleeping(), "lost query support wakes parked car");
    plane->setGlobalPose(plane_pose);
    plane->getScene()->flushQueryUpdates();
    sim.clear_force_accumulators();
    sim.set_validation_speed(0);
    sim.sleep_vehicle_assembly();
    sim.set_brake(1);
    sim.tick(0.005f);
    check(!sim.get_vehicle_sleeping(), "brake pedal wakes a forward-geared parked car for reverse request");
    sim.set_brake(0);
    sim.clear_force_accumulators();
    sim.set_validation_speed(0);
    printf("regression checks passed\n");
}

PxFilterFlags vehicle_filter(PxFilterObjectAttributes a, PxFilterData fa, PxFilterObjectAttributes b, PxFilterData fb, PxPairFlags& flags, const void* data, PxU32 size)
{
    if (fa.word2 == 2 && fb.word2 == 2 && fa.word3 && fa.word3 == fb.word3) return PxFilterFlag::eSUPPRESS;
    auto result = PxDefaultSimulationFilterShader(a, fa, b, fb, flags, data, size);
    if (!PxFilterObjectIsTrigger(a) && !PxFilterObjectIsTrigger(b)) flags |= PxPairFlag::eDETECT_CCD_CONTACT;
    return result;
}

int main(int argc, char** argv)
{
    try
    {
        if (argc > 1 && std::string(argv[1]) == "--tire-evaluate") return evaluate_calibration(argc, argv);
        const bool suspension_only = argc > 1 && std::string(argv[1]) == "--suspension-check";
        const bool large_world_only = argc > 1 && std::string(argv[1]) == "--large-world-check";
        if (argc > 6) validation_hull_path = argv[6];
        PxDefaultAllocator allocator;
        PxDefaultErrorCallback errors;
        PxFoundation* foundation = PxCreateFoundation(PX_PHYSICS_VERSION, allocator, errors);
        PxPhysics* physics = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, PxTolerancesScale());
        PxInitExtensions(*physics, nullptr);
        PxDefaultCpuDispatcher* dispatcher = PxDefaultCpuDispatcherCreate(1);
        PxSceneDesc desc(physics->getTolerancesScale());
        desc.gravity = PxVec3(0, -9.81f, 0);
        desc.cpuDispatcher = dispatcher;
        desc.filterShader = vehicle_filter;
        spartan::ConfigurePhysicsScene(desc);
        if ((suspension_only || large_world_only) && argc > 3 && std::string(argv[3]) == "pgs") desc.solverType = PxSolverType::ePGS;
        PxScene* scene = physics->createScene(desc);
        PxMaterial* material = physics->createMaterial(0.8f, 0.7f, 0.0f);
        PxRigidStatic* plane = PxCreatePlane(*physics, PxPlane(0, 1, 0, 0), *material);
        scene->addActor(*plane);
        const auto* suspension_definition = car::load_car_file("worlds/cars/ferrari_laferrari.car");
        check(suspension_definition != nullptr, "suspension preset loads");
        if (large_world_only)
        {
            large_world_checks(physics, scene, suspension_definition->performance, argc > 2 ? std::stof(argv[2]) : 0.005f, !(argc > 3 && std::string(argv[3]) == "unrebased"));
            scene->release(); material->release(); dispatcher->release(); PxCloseExtensions(); physics->release(); foundation->release();
            return 0;
        }
        assembly_frame_checks(physics, scene, suspension_definition->performance);
        engine_reaction_checks(physics, scene, suspension_definition->performance);
        debug_geometry_checks(physics, scene, suspension_definition->performance);
        suspension_checks(physics, scene, suspension_definition->performance, argc > 2 ? std::stof(argv[2]) : 0.005f);
        if (!suspension_only)
        {
            large_world_checks(physics, scene, suspension_definition->performance, argc > 2 ? std::stof(argv[2]) : 0.005f);
            for (const auto& entry : std::filesystem::directory_iterator("worlds/cars"))
                if (entry.path().extension() == ".car") check(car::load_car_file(entry.path().string()) != nullptr, "preset validation");
            printf("validated %zu car presets\n", car::definitions.size());
            const auto* definition = car::load_car_file("worlds/cars/ferrari_laferrari.car");
            check(definition != nullptr, "Ferrari preset must load");
            car::Simulation simulation;
            simulation.get_spec() = definition->performance;
            if (argc > 3) simulation.get_spec().brake_force = std::stof(argv[3]);
            if (argc > 4) simulation.get_spec().tc_slip_threshold = std::stof(argv[4]);
            car::setup_params params;
            params.physics = physics;
            params.scene = scene;
            check(simulation.setup(params), "vehicle setup");
            install_bench_chassis(simulation, physics);
            regression_checks(simulation, physics, plane);
            const float dt = argc > 2 ? std::stof(argv[2]) : 0.005f;
            if (argc > 1)
            {
                simulation.set_telemetry_path(argv[1]);
                simulation.set_log_to_file(true);
            }
            auto advance = [&](float seconds, float throttle, float brake)
            {
                simulation.set_throttle(throttle);
                simulation.set_brake(brake);
                for (int i = 0; i < static_cast<int>(seconds / dt + 0.5f); ++i)
                {
                    simulation.tick(dt);
                    scene->simulate(dt);
                    scene->fetchResults(true);
                    check(simulation.get_body()->getGlobalPose().isValid(), "finite chassis pose");
                    check(simulation.get_body()->getLinearVelocity().isFinite(), "finite velocity");
                    PxVec3 inertia = simulation.get_multibody_state().driveline.gearbox_output->getMassSpaceInertiaTensor();
                    check(inertia.x <= inertia.y + inertia.z + 1e-4f, "physical principal inertia bounds after gear changes");
                }
            };
            advance(5.0f, 0.0f, 0.0f);
            float load = 0.0f;
            for (int i = 0; i < 4; ++i) load += simulation.get_wheel_tire_load(i);
            printf("settle speed=%.5f m/s load=%.1f N mass=%.1f kg\n", simulation.get_speed_kmh()/3.6f, load, simulation.get_config().mass);
            check(simulation.get_speed_kmh()/3.6f < 0.04f, "Ferrari settle speed target");
            check(simulation.get_vehicle_sleeping(), "park remains asleep without repeated handbrake writes");
            float total_mass = simulation.get_body()->getMass();
            for (int i = 0; i < simulation.get_multibody_state().actor_count; ++i)
                total_mass += simulation.get_multibody_state().actors[i]->getMass();
            printf("assembly actual_mass=%.3f kg actors=%d\n", total_mass, simulation.get_multibody_state().actor_count + 1);
            check(fabsf(total_mass - simulation.get_config().mass) < 0.01f, "assembled curb mass matches preset");
            bool cold = argc > 5 && std::string(argv[5]) == "cold";
            if (!cold) for (int i = 0; i < 4; ++i) { auto& t = simulation.get_wheel_state(i).thermal; t.core = 40; for (float& v : t.surface) v = 80; }
            printf("tire start=%s\n", cold ? "cold20" : "warm80_core40");
            float time_to_100 = -1.0f;
            for (int i = 0; i < static_cast<int>(8.0f / dt); ++i)
            {
                advance(dt, 1.0f, 0.0f);
                check(simulation.get_motor_power_kw() < simulation.get_spec().electric_motor_power_kw * 1.02f, "motor power limit at actual shaft speed");
                if (time_to_100 < 0 && simulation.get_speed_kmh() >= 100) time_to_100 = (i + 1) * dt;
            }
            printf("launch 0-100=%.3f s final_speed=%.2f km/h\n", time_to_100, simulation.get_speed_kmh());
            check(time_to_100 >= simulation.get_spec().validation.zero_to_100_min
                && time_to_100 <= simulation.get_spec().validation.zero_to_100_max * (cold ? 1.4f : 1.0f), "Ferrari acceleration target");
            simulation.set_manual_transmission(true);
            simulation.set_validation_speed(100.0f / 3.6f);
            const PxVec3 start = simulation.get_body()->getGlobalPose().p;
            float stop_time = -1;
            for (int i = 0; i < static_cast<int>(10.0f / dt); ++i)
            {
                advance(dt, 0.0f, 1.0f);
                if (simulation.get_speed_kmh() < 0.5f)
                {
                    stop_time = (i + 1) * dt;
                    break;
                }
            }
            float distance = (simulation.get_body()->getGlobalPose().p - start).magnitude();
            printf("brake 100-0=%.3f s distance=%.2f m\n", stop_time, distance);
            check(stop_time > 0 && distance >= simulation.get_spec().validation.braking_distance_min
                && distance <= simulation.get_spec().validation.braking_distance_max, "Ferrari stopping distance target");
            printf("end temperatures front_surface=%.1f rear_surface=%.1f rear_core=%.1f front_brake=%.1f C\n",
                simulation.get_wheel_temperature(0), simulation.get_wheel_temperature(2),
                simulation.get_wheel_core_temp(2), simulation.get_wheel_brake_temp(0));
        }
        if (!suspension_only)
        {
            const float handling_dt = argc > 2 ? std::stof(argv[2]) : 0.005f;
            contact_checks(physics, scene, plane, car::load_car_file("worlds/cars/ferrari_laferrari.car")->performance, handling_dt);
            handling_checks(physics, scene, plane, car::load_car_file("worlds/cars/ferrari_laferrari.car")->performance, handling_dt);
        }
        scene->release();
        material->release();
        dispatcher->release();
        PxCloseExtensions();
        physics->release();
        foundation->release();
        return 0;
    }
    catch (const std::exception& error)
    {
        fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
