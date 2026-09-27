/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==================================
#include "pch.h"
#include "EditorMcpCommands.h"
#include "Editor.h"
#include "widgets/Sequencer.h"
#include "world/World.h"
#include "world/Entity.h"
#include "world/components/Camera.h"
#include "world/components/Spline.h"
#include "world/components/SplineFollower.h"
#include "car/Car.h"
#include <cstdlib>
#include <sstream>
SP_WARNINGS_OFF
#include "io/pugixml.hpp"
SP_WARNINGS_ON
//=============================================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
using namespace spartan::math;
//======================

namespace editor_mcp
{
    namespace
    {
        const char* missing_panel      = "the sequencer is not available";
        const char* index_out_of_range = "index is out of range";

        string entity_name(uint64_t entity_id)
        {
            Entity* entity = World::GetEntityById(entity_id);
            return entity ? entity->GetObjectName() : "missing";
        }

        string number(float value)
        {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%.4g", value);
            return buffer;
        }

        string vector_json(const Vector3& value)
        {
            return "[" + number(value.x) + "," + number(value.y) + "," + number(value.z) + "]";
        }

        // a reference is either an entity id or an entity name, a name is what a caller writing by hand
        // has and an id is what a previous reply gave it
        template<typename Filter>
        Entity* resolve(const string& value, Filter qualifies)
        {
            if (!value.empty() && value.find_first_not_of("0123456789") == string::npos)
            {
                Entity* entity = World::GetEntityById(strtoull(value.c_str(), nullptr, 10));
                if (entity && qualifies(entity))
                {
                    return entity;
                }
            }
            // names repeat (every car prefab has a "vehicle"), so a match inside a car wins over a stray prop
            Entity* first = nullptr;
            for (Entity* entity : World::GetEntities())
            {
                if (entity->GetObjectName() == value && qualifies(entity))
                {
                    if (Sequencer::FindCar(entity))
                    {
                        return entity;
                    }
                    first = first ? first : entity;
                }
            }
            return first;
        }

        Entity* resolve_camera(const string& value)   { return resolve(value, [](Entity* e) { return e->GetComponent<Camera>() != nullptr; }); }
        Entity* resolve_entity(const string& value)   { return resolve(value, [](Entity*) { return true; }); }
        Entity* resolve_follower(const string& value) { return resolve(value, [](Entity* e) { return e->GetComponent<SplineFollower>() != nullptr; }); }
        Entity* resolve_spline(const string& value)   { return resolve(value, [](Entity* e) { return e->GetComponent<Spline>() != nullptr; }); }

        // any entity inside a car resolves to the car root, which is what physics moves
        Entity* resolve_car(const string& value)
        {
            Entity* entity = resolve_entity(value);
            Car* car       = entity ? Sequencer::FindCar(entity) : nullptr;
            if (!car)
            {
                for (Car* candidate : Car::GetAll())
                {
                    if (candidate && candidate->IsDrivable() && candidate->GetRootEntity() && (value.empty() || value == "car"))
                    {
                        car = candidate;
                        break;
                    }
                }
            }
            return car ? car->GetRootEntity() : nullptr;
        }

        optional<Vector3> as_vector(const string* value)
        {
            if (!value)
            {
                return nullopt;
            }
            string text = *value;
            for (char& c : text)
            {
                if (c == '[' || c == ']' || c == ',')
                {
                    c = ' ';
                }
            }
            istringstream stream(text);
            Vector3 result;
            if (!(stream >> result.x >> result.y >> result.z))
            {
                return nullopt;
            }
            return result;
        }

