/*
Copyright(c) 2015-2026 Panos Karabelas

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

// Exercises the same hub fixture used by the editor; no duplicate torque model.
void dyno_checks(PxPhysics* physics, PxScene* scene)
{
    car::load_car_directory("binaries/project/cars");
    const auto directory = std::filesystem::current_path();
    std::filesystem::current_path("binaries/car_tests");
    for (const auto& entry : car::preset_registry)
    {
        float peaks[2] = {};
        for (int rate = 0; rate < 2; ++rate)
        {
            car::Simulation sim;
            sim.get_spec() = *entry.instance;
            car::setup_params params;
            params.physics = physics; params.scene = scene;
            params.create_mechanisms = rate == 0;
            check(sim.setup(params), "dyno fixture setup");
            sim.mount_dyno(true);
            check(sim.get_body()->getActorFlags().isSet(PxActorFlag::eDISABLE_SIMULATION), "dyno secures chassis");
            for (int i = 0; i < sim.get_multibody_state().actor_count; ++i)
                check(sim.get_multibody_state().actors[i]->getActorFlags().isSet(PxActorFlag::eDISABLE_SIMULATION), "dyno secures all mechanism actors");
            sim.dyno.sweep_seconds = 4;
            sim.dyno.start_rpm = std::max(1800.0f, sim.get_spec().engine_idle_rpm * 2);
            sim.dyno.end_rpm = sim.get_spec().engine_redline_rpm - 150;
            sim.dyno.gear = 2;
            check(sim.start_dyno(), "dyno starts sweep");
            const auto pose = sim.get_body()->getGlobalPose();
            const float dt = rate == 0 ? 0.005f : 0.0025f;
            for (int i = 0; i < 3000 && sim.dyno.running; ++i)
            {
                sim.tick(dt); scene->simulate(dt); scene->fetchResults(true);
            }
            check(!sim.dyno.running && sim.dyno.time >= 6, "dyno sweep completes");
            check(sim.dyno.samples.size() >= 299 && sim.dyno.samples.size() <= 301, "dyno captures at 50 Hz");
            check((sim.get_body()->getGlobalPose().p - pose.p).magnitude() < 1e-6f, "dyno chassis remains secured");
            check(!sim.dyno.export_path.empty() && std::filesystem::file_size(sim.dyno.export_path) > 1000, "dyno automatically exports run");
            for (const auto& s : sim.dyno.samples)
            {
                check(std::isfinite(s.rpm) && std::isfinite(s.axle_nm) && std::isfinite(s.motor_kw), "dyno finite outputs");
                check(fabsf(s.axle_kw - s.axle_nm * s.wheel_rpm * PxTwoPi / 60000) < 0.01f, "hub power uses hub speed, not crank speed");
                check(fabsf(s.wheel_rpm * sim.dyno.run_ratio * sim.dyno.run_final_drive - s.target_rpm) < 0.02f, "dyno speed respects gearing");
                check(s.battery_soc >= -0.0001f && s.battery_soc <= 1.0001f, "dyno battery energy bounded");
                if (s.time >= 2) peaks[rate] = std::max(peaks[rate], s.axle_kw);
            }
            check(peaks[rate] > 1, "dyno measures positive shaft output");
            const auto first = sim.dyno.samples;
            check(sim.start_dyno(), "dyno repeats run");
            while (sim.dyno.running) sim.tick(dt);
            check(first.size() == sim.dyno.samples.size(), "repeatable sample count");
            for (size_t i = 0; i < first.size(); ++i)
                check(fabsf(first[i].axle_kw - sim.dyno.samples[i].axle_kw) < 0.001f, "repeatable power and battery reset");
            sim.dyno.sweep = false; sim.dyno.gear = sim.get_spec().gear_count - 1;
            check(sim.start_dyno(), "hold starts in highest gear");
            for (int i = 0; i < 600; ++i) sim.tick(dt);
            check(fabsf(sim.dyno.samples.back().target_rpm - sim.dyno.start_rpm) < 0.01f, "hold keeps shaft speed fixed");
            sim.stop_dyno();
            check(!sim.dyno.running && sim.get_wheel_state(2).angular_velocity == 0, "stop removes shaft drive");
            const auto stopped_count = sim.dyno.samples.size();
            sim.tick(dt);
            check(sim.dyno.samples.size() == stopped_count, "stopped run does not keep recording");
            sim.dyno.start_rpm = std::numeric_limits<float>::quiet_NaN();
            check(!sim.start_dyno(), "rejects non-finite settings");
            sim.mount_dyno(false);
            check(!sim.get_body()->getActorFlags().isSet(PxActorFlag::eDISABLE_SIMULATION), "unmount restores physics");
        }
        check(fabsf(peaks[0] - peaks[1]) < std::max(1.0f, peaks[0] * 0.02f), "dyno converges across scene timestep and mechanism presence");
        printf("DYNO PASS %s: peak axle %.2f / %.2f kW (200 / 400 Hz)\n", entry.name, peaks[0], peaks[1]);
    }
    std::filesystem::current_path(directory);
}
