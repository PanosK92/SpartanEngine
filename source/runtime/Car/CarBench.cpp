/*
Copyright(c) 2015-2026 Panos Karabelas
*/
#include "pch.h"
#include "CarBench.h"
#include "Car.h"
#include "CarSimulation.h"
#include "CarState.h"
#include "../World/Components/Physics.h"
#include "../Physics/PhysicsWorld.h"
#include "../Core/Engine.h"
#include "../Math/Vector3.h"
#include "../Math/Quaternion.h"
#include "../../editor/ImGui/Source/imgui.h"
#include <chrono>
#include <cstdio>
#include <cstring>

namespace spartan::car_bench
{
    using namespace physx;

    namespace
    {
        struct thresholds
        {
            // body sleep threshold is 0.05 m/s, settle must be stricter than a crawl
            float settle_speed_max = 0.05f;
            float settle_drift_max = 0.05f;
            float settle_warmup_s = 1.5f;
            float wheelie_front_load_min = 80.0f;
            float wheelie_speed_max_kmh = 80.0f;
            float coast_axle_max = 50.0f;
            float reverse_min_body_speed = 1.0f;
            float axle_shift_spike = 8000.0f;
            float power_yaw_fail = 3.0f;
            float power_slip_fail = 0.95f;
        };

        struct runner
        {
            ui_state ui{};
            thresholds limits{};
            PxTransform start_pose = PxTransform(PxIdentity);
            math::Vector3 start_position = math::Vector3::Zero;
            math::Quaternion start_rotation = math::Quaternion::Identity;
            bool was_paused = false;
            bool was_mcp = false;
            bool telemetry_was_on = false;
            int phase = 0;
            float phase_time = 0.0f;
            float speed_at_lift = 0.0f;
            float coast_watch_time = 0.0f;
            int gear_at_lift = 2;
            bool settle_anchored = false;
            PxVec3 settle_anchor = PxVec3(0.0f);
            float settle_max_speed = 0.0f;
            float settle_drift = 0.0f;
            bool failed_current = false;
            bool completed_cleanly = false;
            std::chrono::steady_clock::time_point wall_start{};
        };

        runner g_runner;

        const char* hint_for_metric(const char* metric)
        {
            if (!metric)
            {
                return "inspect car simulation";
            }
            if (strcmp(metric, "front_load") == 0)
            {
                return "wheelie: tire long force pitch path / suspension load transfer";
            }
            if (strcmp(metric, "axle_drive_torque") == 0)
            {
                return "powertrain still propels after lift or during shift, check coast clamp and open driveline";
            }
            if (strcmp(metric, "coast_speedup") == 0)
            {
                return "closed throttle still accelerates, check axle torque and residual motor";
            }
            if (strcmp(metric, "reverse_engage") == 0)
            {
                return "reverse engaged while still moving, gate on body speed not forward component";
            }
            if (strcmp(metric, "coast_downshift") == 0)
            {
                return "auto box downshifts on pure lift, hold gear unless braking";
            }
            if (strcmp(metric, "yaw_rate") == 0 || strcmp(metric, "rear_slip") == 0)
            {
                return "power oversteer, check tire force application, lsd, and traction control";
            }
            if (strcmp(metric, "speed") == 0
                || strcmp(metric, "settle_drift") == 0
                || strcmp(metric, "settle_awake") == 0
                || strcmp(metric, "upright") == 0)
            {
                return "settle creep, open driveline when clutch is out, kill residual axle torque at rest";
            }
            return "inspect car simulation around the failing metric";
        }

        const char* files_for_metric(const char* metric)
        {
            if (!metric)
            {
                return "source/runtime/Car/CarSimulationCore.h";
            }
            if (strcmp(metric, "front_load") == 0)
            {
                return "source/runtime/Car/CarSimulationCore.h (apply_tire_forces)";
            }
            if (strcmp(metric, "axle_drive_torque") == 0
                || strcmp(metric, "coast_speedup") == 0)
            {
                return "source/runtime/Car/CarSimulationCore.h (integrate_powertrain)";
            }
            if (strcmp(metric, "reverse_engage") == 0)
            {
                return "source/runtime/Car/CarSimulationCore.h (apply_service_brakes, update_automatic_gearbox)";
            }
            if (strcmp(metric, "coast_downshift") == 0)
            {
                return "source/runtime/Car/CarSimulationCore.h (update_automatic_gearbox)";
            }
            if (strcmp(metric, "yaw_rate") == 0 || strcmp(metric, "rear_slip") == 0)
            {
                return "source/runtime/Car/CarSimulationCore.h (apply_tire_forces, update_assist_controller)";
            }
            if (strcmp(metric, "speed") == 0
                || strcmp(metric, "settle_drift") == 0
                || strcmp(metric, "settle_awake") == 0
                || strcmp(metric, "upright") == 0)
            {
                return "source/runtime/Car/CarSimulationCore.h (integrate_powertrain, vehicle_assembly_is_settled)";
            }
            return "source/runtime/Car/CarSimulationCore.h";
        }