        bool parse_speed_keys(const string& value, vector<Sequencer::SpeedKey>& keys)
        {
            keys.clear();
            vector<float> numbers;
            string text = value;
            for (char& c : text)
            {
                if (c == ',' || c == '[' || c == ']' || c == ';')
                {
                    c = ' ';
                }
            }
            istringstream stream(text);
            float parsed = 0.0f;
            while (stream >> parsed)
            {
                numbers.push_back(parsed);
            }
            if (numbers.empty() || numbers.size() % 2 != 0)
            {
                return false;
            }
            for (size_t i = 0; i < numbers.size(); i += 2)
            {
                keys.push_back({ numbers[i], numbers[i + 1] });
            }
            return true;
        }

        // shot arguments shared by add and update, any motion argument turns the rig on unless rig is spelled out
        bool apply_shot_arguments(const McpRequest& request, Sequencer::CameraEvent& event, string& error)
        {
            if (const optional<float> time = as_float(find(request, "time")))
            {
                event.time = *time;
            }

            if (const string* camera = find(request, "camera"))
            {
                Entity* entity = resolve_camera(*camera);
                if (!entity)
                {
                    error = "no camera entity matches '" + *camera + "'";
                    return false;
                }
                event.camera_entity_id = entity->GetObjectId();
            }

            // an empty target clears the lock rather than failing to find an entity called nothing
            if (const string* target = find(request, "target"))
            {
                if (target->empty() || to_lower(*target) == "none")
                {
                    event.target_entity_id = 0;
                }
                else
                {
                    Entity* entity = resolve_entity(*target);
                    if (!entity)
                    {
                        error = "no entity matches '" + *target + "'";
                        return false;
                    }
                    event.target_entity_id = Sequencer::GetPersistentEntity(entity)->GetObjectId();
                }
            }

            bool motion = false;
            if (const string* anchor = find(request, "anchor"))
            {
                if (anchor->empty() || to_lower(*anchor) == "none")
                {
                    event.anchor_entity_id = 0;
                }
                else
                {
                    Entity* entity = resolve_entity(*anchor);
                    if (!entity)
                    {
                        error = "no entity matches anchor '" + *anchor + "'";
                        return false;
                    }
                    event.anchor_entity_id = Sequencer::GetPersistentEntity(entity)->GetObjectId();
                }
                motion = true;
            }
            if (const string* space = find(request, "space"))
            {
                const string normalized = to_lower(*space);
                if (normalized == "world")
                {
                    event.space = Sequencer::ShotSpace::World;
                }
                else if (normalized == "anchor" || normalized == "mounted" || normalized == "rigid")
                {
                    event.space = Sequencer::ShotSpace::Anchor;
                }
                else if (normalized == "heading" || normalized == "follow" || normalized == "tracking")
                {
                    event.space = Sequencer::ShotSpace::AnchorHeading;
                }
                else
                {
                    error = "space must be world, anchor or heading";
                    return false;
                }
                motion = true;
            }
            if (const string* ease = find(request, "ease"))
            {
                const string normalized = to_lower(*ease);
                if (normalized == "linear")
                {
                    event.ease = Sequencer::ShotEase::Linear;
                }
                else if (normalized == "in_out" || normalized == "inout" || normalized == "smooth")
                {
                    event.ease = Sequencer::ShotEase::InOut;
                }
                else if (normalized == "in")
                {
                    event.ease = Sequencer::ShotEase::In;
                }
                else if (normalized == "out")
                {
                    event.ease = Sequencer::ShotEase::Out;
                }
                else
                {
                    error = "ease must be linear, in_out, in or out";
                    return false;
                }
                motion = true;
            }

            struct vector_field { const char* name; Vector3* target; };
            const vector_field vectors[] =
            {
                { "position_start", &event.position_start },
                { "position_end",   &event.position_end },
                { "look_start",     &event.look_start },
                { "look_end",       &event.look_end },
            };
            for (const vector_field& field : vectors)
            {
                if (const string* value = find(request, field.name))
                {
                    const optional<Vector3> parsed = as_vector(value);
                    if (!parsed)
                    {
                        error = string(field.name) + " must be x,y,z";
                        return false;
                    }
                    *field.target = *parsed;
                    motion        = true;
                }
            }
            // a single position or look holds still for the whole shot
            if (const optional<Vector3> position = as_vector(find(request, "position")))
            {
                event.position_start = event.position_end = *position;
                motion = true;
            }
            if (const optional<Vector3> look = as_vector(find(request, "look")))
            {
                event.look_start = event.look_end = *look;
                motion = true;
            }

            struct float_field { const char* name; float* target; };
            const float_field floats[] =
            {
                { "fov_start", &event.fov_start },
                { "fov_end",   &event.fov_end },
                { "aperture",  &event.aperture },
                { "roll",      &event.roll },
                { "shake",     &event.shake },
                { "lag",       &event.lag },
            };
            for (const float_field& field : floats)
            {
                if (const optional<float> value = as_float(find(request, field.name)))
                {
                    *field.target = *value;
                    motion        = true;
                }
            }
            if (const optional<float> fov = as_float(find(request, "fov")))
            {
                event.fov_start = event.fov_end = *fov;
                motion = true;
            }

            if (motion)
            {
                event.rig = true;
            }
            if (const optional<bool> rig = as_bool(find(request, "rig")))
            {
                event.rig = *rig;
            }
            return true;
        }

