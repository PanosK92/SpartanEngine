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
#include "world/components/AudioSource.h"
#include "audio/AudioMixer.h"
#include "car/Car.h"
#include "file_system/FileSystem.h"
#include "resource/ResourceCache.h"
#include "mcp/McpJson.h"
#include <cstdlib>
#include <cmath>
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

        bool parse_rev_keys(const string& value, vector<Sequencer::RevKey>& keys)
        {
            vector<Sequencer::SpeedKey> pairs;
            keys.clear();
            if (!parse_speed_keys(value, pairs))
            {
                return false;
            }
            for (const Sequencer::SpeedKey& pair : pairs)
            {
                keys.push_back({ pair.time, clamp(pair.speed_kmh, 0.0f, 1.0f) });
            }
            return true;
        }

        // flat triples of start, end and throttle, an empty list clears them
        bool parse_burnouts(const string& value, vector<Sequencer::BurnoutSpan>& spans)
        {
            spans.clear();
            string text = value;
            for (char& c : text)
            {
                if (c == ',' || c == '[' || c == ']' || c == ';')
                {
                    c = ' ';
                }
            }
            vector<float> numbers;
            istringstream stream(text);
            float parsed = 0.0f;
            while (stream >> parsed)
            {
                numbers.push_back(parsed);
            }
            if (numbers.size() % 3 != 0)
            {
                return false;
            }
            for (size_t i = 0; i < numbers.size(); i += 3)
            {
                if (numbers[i + 1] <= numbers[i])
                {
                    return false;
                }
                spans.push_back({ numbers[i], numbers[i + 1], clamp(numbers[i + 2], 0.82f, 1.0f) });
            }
            return true;
        }

        string unquote(string text)
        {
            const size_t first = text.find_first_not_of(" \t\"'");
            const size_t last  = text.find_last_not_of(" \t\"'");
            return first == string::npos ? "" : text.substr(first, last - first + 1);
        }

        // a time argument is seconds or a reference into the timeline: beat:N, bar:N or marker:NAME, each with an optional +/- offset
        // returns false only for a value that is present and wrong, a missing one leaves the output empty
        bool read_time(const Sequencer* sequencer, const McpRequest& request, const char* name, optional<float>& output, string& error)
        {
            output = nullopt;
            const string* value = find(request, name);
            if (!value)
            {
                return true;
            }
            output = sequencer->ResolveTime(unquote(*value), error);
            if (!output)
            {
                error = string(name) + ": " + error;
                return false;
            }
            return true;
        }

        optional<float> json_time(const Sequencer* sequencer, const mcp_json::value& value, string& error)
        {
            if (value.type == mcp_json::kind::number)
            {
                return static_cast<float>(value.number_value);
            }
            if (value.type == mcp_json::kind::string)
            {
                return sequencer->ResolveTime(value.string_value, error);
            }
            error = "a time must be seconds or a beat:, bar: or marker: reference";
            return nullopt;
        }

        // a list of times as json, or a single bare time
        bool read_times(const Sequencer* sequencer, const string& text, vector<float>& times, string& error)
        {
            times.clear();
            mcp_json::value parsed;
            string parse_error;
            if (!mcp_json::parse(text, parsed, parse_error) || !parsed.is_array())
            {
                // the mcp client sends scalar arrays comma separated, so "1.5,beat:3,marker:drop+0.5"
                size_t begin = 0;
                while (begin <= text.size())
                {
                    size_t end = text.find(',', begin);
                    if (end == string::npos)
                    {
                        end = text.size();
                    }
                    const string item = unquote(text.substr(begin, end - begin));
                    if (!item.empty())
                    {
                        const optional<float> time = sequencer->ResolveTime(item, error);
                        if (!time)
                        {
                            return false;
                        }
                        times.push_back(*time);
                    }
                    begin = end + 1;
                }
                if (times.empty())
                {
                    error = "times is empty";
                    return false;
                }
                return true;
            }
            for (const mcp_json::value& item : parsed.array_items)
            {
                const optional<float> time = json_time(sequencer, item, error);
                if (!time)
                {
                    return false;
                }
                times.push_back(*time);
            }
            return true;
        }

        // shot arguments shared by add and update, any motion argument turns the rig on unless rig is spelled out
        bool apply_shot_arguments(const Sequencer* sequencer, const McpRequest& request, Sequencer::CameraEvent& event, string& error)
        {
            optional<float> time;
            if (!read_time(sequencer, request, "time", time, error))
            {
                return false;
            }
            if (time)
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
                const optional<Sequencer::ShotEase> parsed = Sequencer::FindEase(*ease);
                if (!parsed || *parsed == Sequencer::ShotEase::Hold)
                {
                    error = "ease must be linear, in_out, in or out";
                    return false;
                }
                event.ease = *parsed;
                motion     = true;
            }
            if (const string* move = find(request, "move"))
            {
                const optional<Sequencer::ShotMove> parsed = Sequencer::FindMove(*move);
                if (!parsed)
                {
                    error = "move must be rig, orbit, dolly_zoom or path";
                    return false;
                }
                event.move = *parsed;
                motion     = true;
            }
            if (const string* path = find(request, "path"))
            {
                if (path->empty() || to_lower(*path) == "none")
                {
                    event.path_entity_id = 0;
                }
                else
                {
                    Entity* entity = resolve_spline(*path);
                    if (!entity)
                    {
                        error = "no spline entity matches path '" + *path + "'";
                        return false;
                    }
                    event.path_entity_id = entity->GetObjectId();
                    if (!find(request, "move"))
                    {
                        event.move = Sequencer::ShotMove::Path;
                    }
                }
                motion = true;
            }
            if (const optional<float> value = as_float(find(request, "blend_in")))
            {
                event.blend_in = clamp(*value, 0.0f, 10.0f);
                motion         = true;
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
                { "orbit_radius",      &event.orbit_radius },
                { "orbit_height",      &event.orbit_height },
                { "orbit_angle_start", &event.orbit_angle_start },
                { "orbit_angle_end",   &event.orbit_angle_end },
                { "orbit_rise",        &event.orbit_rise },
                { "path_start",        &event.path_start },
                { "path_end",          &event.path_end },
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

        bool apply_drive_arguments(const Sequencer* sequencer, const McpRequest& request, Sequencer::DriveEvent& event, string& error)
        {
            optional<float> start;
            optional<float> end;
            if (!read_time(sequencer, request, "start", start, error) || !read_time(sequencer, request, "end", end, error))
            {
                return false;
            }
            if (start)
            {
                event.start_time = *start;
            }
            if (end)
            {
                event.end_time = *end;
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
            if (const string* keys = find(request, "rev_keys"))
            {
                if (!parse_rev_keys(*keys, event.rev_keys))
                {
                    error = "rev_keys must be pairs of time seconds and throttle 0 to 1, e.g. 0,0,0.5,1,1.2,0";
                    return false;
                }
            }
            if (const string* spans = find(request, "burnouts"))
            {
                if (!parse_burnouts(*spans, event.burnouts))
                {
                    error = "burnouts must be triples of start seconds, end seconds and throttle 0.82 to 1, e.g. 4,7,1";
                    return false;
                }
            }
            return true;
        }

        string render_json(const Sequencer::RenderStatus& render)
        {
            string json = "{";
            json += string("\"active\":") + boolean(render.active);
            json += string(",\"capture\":") + boolean(render.capture);
            json += string(",\"scrubbed\":") + boolean(render.scrubbed);
            json += ",\"frames_written\":" + to_string(render.frames_written);
            json += ",\"frames_total\":" + to_string(render.frames_total);
            json += ",\"fps\":" + number(render.fps);
            json += ",\"directory\":" + quote(render.directory);
            json += ",\"last_error\":" + quote(render.last_error);
            json += ",\"files\":[";
            for (size_t i = 0; i < render.files.size(); i++)
            {
                json += (i > 0 ? "," : "") + quote(render.files[i]);
            }
            json += "],\"times\":[";
            for (size_t i = 0; i < render.times.size(); i++)
            {
                json += (i > 0 ? "," : "") + number(render.times[i]);
            }
            json += "]}";
            return json;
        }

        string color_json(const Vector3& value)
        {
            return "[" + number(value.x) + "," + number(value.y) + "," + number(value.z) + "]";
        }

        // the keyed tracks, music, beat grid, titles and markers, only keyed channels are listed
        string cinematic_json(const Sequencer::Snapshot& snapshot)
        {
            const Sequencer::Cinematic& cinematic = snapshot.cinematic;
            string json = "\"time_scale\":" + number(snapshot.time_scale);
            json += ",\"cinematic\":{";
            json += "\"bpm\":" + number(cinematic.bpm);
            json += ",\"beat_offset\":" + number(cinematic.beat_offset);
            json += ",\"beats_per_bar\":" + to_string(cinematic.beats_per_bar);
            json += string(",\"snap_to_beats\":") + boolean(cinematic.snap_to_beats);
            json += string(",\"slowmo_audio\":") + boolean(cinematic.slowmo_audio);
            json += ",\"fade_color\":" + color_json(cinematic.fade_color);
            json += ",\"music\":{\"path\":" + quote(cinematic.music.path) + ",\"start\":" + number(cinematic.music.start) +
                ",\"offset\":" + number(cinematic.music.offset) + ",\"volume\":" + number(cinematic.music.volume) + "}";
            json += ",\"curves\":{";
            bool first = true;
            for (size_t c = 0; c < static_cast<size_t>(Sequencer::Channel::Count); c++)
            {
                const vector<Sequencer::CurveKey>& keys = cinematic.curves[c];
                if (keys.empty())
                {
                    continue;
                }
                json += (first ? "" : ",") + quote(Sequencer::GetChannelName(static_cast<Sequencer::Channel>(c))) + ":[";
                first = false;
                for (size_t k = 0; k < keys.size(); k++)
                {
                    json += (k > 0 ? "," : "") + string("{\"index\":") + to_string(k) + ",\"time\":" + number(keys[k].time) + ",\"value\":" + number(keys[k].value) +
                        ",\"ease\":" + quote(Sequencer::GetEaseName(keys[k].ease)) + "}";
                }
                json += "]";
            }
            json += "},\"titles\":[";
            for (size_t i = 0; i < cinematic.titles.size(); i++)
            {
                const Sequencer::TitleEvent& title = cinematic.titles[i];
                json += (i > 0 ? "," : "") + string("{\"index\":") + to_string(i) + ",\"start\":" + number(title.start_time) + ",\"end\":" + number(title.end_time) +
                    ",\"text\":" + quote(title.text) + ",\"x\":" + number(title.x) + ",\"y\":" + number(title.y) + ",\"large\":" + boolean(title.large) +
                    ",\"fade\":" + number(title.fade) + ",\"color\":" + color_json(title.color) +
                    ",\"style\":" + quote(Sequencer::GetTitleStyleName(title.style)) + ",\"kicker\":" + quote(title.kicker) + ",\"accent\":" + color_json(title.accent) + "}";
            }
            json += "],\"markers\":[";
            for (size_t i = 0; i < cinematic.markers.size(); i++)
            {
                json += (i > 0 ? "," : "") + string("{\"index\":") + to_string(i) + ",\"time\":" + number(cinematic.markers[i].time) + ",\"name\":" + quote(cinematic.markers[i].name) + "}";
            }
            json += "]}";
            return json;
        }

        // every command answers with the whole timeline, entity names included so a caller can read the
        // reply without resolving ids of its own
        string state_reply(const Sequencer* sequencer)
        {
            const Sequencer::Snapshot snapshot = sequencer->GetSnapshot();

            string json = "{\"ok\":true";
            json += ",\"file\":\"" + escape(sequencer->GetFilePath()) + "\"";
            json += ",\"sequences\":[";
            const vector<string> sequences = sequencer->GetSequenceFiles();
            for (size_t i = 0; i < sequences.size(); i++)
            {
                json += (i > 0 ? ",\"" : "\"") + escape(FileSystem::GetFileNameFromFilePath(sequences[i])) + "\"";
            }
            json += "]";
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
                if (Sequencer::IsAnimated(event))
                {
                    const char* spaces[] = { "world", "anchor", "heading" };
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
                    json += ",\"ease\":" + quote(Sequencer::GetEaseName(event.ease));
                    json += ",\"move\":" + quote(Sequencer::GetMoveName(event.move));
                    if (event.move == Sequencer::ShotMove::Orbit)
                    {
                        json += ",\"orbit_radius\":" + number(event.orbit_radius);
                        json += ",\"orbit_height\":" + number(event.orbit_height);
                        json += ",\"orbit_angle_start\":" + number(event.orbit_angle_start);
                        json += ",\"orbit_angle_end\":" + number(event.orbit_angle_end);
                        json += ",\"orbit_rise\":" + number(event.orbit_rise);
                    }
                    if (event.move == Sequencer::ShotMove::Path)
                    {
                        json += ",\"path\":" + quote(event.path_entity_id != 0 ? entity_name(event.path_entity_id) : "");
                        json += ",\"path_start\":" + number(event.path_start);
                        json += ",\"path_end\":" + number(event.path_end);
                    }
                    json += ",\"blend_in\":" + number(event.blend_in);
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
                json += "],\"rev_keys\":[";
                for (size_t k = 0; k < event.rev_keys.size(); k++)
                {
                    json += (k > 0 ? "," : "") + string("[") + number(event.rev_keys[k].time) + "," + number(event.rev_keys[k].throttle) + "]";
                }
                json += "],\"burnouts\":[";
                for (size_t k = 0; k < event.burnouts.size(); k++)
                {
                    json += (k > 0 ? "," : "") + string("[") + number(event.burnouts[k].start) + "," + number(event.burnouts[k].end) + "," + number(event.burnouts[k].throttle) + "]";
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
            json += "]," + cinematic_json(snapshot);
            json += ",\"render\":" + render_json(snapshot.render);
            json += "}";
            return json;
        }

        // an all=true argument means the whole track goes, which is a different operation from removing
        // one entry and is spelled that way in every remove command
        bool wants_everything(const McpRequest& request)
        {
            return as_bool(find(request, "all")).value_or(false);
        }

        using Channel    = Sequencer::Channel;
        using TrackGroup = Sequencer::TrackGroup;

        string channel_list(TrackGroup group)
        {
            string names;
            for (size_t c = 0; c < static_cast<size_t>(Channel::Count); c++)
            {
                if (Sequencer::GetChannelGroup(static_cast<Channel>(c)) == group)
                {
                    names += (names.empty() ? "" : ", ") + string(Sequencer::GetChannelName(static_cast<Channel>(c)));
                }
            }
            return names;
        }

        // the channel argument within a track, a track with a single channel needs none
        bool read_channel(const McpRequest& request, TrackGroup group, optional<Channel> fallback, optional<Channel>& output, string& error)
        {
            output              = fallback;
            const string* value = find(request, "channel");
            if (!value)
            {
                return true;
            }
            const optional<Channel> channel = Sequencer::FindChannel(*value);
            if (!channel || Sequencer::GetChannelGroup(*channel) != group)
            {
                error = "channel must be one of " + channel_list(group);
                return false;
            }
            output = channel;
            return true;
        }

        bool read_ease(const McpRequest& request, optional<Sequencer::ShotEase>& output, string& error)
        {
            output = nullopt;
            if (const string* value = find(request, "ease"))
            {
                output = Sequencer::FindEase(*value);
                if (!output)
                {
                    error = "ease must be linear, in_out, in, out or hold";
                    return false;
                }
            }
            return true;
        }

        // keys as json, [[time, value, ease], ...] or [{time, value, ease}, ...], time may be a beat:, bar: or marker: reference
        bool parse_curve_keys(const Sequencer* sequencer, const string& text, Sequencer::ShotEase default_ease, vector<Sequencer::CurveKey>& keys, string& error)
        {
            mcp_json::value parsed;
            if (!mcp_json::parse(text, parsed, error) || !parsed.is_array())
            {
                error = "keys must be a json array of [time, value, ease] or {time, value, ease}";
                return false;
            }
            for (const mcp_json::value& item : parsed.array_items)
            {
                const mcp_json::value* time  = nullptr;
                const mcp_json::value* value = nullptr;
                const mcp_json::value* ease  = nullptr;
                if (item.is_array() && item.array_items.size() >= 2)
                {
                    time  = &item.array_items[0];
                    value = &item.array_items[1];
                    ease  = item.array_items.size() > 2 ? &item.array_items[2] : nullptr;
                }
                else if (item.is_object())
                {
                    time  = item.find("time");
                    value = item.find("value");
                    ease  = item.find("ease");
                }
                if (!time || !value)
                {
                    error = "every key needs a time and a value";
                    return false;
                }
                Sequencer::CurveKey key;
                const optional<float> resolved = json_time(sequencer, *time, error);
                if (!resolved)
                {
                    return false;
                }
                key.time  = *resolved;
                key.value = static_cast<float>(value->number_or(0.0));
                key.ease  = default_ease;
                if (ease)
                {
                    const optional<Sequencer::ShotEase> parsed_ease = Sequencer::FindEase(ease->string_or(""));
                    if (!parsed_ease)
                    {
                        error = "ease must be linear, in_out, in, out or hold";
                        return false;
                    }
                    key.ease = *parsed_ease;
                }
                keys.push_back(key);
            }
            return true;
        }

        // set, span, remove, clear and list on the curve channels of one track
        string curve_command(Sequencer* sequencer, const McpRequest& request, TrackGroup group, optional<Channel> default_channel, const string& action)
        {
            string error;
            optional<Channel> channel;
            if (!read_channel(request, group, default_channel, channel, error))
            {
                return failure(error);
            }
            if (action == "list")
            {
                return state_reply(sequencer);
            }
            if (action == "clear")
            {
                for (size_t c = 0; c < static_cast<size_t>(Channel::Count); c++)
                {
                    const Channel candidate = static_cast<Channel>(c);
                    if (Sequencer::GetChannelGroup(candidate) == group && (!channel || *channel == candidate))
                    {
                        sequencer->ClearCurve(candidate);
                    }
                }
                return state_reply(sequencer);
            }
            if (!channel)
            {
                return failure("missing channel, one of " + channel_list(group));
            }
            optional<Sequencer::ShotEase> ease;
            if (!read_ease(request, ease, error))
            {
                return failure(error);
            }

            if (action == "set")
            {
                if (const string* keys_text = find(request, "keys"))
                {
                    vector<Sequencer::CurveKey> keys;
                    if (!parse_curve_keys(sequencer, *keys_text, ease.value_or(Sequencer::ShotEase::InOut), keys, error))
                    {
                        return failure(error);
                    }
                    if (as_bool(find(request, "replace")).value_or(false))
                    {
                        sequencer->ClearCurve(*channel);
                    }
                    for (const Sequencer::CurveKey& key : keys)
                    {
                        sequencer->SetCurveKey(*channel, key);
                    }
                    return state_reply(sequencer);
                }

                optional<float> time;
                if (!read_time(sequencer, request, "time", time, error))
                {
                    return failure(error);
                }
                const optional<float> value = as_float(find(request, "value"));
                if (!time || !value)
                {
                    return failure("set needs time and value, or keys");
                }
                Sequencer::CurveKey key;
                key.time  = *time;
                key.value = *value;
                key.ease  = ease.value_or(Sequencer::ShotEase::InOut);
                sequencer->SetCurveKey(*channel, key);
                return state_reply(sequencer);
            }

            if (action == "span")
            {
                // value between start and end, ramping from and back to base, a ramp of 0 cuts
                optional<float> start;
                optional<float> end;
                if (!read_time(sequencer, request, "start", start, error) || !read_time(sequencer, request, "end", end, error))
                {
                    return failure(error);
                }
                const optional<float> value = as_float(find(request, "value"));
                if (!start || !end || !value || *end <= *start)
                {
                    return failure("span needs start < end and value");
                }
                const Sequencer::Snapshot snapshot = sequencer->GetSnapshot();
                const vector<Sequencer::CurveKey>& existing = snapshot.cinematic.curves[static_cast<size_t>(*channel)];
                const float ramp       = clamp(as_float(find(request, "ramp")).value_or(0.25f), 0.0f, 10.0f);
                const bool cut         = ramp <= 0.0f;
                const float r          = cut ? 0.002f : ramp;
                optional<float> base   = as_float(find(request, "base"));
                if (!base)
                {
                    if (existing.empty() && group == TrackGroup::World)
                    {
                        return failure("world channels have no neutral value, pass base (the value outside the span)");
                    }
                    base = sequencer->SampleTracks(max(*start - r - 0.001f, 0.0f)).values[static_cast<size_t>(*channel)];
                }

                // whatever was keyed inside the span is replaced, so repeating a span is harmless
                for (int i = static_cast<int>(existing.size()) - 1; i >= 0; i--)
                {
                    if (existing[i].time >= *start - r - 0.001f && existing[i].time <= *end + r + 0.001f)
                    {
                        sequencer->RemoveCurveKey(*channel, i);
                    }
                }
                const Sequencer::ShotEase shape = ease.value_or(Sequencer::ShotEase::InOut);
                if (*start - r >= 0.0f)
                {
                    sequencer->SetCurveKey(*channel, { *start - r, *base, shape });
                }
                sequencer->SetCurveKey(*channel, { *start, *value, cut ? Sequencer::ShotEase::Hold : shape });
                sequencer->SetCurveKey(*channel, { *end, *value, Sequencer::ShotEase::Linear });
                if (*end + r <= snapshot.duration)
                {
                    sequencer->SetCurveKey(*channel, { *end + r, *base, cut ? Sequencer::ShotEase::Hold : shape });
                }
                return state_reply(sequencer);
            }

            if (action == "remove")
            {
                if (const optional<uint64_t> index = as_uint(find(request, "index")))
                {
                    if (!sequencer->RemoveCurveKey(*channel, static_cast<int>(*index)))
                    {
                        return failure(index_out_of_range);
                    }
                    return state_reply(sequencer);
                }
                optional<float> time;
                if (!read_time(sequencer, request, "time", time, error))
                {
                    return failure(error);
                }
                if (!time)
                {
                    return failure("remove needs index or time");
                }
                const Sequencer::Snapshot snapshot = sequencer->GetSnapshot();
                const vector<Sequencer::CurveKey>& keys = snapshot.cinematic.curves[static_cast<size_t>(*channel)];
                for (int i = 0; i < static_cast<int>(keys.size()); i++)
                {
                    if (fabsf(keys[i].time - *time) < 0.05f)
                    {
                        sequencer->RemoveCurveKey(*channel, i);
                        return state_reply(sequencer);
                    }
                }
                return failure("no " + string(Sequencer::GetChannelName(*channel)) + " key within 0.05 s of that time");
            }

            return failure("action must be set, span, remove, clear or list");
        }

        string curve_action(const McpRequest& request)
        {
            if (const string* action = find(request, "action"))
            {
                return to_lower(*action);
            }
            return find(request, "value") || find(request, "keys") ? "set" : "list";
        }

        bool apply_title_arguments(const Sequencer* sequencer, const McpRequest& request, Sequencer::TitleEvent& title, string& error)
        {
            optional<float> start;
            optional<float> end;
            if (!read_time(sequencer, request, "start", start, error) || !read_time(sequencer, request, "end", end, error))
            {
                return false;
            }
            if (start)
            {
                const float span = title.end_time - title.start_time;
                title.start_time = *start;
                title.end_time   = *start + max(span, 0.05f);
            }
            if (end)
            {
                title.end_time = *end;
            }
            if (const optional<float> duration = as_float(find(request, "duration")))
            {
                title.end_time = title.start_time + max(*duration, 0.05f);
            }
            if (const string* text = find(request, "text"))
            {
                title.text = *text;
                // json escapes arrive literally from some callers, a backslash n is meant as a line break
                for (size_t at = title.text.find("\\n"); at != string::npos; at = title.text.find("\\n", at))
                {
                    title.text.replace(at, 2, "\n");
                }
            }
            if (const string* preset = find(request, "position"))
            {
                const string name = to_lower(*preset);
                if (name == "center" || name == "centre")
                {
                    title.x = 0.5f;
                    title.y = 0.5f;
                }
                else if (name == "lower_third" || name == "bottom")
                {
                    title.x = 0.5f;
                    title.y = 0.82f;
                }
                else if (name == "top")
                {
                    title.x = 0.5f;
                    title.y = 0.14f;
                }
                else
                {
                    error = "position must be center, lower_third or top, or use x and y";
                    return false;
                }
            }
            if (const optional<float> x = as_float(find(request, "x")))
            {
                title.x = clamp(*x, 0.0f, 1.0f);
            }
            if (const optional<float> y = as_float(find(request, "y")))
            {
                title.y = clamp(*y, 0.0f, 1.0f);
            }
            if (const optional<bool> large = as_bool(find(request, "large")))
            {
                title.large = *large;
            }
            if (const optional<float> fade = as_float(find(request, "fade")))
            {
                title.fade = clamp(*fade, 0.0f, 10.0f);
            }
            if (const string* color = find(request, "color"))
            {
                const optional<Vector3> parsed = as_vector(color);
                if (!parsed)
                {
                    error = "color must be r,g,b in 0..1";
                    return false;
                }
                title.color = *parsed;
            }
            if (const string* style = find(request, "style"))
            {
                const optional<Sequencer::TitleStyle> parsed = Sequencer::FindTitleStyle(unquote(*style));
                if (!parsed)
                {
                    error = "style must be plain, trailer or legend";
                    return false;
                }
                title.style = *parsed;
            }
            if (const string* kicker = find(request, "kicker"))
            {
                title.kicker = *kicker;
            }
            if (const string* accent = find(request, "accent"))
            {
                const optional<Vector3> parsed = as_vector(accent);
                if (!parsed)
                {
                    error = "accent must be r,g,b in 0..1";
                    return false;
                }
                title.accent = *parsed;
            }
            return true;
        }

        float to_db(float peak)
        {
            return peak > 1e-5f ? 20.0f * log10f(peak) : -100.0f;
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

                // a bare name means sequencer_<name>.xml in the project folder, the same files the toolbar picker lists
                if (const string* file = find(request, "file"))
                {
                    string file_path = *file;
                    if (file_path.find('/') == string::npos && file_path.find('\\') == string::npos)
                    {
                        const string name = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_path);
                        file_path         = string(ResourceCache::GetProjectDirectory()) + (name.rfind("sequencer_", 0) == 0 ? name : "sequencer_" + name) + ".xml";
                    }
                    if (!FileSystem::Exists(file_path))
                    {
                        return failure("no sequence at '" + file_path + "'");
                    }
                    sequencer->LoadSequence(file_path);
                }

                Sequencer::TimelineRequest timeline;
                string error;
                if (!read_time(sequencer, request, "time", timeline.time, error))
                {
                    return failure(error);
                }
                timeline.duration = as_float(find(request, "duration"));
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

                if (!find(request, "time"))
                {
                    return failure("missing time");
                }

                Sequencer::CameraEvent event;
                string error;
                if (!apply_shot_arguments(sequencer, request, event, error))
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
                if (!apply_shot_arguments(sequencer, request, event, error))
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

                optional<float> start;
                optional<float> end;
                string error;
                if (!read_time(sequencer, request, "start", start, error) || !read_time(sequencer, request, "end", end, error))
                {
                    return failure(error);
                }
                const string* follower = find(request, "follower");
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

                optional<float> start;
                optional<float> end;
                string error;
                if (!read_time(sequencer, request, "start", start, error) || !read_time(sequencer, request, "end", end, error))
                {
                    return failure(error);
                }
                if (!sequencer->UpdateSplineEvent(static_cast<int>(*index), start, end, follower_id))
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

                if (!find(request, "start") || !find(request, "end") || !find(request, "spline"))
                {
                    return failure("missing start, end or spline");
                }

                Sequencer::DriveEvent event;
                string error;
                if (!apply_drive_arguments(sequencer, request, event, error))
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
                if (!apply_drive_arguments(sequencer, request, event, error))
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
                    string error;
                    optional<float> start;
                    optional<float> end;
                    if (!read_time(sequencer, request, "start", start, error) || !read_time(sequencer, request, "end", end, error))
                    {
                        return failure(error);
                    }
                    Sequencer::RenderRequest render;
                    render.fps     = as_float(find(request, "fps")).value_or(30.0f);
                    render.start   = start.value_or(0.0f);
                    render.end     = end.value_or(-1.0f);
                    render.preroll = as_float(find(request, "preroll")).value_or(1.0f);
                    render.stride  = static_cast<uint32_t>(as_uint(find(request, "stride")).value_or(1));
                    if (const string* directory = find(request, "directory"))
                    {
                        render.directory = *directory;
                    }
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

        add(
            "sequencer_time",
            [editor](const McpRequest& request) -> string
            {
                Sequencer* sequencer = editor->GetWidget<Sequencer>();
                if (!sequencer)
                {
                    return failure(missing_panel);
                }
                return curve_command(sequencer, request, TrackGroup::Time, Channel::TimeScale, curve_action(request));
            }
        );

        add(
            "sequencer_world",
            [editor](const McpRequest& request) -> string
            {
                Sequencer* sequencer = editor->GetWidget<Sequencer>();
                if (!sequencer)
                {
                    return failure(missing_panel);
                }
                return curve_command(sequencer, request, TrackGroup::World, nullopt, curve_action(request));
            }
        );

        add(
            "sequencer_audio",
            [editor](const McpRequest& request) -> string
            {
                Sequencer* sequencer = editor->GetWidget<Sequencer>();
                if (!sequencer)
                {
                    return failure(missing_panel);
                }

                const string action = curve_action(request);
                if (action == "music")
                {
                    Sequencer::Cinematic settings = sequencer->GetSnapshot().cinematic;
                    if (const string* path = find(request, "path"))
                    {
                        if (!path->empty() && !FileSystem::Exists(*path))
                        {
                            return failure("no audio file at '" + *path + "', paths are relative to the binaries folder or absolute");
                        }
                        settings.music.path = *path;
                    }
                    optional<float> start;
                    string error;
                    if (!read_time(sequencer, request, "start", start, error))
                    {
                        return failure(error);
                    }
                    settings.music.start  = start.value_or(settings.music.start);
                    settings.music.offset = as_float(find(request, "offset")).value_or(settings.music.offset);
                    settings.music.volume = as_float(find(request, "volume")).value_or(settings.music.volume);
                    sequencer->SetCinematicSettings(settings);
                    return state_reply(sequencer);
                }
                if (action == "slowmo_audio")
                {
                    Sequencer::Cinematic settings = sequencer->GetSnapshot().cinematic;
                    settings.slowmo_audio         = as_bool(find(request, "enabled")).value_or(true);
                    sequencer->SetCinematicSettings(settings);
                    return state_reply(sequencer);
                }
                if (action == "meter")
                {
                    // peaks are what each group produced since the previous meter call, after its gain
                    string json = "{\"ok\":true,\"time\":" + number(sequencer->GetSnapshot().time) + ",\"time_scale\":" + number(static_cast<float>(Timer::GetTimeScale())) + ",\"groups\":{";
                    for (uint8_t g = static_cast<uint8_t>(AudioGroup::Vehicle); g < static_cast<uint8_t>(AudioGroup::Count); g++)
                    {
                        const AudioGroup group = static_cast<AudioGroup>(g);
                        const float peak       = AudioMixer::ConsumePeak(group);
                        json += (g > static_cast<uint8_t>(AudioGroup::Vehicle) ? "," : "") + quote(AudioMixer::GetName(group)) + ":{";
                        json += "\"peak\":" + number(peak) + ",\"peak_db\":" + number(to_db(peak));
                        json += ",\"gain\":" + number(AudioMixer::GetGain(group)) + ",\"muffle\":" + number(AudioMixer::GetMuffle(group)) + ",\"pitch\":" + number(AudioMixer::GetPitch(group)) + "}";
                    }
                    json += "}}";
                    return json;
                }
                if (action == "group")
                {
                    const string* entity_ref = find(request, "entity");
                    const string* group_name = find(request, "group");
                    if (!entity_ref || !group_name)
                    {
                        return failure("group needs entity and group (auto, vehicle, ambience, sfx, music)");
                    }
                    AudioGroup group = AudioGroup::Auto;
                    if (!AudioMixer::FromName(*group_name, group))
                    {
                        return failure("group must be auto, vehicle, ambience, sfx or music");
                    }
                    Entity* entity = resolve_entity(*entity_ref);
                    if (!entity)
                    {
                        return failure("no entity matches '" + *entity_ref + "'");
                    }
                    // a car or prefab keeps its sources on children, every source below the entity joins the group
                    vector<Entity*> pending = { entity };
                    uint32_t changed        = 0;
                    while (!pending.empty())
                    {
                        Entity* current = pending.back();
                        pending.pop_back();
                        if (AudioSource* source = current->GetComponent<AudioSource>())
                        {
                            source->SetGroup(group);
                            changed++;
                        }
                        for (Entity* child : current->GetChildren())
                        {
                            pending.push_back(child);
                        }
                    }
                    if (changed == 0)
                    {
                        return failure("'" + entity->GetObjectName() + "' has no audio sources in its hierarchy");
                    }
                    return "{\"ok\":true,\"entity\":" + quote(entity->GetObjectName()) + ",\"group\":" + quote(AudioMixer::GetName(group)) + ",\"sources\":" + to_string(changed) + "}";
                }
                optional<Channel> fallback;
                if (const string* group_name = find(request, "group"))
                {
                    fallback = Sequencer::FindChannel(*group_name);
                }
                return curve_command(sequencer, request, TrackGroup::Audio, fallback, action);
            }
        );

        add(
            "sequencer_overlay",
            [editor](const McpRequest& request) -> string
            {
                Sequencer* sequencer = editor->GetWidget<Sequencer>();
                if (!sequencer)
                {
                    return failure(missing_panel);
                }

                string error;
                const string action = curve_action(request);
                if (action == "title" || action == "title_add")
                {
                    if (!find(request, "text") || !find(request, "start"))
                    {
                        return failure("title needs text and start");
                    }
                    Sequencer::TitleEvent title;
                    if (!apply_title_arguments(sequencer, request, title, error))
                    {
                        return failure(error);
                    }
                    sequencer->AddTitle(title);
                    return state_reply(sequencer);
                }
                if (action == "title_update")
                {
                    const optional<uint64_t> index = as_uint(find(request, "index"));
                    const Sequencer::Snapshot snapshot = sequencer->GetSnapshot();
                    if (!index || *index >= snapshot.cinematic.titles.size())
                    {
                        return failure(index ? index_out_of_range : "missing index");
                    }
                    Sequencer::TitleEvent title = snapshot.cinematic.titles[*index];
                    if (!apply_title_arguments(sequencer, request, title, error))
                    {
                        return failure(error);
                    }
                    sequencer->UpdateTitle(static_cast<int>(*index), title);
                    return state_reply(sequencer);
                }
                if (action == "title_remove")
                {
                    if (wants_everything(request))
                    {
                        sequencer->ClearTitles();
                        return state_reply(sequencer);
                    }
                    const optional<uint64_t> index = as_uint(find(request, "index"));
                    if (!index)
                    {
                        return failure("missing index, pass all=true to clear every title");
                    }
                    if (!sequencer->RemoveTitle(static_cast<int>(*index)))
                    {
                        return failure(index_out_of_range);
                    }
                    return state_reply(sequencer);
                }
                if (action == "fade_color")
                {
                    const optional<Vector3> color = as_vector(find(request, "color"));
                    if (!color)
                    {
                        return failure("fade_color needs color r,g,b in 0..1");
                    }
                    Sequencer::Cinematic settings = sequencer->GetSnapshot().cinematic;
                    settings.fade_color           = *color;
                    sequencer->SetCinematicSettings(settings);
                    return state_reply(sequencer);
                }
                if (action == "fade_in" || action == "fade_out")
                {
                    // fade_in clears from solid over duration starting at time, fade_out goes to solid
                    optional<float> time;
                    if (!read_time(sequencer, request, "time", time, error))
                    {
                        return failure(error);
                    }
                    const float start    = time.value_or(action == "fade_in" ? 0.0f : max(sequencer->GetSnapshot().duration - 1.0f, 0.0f));
                    const float duration = clamp(as_float(find(request, "duration")).value_or(1.0f), 0.0f, 60.0f);
                    const bool in        = action == "fade_in";
                    sequencer->SetCurveKey(Channel::Fade, { start, in ? 1.0f : 0.0f, Sequencer::ShotEase::InOut });
                    sequencer->SetCurveKey(Channel::Fade, { start + max(duration, 0.01f), in ? 0.0f : 1.0f, duration > 0.0f ? Sequencer::ShotEase::InOut : Sequencer::ShotEase::Hold });
                    return state_reply(sequencer);
                }
                return curve_command(sequencer, request, TrackGroup::Overlay, nullopt, action);
            }
        );

        add(
            "sequencer_markers",
            [editor](const McpRequest& request) -> string
            {
                Sequencer* sequencer = editor->GetWidget<Sequencer>();
                if (!sequencer)
                {
                    return failure(missing_panel);
                }

                string error;
                const string action = to_lower(find(request, "action") ? *find(request, "action") : string("list"));
                if (action == "list")
                {
                    return state_reply(sequencer);
                }
                if (action == "set")
                {
                    const string* name = find(request, "name");
                    optional<float> time;
                    if (!read_time(sequencer, request, "time", time, error))
                    {
                        return failure(error);
                    }
                    if (!name || name->empty() || !time)
                    {
                        return failure("set needs name and time, a marker with the same name moves");
                    }
                    sequencer->SetMarker({ *time, *name });
                    return state_reply(sequencer);
                }
                if (action == "remove")
                {
                    if (wants_everything(request))
                    {
                        sequencer->ClearMarkers();
                        return state_reply(sequencer);
                    }
                    const Sequencer::Snapshot snapshot = sequencer->GetSnapshot();
                    optional<uint64_t> index           = as_uint(find(request, "index"));
                    if (const string* name = find(request, "name"))
                    {
                        for (size_t i = 0; i < snapshot.cinematic.markers.size(); i++)
                        {
                            if (to_lower(snapshot.cinematic.markers[i].name) == to_lower(*name))
                            {
                                index = i;
                            }
                        }
                    }
                    if (!index || !sequencer->RemoveMarker(static_cast<int>(*index)))
                    {
                        return failure("remove needs a valid index or name, or all=true");
                    }
                    return state_reply(sequencer);
                }
                if (action == "grid")
                {
                    Sequencer::Cinematic settings = sequencer->GetSnapshot().cinematic;
                    settings.bpm                  = as_float(find(request, "bpm")).value_or(settings.bpm);
                    settings.beat_offset          = as_float(find(request, "beat_offset")).value_or(settings.beat_offset);
                    settings.beats_per_bar        = static_cast<uint32_t>(as_uint(find(request, "beats_per_bar")).value_or(settings.beats_per_bar));
                    settings.snap_to_beats        = as_bool(find(request, "snap")).value_or(settings.snap_to_beats);
                    sequencer->SetCinematicSettings(settings);
                    return state_reply(sequencer);
                }
                if (action == "resolve")
                {
                    const string* times_text = find(request, "times");
                    if (!times_text)
                    {
                        return failure("resolve needs times");
                    }
                    mcp_json::value parsed;
                    string parse_error;
                    vector<string> labels;
                    if (mcp_json::parse(*times_text, parsed, parse_error) && parsed.is_array())
                    {
                        for (const mcp_json::value& item : parsed.array_items)
                        {
                            labels.push_back(item.string_or(""));
                        }
                    }
                    else
                    {
                        size_t begin = 0;
                        while (begin <= times_text->size())
                        {
                            size_t end = times_text->find(',', begin);
                            if (end == string::npos)
                            {
                                end = times_text->size();
                            }
                            const string item = unquote(times_text->substr(begin, end - begin));
                            if (!item.empty())
                            {
                                labels.push_back(item);
                            }
                            begin = end + 1;
                        }
                    }
                    string json = "{\"ok\":true,\"times\":[";
                    for (size_t i = 0; i < labels.size(); i++)
                    {
                        string resolve_error;
                        const optional<float> time = sequencer->ResolveTime(labels[i], resolve_error);
                        json += (i > 0 ? "," : "") + string("{\"input\":") + quote(labels[i]) + (time ? ",\"seconds\":" + number(*time) : ",\"error\":" + quote(resolve_error)) + "}";
                    }
                    json += "]}";
                    return json;
                }
                return failure("action must be list, set, remove, grid or resolve");
            }
        );

        add(
            "sequencer_sample",
            [editor](const McpRequest& request) -> string
            {
                Sequencer* sequencer = editor->GetWidget<Sequencer>();
                if (!sequencer)
                {
                    return failure(missing_panel);
                }

                const string* times_text = find(request, "times");
                if (!times_text)
                {
                    return failure("missing times");
                }
                vector<float> times;
                string error;
                if (!read_times(sequencer, *times_text, times, error))
                {
                    return failure(error);
                }

                const Sequencer::Snapshot snapshot = sequencer->GetSnapshot();
                string json = "{\"ok\":true,\"samples\":[";
                for (size_t i = 0; i < times.size(); i++)
                {
                    const Sequencer::TrackSample sample = sequencer->SampleTracks(times[i]);
                    json += (i > 0 ? "," : "") + string("{\"time\":") + number(sample.time);
                    json += ",\"shot\":" + to_string(sample.shot);
                    if (sample.shot >= 0 && sample.shot < static_cast<int>(snapshot.events.size()))
                    {
                        const Sequencer::CameraEvent& event = snapshot.events[sample.shot];
                        json += ",\"shot_move\":" + quote(Sequencer::IsAnimated(event) ? Sequencer::GetMoveName(event.move) : "cut");
                        json += ",\"shot_target\":" + quote(event.target_entity_id != 0 ? entity_name(event.target_entity_id) : "");
                    }
                    if (sample.beat > 0.0f || snapshot.cinematic.bpm > 0.0f)
                    {
                        const uint32_t per_bar = max(snapshot.cinematic.beats_per_bar, 1u);
                        const float beat_index = max(sample.beat - 1.0f, 0.0f);
                        json += ",\"beat\":" + number(sample.beat);
                        json += ",\"bar\":" + to_string(static_cast<uint32_t>(beat_index) / per_bar + 1);
                    }
                    json += ",\"tracks\":{\"time_scale\":" + number(sample.values[static_cast<size_t>(Channel::TimeScale)]);
                    for (size_t c = 1; c < static_cast<size_t>(Channel::Count); c++)
                    {
                        if (sample.keyed[c])
                        {
                            json += "," + quote(Sequencer::GetChannelName(static_cast<Channel>(c))) + ":" + number(sample.values[c]);
                        }
                    }
                    json += "},\"titles\":[";
                    for (size_t t = 0; t < sample.titles.size(); t++)
                    {
                        json += (t > 0 ? "," : "") + quote(snapshot.cinematic.titles[sample.titles[t]].text);
                    }
                    json += "]";
                    for (const Sequencer::Marker& marker : snapshot.cinematic.markers)
                    {
                        if (fabsf(marker.time - sample.time) < 0.05f)
                        {
                            json += ",\"marker\":" + quote(marker.name);
                        }
                    }
                    json += "}";
                }
                json += "]}";
                return json;
            }
        );

        add(
            "sequencer_capture",
            [editor](const McpRequest& request) -> string
            {
                Sequencer* sequencer = editor->GetWidget<Sequencer>();
                if (!sequencer)
                {
                    return failure(missing_panel);
                }

                const string action = to_lower(find(request, "action") ? *find(request, "action") : string("start"));
                if (action == "status")
                {
                    return "{\"ok\":true,\"render\":" + render_json(sequencer->GetSnapshot().render) + "}";
                }
                if (action == "stop")
                {
                    sequencer->StopRender();
                    return "{\"ok\":true,\"render\":" + render_json(sequencer->GetSnapshot().render) + "}";
                }
                if (action != "start")
                {
                    return failure("action must be start, status or stop");
                }

                const string* times_text = find(request, "times");
                if (!times_text)
                {
                    return failure("missing times");
                }
                Sequencer::CaptureRequest capture;
                string error;
                if (!read_times(sequencer, *times_text, capture.times, error))
                {
                    return failure(error);
                }
                if (const string* mode = find(request, "mode"))
                {
                    capture.mode = to_lower(*mode);
                }
                capture.fps = as_float(find(request, "fps")).value_or(30.0f);
                if (const string* directory = find(request, "directory"))
                {
                    capture.directory = *directory;
                }
                if (!sequencer->StartCapture(capture, error))
                {
                    return failure(error);
                }
                return "{\"ok\":true,\"render\":" + render_json(sequencer->GetSnapshot().render) + "}";
            }
        );
    }
}