        const char* code_for_metric(const char* metric)
        {
            if (!metric || metric[0] == '\0' || strcmp(metric, "ok") == 0)
            {
                return "pass";
            }
            if (strcmp(metric, "front_load") == 0)
            {
                return "WHEELIE";
            }
            if (strcmp(metric, "axle_drive_torque") == 0)
            {
                return "COAST_DRIVE";
            }
            if (strcmp(metric, "coast_speedup") == 0)
            {
                return "COAST_SPEEDUP";
            }
            if (strcmp(metric, "reverse_engage") == 0)
            {
                return "REVERSE_ENGAGE";
            }
            if (strcmp(metric, "coast_downshift") == 0)
            {
                return "COAST_DOWNSHIFT";
            }
            if (strcmp(metric, "yaw_rate") == 0)
            {
                return "POWER_YAW";
            }
            if (strcmp(metric, "rear_slip") == 0)
            {
                return "POWER_SLIP";
            }
            if (strcmp(metric, "speed") == 0)
            {
                return "SETTLE_SPEED";
            }
            if (strcmp(metric, "settle_drift") == 0)
            {
                return "SETTLE_DRIFT";
            }
            if (strcmp(metric, "settle_awake") == 0)
            {
                return "SETTLE_AWAKE";
            }
            if (strcmp(metric, "upright") == 0)
            {
                return "SETTLE_UPRIGHT";
            }
            return "FAIL";
        }

        void init_enabled_defaults()
        {
            static bool done = false;
            if (done)
            {
                return;
            }
            for (int i = 0; i < static_cast<int>(scenario::count); i++)
            {
                g_runner.ui.enabled[i] = true;
            }
            done = true;
        }

        void set_error(const char* message)
        {
            snprintf(g_runner.ui.error, sizeof(g_runner.ui.error), "%s", message ? message : "");
        }

        void set_status(const char* message)
        {
            snprintf(g_runner.ui.status, sizeof(g_runner.ui.status), "%s", message ? message : "");
        }

        void fail_current(const char* metric, float time, const char* detail)
        {
            if (g_runner.failed_current || g_runner.ui.current_scenario < 0)
            {
                return;
            }
            g_runner.failed_current = true;
            scenario_result& result = g_runner.ui.results[g_runner.ui.current_scenario];
            result.passed = false;
            result.fail_time = time;
            snprintf(result.fail_metric, sizeof(result.fail_metric), "%s", metric ? metric : "");
            snprintf(result.detail, sizeof(result.detail), "%s", detail ? detail : "");
            snprintf(result.fail_code, sizeof(result.fail_code), "%s", code_for_metric(metric));
            snprintf(result.hint, sizeof(result.hint), "%s", hint_for_metric(metric));
            snprintf(result.likely_files, sizeof(result.likely_files), "%s", files_for_metric(metric));
        }

        void json_escape(const char* src, char* dst, int dst_size)
        {
            if (!src || dst_size <= 0)
            {
                return;
            }
            int o = 0;
            for (int i = 0; src[i] != '\0' && o + 2 < dst_size; i++)
            {
                char c = src[i];
                if (c == '"' || c == '\\')
                {
                    dst[o++] = '\\';
                    dst[o++] = c;
                }
                else if (c == '\n' || c == '\r' || c == '\t')
                {
                    dst[o++] = ' ';
                }
                else
                {
                    dst[o++] = c;
                }
            }
            dst[o] = '\0';
        }

        void build_agent_prompt()
        {
            g_runner.ui.pass_count = 0;
            g_runner.ui.fail_count = 0;
            for (int i = 0; i < static_cast<int>(scenario::count); i++)
            {
                const scenario_result& result = g_runner.ui.results[i];
                if (!result.ran)
                {
                    continue;
                }
                if (result.passed)
                {
                    g_runner.ui.pass_count++;
                }
                else
                {
                    g_runner.ui.fail_count++;
                }
            }

            if (g_runner.ui.fail_count <= 0)
            {
                snprintf(
                    g_runner.ui.agent_prompt,
                    sizeof(g_runner.ui.agent_prompt),
                    "Car bench PASS for '%s' (%d scenarios). No action needed. Digest: %s",
                    g_runner.ui.car_name[0] ? g_runner.ui.car_name : "car",
                    g_runner.ui.pass_count,
                    g_runner.ui.digest_path
                );
                return;
            }

            // point the agent at the first failure
            const scenario_result* first_fail = nullptr;
            const char* first_name = "";
            for (int i = 0; i < static_cast<int>(scenario::count); i++)
            {
                if (g_runner.ui.results[i].ran && !g_runner.ui.results[i].passed)
                {
                    first_fail = &g_runner.ui.results[i];
                    first_name = scenario_name(static_cast<scenario>(i));
                    break;
                }
            }
            if (!first_fail)
            {
                g_runner.ui.agent_prompt[0] = '\0';
                return;
            }
            snprintf(
                g_runner.ui.agent_prompt,
                sizeof(g_runner.ui.agent_prompt),
                "Car bench FAIL %d/%d on '%s'. First: %s [%s] %s @t=%.2f (%s). Fix in %s. Read %s then re-run Bench.",
                g_runner.ui.fail_count,
                g_runner.ui.pass_count + g_runner.ui.fail_count,
                g_runner.ui.car_name[0] ? g_runner.ui.car_name : "car",
                first_name,
                first_fail->fail_code,
                first_fail->fail_metric,
                first_fail->fail_time,
                first_fail->detail,
                first_fail->likely_files,
                g_runner.ui.feedback_path
            );
        }