        bool apply_drive_arguments(const McpRequest& request, Sequencer::DriveEvent& event, string& error)
        {
            if (const optional<float> value = as_float(find(request, "start")))
            {
                event.start_time = *value;
            }
            if (const optional<float> value = as_float(find(request, "end")))
            {
                event.end_time = *value;
            }
            if (const string* car = find(request, "car"))
            {
                Entity* entity = resolve_car(*car);
                if (!entity)
                {
                    error = "no drivable car matches '" + *car + "'";
                    return false;
                }
                event.car_entity_id = Sequencer::GetPersistentEntity(entity)->GetObjectId();
            }
            if (const string* spline = find(request, "spline"))
            {
                Entity* entity = resolve_spline(*spline);
                if (!entity)
                {
                    error = "no spline entity matches '" + *spline + "'";
                    return false;
                }
                event.spline_entity_id = entity->GetObjectId();
            }
            if (const optional<float> value = as_float(find(request, "start_distance")))
            {
                event.start_distance = *value;
            }
            if (const optional<float> value = as_float(find(request, "lane_offset")))
            {
                event.lane_offset = *value;
            }
            if (const optional<float> value = as_float(find(request, "max_lateral_g")))
            {
                event.max_lateral_g = *value;
            }
            if (const optional<bool> value = as_bool(find(request, "reverse")))
            {
                event.reverse = *value;
            }
            if (const string* keys = find(request, "speed_keys"))
            {
                if (!parse_speed_keys(*keys, event.speed_keys))
                {
                    error = "speed_keys must be pairs of time seconds and km/h, e.g. 0,0,3,120";
                    return false;
                }
            }
            return true;
        }

