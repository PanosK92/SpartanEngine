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

//= INCLUDES ==================================
#include "pch.h"
#include "EditorMcpCommands.h"
#include "Editor.h"
#include "widgets/Sequencer.h"
#include "world/World.h"
#include "world/Entity.h"
#include "world/components/Camera.h"
#include "world/components/SplineFollower.h"
#include <cstdlib>
//=============================================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
//======================

namespace editor_mcp
{
    namespace
    {
        const char* missing_panel = "the sequencer is not available";
        const char* index_out_of_range  = "index is out of range";

        string entity_name(uint64_t entity_id)
        {
            Entity* entity = World::GetEntityById(entity_id);
            return entity ? entity->GetObjectName() : "missing";
        }

        // a reference is either an entity id or an entity name, a name is what a caller writing by hand
        // has and an id is what a previous reply gave it
        Entity* resolve(const string& value, bool require_camera, bool require_follower)
        {
            const auto qualifies = [require_camera, require_follower](Entity* entity)
            {
                if (!entity)
                {
                    return false;
                }
                if (require_camera && !entity->GetComponent<Camera>())
                {
                    return false;
                }
                if (require_follower && !entity->GetComponent<SplineFollower>())
                {
                    return false;
                }
                return true;
            };

            if (!value.empty() && value.find_first_not_of("0123456789") == string::npos)
            {
                Entity* entity = World::GetEntityById(strtoull(value.c_str(), nullptr, 10));
                if (qualifies(entity))
                {
                    return entity;
                }
            }
            for (Entity* entity : World::GetEntities())
            {
                if (entity->GetObjectName() == value && qualifies(entity))
                {
                    return entity;
                }
            }
            return nullptr;
        }

        Entity* resolve_camera(const string& value)   { return resolve(value, true, false); }
        Entity* resolve_entity(const string& value)   { return resolve(value, false, false); }
        Entity* resolve_follower(const string& value) { return resolve(value, false, true); }

        // every command answers with the whole timeline, entity names included so a caller can read the
        // reply without resolving ids of its own
        string state_reply(const Sequencer* sequencer)
        {
            const Sequencer::Snapshot snapshot = sequencer->GetSnapshot();

            string json = "{\"ok\":true";
            json += ",\"duration\":" + to_string(snapshot.duration);
            json += ",\"time\":" + to_string(snapshot.time);
            json += string(",\"playing\":") + boolean(snapshot.playing);
            json += string(",\"loop\":") + boolean(snapshot.loop);
            json += ",\"events\":[";
            for (size_t i = 0; i < snapshot.events.size(); i++)
            {
                if (i > 0)
                {
                    json += ",";
                }
                const Sequencer::CameraEvent& event = snapshot.events[i];
                json += "{\"index\":" + to_string(i);
                json += ",\"time\":" + to_string(event.time);
                json += ",\"camera_entity_id\":" + quote(to_string(event.camera_entity_id));
                json += ",\"camera_name\":" + quote(entity_name(event.camera_entity_id));
                json += ",\"target_entity_id\":" + quote(to_string(event.target_entity_id));
                json += ",\"target_name\":" + quote(event.target_entity_id != 0 ? entity_name(event.target_entity_id) : "");
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
                json += ",\"start_time\":" + to_string(event.start_time);
                json += ",\"end_time\":" + to_string(event.end_time);
                json += ",\"follower_entity_id\":" + quote(to_string(event.follower_entity_id));
                json += ",\"follower_name\":" + quote(entity_name(event.follower_entity_id));
                json += "}";
            }
            json += "]}";
            return json;
        }

        // an all=true argument means the whole track goes, which is a different operation from removing
        // one entry and is spelled that way in both remove commands
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

                const optional<float> time = as_float(find(request, "time"));
                const string* camera       = find(request, "camera");
                if (!time || !camera)
                {
                    return failure("missing time or camera");
                }
                Entity* entity = resolve_camera(*camera);
                if (!entity)
                {
                    return failure("no camera entity matches '" + *camera + "'");
                }

                uint64_t target_id = 0;
                if (const string* target = find(request, "target"))
                {
                    Entity* target_entity = resolve_entity(*target);
                    if (!target_entity)
                    {
                        return failure("no entity matches '" + *target + "'");
                    }
                    target_id = target_entity->GetObjectId();
                }

                sequencer->AddCameraEvent(*time, entity->GetObjectId(), target_id);
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

                optional<uint64_t> camera_id;
                if (const string* camera = find(request, "camera"))
                {
                    Entity* entity = resolve_camera(*camera);
                    if (!entity)
                    {
                        return failure("no camera entity matches '" + *camera + "'");
                    }
                    camera_id = entity->GetObjectId();
                }

                // an empty target clears the lock rather than failing to find an entity called nothing
                optional<uint64_t> target_id;
                if (const string* target = find(request, "target"))
                {
                    if (target->empty() || to_lower(*target) == "none")
                    {
                        target_id = 0;
                    }
                    else
                    {
                        Entity* target_entity = resolve_entity(*target);
                        if (!target_entity)
                        {
                            return failure("no entity matches '" + *target + "'");
                        }
                        target_id = target_entity->GetObjectId();
                    }
                }

                if (
                    !sequencer->UpdateCameraEvent(
                        static_cast<int>(*index),
                        as_float(find(request, "time")),
                        camera_id,
                        target_id
                    )
                )
                {
                    return failure(index_out_of_range);
                }
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
    }
}