        bool any_wheel_grounded(car::Simulation* sim)
        {
            for (int i = 0; i < car::wheel_count; i++)
            {
                if (sim->get_wheel_state(i).grounded && sim->get_wheel_state(i).tire_load > 1.0f)
                {
                    return true;
                }
            }
            return false;
        }

        float body_speed(car::Simulation* sim)
        {
            PxRigidDynamic* body = sim->get_body();
            return body ? body->getLinearVelocity().magnitude() : 0.0f;
        }

        float yaw_rate(car::Simulation* sim)
        {
            PxRigidDynamic* body = sim->get_body();
            if (!body)
            {
                return 0.0f;
            }
            PxTransform pose = body->getGlobalPose();
            PxVec3 up = pose.q.rotate(PxVec3(0.0f, 1.0f, 0.0f));
            return body->getAngularVelocity().dot(up);
        }

        float front_load(car::Simulation* sim)
        {
            return sim->get_wheel_state(car::front_left).tire_load
                + sim->get_wheel_state(car::front_right).tire_load;
        }

        float rear_slip_max(car::Simulation* sim)
        {
            return PxMax(
                fabsf(sim->get_wheel_state(car::rear_left).slip_ratio),
                fabsf(sim->get_wheel_state(car::rear_right).slip_ratio)
            );
        }

        void apply_input(Physics* physics, float throttle, float brake, float steering, float handbrake)
        {
            physics->SetVehicleThrottle(throttle);
            physics->SetVehicleBrake(brake);
            physics->SetVehicleSteering(steering);
            physics->SetVehicleHandbrake(handbrake);
        }

        bool reset_to_start(Physics* physics, car::Simulation* sim, float initial_speed_ms)
        {
            physics->SetBodyTransform(g_runner.start_position, g_runner.start_rotation, true);
            apply_input(physics, 0.0f, 0.0f, 0.0f, 0.0f);
            if (initial_speed_ms > 0.05f)
            {
                sim->set_validation_speed(initial_speed_ms);
            }
            return any_wheel_grounded(sim) || body_speed(sim) > 0.0f;
        }

        void pump_one_step(Physics* physics, car::Simulation* sim, float dt)
        {
            std::lock_guard<std::recursive_mutex> lock(PhysicsWorld::GetMutex());
            PxScene* scene = static_cast<PxScene*>(PhysicsWorld::GetScene());
            if (!scene || !sim->get_body())
            {
                return;
            }
            sim->clear_force_accumulators();
            sim->tick(dt);
            scene->simulate(dt);
            scene->fetchResults(true);
        }

        void begin_scenario(int index, Physics* physics, car::Simulation* sim)
        {
            g_runner.ui.current_scenario = index;
            g_runner.ui.scenario_time = 0.0f;
            g_runner.phase = 0;
            g_runner.phase_time = 0.0f;
            g_runner.failed_current = false;
            g_runner.speed_at_lift = 0.0f;
            g_runner.coast_watch_time = 0.0f;
            g_runner.gear_at_lift = 2;
            g_runner.settle_anchored = false;
            g_runner.settle_anchor = PxVec3(0.0f);
            g_runner.settle_max_speed = 0.0f;
            g_runner.settle_drift = 0.0f;
            scenario_result& result = g_runner.ui.results[index];
            result = scenario_result{};
            result.ran = true;
            result.passed = true;

            float initial_speed = 0.0f;
            scenario id = static_cast<scenario>(index);
            if (id == scenario::hard_brake)
            {
                initial_speed = 0.0f;
            }
            reset_to_start(physics, sim, initial_speed);
            set_status(scenario_name(id));
        }

        void finish_scenario()
        {
            if (g_runner.ui.current_scenario < 0)
            {
                return;
            }
            scenario_result& result = g_runner.ui.results[g_runner.ui.current_scenario];
            if (!g_runner.failed_current)
            {
                result.passed = true;
                snprintf(result.fail_code, sizeof(result.fail_code), "%s", "PASS");
                snprintf(result.fail_metric, sizeof(result.fail_metric), "%s", "ok");
                snprintf(result.detail, sizeof(result.detail), "%s", "passed");
                snprintf(result.hint, sizeof(result.hint), "%s", "none");
                snprintf(result.likely_files, sizeof(result.likely_files), "%s", "");
            }
            g_runner.ui.current_scenario = -1;
        }

        int next_enabled_scenario(int after)
        {
            for (int i = after + 1; i < static_cast<int>(scenario::count); i++)
            {
                if (g_runner.ui.enabled[i])
                {
                    return i;
                }
            }
            return -1;
        }