        // every command answers with the whole timeline, entity names included so a caller can read the
        // reply without resolving ids of its own
        string state_reply(const Sequencer* sequencer)
        {
            const Sequencer::Snapshot snapshot = sequencer->GetSnapshot();

            string json = "{\"ok\":true";
            json += ",\"duration\":" + number(snapshot.duration);
            json += ",\"time\":" + number(snapshot.time);
            json += string(",\"playing\":") + boolean(snapshot.playing);
            json += string(",\"loop\":") + boolean(snapshot.loop);
            json += string(",\"preview\":") + boolean(snapshot.preview);
            json += ",\"events\":[";
            for (size_t i = 0; i < snapshot.events.size(); i++)
            {
                if (i > 0)
                {
                    json += ",";
                }
                const Sequencer::CameraEvent& event = snapshot.events[i];
                json += "{\"index\":" + to_string(i);
                json += ",\"time\":" + number(event.time);
                json += ",\"camera_entity_id\":" + quote(to_string(event.camera_entity_id));
                json += ",\"camera_name\":" + quote(entity_name(event.camera_entity_id));
                json += ",\"target_entity_id\":" + quote(to_string(event.target_entity_id));
                json += ",\"target_name\":" + quote(event.target_entity_id != 0 ? entity_name(event.target_entity_id) : "");
                json += string(",\"rig\":") + boolean(event.rig);
                if (event.rig)
                {
                    const char* spaces[] = { "world", "anchor", "heading" };
                    const char* eases[]  = { "linear", "in_out", "in", "out" };
                    json += ",\"space\":" + quote(spaces[static_cast<int>(event.space)]);
                    json += ",\"anchor\":" + quote(event.anchor_entity_id != 0 ? entity_name(event.anchor_entity_id) : "");
                    json += ",\"position_start\":" + vector_json(event.position_start);
                    json += ",\"position_end\":" + vector_json(event.position_end);
                    json += ",\"look_start\":" + vector_json(event.look_start);
                    json += ",\"look_end\":" + vector_json(event.look_end);
                    json += ",\"fov_start\":" + number(event.fov_start);
                    json += ",\"fov_end\":" + number(event.fov_end);
                    json += ",\"aperture\":" + number(event.aperture);
                    json += ",\"roll\":" + number(event.roll);
                    json += ",\"shake\":" + number(event.shake);
                    json += ",\"lag\":" + number(event.lag);
                    json += ",\"ease\":" + quote(eases[static_cast<int>(event.ease)]);
                }
                json += "}";
            }
            json += "],\"spline_events\":[";
            for (size_t i = 0; i < snapshot.spline_events.size(); i++)
            {
                if (i > 0)
                {
                    json += ",";
                }
                const Sequencer::SplineEvent& event = snapshot.spline_events[i];
                json += "{\"index\":" + to_string(i);
                json += ",\"start_time\":" + number(event.start_time);
                json += ",\"end_time\":" + number(event.end_time);
                json += ",\"follower_entity_id\":" + quote(to_string(event.follower_entity_id));
                json += ",\"follower_name\":" + quote(entity_name(event.follower_entity_id));
                json += "}";
            }
            json += "],\"drive_events\":[";
            for (size_t i = 0; i < snapshot.drive_events.size(); i++)
            {
                if (i > 0)
                {
                    json += ",";
                }
                const Sequencer::DriveEvent& event          = snapshot.drive_events[i];
                const Sequencer::DriveTelemetry& telemetry = snapshot.drive_telemetry[i];
                json += "{\"index\":" + to_string(i);
                json += ",\"start_time\":" + number(event.start_time);
                json += ",\"end_time\":" + number(event.end_time);
                json += ",\"car_entity_id\":" + quote(to_string(event.car_entity_id));
                json += ",\"car_name\":" + quote(entity_name(event.car_entity_id));
                json += ",\"spline_entity_id\":" + quote(to_string(event.spline_entity_id));
                json += ",\"spline_name\":" + quote(entity_name(event.spline_entity_id));
                json += ",\"start_distance\":" + number(event.start_distance);
                json += ",\"lane_offset\":" + number(event.lane_offset);
                json += ",\"max_lateral_g\":" + number(event.max_lateral_g);
                json += string(",\"reverse\":") + boolean(event.reverse);
                json += ",\"speed_keys\":[";
                for (size_t k = 0; k < event.speed_keys.size(); k++)
                {
                    json += (k > 0 ? "," : "") + string("[") + number(event.speed_keys[k].time) + "," + number(event.speed_keys[k].speed_kmh) + "]";
                }
                json += "],\"autopilot\":{";
                json += string("\"staged\":") + boolean(telemetry.staged);
                json += string(",\"active\":") + boolean(telemetry.active);
                json += ",\"distance\":" + number(telemetry.distance);
                json += ",\"path_length\":" + number(telemetry.path_length);
                json += ",\"speed_kmh\":" + number(telemetry.speed_kmh);
                json += ",\"target_kmh\":" + number(telemetry.target_kmh);
                json += ",\"lateral_error\":" + number(telemetry.lateral_error);
                json += ",\"throttle\":" + number(telemetry.throttle);
                json += ",\"brake\":" + number(telemetry.brake);
                json += ",\"steering\":" + number(telemetry.steering);
                json += "}}";
            }
            json += "],\"render\":{";
            json += string("\"active\":") + boolean(snapshot.render.active);
            json += ",\"frames_written\":" + to_string(snapshot.render.frames_written);
            json += ",\"frames_total\":" + to_string(snapshot.render.frames_total);
            json += ",\"fps\":" + number(snapshot.render.fps);
            json += ",\"directory\":" + quote(snapshot.render.directory);
            json += ",\"last_error\":" + quote(snapshot.render.last_error);
            json += "}}";
            return json;
        }

