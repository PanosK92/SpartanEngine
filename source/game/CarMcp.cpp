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
#include "CarMcp.h"
#include "mcp/McpCommands.h"
#include "mcp/McpCommandsCommon.h"
#include "car/AiDriver.h"
#include "car/Car.h"
#include "car/CarSimulation.h"
#include "car/CarEngineSoundSynthesis.h"
#include "car/RacingLine.h"
#include "car/CarState.h"
#include "world/World.h"
#include "world/Entity.h"
#include "world/components/Physics.h"
#include "components/RaceDriver.h"
#include "world/components/Camera.h"
#include "rendering/Renderer.h"
#include "physics/PhysicsWorld.h"
#include "world/components/Render.h"
#include "core/ProgressTracker.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace spartan
{
    namespace
    {
        using namespace mcp_common;
        const char* car_view_to_name(CarView view)
        {
            switch (view)
            {
            case CarView::Chase: return "chase";
            case CarView::Hood:  return "hood";
            case CarView::Wheel: return "wheel";
            }
            return "chase";
        }

        bool car_view_from_name(const std::string& name, CarView& view)
        {
            const std::string lower = to_lower_copy(name);
            if (lower == "chase")
            {
                view = CarView::Chase;
                return true;
            }
            if (lower == "hood")
            {
                view = CarView::Hood;
                return true;
            }
            if (lower == "wheel")
            {
                view = CarView::Wheel;
                return true;
            }
            return false;
        }

        Car* find_car_from_request(const McpRequest& request, std::string& error)
        {
            const std::optional<std::string> id_arg = get_argument(request, "id");
            if (id_arg)
            {
                Entity* entity = get_entity_from_request(request, error);
                if (entity == nullptr)
                {
                    return nullptr;
                }

                for (Car* car : Car::GetAll())
                {
                    if (car == nullptr)
                    {
                        continue;
                    }
                    Entity* root = car->GetRootEntity();
                    Entity* body = car->GetBodyEntity();
                    if (root == entity || body == entity)
                    {
                        return car;
                    }
                    // prefab worlds parent the vehicle under an entity like player_car
                    if (root && root->GetParent() == entity)
                    {
                        return car;
                    }
                }

                error = "entity is not a drivable car";
                return nullptr;
            }

            Car* occupied = nullptr;
            Car* first_drivable = nullptr;
            int drivable_count = 0;
            for (Car* car : Car::GetAll())
            {
                if (car == nullptr || !car->IsDrivable() || !car->GetRootEntity())
                {
                    continue;
                }
                if (car->IsOccupied())
                {
                    if (occupied != nullptr)
                    {
                        error = "multiple occupied cars, pass id";
                        return nullptr;
                    }
                    occupied = car;
                }
                drivable_count++;
                if (first_drivable == nullptr)
                {
                    first_drivable = car;
                }
            }

            if (occupied != nullptr)
            {
                return occupied;
            }
            if (drivable_count == 1)
            {
                return first_drivable;
            }
            if (drivable_count == 0)
            {
                error = "no cars in world";
                return nullptr;
            }

            error = "multiple cars, pass id";
            return nullptr;
        }

        constexpr float tire_psi_per_bar = 14.503774f;

        std::string car_status_json(Car* car)
        {
            Entity* root = car->GetRootEntity();
            Physics* physics = root ? root->GetComponent<Physics>() : nullptr;

            std::string json = "{\"ok\":true";
            if (root)
            {
                json += ",\"entity\":" + entity_to_json_compact(root);
            }
            json += ",\"occupied\":" + json_bool(car->IsOccupied()); json += ",\"spectated\":" + json_bool(car->IsSpectated());
            uint32_t decal_count = 0;
            uint32_t scratch_count = 0;
            std::string scratches = "[";
            if (root)
            {
                std::vector<Entity*> receivers;
                root->GetDescendants(&receivers);
                receivers.push_back(root);
                for (Entity* receiver : receivers)
                    if (Render* render = receiver->GetComponent<Render>())
                    {
                        decal_count += render->GetDecalCount();
                        for (const auto& decal : render->GetDecals())
                        {
                            if (decal.parameters.kind != 1) continue;
                            if (scratch_count++) scratches += ",";
                            const auto projector = decal.parameters.world_to_decal.Inverted();
                            const auto position = receiver->GetMatrix() * (projector * math::Vector3::Zero);
                            const auto normal = (receiver->GetMatrix() * (projector * math::Vector3::Forward) - position).Normalized();
                            scratches += "{\"receiver_id\":" + json_string(std::to_string(receiver->GetObjectId()));
                            scratches += ",\"receiver_name\":" + json_string(receiver->GetObjectName());
                            scratches += ",\"position\":" + json_vector3(position);
                            scratches += ",\"normal\":" + json_vector3(normal) + "}";
                        }
                    }
            }
            json += ",\"decal_count\":" + std::to_string(decal_count);
            json += ",\"scratch_count\":" + std::to_string(scratch_count);
            json += ",\"scratches\":" + scratches + "]";
            json += ",\"mcp_controlled\":" + json_bool(car->IsExternallyControlled());
            json += ",\"view\":" + json_string(car_view_to_name(car->GetCurrentView()));
            json += ",\"show_telemetry\":" + json_bool(car->GetShowTelemetry());
            json += ",\"playing\":" + json_bool(Engine::IsFlagSet(EngineMode::Playing));
            if (physics && physics->GetBodyType() == BodyType::Custom)
            {
                const math::Vector3 velocity = physics->GetLinearVelocity();
                json += ",\"throttle\":" + std::to_string(CarPhysics::Get(*physics).GetVehicleThrottle());
                json += ",\"brake\":" + std::to_string(CarPhysics::Get(*physics).GetVehicleBrake());
                json += ",\"steering\":" + std::to_string(CarPhysics::Get(*physics).GetVehicleSteering());
                json += ",\"handbrake\":" + std::to_string(CarPhysics::Get(*physics).GetVehicleHandbrake());
                json += ",\"gear\":" + json_string(CarPhysics::Get(*physics).GetCurrentGearString());
                json += ",\"manual_shifting\":" + json_bool(CarPhysics::Get(*physics).GetManualTransmission());
                json += ",\"engine_rpm\":" + std::to_string(CarPhysics::Get(*physics).GetEngineRPM());
                json += ",\"speed_kmh\":" + std::to_string(velocity.Length() * 3.6f);
                json += ",\"position\":" + json_vector3(root->GetPosition());
                json += ",\"linear_velocity\":" + json_vector3(velocity);
                json += ",\"abs_active\":" + json_bool(CarPhysics::Get(*physics).IsAbsActiveAny());
                json += ",\"tc_active\":" + json_bool(CarPhysics::Get(*physics).IsTcActive());
                json += ",\"tire_pressure_bar\":" + std::to_string(CarPhysics::Get(*physics).GetTirePressure());
                json += ",\"tire_pressure_psi\":" + std::to_string(CarPhysics::Get(*physics).GetTirePressure() * tire_psi_per_bar);
                json += ",\"tire_pressure_optimal_bar\":" + std::to_string(CarPhysics::Get(*physics).GetTirePressureOptimal());
            }
            if (const AiDriver* driver = car->GetAiDriver())
            {
                const AiDriverStats& stats = driver->GetStats();
                json += ",\"ai_driver\":{\"track_id\":" + json_string(std::to_string(driver->GetLine()->GetTrackEntityId()));
                json += ",\"skill\":" + std::to_string(driver->GetSettings().skill);
                json += ",\"lap\":" + std::to_string(stats.lap) + ",\"resets\":" + std::to_string(stats.resets);
                json += ",\"lap_time\":" + std::to_string(stats.lap_time) + ",\"last_lap_time\":" + std::to_string(stats.last_lap_time);
                json += ",\"best_lap_time\":" + std::to_string(stats.best_lap_time) + ",\"ideal_lap\":" + std::to_string(stats.ideal_lap);
                json += ",\"distance\":" + std::to_string(stats.distance) + ",\"travelled\":" + std::to_string(stats.travelled) + ",\"line_error\":" + std::to_string(stats.line_error);
                json += ",\"target_kmh\":" + std::to_string(stats.target_kmh) + "}";
            }
            else
            {
                json += ",\"ai_driver\":null";
            }
            json += "}";
            return json;
        }

        // the racing line of a spline road, shared with a race on that road or with earlier drivers so every car races the same line
        std::shared_ptr<RacingLine> find_racing_line(const std::optional<std::string>& track, std::string& error)
        {
            static std::unordered_map<uint64_t, std::weak_ptr<RacingLine>> built_lines;

            Entity* track_entity = nullptr;
            if (track)
            {
                uint64_t id = 0;
                if (parse_uint64(*track, id))
                {
                    track_entity = World::GetEntityById(id);
                }
                else
                {
                    track_entity = find_entity_by_name_unique(*track, true, error);
                    if (!track_entity)
                    {
                        track_entity = find_entity_by_name_unique(*track, false, error);
                    }
                }
                if (!track_entity)
                {
                    error = "track not found";
                    return nullptr;
                }
            }

            for (Entity* entity : World::GetEntities())
            {
                RaceDriver* race = entity ? entity->GetComponent<RaceDriver>() : nullptr;
                if (race && race->GetRacingLine() && (!track_entity || race->GetRacingLine()->GetTrackEntityId() == track_entity->GetObjectId()))
                {
                    return race->GetRacingLine();
                }
            }
            if (!track_entity)
            {
                error = "no race is running, pass track with a spline road id or name";
                return nullptr;
            }
            if (std::shared_ptr<RacingLine> line = built_lines[track_entity->GetObjectId()].lock())
            {
                return line;
            }
            std::shared_ptr<RacingLine> line = RacingLine::Build(track_entity);
            if (!line)
            {
                error = "track is not a spline road with at least three control points";
                return nullptr;
            }
            built_lines[track_entity->GetObjectId()] = line;
            return line;
        }

        std::string command_vehicle_ai(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!Engine::IsFlagSet(EngineMode::Playing))
            {
                return json_error("vehicle ai requires play mode");
            }

            std::string error;
            Car* car = find_car_from_request(request, error);
            if (car == nullptr)
            {
                return json_error(error);
            }

            bool stop = false;
            if (const std::optional<std::string> value = get_argument(request, "stop"))
            {
                if (!parse_bool(*value, stop))
                {
                    return json_error("invalid stop");
                }
            }
            if (stop)
            {
                car->SetAiDriver(nullptr);
                return car_status_json(car);
            }

            std::shared_ptr<RacingLine> line = find_racing_line(get_argument(request, "track"), error);
            if (!line)
            {
                return json_error(error);
            }

            AiDriverSettings settings;
            const std::pair<const char*, float*> floats[] = { { "skill", &settings.skill }, { "max_speed", &settings.max_speed }, { "launch_delay", &settings.launch_delay } };
            for (const auto& [name, target] : floats)
            {
                if (const std::optional<std::string> value = get_argument(request, name))
                {
                    if (!parse_float(*value, *target))
                    {
                        return json_error(std::string("invalid ") + name);
                    }
                }
            }
            const std::pair<const char*, bool*> flags[] = { { "learning", &settings.learning }, { "verbose", &settings.verbose } };
            for (const auto& [name, target] : flags)
            {
                if (const std::optional<std::string> value = get_argument(request, name))
                {
                    if (!parse_bool(*value, *target))
                    {
                        return json_error(std::string("invalid ") + name);
                    }
                }
            }

            car->SetAiDriver(std::make_unique<AiDriver>(line, settings));
            return car_status_json(car);
        }

        std::string command_vehicle_set_tire_pressure(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            std::string error;
            Car* car = find_car_from_request(request, error);
            if (car == nullptr)
            {
                return json_error(error);
            }
            Physics* physics = car->GetRootEntity() ? car->GetRootEntity()->GetComponent<Physics>() : nullptr;
            ::car::Simulation* simulation = physics ? CarPhysics::Get(*physics).GetVehicleSimulation() : nullptr;
            if (!simulation)
            {
                return json_error("vehicle has no simulation, enter play mode first");
            }

            float bar = 0.0f;
            const std::optional<std::string> psi_arg = get_argument(request, "psi");
            const std::optional<std::string> bar_arg = get_argument(request, "bar");
            if (psi_arg.has_value() == bar_arg.has_value())
            {
                return json_error("pass exactly one of psi or bar");
            }
            if (!parse_float(psi_arg ? *psi_arg : *bar_arg, bar) || !std::isfinite(bar) || bar <= 0.0f)
            {
                return json_error("pressure must be a positive number");
            }
            if (psi_arg)
            {
                bar /= tire_psi_per_bar;
            }

            simulation->set_tire_pressure(bar);
            return car_status_json(car);
        }

        std::string command_vehicle_list()
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            std::string json = "{\"ok\":true,\"cars\":[";
            bool first = true;
            for (Car* car : Car::GetAll())
            {
                if (car == nullptr || !car->IsDrivable() || car->GetRootEntity() == nullptr)
                {
                    continue;
                }
                if (!first)
                {
                    json += ",";
                }
                first = false;

                Entity* root = car->GetRootEntity();
                Physics* physics = root->GetComponent<Physics>();
                Entity* parent = root->GetParent();
                json += "{";
                json += "\"id\":" + json_string(std::to_string(root->GetObjectId()));
                json += ",\"name\":" + json_string(root->GetObjectName());
                if (parent)
                {
                    json += ",\"parent_id\":" + json_string(std::to_string(parent->GetObjectId()));
                    json += ",\"parent_name\":" + json_string(parent->GetObjectName());
                }
                json += ",\"occupied\":" + json_bool(car->IsOccupied()); json += ",\"spectated\":" + json_bool(car->IsSpectated());
                json += ",\"mcp_controlled\":" + json_bool(car->IsExternallyControlled());
                json += ",\"view\":" + json_string(car_view_to_name(car->GetCurrentView()));
                json += ",\"position\":" + json_vector3(root->GetPosition());
                if (physics)
                {
                    json += ",\"speed_kmh\":" + std::to_string(physics->GetLinearVelocity().Length() * 3.6f);
                }
                json += "}";
            }
            json += "]}";
            return json;
        }

        std::string command_vehicle_get(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            std::string error;
            Car* car = find_car_from_request(request, error);
            if (car == nullptr)
            {
                return json_error(error);
            }
            return car_status_json(car);
        }

        std::string command_vehicle_dyno(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading()) return json_error("world is loading");
            std::string error;
            Car* vehicle = find_car_from_request(request, error);
            if (!vehicle || !vehicle->GetRootEntity()) return json_error(error);
            auto* physics = vehicle->GetRootEntity()->GetComponent<Physics>();
            auto* simulation = physics ? CarPhysics::Get(*physics).GetVehicleSimulation() : nullptr;
            if (!simulation) return json_error("vehicle has no simulation");
            auto& d = simulation->dyno;
            const std::string action = get_argument(request, "action").value_or("status");
            if (action == "stop")
            {
                if (!d.mounted) return json_error("vehicle is not mounted on the dyno");
                simulation->stop_dyno();
            }
            else if (action == "start")
            {
                if (!d.mounted || !Engine::IsFlagSet(EngineMode::Playing)) return json_error("load Dyno world and enter play mode first");
                if (d.running) return json_error("dyno is already running");
                auto read = [&](const char* key, float& target)
                {
                    if (const auto value = get_argument(request, key)) return parse_float(*value, target) && std::isfinite(target);
                    return true;
                };
                float start = d.start_rpm, end = d.end_rpm, seconds = d.sweep_seconds, throttle = d.throttle;
                float gear = static_cast<float>(d.gear - 1);
                bool sweep = d.sweep;
                if (!read("start_rpm", start) || !read("end_rpm", end) || !read("seconds", seconds)
                    || !read("throttle", throttle) || !read("gear", gear)
                    || gear < 1 || gear > simulation->get_spec().gear_count - 2 || floorf(gear) != gear)
                    return json_error("invalid dyno parameters");
                if (const auto value = get_argument(request, "sweep"))
                    if (!parse_bool(*value, sweep)) return json_error("invalid sweep boolean");
                d.start_rpm = start; d.end_rpm = end; d.sweep_seconds = seconds; d.throttle = throttle;
                d.gear = static_cast<int>(gear) + 1; d.sweep = sweep;
                if (!simulation->start_dyno()) return json_error(d.status);
            }
            else if (action == "select")
            {
                if (!d.mounted || d.running) return json_error("select requires an idle mounted dyno");
                const auto name = get_argument(request, "car");
                const car::car_definition* definition = nullptr;
                for (const auto& entry : car::preset_registry)
                    if (name && *name == entry.name) definition = entry.definition;
                if (!definition) return json_error("unknown car; use the exact catalog name");
                if (definition != vehicle->GetDefinition())
                {
                    if (!d.samples.empty()) { d.previous_samples = d.samples; d.previous_car = d.car_name; }
                    d.samples.clear();
                    simulation->mount_dyno(false);
                    vehicle->LoadDefinition(definition);
                    simulation->mount_dyno(true);
                    d.car_name = simulation->get_spec().name;
                    d.export_path.clear(); d.status = "Ready";
                }
            }
            else if (action != "status") return json_error("action must be status, start, stop or select");
            std::string json = "{\"ok\":true,\"fixture\":\"speed_controlled_hub\",\"running\":" + std::string(d.running ? "true" : "false");
            json += ",\"status\":" + json_string(d.status) + ",\"car\":" + json_string(d.car_name);
            json += ",\"time_s\":" + std::to_string(d.time) + ",\"samples\":" + std::to_string(d.samples.size());
            json += ",\"csv\":" + json_string(d.export_path);
            if (!d.samples.empty())
            {
                const auto& s = d.samples.back();
                json += ",\"rpm\":" + std::to_string(s.rpm) + ",\"axle_kw\":" + std::to_string(s.axle_kw);
                json += ",\"axle_nm\":" + std::to_string(s.axle_nm) + ",\"combustion_kw\":" + std::to_string(s.combustion_kw);
                json += ",\"engine_net_nm\":" + std::to_string(s.engine_net_nm) + ",\"engine_net_kw\":" + std::to_string(s.engine_net_kw);
            }
            return json + "}";
        }

        std::string command_vehicle_enter(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!Engine::IsFlagSet(EngineMode::Playing))
            {
                return json_error("vehicle enter requires play mode");
            }

            std::string error;
            Car* car = find_car_from_request(request, error);
            if (car == nullptr)
            {
                return json_error(error);
            }

            bool mcp_controlled = true;
            if (const std::optional<std::string> value = get_argument(request, "mcp_controlled"))
            {
                if (!parse_bool(*value, mcp_controlled))
                {
                    return json_error("invalid mcp_controlled");
                }
            }

            if (!car->IsOccupied())
            {
                car->Enter();
            }
            if (!car->IsOccupied())
            {
                return json_error("failed to enter car");
            }

            car->SetExternallyControlled(mcp_controlled);
            return car_status_json(car);
        }

        std::string command_vehicle_exit(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            std::string error;
            Car* car = find_car_from_request(request, error);
            if (car == nullptr)
            {
                return json_error(error);
            }

            if (car->IsOccupied())
            {
                car->Exit();
            }
            car->SetExternallyControlled(false);
            return car_status_json(car);
        }

        std::string command_vehicle_spectate(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!Engine::IsFlagSet(EngineMode::Playing))
            {
                return json_error("vehicle spectate requires play mode");
            }

            // stop: true hands the camera back to the player on foot
            bool stop = false;
            if (const std::optional<std::string> value = get_argument(request, "stop"))
            {
                if (!parse_bool(*value, stop))
                {
                    return json_error("invalid stop");
                }
            }
            if (stop)
            {
                if (Car* viewed = Car::GetViewed(); viewed && viewed->IsSpectated())
                {
                    viewed->StopSpectating();
                }
                return "{\"ok\":true,\"spectating\":false}";
            }

            std::string error;
            Car* car = find_car_from_request(request, error);
            if (car == nullptr)
            {
                return json_error(error);
            }
            if (car->IsOccupied())
            {
                return json_error("the player drives this car, spectate another one");
            }

            bool telemetry = true;
            if (const std::optional<std::string> value = get_argument(request, "telemetry"))
            {
                if (!parse_bool(*value, telemetry))
                {
                    return json_error("invalid telemetry");
                }
            }
            car->Spectate();
            if (!car->IsSpectated())
            {
                return json_error("failed to spectate car");
            }
            car->SetShowTelemetry(telemetry);
            return car_status_json(car);
        }

        std::string command_vehicle_set_input(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!Engine::IsFlagSet(EngineMode::Playing))
            {
                return json_error("vehicle input requires play mode");
            }

            std::string error;
            Car* car = find_car_from_request(request, error);
            if (car == nullptr)
            {
                return json_error(error);
            }
            if (!car->IsOccupied())
            {
                return json_error("car is not occupied, call vehicle_enter first");
            }

            car->SetExternallyControlled(true);

            if (const std::optional<std::string> value = get_argument(request, "throttle"))
            {
                float parsed = 0.0f;
                if (!parse_float(*value, parsed))
                {
                    return json_error("invalid throttle");
                }
                car->SetThrottle(std::clamp(parsed, 0.0f, 1.0f));
            }
            if (const std::optional<std::string> value = get_argument(request, "brake"))
            {
                float parsed = 0.0f;
                if (!parse_float(*value, parsed))
                {
                    return json_error("invalid brake");
                }
                car->SetBrake(std::clamp(parsed, 0.0f, 1.0f));
            }
            if (const std::optional<std::string> value = get_argument(request, "steering"))
            {
                float parsed = 0.0f;
                if (!parse_float(*value, parsed))
                {
                    return json_error("invalid steering");
                }
                car->SetSteering(std::clamp(parsed, -1.0f, 1.0f));
            }
            if (const std::optional<std::string> value = get_argument(request, "handbrake"))
            {
                float parsed = 0.0f;
                if (!parse_float(*value, parsed))
                {
                    return json_error("invalid handbrake");
                }
                car->SetHandbrake(std::clamp(parsed, 0.0f, 1.0f));
            }

            return car_status_json(car);
        }

        std::string command_vehicle_shift(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!Engine::IsFlagSet(EngineMode::Playing))
            {
                return json_error("vehicle shift requires play mode");
            }

            std::string error;
            Car* car = find_car_from_request(request, error);
            if (car == nullptr)
            {
                return json_error(error);
            }

            Entity* root = car->GetRootEntity();
            Physics* physics = root ? root->GetComponent<Physics>() : nullptr;
            if (!physics || physics->GetBodyType() != BodyType::Custom)
            {
                return json_error("vehicle physics not found");
            }

            const std::optional<std::string> action_arg = get_argument(request, "action");
            if (!action_arg)
            {
                return json_error("missing action");
            }

            const std::string action = to_lower_copy(*action_arg);
            if (action == "manual" || action == "automatic")
            {
                CarPhysics::Get(*physics).SetManualTransmission(action == "manual");
                return car_status_json(car);
            }
            if (action != "up" && action != "down" && action != "neutral")
                return json_error("action must be up, down, neutral, manual, or automatic");
            auto* simulation = CarPhysics::Get(*physics).GetVehicleSimulation();
            if (!simulation) return json_error("vehicle simulation not found");
            if (!CarPhysics::Get(*physics).GetManualTransmission())
                return json_error("manual shifting is disabled; select action=manual before requesting a gear");
            if (simulation->get_is_shifting())
                return json_error("gear change already in progress");
            const int previous_gear = simulation->get_current_gear();
            if (action == "up")
            {
                CarPhysics::Get(*physics).ShiftUp();
            }
            else if (action == "down")
            {
                CarPhysics::Get(*physics).ShiftDown();
            }
            else if (action == "neutral")
            {
                CarPhysics::Get(*physics).ShiftToNeutral();
            }
            if (simulation->get_current_gear() == previous_gear && action != "neutral")
                return json_error("gear request exceeds the available gear range");

            car->SetExternallyControlled(true);
            return car_status_json(car);
        }

        std::string command_vehicle_reset(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            std::string error;
            Car* car = find_car_from_request(request, error);
            if (car == nullptr)
            {
                return json_error(error);
            }

            car->ResetToSpawn();
            car->SetThrottle(0.0f);
            car->SetBrake(0.0f);
            car->SetSteering(0.0f);
            car->SetHandbrake(1.0f);
            return car_status_json(car);
        }

        std::string command_vehicle_set_view(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            std::string error;
            Car* car = find_car_from_request(request, error);
            if (car == nullptr)
            {
                return json_error(error);
            }

            const std::optional<std::string> view_arg = get_argument(request, "view");
            if (!view_arg)
            {
                return json_error("missing view");
            }

            const std::string view_name = to_lower_copy(*view_arg);
            if (view_name == "cycle" || view_name == "next")
            {
                car->CycleView();
            }
            else
            {
                CarView view = CarView::Chase;
                if (!car_view_from_name(view_name, view))
                {
                    return json_error("view must be chase, hood, wheel, or cycle");
                }
                car->SetView(view);
            }

            return car_status_json(car);
        }

        // records what the procedural engine sound actually plays, with its control track, for tools/engine_sound/analyze.py
        std::string command_engine_sound_capture(const McpRequest& request)
        {
            engine_sound::synthesizer& synth = engine_sound::get_synthesizer();
            if (!synth.is_initialized())
            {
                return json_error("engine sound is not running, enter or spectate a car first");
            }

            const std::string action = get_argument(request, "action").value_or("status");
            std::string saved_path;
            if (action == "start")
            {
                float seconds = 10.0f;
                if (const auto value = get_argument(request, "seconds"))
                {
                    if (!parse_float(*value, seconds))
                    {
                        return json_error("invalid seconds");
                    }
                }
                if (!synth.begin_dump(seconds))
                {
                    return json_error("a capture is running or waiting to be saved, or seconds is outside 0-120");
                }
            }
            else if (action == "save")
            {
                if (!synth.dump_ready())
                {
                    return json_error("no finished capture to save");
                }
                std::filesystem::path path = get_argument(request, "path").value_or("engine_sound_capture.wav");
                path = std::filesystem::absolute(path);
                if (path.has_parent_path())
                {
                    std::error_code ignored;
                    std::filesystem::create_directories(path.parent_path(), ignored);
                }
                if (!synth.save_dump(path.string().c_str()))
                {
                    return json_error("failed to write " + path.string());
                }
                const engine_sound::engine_config& cfg = synth.get_config();
                std::filesystem::path meta_path = path;
                meta_path.replace_extension(".json");
                std::ofstream meta(meta_path);
                meta << "{\"car\":" << json_string(get_argument(request, "label").value_or("in-game capture"))
                     << ",\"scenario\":\"capture\",\"sample_rate\":" << synth.get_output_sample_rate()
                     << ",\"cylinders\":" << cfg.cylinder_count << ",\"banks\":" << cfg.bank_count
                     << ",\"idle_rpm\":" << cfg.idle_rpm << ",\"redline_rpm\":" << cfg.redline_rpm
                     << ",\"muffler_level\":" << cfg.muffler_level << ",\"turbo\":" << (cfg.turbo_enabled ? "true" : "false") << "}\n";
                saved_path = path.string();
            }
            else if (action != "status")
            {
                return json_error("action must be status, start or save");
            }

            const engine_sound::debug_data debug = synth.get_debug();
            const float sample_rate = static_cast<float>(std::max(synth.get_output_sample_rate(), 1));
            std::string json = "{\"ok\":true";
            json += ",\"capturing\":" + json_bool(debug.dump_progress < debug.dump_total && !synth.dump_ready());
            json += ",\"ready\":" + json_bool(synth.dump_ready());
            json += ",\"sample_rate\":" + std::to_string(synth.get_output_sample_rate());
            json += ",\"progress_seconds\":" + std::to_string(debug.dump_progress / sample_rate);
            json += ",\"total_seconds\":" + std::to_string(debug.dump_total / sample_rate);
            json += ",\"rpm\":" + std::to_string(debug.rpm);
            json += ",\"load\":" + std::to_string(debug.load);
            json += ",\"output_level\":" + std::to_string(debug.output_level);
            if (!saved_path.empty())
            {
                json += ",\"path\":" + json_string(saved_path);
            }
            json += "}";
            return json;
        }

        std::string command_vehicle_export_hull(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading()) return json_error("world is loading");
            std::string error;
            Car* car = find_car_from_request(request, error);
            if (!car) return json_error(error);
            Entity* entity = car->GetRootEntity();
            Physics* physics = entity ? entity->GetComponent<Physics>() : nullptr;
            auto* sim = physics ? CarPhysics::Get(*physics).GetVehicleSimulation() : nullptr;
            if (!sim || !sim->export_chassis_hulls("car_validation_hulls.csv")) return json_error("no cooked chassis hulls available");
            return "{\"ok\":true,\"path\":\"car_validation_hulls.csv\"}";
        }

        std::string command_vehicle_telemetry(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            std::string error;
            Car* car = find_car_from_request(request, error);
            if (!car)
            {
                return json_error(error);
            }
            Entity* entity = car->GetRootEntity();
            Physics* physics = entity ? entity->GetComponent<Physics>() : nullptr;
            if (!physics)
            {
                return json_error("target car has no vehicle simulation");
            }
            ::car::Simulation* simulation = CarPhysics::Get(*physics).GetVehicleSimulation();
            if (!simulation)
            {
                return json_error("target car has no vehicle simulation");
            }

            int max_rows = 5;
            if (const std::optional<std::string> rows_arg = get_argument(request, "max_rows"))
            {
                int32_t parsed = 0;
                if (!parse_int32(*rows_arg, parsed) || parsed < 1 || parsed > 5000)
                {
                    return json_error("max_rows must be between 1 and 5000");
                }
                max_rows = parsed;
            }

            bool include_csv = true;
            if (const std::optional<std::string> include_arg = get_argument(request, "include_csv"))
            {
                if (!parse_bool(*include_arg, include_csv))
                {
                    return json_error("invalid include_csv");
                }
            }

            bool include_skeleton = true;
            if (const auto value = get_argument(request, "include_skeleton"))
                if (!parse_bool(*value, include_skeleton)) return json_error("invalid include_skeleton");
            std::string csv_text;
            std::string path;
            int total_lines = 0;
            if (const auto value = get_argument(request, "recording"))
            {
                bool recording = false;
                if (!parse_bool(*value, recording)) return json_error("invalid recording");
                simulation->set_log_to_file(recording);
                if (recording && !simulation->open_telemetry_if_needed())
                    return json_error("failed to open telemetry recording");
            }
            // A live skeleton query must not scan a potentially multi-GB recording.
            bool ok = true;
            if (include_csv) ok = simulation->snapshot_telemetry_tail(max_rows, csv_text, path, total_lines);
            else path = simulation->get_telemetry_path();

            std::string json = "{\"ok\":true";
            json += ",\"path\":" + json_string(path);
            json += ",\"log_to_file\":" + json_bool(simulation->get_log_to_file());
            if (include_skeleton) json += ",\"physics_skeleton\":" + simulation->get_physics_telemetry_json();
            json += ",\"total_lines\":" + (include_csv ? std::to_string(total_lines) : "null");
            json += ",\"returned_data_rows\":" + std::to_string(std::max(0, std::min(max_rows, std::max(0, total_lines - 1))));
            json += ",\"file_ready\":" + (include_csv ? json_bool(ok && total_lines > 0) : "null");
            if (include_csv)
            {
                json += ",\"csv\":" + json_string(csv_text);
            }
            if (!ok && total_lines == 0)
            {
                json += ",\"note\":" + json_string("telemetry file not found yet, enter play mode with a drivable car and drive first");
            }
            json += "}";
            return json;
        }

    }
    void game::RegisterCarMcpCommands()
    {
        RegisterMcpCommand("vehicle_list", [](const McpRequest&) { return command_vehicle_list(); });
        RegisterMcpCommand("vehicle_get", command_vehicle_get);
        RegisterMcpCommand("vehicle_dyno", command_vehicle_dyno);
        RegisterMcpCommand("vehicle_enter", command_vehicle_enter);
        RegisterMcpCommand("vehicle_exit", command_vehicle_exit);
        RegisterMcpCommand("vehicle_spectate", command_vehicle_spectate);
        RegisterMcpCommand("vehicle_ai", command_vehicle_ai);
        RegisterMcpCommand("vehicle_set_input", command_vehicle_set_input);
        RegisterMcpCommand("vehicle_set_tire_pressure", command_vehicle_set_tire_pressure);
        RegisterMcpCommand("vehicle_shift", command_vehicle_shift);
        RegisterMcpCommand("vehicle_reset", command_vehicle_reset);
        RegisterMcpCommand("vehicle_set_view", command_vehicle_set_view);
        RegisterMcpCommand("vehicle_telemetry", command_vehicle_telemetry);
        RegisterMcpCommand("vehicle_export_hull", command_vehicle_export_hull);
        RegisterMcpCommand("engine_sound_capture", command_engine_sound_capture);
    }
}