        void sample_settle(car::Simulation* sim, float t)
        {
            // ignore suspension bounce after teleport
            if (t < g_runner.limits.settle_warmup_s)
            {
                return;
            }
            PxRigidDynamic* body = sim->get_body();
            if (!body)
            {
                return;
            }
            PxVec3 pos = body->getGlobalPose().p;
            float speed = body_speed(sim);
            float up_y = body->getGlobalPose().q.rotate(PxVec3(0.0f, 1.0f, 0.0f)).y;
            if (!g_runner.settle_anchored)
            {
                g_runner.settle_anchored = true;
                g_runner.settle_anchor = pos;
            }
            PxVec3 delta = pos - g_runner.settle_anchor;
            delta.y = 0.0f;
            g_runner.settle_drift = PxMax(g_runner.settle_drift, delta.magnitude());
            g_runner.settle_max_speed = PxMax(g_runner.settle_max_speed, speed);

            if (speed > g_runner.limits.settle_speed_max)
            {
                char detail[160];
                snprintf(detail, sizeof(detail), "speed=%.3f m/s", speed);
                fail_current("speed", t, detail);
            }
            if (g_runner.settle_drift > g_runner.limits.settle_drift_max)
            {
                char detail[160];
                snprintf(detail, sizeof(detail), "drift=%.3f m", g_runner.settle_drift);
                fail_current("settle_drift", t, detail);
            }
            if (up_y < 0.85f)
            {
                char detail[160];
                snprintf(detail, sizeof(detail), "up_y=%.2f", up_y);
                fail_current("upright", t, detail);
            }
            // last half second must be asleep, otherwise it never really settled
            if (t >= 4.5f && !sim->get_vehicle_sleeping())
            {
                char detail[160];
                snprintf(
                    detail,
                    sizeof(detail),
                    "awake max_spd=%.3f drift=%.3f",
                    g_runner.settle_max_speed,
                    g_runner.settle_drift
                );
                fail_current("settle_awake", t, detail);
            }
        }

        void sample_launch(car::Simulation* sim, float t)
        {
            // skip contact settle after teleport
            if (t < 0.35f)
            {
                return;
            }
            float spd_kmh = body_speed(sim) * 3.6f;
            float front = front_load(sim);
            // launch commands full throttle the whole time
            if (spd_kmh < g_runner.limits.wheelie_speed_max_kmh && front < g_runner.limits.wheelie_front_load_min)
            {
                char detail[160];
                snprintf(detail, sizeof(detail), "front_load=%.0f spd=%.1f", front, spd_kmh);
                fail_current("front_load", t, detail);
            }
        }

        void sample_hard_brake(car::Simulation* sim, float t)
        {
            float speed = body_speed(sim);
            int gear = sim->get_current_gear();
            if (gear == 0 && speed > g_runner.limits.reverse_min_body_speed)
            {
                char detail[160];
                snprintf(detail, sizeof(detail), "gear=R speed=%.2f", speed);
                fail_current("reverse_engage", t, detail);
            }
            if (sim->get_is_shifting() && fabsf(sim->get_axle_drive_torque()) > g_runner.limits.axle_shift_spike)
            {
                char detail[160];
                snprintf(detail, sizeof(detail), "axle=%.0f", sim->get_axle_drive_torque());
                fail_current("axle_drive_torque", t, detail);
            }
        }

        void sample_lift_turn(car::Simulation* sim, float t)
        {
            // wait one coast step, transition frame must not sample powered axle
            if (g_runner.phase == 0 || g_runner.coast_watch_time < 0.02f)
            {
                return;
            }
            int gear = sim->get_current_gear();
            float axle = sim->get_axle_drive_torque();
            if (gear >= 2 && axle > g_runner.limits.coast_axle_max)
            {
                char detail[160];
                snprintf(detail, sizeof(detail), "axle=%.0f gear=%d", axle, gear);
                fail_current("axle_drive_torque", t, detail);
            }
            float speed = body_speed(sim) * 3.6f;
            if (g_runner.coast_watch_time < 1.0f && g_runner.speed_at_lift > 5.0f)
            {
                if (speed > g_runner.speed_at_lift + 0.5f)
                {
                    char detail[160];
                    snprintf(detail, sizeof(detail), "spd %.1f->%.1f", g_runner.speed_at_lift, speed);
                    fail_current("coast_speedup", t, detail);
                }
            }
        }

        void sample_coast_hold(car::Simulation* sim, float t)
        {
            if (g_runner.phase == 0)
            {
                return;
            }
            int gear = sim->get_current_gear();
            if (gear < g_runner.gear_at_lift && gear >= 2)
            {
                char detail[160];
                snprintf(detail, sizeof(detail), "gear %d->%d", g_runner.gear_at_lift, gear);
                fail_current("coast_downshift", t, detail);
            }
        }

        void sample_power_turn(car::Simulation* sim, float t)
        {
            float yaw = fabsf(yaw_rate(sim));
            float slip = rear_slip_max(sim);
            if (yaw > g_runner.limits.power_yaw_fail)
            {
                char detail[160];
                snprintf(detail, sizeof(detail), "yaw=%.2f", yaw);
                fail_current("yaw_rate", t, detail);
            }
            if (slip > g_runner.limits.power_slip_fail)
            {
                char detail[160];
                snprintf(detail, sizeof(detail), "rear_slip=%.2f", slip);
                fail_current("rear_slip", t, detail);
            }
        }