        // an all=true argument means the whole track goes, which is a different operation from removing
        // one entry and is spelled that way in every remove command
        bool wants_everything(const McpRequest& request)
        {
            return as_bool(find(request, "all")).value_or(false);
        }
    }

    void register_sequencer(Editor* editor)
    {
        add(
            "sequencer_get",
            [editor](const McpRequest&) -> string
            {
                Sequencer* sequencer = editor->GetWidget<Sequencer>();
                if (!sequencer)
                {
                    return failure(missing_panel);
                }

                return state_reply(sequencer);
            }
        );

        add(
            "sequencer_set",
            [editor](const McpRequest& request) -> string
            {
                Sequencer* sequencer = editor->GetWidget<Sequencer>();
                if (!sequencer)
                {
                    return failure(missing_panel);
                }

                Sequencer::TimelineRequest timeline;
                timeline.duration = as_float(find(request, "duration"));
                timeline.time     = as_float(find(request, "time"));
                timeline.loop     = as_bool(find(request, "loop"));
                timeline.visible  = as_bool(find(request, "visible"));
                timeline.preview  = as_bool(find(request, "preview"));
                sequencer->SetTimeline(timeline);
                return state_reply(sequencer);
            }
        );

        add(
            "sequencer_playback",
            [editor](const McpRequest& request) -> string
            {
                Sequencer* sequencer = editor->GetWidget<Sequencer>();
                if (!sequencer)
                {
                    return failure(missing_panel);
                }

                const string* action = find(request, "action");
                if (!action)
                {
                    return failure("missing action");
                }
                const string normalized = to_lower(*action);
                if (normalized == "play")
                {
                    sequencer->SetPlayback(Sequencer::Playback::Play);
                }
                else if (normalized == "pause")
                {
                    sequencer->SetPlayback(Sequencer::Playback::Pause);
                }
                else if (normalized == "stop")
                {
                    sequencer->SetPlayback(Sequencer::Playback::Stop);
                }
                else
                {
                    return failure("action must be play, pause or stop");
                }
                return state_reply(sequencer);
            }
        );

        add(
            "sequencer_event_add",
            [editor](const McpRequest& request) -> string
            {
                Sequencer* sequencer = editor->GetWidget<Sequencer>();
                if (!sequencer)
                {
                    return failure(missing_panel);
                }

                if (!as_float(find(request, "time")))
                {
                    return failure("missing time");
                }

                Sequencer::CameraEvent event;
                string error;
                if (!apply_shot_arguments(request, event, error))
                {
                    return failure(error);
                }
                if (event.camera_entity_id == 0)
                {
                    if (!event.rig)
                    {
                        return failure("missing camera, pass a camera or describe a shot with position_start/look_start so the timeline camera can be used");
                    }
                    event.camera_entity_id = Sequencer::GetOrCreateShotCamera()->GetObjectId();
                }

                sequencer->AddCameraEvent(event);
                return state_reply(sequencer);
            }
        );

        add(
            "sequencer_event_update",
            [editor](const McpRequest& request) -> string
            {
                Sequencer* sequencer = editor->GetWidget<Sequencer>();
                if (!sequencer)
                {
                    return failure(missing_panel);
                }

                const optional<uint64_t> index = as_uint(find(request, "index"));
                if (!index)
                {
                    return failure("missing index");
                }
                const Sequencer::Snapshot snapshot = sequencer->GetSnapshot();
                if (*index >= snapshot.events.size())
                {
                    return failure(index_out_of_range);
                }

                Sequencer::CameraEvent event = snapshot.events[*index];
                string error;
                if (!apply_shot_arguments(request, event, error))
                {
                    return failure(error);
                }
                sequencer->UpdateCameraEvent(static_cast<int>(*index), event);
                return state_reply(sequencer);
            }
        );

        add(
            "sequencer_event_remove",
            [editor](const McpRequest& request) -> string
            {
                Sequencer* sequencer = editor->GetWidget<Sequencer>();
                if (!sequencer)
                {
                    return failure(missing_panel);
                }

                if (wants_everything(request))
                {
                    sequencer->ClearCameraEvents();
                    return state_reply(sequencer);
                }

                const optional<uint64_t> index = as_uint(find(request, "index"));
                if (!index)
                {
                    return failure("missing index, pass all=true to clear every event");
                }
                if (!sequencer->RemoveCameraEvent(static_cast<int>(*index)))
                {
                    return failure(index_out_of_range);
                }
                return state_reply(sequencer);
            }
        );

        add(
            "sequencer_spline_add",
            [editor](const McpRequest& request) -> string
            {
                Sequencer* sequencer = editor->GetWidget<Sequencer>();
                if (!sequencer)
                {
                    return failure(missing_panel);
                }

                const optional<float> start = as_float(find(request, "start"));
                const optional<float> end   = as_float(find(request, "end"));
                const string* follower      = find(request, "follower");
                if (!start || !end || !follower)
                {
                    return failure("missing start, end or follower");
                }
                Entity* entity = resolve_follower(*follower);
                if (!entity)
                {
                    return failure("no spline follower entity matches '" + *follower + "'");
                }

                sequencer->AddSplineEvent(*start, *end, entity->GetObjectId());
                return state_reply(sequencer);
            }
        );

        add(
            "sequencer_spline_update",
            [editor](const McpRequest& request) -> string
            {
                Sequencer* sequencer = editor->GetWidget<Sequencer>();
                if (!sequencer)
                {
                    return failure(missing_panel);
                }

                const optional<uint64_t> index = as_uint(find(request, "index"));
                if (!index)
                {
                    return failure("missing index");
                }

                optional<uint64_t> follower_id;
                if (const string* follower = find(request, "follower"))
                {
                    Entity* entity = resolve_follower(*follower);
                    if (!entity)
                    {
                        return failure("no spline follower entity matches '" + *follower + "'");
                    }
                    follower_id = entity->GetObjectId();
                }

                if (
                    !sequencer->UpdateSplineEvent(
                        static_cast<int>(*index),
                        as_float(find(request, "start")),
                        as_float(find(request, "end")),
                        follower_id
                    )
                )
                {
                    return failure(index_out_of_range);
                }
                return state_reply(sequencer);
            }
        );

        add(
            "sequencer_spline_remove",
            [editor](const McpRequest& request) -> string
            {
                Sequencer* sequencer = editor->GetWidget<Sequencer>();
                if (!sequencer)
                {
                    return failure(missing_panel);
                }

                if (wants_everything(request))
                {
                    sequencer->ClearSplineEvents();
                    return state_reply(sequencer);
                }

                const optional<uint64_t> index = as_uint(find(request, "index"));
                if (!index)
                {
                    return failure("missing index, pass all=true to clear every spline event");
                }
                if (!sequencer->RemoveSplineEvent(static_cast<int>(*index)))
                {
                    return failure(index_out_of_range);
                }
                return state_reply(sequencer);
            }
        );

        add(
            "sequencer_drive_add",
            [editor](const McpRequest& request) -> string
            {
                Sequencer* sequencer = editor->GetWidget<Sequencer>();
                if (!sequencer)
                {
                    return failure(missing_panel);
                }

                if (!as_float(find(request, "start")) || !as_float(find(request, "end")) || !find(request, "spline"))
                {
                    return failure("missing start, end or spline");
                }

                Sequencer::DriveEvent event;
                string error;
                if (!apply_drive_arguments(request, event, error))
                {
                    return failure(error);
                }
                if (event.car_entity_id == 0)
                {
                    Entity* car = resolve_car("");
                    if (!car)
                    {
                        return failure("there is no drivable car in the world");
                    }
                    event.car_entity_id = Sequencer::GetPersistentEntity(car)->GetObjectId();
                }
                if (event.speed_keys.empty())
                {
                    event.speed_keys.push_back({ event.start_time, 0.0f });
                    event.speed_keys.push_back({ event.start_time + 4.0f, 100.0f });
                }

                sequencer->AddDriveEvent(event);
                return state_reply(sequencer);
            }
        );

        add(
            "sequencer_drive_update",
            [editor](const McpRequest& request) -> string
            {
                Sequencer* sequencer = editor->GetWidget<Sequencer>();
                if (!sequencer)
                {
                    return failure(missing_panel);
                }

                const optional<uint64_t> index = as_uint(find(request, "index"));
                if (!index)
                {
                    return failure("missing index");
                }
                const Sequencer::Snapshot snapshot = sequencer->GetSnapshot();
                if (*index >= snapshot.drive_events.size())
                {
                    return failure(index_out_of_range);
                }

                Sequencer::DriveEvent event = snapshot.drive_events[*index];
                string error;
                if (!apply_drive_arguments(request, event, error))
                {
                    return failure(error);
                }
                sequencer->UpdateDriveEvent(static_cast<int>(*index), event);
                return state_reply(sequencer);
            }
        );

        add(
            "sequencer_drive_remove",
            [editor](const McpRequest& request) -> string
            {
                Sequencer* sequencer = editor->GetWidget<Sequencer>();
                if (!sequencer)
                {
                    return failure(missing_panel);
                }

                if (wants_everything(request))
                {
                    sequencer->ClearDriveEvents();
                    return state_reply(sequencer);
                }

                const optional<uint64_t> index = as_uint(find(request, "index"));
                if (!index)
                {
                    return failure("missing index, pass all=true to clear every drive event");
                }
                if (!sequencer->RemoveDriveEvent(static_cast<int>(*index)))
                {
                    return failure(index_out_of_range);
                }
                return state_reply(sequencer);
            }
        );

        add(
            "sequencer_render",
            [editor](const McpRequest& request) -> string
            {
                Sequencer* sequencer = editor->GetWidget<Sequencer>();
                if (!sequencer)
                {
                    return failure(missing_panel);
                }

                const string action = to_lower(find(request, "action") ? *find(request, "action") : string("status"));
                if (action == "start")
                {
                    Sequencer::RenderRequest render;
                    render.fps     = as_float(find(request, "fps")).value_or(30.0f);
                    render.start   = as_float(find(request, "start")).value_or(0.0f);
                    render.end     = as_float(find(request, "end")).value_or(-1.0f);
                    render.preroll = as_float(find(request, "preroll")).value_or(1.0f);
                    render.stride  = static_cast<uint32_t>(as_uint(find(request, "stride")).value_or(1));
                    if (const string* directory = find(request, "directory"))
                    {
                        render.directory = *directory;
                    }
                    string error;
                    if (!sequencer->StartRender(render, error))
                    {
                        return failure(error);
                    }
                }
                else if (action == "stop")
                {
                    sequencer->StopRender();
                }
                else if (action != "status")
                {
                    return failure("action must be start, stop or status");
                }
                return state_reply(sequencer);
            }
        );
    }
}