        void apply_scenario_input(scenario id, Physics* physics, car::Simulation* sim, float dt)
        {
            float t = g_runner.ui.scenario_time;
            switch (id)
            {
            case scenario::settle:
                apply_input(physics, 0.0f, 0.0f, 0.0f, 0.0f);
                break;
            case scenario::launch:
                apply_input(physics, 1.0f, 0.0f, 0.0f, 0.0f);
                break;
            case scenario::hard_brake:
                if (g_runner.phase == 0 && t < 3.5f && body_speed(sim) * 3.6f <= 80.0f)
                {
                    apply_input(physics, 1.0f, 0.0f, 0.0f, 0.0f);
                }
                else
                {
                    if (g_runner.phase == 0)
                    {
                        g_runner.phase = 1;
                        g_runner.phase_time = 0.0f;
                    }
                    apply_input(physics, 0.0f, 1.0f, 0.0f, 0.0f);
                }
                break;
            case scenario::lift_turn:
                if (g_runner.phase == 0 && t < 2.0f)
                {
                    apply_input(physics, 1.0f, 0.0f, 0.4f, 0.0f);
                }
                else
                {
                    if (g_runner.phase == 0)
                    {
                        g_runner.phase = 1;
                        g_runner.phase_time = 0.0f;
                        g_runner.speed_at_lift = body_speed(sim) * 3.6f;
                        g_runner.coast_watch_time = 0.0f;
                        g_runner.gear_at_lift = sim->get_current_gear();
                    }
                    apply_input(physics, 0.0f, 0.0f, 0.4f, 0.0f);
                    g_runner.coast_watch_time += dt;
                }
                break;
            case scenario::coast_hold_gear:
                if (g_runner.phase == 0 && t < 2.5f)
                {
                    apply_input(physics, 1.0f, 0.0f, 0.0f, 0.0f);
                }
                else
                {
                    if (g_runner.phase == 0)
                    {
                        g_runner.phase = 1;
                        g_runner.gear_at_lift = sim->get_current_gear();
                    }
                    apply_input(physics, 0.0f, 0.0f, 0.0f, 0.0f);
                }
                break;
            case scenario::power_turn:
                apply_input(physics, 1.0f, 0.0f, 0.5f, 0.0f);
                break;
            default:
                apply_input(physics, 0.0f, 0.0f, 0.0f, 0.0f);
                break;
            }
        }

        void sample_scenario(scenario id, car::Simulation* sim)
        {
            float t = g_runner.ui.scenario_time;
            switch (id)
            {
            case scenario::settle:
                sample_settle(sim, t);
                break;
            case scenario::launch:
                sample_launch(sim, t);
                break;
            case scenario::hard_brake:
                sample_hard_brake(sim, t);
                break;
            case scenario::lift_turn:
                sample_lift_turn(sim, t);
                break;
            case scenario::coast_hold_gear:
                sample_coast_hold(sim, t);
                break;
            case scenario::power_turn:
                sample_power_turn(sim, t);
                break;
            default:
                break;
            }
        }

        bool scenario_complete(scenario id)
        {
            float t = g_runner.ui.scenario_time;
            switch (id)
            {
            case scenario::settle: return t >= 5.0f;
            case scenario::launch: return t >= 4.0f;
            case scenario::hard_brake: return t >= 7.0f;
            case scenario::lift_turn: return t >= 4.0f;
            case scenario::coast_hold_gear: return t >= 5.0f;
            case scenario::power_turn: return t >= 3.0f;
            default: return true;
            }
        }
    }

    ui_state& get_state()
    {
        init_enabled_defaults();
        return g_runner.ui;
    }

    const char* scenario_name(scenario id)
    {
        switch (id)
        {
        case scenario::settle: return "settle";
        case scenario::launch: return "launch";
        case scenario::hard_brake: return "hard_brake";
        case scenario::lift_turn: return "lift_turn";
        case scenario::coast_hold_gear: return "coast_hold_gear";
        case scenario::power_turn: return "power_turn";
        default: return "unknown";
        }
    }

    void open_window()
    {
        init_enabled_defaults();
        g_runner.ui.window_open = true;
    }

    void start(Car* car, Physics* physics)
    {
        init_enabled_defaults();
        set_error("");
        if (!car || !physics)
        {
            set_error("need active car");
            return;
        }
        car::Simulation* sim = physics->GetVehicleSimulation();
        if (!sim || !sim->get_body())
        {
            set_error("need vehicle simulation");
            return;
        }
        if (!any_wheel_grounded(sim))
        {
            set_error("need grounded car");
            return;
        }

        int first = next_enabled_scenario(-1);
        if (first < 0)
        {
            set_error("enable at least one scenario");
            return;
        }

        g_runner.start_pose = sim->get_body()->getGlobalPose();
        g_runner.start_position = math::Vector3(
            g_runner.start_pose.p.x,
            g_runner.start_pose.p.y,
            g_runner.start_pose.p.z
        );
        g_runner.start_rotation = math::Quaternion(
            g_runner.start_pose.q.x,
            g_runner.start_pose.q.y,
            g_runner.start_pose.q.z,
            g_runner.start_pose.q.w
        );

        for (int i = 0; i < static_cast<int>(scenario::count); i++)
        {
            g_runner.ui.results[i] = scenario_result{};
        }

        snprintf(g_runner.ui.car_name, sizeof(g_runner.ui.car_name), "%s", sim->get_spec().name ? sim->get_spec().name : "car");
        snprintf(g_runner.ui.digest_path, sizeof(g_runner.ui.digest_path), "%s", "car_bench_digest.json");
        snprintf(g_runner.ui.feedback_path, sizeof(g_runner.ui.feedback_path), "%s", "car_bench_feedback.md");
        g_runner.ui.agent_prompt[0] = '\0';
        g_runner.ui.pass_count = 0;
        g_runner.ui.fail_count = 0;
        g_runner.completed_cleanly = false;

        g_runner.was_paused = Engine::IsFlagSet(EngineMode::Paused);
        g_runner.was_mcp = car->IsMcpControlled();
        g_runner.telemetry_was_on = sim->get_log_to_file();
        Engine::SetFlag(EngineMode::Paused, true);
        car->SetMcpControlled(true);
        if (g_runner.ui.write_telemetry)
        {
            sim->set_telemetry_path("car_bench_telemetry.csv");
            sim->set_log_to_file(true);
        }
        else
        {
            sim->set_log_to_file(false);
        }

        g_runner.ui.running = true;
        g_runner.ui.stop_requested = false;
        g_runner.ui.sim_time_total = 0.0f;
        g_runner.ui.wall_time_total = 0.0f;
        g_runner.wall_start = std::chrono::steady_clock::now();
        begin_scenario(first, physics, sim);
    }

    void stop(Car* car, Physics* physics)
    {
        if (!g_runner.ui.running)
        {
            return;
        }
        g_runner.ui.running = false;
        g_runner.ui.stop_requested = false;
        g_runner.ui.current_scenario = -1;

        if (physics)
        {
            apply_input(physics, 0.0f, 0.0f, 0.0f, 0.0f);
            car::Simulation* sim = physics->GetVehicleSimulation();
            if (sim)
            {
                sim->set_log_to_file(g_runner.telemetry_was_on);
                physics->SetBodyTransform(g_runner.start_position, g_runner.start_rotation, true);
            }
        }
        if (car)
        {
            car->SetMcpControlled(g_runner.was_mcp);
        }
        Engine::SetFlag(EngineMode::Paused, g_runner.was_paused);

        auto wall_end = std::chrono::steady_clock::now();
        g_runner.ui.wall_time_total = std::chrono::duration<float>(wall_end - g_runner.wall_start).count();

        write_agent_digest();
        if (g_runner.completed_cleanly)
        {
            set_status(g_runner.ui.fail_count > 0 ? "done, see feedback for agent" : "all passed");
        }
        else
        {
            set_status("stopped, digest written");
        }
    }

    void tick(Car* car, Physics* physics)
    {
        if (!g_runner.ui.running || !car || !physics)
        {
            return;
        }
        if (g_runner.ui.stop_requested)
        {
            stop(car, physics);
            return;
        }

        car::Simulation* sim = physics->GetVehicleSimulation();
        if (!sim || !sim->get_body())
        {
            set_error("lost vehicle simulation");
            stop(car, physics);
            return;
        }

        const float dt = PhysicsWorld::GetFixedTimeStep();
        constexpr int steps_per_frame = 64;
        for (int step = 0; step < steps_per_frame && g_runner.ui.running; step++)
        {
            if (g_runner.ui.current_scenario < 0)
            {
                int next = next_enabled_scenario(-1);
                if (next < 0)
                {
                    g_runner.completed_cleanly = true;
                    stop(car, physics);
                    return;
                }
                begin_scenario(next, physics, sim);
            }

            scenario id = static_cast<scenario>(g_runner.ui.current_scenario);
            apply_scenario_input(id, physics, sim, dt);
            pump_one_step(physics, sim, dt);
            g_runner.phase_time += dt;
            g_runner.ui.scenario_time += dt;
            g_runner.ui.sim_time_total += dt;
            sample_scenario(id, sim);

            if (scenario_complete(id) || g_runner.failed_current)
            {
                int finished = g_runner.ui.current_scenario;
                finish_scenario();
                int next = next_enabled_scenario(finished);
                if (next < 0)
                {
                    g_runner.completed_cleanly = true;
                    stop(car, physics);
                    return;
                }
                begin_scenario(next, physics, sim);
            }
        }

        auto now = std::chrono::steady_clock::now();
        g_runner.ui.wall_time_total = std::chrono::duration<float>(now - g_runner.wall_start).count();
    }

    void write_agent_digest()
    {
        snprintf(g_runner.ui.digest_path, sizeof(g_runner.ui.digest_path), "%s", "car_bench_digest.json");
        snprintf(g_runner.ui.feedback_path, sizeof(g_runner.ui.feedback_path), "%s", "car_bench_feedback.md");
        build_agent_prompt();

        char esc_car[128];
        char esc_prompt[640];
        json_escape(g_runner.ui.car_name, esc_car, sizeof(esc_car));
        json_escape(g_runner.ui.agent_prompt, esc_prompt, sizeof(esc_prompt));

        FILE* json = nullptr;
        fopen_s(&json, g_runner.ui.digest_path, "w");
        if (!json)
        {
            set_error("could not write car_bench_digest.json");
            return;
        }

        float speedup = g_runner.ui.wall_time_total > 0.001f
            ? g_runner.ui.sim_time_total / g_runner.ui.wall_time_total
            : 0.0f;

        fprintf(json, "{\n");
        fprintf(json, "  \"tool\": \"car_bench\",\n");
        fprintf(json, "  \"car\": \"%s\",\n", esc_car);
        fprintf(json, "  \"pass_count\": %d,\n", g_runner.ui.pass_count);
        fprintf(json, "  \"fail_count\": %d,\n", g_runner.ui.fail_count);
        fprintf(json, "  \"sim_time_s\": %.4f,\n", g_runner.ui.sim_time_total);
        fprintf(json, "  \"wall_time_s\": %.4f,\n", g_runner.ui.wall_time_total);
        fprintf(json, "  \"speedup\": %.3f,\n", speedup);
        fprintf(json, "  \"completed\": %s,\n", g_runner.completed_cleanly ? "true" : "false");
        fprintf(json, "  \"agent_prompt\": \"%s\",\n", esc_prompt);
        fprintf(json, "  \"scenarios\": [\n");
        for (int i = 0; i < static_cast<int>(scenario::count); i++)
        {
            const scenario_result& result = g_runner.ui.results[i];
            char esc_metric[96];
            char esc_detail[200];
            char esc_hint[240];
            char esc_files[240];
            char esc_code[64];
            json_escape(result.fail_metric, esc_metric, sizeof(esc_metric));
            json_escape(result.detail, esc_detail, sizeof(esc_detail));
            json_escape(result.hint, esc_hint, sizeof(esc_hint));
            json_escape(result.likely_files, esc_files, sizeof(esc_files));
            json_escape(result.fail_code, esc_code, sizeof(esc_code));
            fprintf(json, "    {\n");
            fprintf(json, "      \"name\": \"%s\",\n", scenario_name(static_cast<scenario>(i)));
            fprintf(json, "      \"enabled\": %s,\n", g_runner.ui.enabled[i] ? "true" : "false");
            fprintf(json, "      \"ran\": %s,\n", result.ran ? "true" : "false");
            fprintf(json, "      \"passed\": %s,\n", result.passed ? "true" : "false");
            fprintf(json, "      \"fail_code\": \"%s\",\n", esc_code);
            fprintf(json, "      \"fail_metric\": \"%s\",\n", esc_metric);
            fprintf(json, "      \"fail_time\": %.4f,\n", result.fail_time);
            fprintf(json, "      \"detail\": \"%s\",\n", esc_detail);
            fprintf(json, "      \"hint\": \"%s\",\n", esc_hint);
            fprintf(json, "      \"likely_files\": \"%s\"\n", esc_files);
            fprintf(json, "    }%s\n", i + 1 < static_cast<int>(scenario::count) ? "," : "");
        }
        fprintf(json, "  ],\n");
        fprintf(json, "  \"next_action\": \"%s\"\n", g_runner.ui.fail_count > 0
            ? "fix first failing scenario using hint and likely_files, rebuild, re-run Bench"
            : "none");
        fprintf(json, "}\n");
        fclose(json);

        FILE* md = nullptr;
        fopen_s(&md, g_runner.ui.feedback_path, "w");
        if (!md)
        {
            set_error("could not write car_bench_feedback.md");
            return;
        }
        fprintf(md, "# Car Bench Feedback\n\n");
        fprintf(md, "Paste this file into the agent chat after a Bench run.\n\n");
        fprintf(md, "- **car:** %s\n", g_runner.ui.car_name[0] ? g_runner.ui.car_name : "car");
        fprintf(md, "- **result:** %d pass / %d fail\n", g_runner.ui.pass_count, g_runner.ui.fail_count);
        fprintf(md, "- **sim_time:** %.2fs  **wall:** %.2fs  **speedup:** %.1fx\n",
            g_runner.ui.sim_time_total, g_runner.ui.wall_time_total, speedup);
        fprintf(md, "- **digest:** `%s`\n\n", g_runner.ui.digest_path);
        fprintf(md, "## Agent prompt\n\n%s\n\n", g_runner.ui.agent_prompt);
        fprintf(md, "## Failures (fix in this order)\n\n");
        int fail_index = 0;
        for (int i = 0; i < static_cast<int>(scenario::count); i++)
        {
            const scenario_result& result = g_runner.ui.results[i];
            if (!result.ran || result.passed)
            {
                continue;
            }
            fail_index++;
            fprintf(md, "%d. **%s** `%s`\n", fail_index, scenario_name(static_cast<scenario>(i)), result.fail_code);
            fprintf(md, "   - metric: `%s` @ t=%.3fs\n", result.fail_metric, result.fail_time);
            fprintf(md, "   - detail: %s\n", result.detail);
            fprintf(md, "   - hint: %s\n", result.hint);
            fprintf(md, "   - files: `%s`\n\n", result.likely_files);
        }
        if (fail_index == 0)
        {
            fprintf(md, "_none_\n\n");
        }
        fprintf(md, "## Loop\n\n");
        fprintf(md, "1. Fix the first failure above\n");
        fprintf(md, "2. Rebuild\n");
        fprintf(md, "3. Telemetry -> Bench -> Run\n");
        fprintf(md, "4. Paste this file again\n");
        fclose(md);

        // also keep a simple csv for spreadsheets
        FILE* csv = nullptr;
        fopen_s(&csv, "car_bench_report.csv", "w");
        if (csv)
        {
            fprintf(csv, "scenario,ran,passed,fail_code,fail_time,fail_metric,detail,hint,likely_files\n");
            for (int i = 0; i < static_cast<int>(scenario::count); i++)
            {
                const scenario_result& result = g_runner.ui.results[i];
                fprintf(
                    csv,
                    "%s,%d,%d,%s,%.4f,%s,%s,%s,%s\n",
                    scenario_name(static_cast<scenario>(i)),
                    result.ran ? 1 : 0,
                    result.passed ? 1 : 0,
                    result.fail_code,
                    result.fail_time,
                    result.fail_metric,
                    result.detail,
                    result.hint,
                    result.likely_files
                );
            }
            fclose(csv);
        }
    }

    void export_report()
    {
        write_agent_digest();
        set_status("wrote car_bench_digest.json + car_bench_feedback.md");
    }

    void draw_window(Car* car, Physics* physics)
    {
        init_enabled_defaults();
        if (!g_runner.ui.window_open)
        {
            return;
        }

        tick(car, physics);

        ImGui::SetNextWindowSize(ImVec2(520.0f, 560.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Car Bench", &g_runner.ui.window_open))
        {
            ImGui::End();
            if (!g_runner.ui.window_open && g_runner.ui.running)
            {
                stop(car, physics);
            }
            return;
        }

        ImGui::TextUnformatted("fast stress test, auto-writes agent feedback when finished");
        ImGui::Separator();

        for (int i = 0; i < static_cast<int>(scenario::count); i++)
        {
            ImGui::Checkbox(scenario_name(static_cast<scenario>(i)), &g_runner.ui.enabled[i]);
        }

        ImGui::Checkbox("write telemetry csv", &g_runner.ui.write_telemetry);
        ImGui::Separator();

        if (!g_runner.ui.running)
        {
            if (ImGui::Button("Run", ImVec2(120.0f, 0.0f)))
            {
                start(car, physics);
            }
        }
        else
        {
            if (ImGui::Button("Stop", ImVec2(120.0f, 0.0f)))
            {
                g_runner.ui.stop_requested = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Write feedback now"))
        {
            export_report();
        }

        if (g_runner.ui.error[0] != '\0')
        {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", g_runner.ui.error);
        }
        if (g_runner.ui.status[0] != '\0')
        {
            ImGui::Text("status: %s", g_runner.ui.status);
        }

        float speedup = g_runner.ui.wall_time_total > 0.001f
            ? g_runner.ui.sim_time_total / g_runner.ui.wall_time_total
            : 0.0f;
        ImGui::Text(
            "sim %.2fs  wall %.2fs  speedup %.1fx   %d pass / %d fail",
            g_runner.ui.sim_time_total,
            g_runner.ui.wall_time_total,
            speedup,
            g_runner.ui.pass_count,
            g_runner.ui.fail_count
        );
        if (g_runner.ui.current_scenario >= 0)
        {
            ImGui::Text(
                "current: %s  t=%.2f",
                scenario_name(static_cast<scenario>(g_runner.ui.current_scenario)),
                g_runner.ui.scenario_time
            );
        }

        if (g_runner.ui.feedback_path[0] != '\0')
        {
            ImGui::Separator();
            ImGui::TextWrapped("agent loop: paste `%s` into chat", g_runner.ui.feedback_path);
            ImGui::Text("json: %s", g_runner.ui.digest_path);
            if (g_runner.ui.agent_prompt[0] != '\0')
            {
                ImGui::Spacing();
                ImGui::TextWrapped("%s", g_runner.ui.agent_prompt);
            }
        }

        ImGui::Separator();
        if (ImGui::BeginTable("##bench_results", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("scenario");
            ImGui::TableSetupColumn("result");
            ImGui::TableSetupColumn("code");
            ImGui::TableSetupColumn("metric");
            ImGui::TableSetupColumn("hint");
            ImGui::TableHeadersRow();
            for (int i = 0; i < static_cast<int>(scenario::count); i++)
            {
                const scenario_result& result = g_runner.ui.results[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(scenario_name(static_cast<scenario>(i)));
                ImGui::TableNextColumn();
                if (!result.ran)
                {
                    ImGui::TextUnformatted("-");
                }
                else if (result.passed)
                {
                    ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.4f, 1.0f), "pass");
                }
                else
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "fail @%.2f", result.fail_time);
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(result.fail_code);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(result.fail_metric);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(result.hint);
            }
            ImGui::EndTable();
        }

        ImGui::End();

        if (!g_runner.ui.window_open && g_runner.ui.running)
        {
            stop(car, physics);
        }
    }
}
