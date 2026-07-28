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

//= INCLUDES ===================================
#include "pch.h"
#include "McpCommandsWorldBuild.h"
#include "McpCommands.h"
#include "McpCommandsCommon.h"
#include "McpGeometryKernel.h"
#include "McpTextureKernel.h"
#include "../Commands/Console/ConsoleCommands.h"
#include "../Commands/CommandStack.h"
#include "../Core/ProgressTracker.h"
#include "../Logging/Log.h"
#include "../Physics/PhysicsWorld.h"
#include "../Profiling/Profiler.h"
#include "../World/World.h"
#include "../World/Entity.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Component.h"
#include "../World/Components/AudioSource.h"
#include "../World/Components/Light.h"
#include "../World/Components/ParticleSystem.h"
#include "../World/Components/Physics.h"
#include "../World/Components/Render.h"
#include "../World/Components/Script.h"
#include "../World/Components/Spline.h"
#include "../World/Components/SplineFollower.h"
#include "../World/Components/Terrain.h"
#include "../World/Components/Text3D.h"
#include "../World/Prefab.h"
#include "../World/GameReady.h"
#include "../Car/Car.h"
#include "../Car/CarSimulation.h"
#include "../Car/CarState.h"
#include "../Resource/ResourceCache.h"
#include "../Resource/Import/ImageImporter.h"
#include "../Animation/Animation.h"
#include "../Geometry/GeometryGeneration.h"
#include "../Geometry/Mesh.h"
#include "../RHI/RHI_Texture.h"
#include "../Rendering/Material.h"
#include "../Rendering/Renderer.h"
#include "../Math/Vector2.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <optional>
#include <sstream>
#include <typeinfo>
#include <unordered_map>
//==============================================

// splines, districts and city blockouts, the largest group of commands and the one that shares the least
// with the rest, so it sits here rather than in McpCommands.cpp
namespace spartan::mcp_world_build
{
    using namespace mcp_common;

    std::string command_spline_query(const McpRequest& request)
    {
        if (ProgressTracker::IsLoading())
        {
            return json_error("world is loading");
        }

        Entity* entity = nullptr;
        if (get_argument(request, "id"))
        {
            std::string error;
            entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }
        }
        else
        {
            // no id given, prefer the spline that has followers, fall back to any spline
            Entity* any_spline = nullptr;
            for (Entity* candidate : World::GetEntities())
            {
                if (SplineFollower* follower = candidate->GetComponent<SplineFollower>())
                {
                    if (Entity* followed = World::GetEntityById(follower->GetSplineEntityId()))
                    {
                        entity = followed;
                        break;
                    }
                }
                if (!any_spline && candidate->GetComponent<Spline>())
                {
                    any_spline = candidate;
                }
            }
            if (entity == nullptr)
            {
                entity = any_spline;
            }
            if (entity == nullptr)
            {
                return json_error("no spline entity in the world, pass id to pick one");
            }
        }

        Spline* spline = entity->GetComponent<Spline>();
        if (spline == nullptr)
        {
            return json_error("entity does not have a spline component");
        }

        const float length = spline->GetLength();

        std::string json = "{\"ok\":true";
        json += ",\"id\":" + json_string(std::to_string(entity->GetObjectId()));
        json += ",\"name\":" + json_string(entity->GetObjectName());
        json += ",\"length\":" + std::to_string(length);
        json += ",\"closed_loop\":" + json_bool(spline->GetClosedLoop());

        const std::vector<math::Vector3> control_points = spline->GetControlPoints();
        json += ",\"control_points\":[";
        for (size_t i = 0; i < control_points.size(); i++)
        {
            if (i > 0)
            {
                json += ",";
            }
            json += json_vector3(control_points[i]);
        }
        json += "]";

        // every follower that is set to follow this spline, travel time makes camera cut math trivial
        float follower_speed = 0.0f;
        json += ",\"followers\":[";
        bool first = true;
        for (Entity* candidate : World::GetEntities())
        {
            SplineFollower* follower = candidate->GetComponent<SplineFollower>();
            if (!follower || follower->GetSplineEntityId() != entity->GetObjectId())
            {
                continue;
            }
            if (follower_speed == 0.0f)
            {
                follower_speed = follower->GetSpeed();
            }
            if (!first)
            {
                json += ",";
            }
            first = false;
            json += "{\"id\":" + json_string(std::to_string(candidate->GetObjectId()));
            json += ",\"name\":" + json_string(candidate->GetObjectName());
            json += ",\"speed\":" + std::to_string(follower->GetSpeed());
            json += ",\"follow_mode\":" + json_string(spline_follow_mode_to_name(follower->GetFollowMode()));
            json += ",\"align_to_spline\":" + json_bool(follower->GetAlignToSpline());
            json += ",\"flip_forward\":" + json_bool(follower->GetFlipForward());
            json += ",\"progress\":" + std::to_string(follower->GetProgress());
            json += ",\"position\":" + json_vector3(candidate->GetPosition());
            json += ",\"travel_time_seconds\":" + (follower->GetSpeed() > 0.0f ? std::to_string(length / follower->GetSpeed()) : std::string("null"));
            json += "}";
        }
        json += "]";

        // targets to project onto the spline, explicit list or every camera in the world by default
        std::vector<Entity*> targets;
        std::vector<std::string> unresolved;
        if (const std::optional<std::string> closest_to = get_argument(request, "closest_to"))
        {
            std::stringstream tokens(*closest_to);
            std::string token;
            while (std::getline(tokens, token, ','))
            {
                if (token.empty())
                {
                    continue;
                }

                Entity* target = nullptr;
                uint64_t target_id = 0;
                if (parse_uint64(token, target_id))
                {
                    target = World::GetEntityById(target_id);
                }
                else
                {
                    std::string resolve_error;
                    target = find_entity_by_name_unique(token, true, resolve_error);
                    if (target == nullptr)
                    {
                        target = find_entity_by_name_unique(token, false, resolve_error);
                    }
                }

                if (target != nullptr)
                {
                    targets.push_back(target);
                }
                else
                {
                    unresolved.push_back(token);
                }
            }
        }
        else
        {
            for (Entity* candidate : World::GetEntities())
            {
                if (candidate->GetComponent<Camera>())
                {
                    targets.push_back(candidate);
                }
            }
        }

        // closest point on the spline for each target, arc distance and pass time give exact camera cut moments
        if (!targets.empty() || !unresolved.empty())
        {
            const uint32_t sample_count = 512;

            // sample once, keep cumulative arc length per sample
            std::vector<math::Vector3> samples(sample_count + 1);
            std::vector<float> arc_lengths(sample_count + 1, 0.0f);
            for (uint32_t i = 0; i <= sample_count; i++)
            {
                samples[i] = spline->GetPoint(static_cast<float>(i) / static_cast<float>(sample_count));
                if (i > 0)
                {
                    arc_lengths[i] = arc_lengths[i - 1] + samples[i].Distance(samples[i - 1]);
                }
            }

            json += ",\"closest\":[";
            first = true;
            for (const std::string& token : unresolved)
            {
                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += "{\"query\":" + json_string(token) + ",\"error\":\"entity not found\"}";
            }
            for (Entity* target : targets)
            {
                if (!first)
                {
                    json += ",";
                }
                first = false;

                const math::Vector3 target_position = target->GetPosition();
                uint32_t best_index = 0;
                float best_distance_sq = std::numeric_limits<float>::max();
                for (uint32_t i = 0; i <= sample_count; i++)
                {
                    const float distance_sq = (samples[i] - target_position).LengthSquared();
                    if (distance_sq < best_distance_sq)
                    {
                        best_distance_sq = distance_sq;
                        best_index       = i;
                    }
                }

                json += "{\"id\":" + json_string(std::to_string(target->GetObjectId()));
                json += ",\"name\":" + json_string(target->GetObjectName());
                json += ",\"position\":" + json_vector3(target_position);
                json += ",\"t\":" + std::to_string(static_cast<float>(best_index) / static_cast<float>(sample_count));
                json += ",\"arc_distance\":" + std::to_string(arc_lengths[best_index]);
                json += ",\"distance_to_spline\":" + std::to_string((samples[best_index] - target_position).Length());
                json += ",\"spline_position\":" + json_vector3(samples[best_index]);
                json += ",\"pass_time_seconds\":" + (follower_speed > 0.0f ? std::to_string(arc_lengths[best_index] / follower_speed) : std::string("null"));
                json += "}";
            }
            json += "]";
        }

        json += "}";
        return json;
    }

    std::string command_spline_distribute(const McpRequest& request)
    {
        if (ProgressTracker::IsLoading())
        {
            return json_error("world is loading");
        }
        if (!is_edit_mode())
        {
            return json_error("distributing entities requires edit mode");
        }

        Entity* entity = nullptr;
        if (get_argument(request, "id"))
        {
            std::string error;
            entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }
        }
        else
        {
            for (Entity* candidate : World::GetEntities())
            {
                if (candidate->GetComponent<Spline>())
                {
                    entity = candidate;
                    break;
                }
            }
            if (entity == nullptr)
            {
                return json_error("no spline entity in the world, pass id to pick one");
            }
        }

        Spline* spline = entity->GetComponent<Spline>();
        if (spline == nullptr)
        {
            return json_error("entity does not have a spline component");
        }

        const float length = spline->GetLength();
        if (length <= 0.0f)
        {
            return json_error("spline has no length");
        }

        // targets, explicit comma separated list or every camera child of the spline entity
        std::vector<Entity*> targets;
        if (const std::optional<std::string> entities = get_argument(request, "entities"))
        {
            std::stringstream tokens(*entities);
            std::string token;
            while (std::getline(tokens, token, ','))
            {
                if (token.empty())
                {
                    continue;
                }

                Entity* target = nullptr;
                uint64_t target_id = 0;
                if (parse_uint64(token, target_id))
                {
                    target = World::GetEntityById(target_id);
                }
                else
                {
                    std::string resolve_error;
                    target = find_entity_by_name_unique(token, true, resolve_error);
                    if (target == nullptr)
                    {
                        target = find_entity_by_name_unique(token, false, resolve_error);
                    }
                }

                if (target == nullptr)
                {
                    return json_error("entity not found: " + token);
                }
                targets.push_back(target);
            }
        }
        else
        {
            for (Entity* child : entity->GetChildren())
            {
                if (child->GetComponent<Camera>())
                {
                    targets.push_back(child);
                }
            }
        }

        if (targets.size() < 2)
        {
            return json_error("need at least two entities to distribute");
        }

        // sample once for closest point lookups
        const uint32_t sample_count = 512;
        std::vector<math::Vector3> samples(sample_count + 1);
        std::vector<float> arc_lengths(sample_count + 1, 0.0f);
        for (uint32_t i = 0; i <= sample_count; i++)
        {
            samples[i] = spline->GetPoint(static_cast<float>(i) / static_cast<float>(sample_count));
            if (i > 0)
            {
                arc_lengths[i] = arc_lengths[i - 1] + samples[i].Distance(samples[i - 1]);
            }
        }

        auto closest_arc_distance = [&](const math::Vector3& position)
        {
            uint32_t best_index = 0;
            float best_distance_sq = std::numeric_limits<float>::max();
            for (uint32_t i = 0; i <= sample_count; i++)
            {
                const float distance_sq = (samples[i] - position).LengthSquared();
                if (distance_sq < best_distance_sq)
                {
                    best_distance_sq = distance_sq;
                    best_index       = i;
                }
            }
            return arc_lengths[best_index];
        };

        // keep the order the entities already have along the road
        std::sort(targets.begin(), targets.end(), [&](Entity* a, Entity* b)
        {
            return closest_arc_distance(a->GetPosition()) < closest_arc_distance(b->GetPosition());
        });

        // optional overrides, lateral is signed meters from the centerline, positive is the right of travel
        float lateral_override = 0.0f;
        bool has_lateral = false;
        if (const std::optional<std::string> lateral_arg = get_argument(request, "lateral_offset"))
        {
            if (!parse_float(*lateral_arg, lateral_override))
            {
                return json_error("invalid lateral_offset");
            }
            has_lateral = true;
        }

        // edge offset is signed meters beyond the road edge, it tracks the varying road width so entities always clear the asphalt
        float edge_offset = 0.0f;
        bool has_edge = false;
        if (const std::optional<std::string> edge_arg = get_argument(request, "edge_offset"))
        {
            if (!parse_float(*edge_arg, edge_offset))
            {
                return json_error("invalid edge_offset");
            }
            has_edge = true;
        }
        float height_override = 0.0f;
        bool has_height = false;
        if (const std::optional<std::string> height_arg = get_argument(request, "height"))
        {
            if (!parse_float(*height_arg, height_override))
            {
                return json_error("invalid height");
            }
            has_height = true;
        }

        std::string json = "{\"ok\":true";
        json += ",\"id\":" + json_string(std::to_string(entity->GetObjectId()));
        json += ",\"name\":" + json_string(entity->GetObjectName());
        json += ",\"length\":" + std::to_string(length);
        json += ",\"placed\":[";

        // closed loops space n slots, open splines include both ends
        const float step = spline->GetClosedLoop() ? length / static_cast<float>(targets.size()) : length / static_cast<float>(targets.size() - 1);
        for (size_t i = 0; i < targets.size(); i++)
        {
            Entity* target = targets[i];

            // preserve each entity offset relative to the spline frame so framing survives the move
            const math::Vector3 old_position = target->GetPosition();
            const float old_t                = spline->GetTAtDistance(closest_arc_distance(old_position), 32);
            const math::Vector3 old_point    = spline->GetPoint(old_t);
            math::Vector3 old_tangent        = spline->GetTangent(old_t);
            old_tangent.Normalize();

            const float new_distance      = static_cast<float>(i) * step;
            const float new_t             = spline->GetTAtDistance(new_distance, 32);
            const math::Vector3 new_point = spline->GetPoint(new_t);
            math::Vector3 new_tangent     = spline->GetTangent(new_t);
            new_tangent.Normalize();

            const math::Quaternion old_frame = math::Quaternion::FromLookRotation(old_tangent, math::Vector3::Up);
            const math::Quaternion new_frame = math::Quaternion::FromLookRotation(new_tangent, math::Vector3::Up);
            const math::Quaternion delta     = new_frame * old_frame.Inverse();

            if (has_lateral || has_edge || has_height)
            {
                math::Vector3 old_right = old_tangent.Cross(math::Vector3::Up);
                old_right.Normalize();
                math::Vector3 new_right = new_tangent.Cross(math::Vector3::Up);
                new_right.Normalize();

                float lateral = has_lateral ? lateral_override : old_right.Dot(old_position - old_point);
                if (has_edge)
                {
                    // road width interpolates from start to end, sidewalk extends the surface further
                    float half_width = (spline->GetRoadWidth() + (spline->GetRoadWidthEnd() - spline->GetRoadWidth()) * new_t) * 0.5f;
                    if (spline->GetSidewalkEnabled())
                    {
                        half_width += spline->GetSidewalkWidth();
                    }
                    lateral = (edge_offset < 0.0f ? -1.0f : 1.0f) * half_width + edge_offset;
                }
                const float height = has_height ? height_override : old_position.y - old_point.y;
                target->SetPosition(new_point + new_right * lateral + math::Vector3::Up * height);
            }
            else
            {
                target->SetPosition(new_point + delta * (old_position - old_point));
            }
            target->SetRotation(delta * target->GetRotation());

            if (i > 0)
            {
                json += ",";
            }
            json += "{\"id\":" + json_string(std::to_string(target->GetObjectId()));
            json += ",\"name\":" + json_string(target->GetObjectName());
            json += ",\"arc_distance\":" + std::to_string(new_distance);
            json += ",\"position\":" + json_vector3(target->GetPosition());
            json += "}";
        }
        json += "]}";
        return json;
    }

    bool parse_vector3_list(const std::string& value, std::vector<math::Vector3>& points)
    {
        std::vector<float> values;
        std::stringstream stream(value);
        std::string part;
        while (std::getline(stream, part, ','))
        {
            float parsed = 0.0f;
            if (!parse_float(part, parsed))
            {
                return false;
            }
            values.emplace_back(parsed);
        }

        if (values.empty() || (values.size() % 3) != 0)
        {
            return false;
        }

        points.clear();
        points.reserve(values.size() / 3);
        for (size_t i = 0; i + 2 < values.size(); i += 3)
        {
            const math::Vector3 point(values[i], values[i + 1], values[i + 2]);
            if (!point.IsFinite())
            {
                return false;
            }
            points.emplace_back(point);
        }
        return !points.empty();
    }

    std::vector<std::string> split_csv_tokens(const std::string& value)
    {
        std::vector<std::string> tokens;
        std::stringstream stream(value);
        std::string part;
        while (std::getline(stream, part, ','))
        {
            // trim spaces
            size_t start = 0;
            while (start < part.size() && std::isspace(static_cast<unsigned char>(part[start])))
            {
                start++;
            }
            size_t end = part.size();
            while (end > start && std::isspace(static_cast<unsigned char>(part[end - 1])))
            {
                end--;
            }
            if (end > start)
            {
                tokens.emplace_back(part.substr(start, end - start));
            }
        }
        return tokens;
    }

    Entity* resolve_entity_token(const std::string& token, std::string& error)
    {
        uint64_t id = 0;
        if (parse_uint64(token, id))
        {
            Entity* entity = World::GetEntityById(id);
            if (entity == nullptr)
            {
                error = "entity id not found";
                return nullptr;
            }
            return entity;
        }

        Entity* entity = find_entity_by_name_unique(token, true, error);
        if (entity != nullptr)
        {
            return entity;
        }
        return find_entity_by_name_unique(token, false, error);
    }

    math::Vector3 world_to_local_point(Entity* entity, const math::Vector3& world_position)
    {
        if (entity == nullptr)
        {
            return world_position;
        }
        return entity->GetMatrix().Inverted() * world_position;
    }

    void clear_spline_control_points(Entity* entity)
    {
        if (entity == nullptr)
        {
            return;
        }

        for (uint32_t pass = 0; pass < 64; pass++)
        {
            entity->AcquireChildren();
            std::vector<Entity*> to_remove;
            for (Entity* child : entity->GetChildren())
            {
                if (child == nullptr || !World::EntityExists(child))
                {
                    continue;
                }
                if (child->GetObjectName().find("spline_point_") == 0)
                {
                    to_remove.push_back(child);
                }
            }
            if (to_remove.empty())
            {
                break;
            }
            for (Entity* child : to_remove)
            {
                World::RemoveEntityImmediate(child);
            }
        }
    }

    bool subtree_render_bounds(Entity* root, math::BoundingBox& out_bounds)
    {
        bool has_bounds = false;
        out_bounds = math::BoundingBox::Zero;
        if (root == nullptr)
        {
            return false;
        }

        std::vector<Entity*> entities;
        entities.push_back(root);
        root->GetDescendants(&entities);
        for (Entity* entity : entities)
        {
            if (entity == nullptr)
            {
                continue;
            }
            if (Render* render = entity->GetComponent<Render>())
            {
                const math::BoundingBox& box = render->GetBoundingBox();
                if (box.GetMin().IsFinite() && box.GetMax().IsFinite() && box.GetSize().LengthSquared() > 0.0f)
                {
                    if (!has_bounds)
                    {
                        out_bounds = box;
                        has_bounds = true;
                    }
                    else
                    {
                        out_bounds.Merge(box);
                    }
                }
            }
        }
        return has_bounds;
    }

    bool is_landmark_noise(Entity* entity)
    {
        if (entity == nullptr)
        {
            return true;
        }

        const std::string name = to_lower_copy(entity->GetObjectName());
        if (
            name == "ground" || name == "default_ground" || name == "sun" || name == "directional_light" ||
            name == "camera" || name == "editor_camera" || name == "sky" || name.empty()
        )
        {
            return true;
        }
        if (entity->GetComponent<Camera>() != nullptr && entity->GetParent() == nullptr && entity->GetChildrenCount() == 0)
        {
            return true;
        }
        if (entity->GetComponent<Light>() != nullptr && entity->GetParent() == nullptr && entity->GetChildrenCount() == 0)
        {
            // bare root lights are not city landmarks
            if (name.find("light") != std::string::npos || name.find("sun") != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    std::string spline_road_receipt(Entity* entity, Spline* spline)
    {
        std::string json = "{\"ok\":true";
        json += ",\"entity\":" + entity_to_json_compact(entity);
        json += ",\"point_count\":" + std::to_string(spline->GetControlPointCount());
        json += ",\"length\":" + std::to_string(spline->GetLength());
        json += ",\"road_width\":" + std::to_string(spline->GetRoadWidth());
        json += ",\"profile\":" + json_string(spline_profile_to_name(spline->GetProfile()));
        json += ",\"conform_to_terrain\":" + json_bool(spline->GetConformToTerrain());
        json += ",\"closed_loop\":" + json_bool(spline->GetClosedLoop());
        json += ",\"mesh_enabled\":" + json_bool(spline->GetMeshEnabled());
        json += "}";
        return json;
    }

    bool apply_spline_road_properties(Spline* spline, const McpRequest& request, std::string& error, bool apply_defaults)
    {
        if (const std::optional<std::string> profile = get_argument(request, "profile"))
        {
            const std::optional<SplineProfile> parsed = spline_profile_from_name(*profile);
            if (!parsed)
            {
                error = "invalid profile";
                return false;
            }
            spline->SetProfile(*parsed);
        }
        else if (apply_defaults)
        {
            spline->SetProfile(SplineProfile::Road);
        }

        if (const std::optional<std::string> road_width = get_argument(request, "road_width"))
        {
            float parsed = 0.0f;
            if (!parse_float(*road_width, parsed) || parsed <= 0.0f)
            {
                error = "invalid road_width";
                return false;
            }
            spline->SetRoadWidth(parsed);
        }

        if (const std::optional<std::string> conform_to_terrain = get_argument(request, "conform_to_terrain"))
        {
            bool conform = true;
            if (!parse_bool(*conform_to_terrain, conform))
            {
                error = "invalid conform_to_terrain";
                return false;
            }
            spline->SetConformToTerrain(conform);
        }
        else if (apply_defaults)
        {
            spline->SetConformToTerrain(true);
        }

        if (const std::optional<std::string> closed = get_argument(request, "closed_loop"))
        {
            bool closed_loop = false;
            if (!parse_bool(*closed, closed_loop))
            {
                error = "invalid closed_loop";
                return false;
            }
            spline->SetClosedLoop(closed_loop);
        }
        else if (apply_defaults)
        {
            spline->SetClosedLoop(false);
        }

        if (const std::optional<std::string> mesh = get_argument(request, "mesh_enabled"))
        {
            bool mesh_enabled = true;
            if (!parse_bool(*mesh, mesh_enabled))
            {
                error = "invalid mesh_enabled";
                return false;
            }
            spline->SetMeshEnabled(mesh_enabled);
        }
        else if (apply_defaults)
        {
            spline->SetMeshEnabled(true);
        }

        if (const std::optional<std::string> sidewalk = get_argument(request, "sidewalk_enabled"))
        {
            bool sidewalk_enabled = false;
            if (!parse_bool(*sidewalk, sidewalk_enabled))
            {
                error = "invalid sidewalk_enabled";
                return false;
            }
            spline->SetSidewalkEnabled(sidewalk_enabled);
        }

        if (const std::optional<std::string> sidewalk_width = get_argument(request, "sidewalk_width"))
        {
            float parsed = 0.0f;
            if (!parse_float(*sidewalk_width, parsed) || parsed < 0.0f)
            {
                error = "invalid sidewalk_width";
                return false;
            }
            spline->SetSidewalkWidth(parsed);
            if (parsed > 0.0f)
            {
                spline->SetSidewalkEnabled(true);
            }
        }
        return true;
    }

    bool set_spline_control_points_world(Entity* entity, Spline* spline, const std::vector<math::Vector3>& world_points, bool append, std::string& error)
    {
        if (entity == nullptr || spline == nullptr)
        {
            error = "missing spline entity";
            return false;
        }
        if (world_points.size() < 2 && !append)
        {
            error = "need at least 2 control points";
            return false;
        }
        if (world_points.empty())
        {
            error = "missing control points";
            return false;
        }

        if (!append)
        {
            clear_spline_control_points(entity);
        }

        for (const math::Vector3& world_point : world_points)
        {
            spline->AddControlPoint(world_to_local_point(entity, world_point));
        }

        if (spline->GetControlPointCount() < 2)
        {
            error = "need at least 2 control points";
            return false;
        }

        if (spline->GetMeshEnabled())
        {
            spline->GenerateRoadMesh();
        }
        return true;
    }

    struct RoadObstacle
    {
        math::BoundingBox box;
        std::string name;
        uint64_t entity_id = 0;
    };

    math::BoundingBox expand_box_xz(const math::BoundingBox& box, float clearance)
    {
        math::Vector3 min = box.GetMin();
        math::Vector3 max = box.GetMax();
        min.x -= clearance;
        min.z -= clearance;
        max.x += clearance;
        max.z += clearance;
        return math::BoundingBox(min, max);
    }

    bool point_in_box_xz(const math::Vector3& point, const math::BoundingBox& box)
    {
        return point.x >= box.GetMin().x && point.x <= box.GetMax().x &&
               point.z >= box.GetMin().z && point.z <= box.GetMax().z;
    }

    math::Vector3 approach_point_outside_box(const math::Vector3& from, const math::BoundingBox& box, float standoff, float y)
    {
        const math::Vector3 min = box.GetMin();
        const math::Vector3 max = box.GetMax();
        const math::Vector3 center((min.x + max.x) * 0.5f, y, (min.z + max.z) * 0.5f);
        math::Vector3 dir = from - center;
        dir.y = 0.0f;
        if (dir.LengthSquared() < 0.0001f)
        {
            dir = math::Vector3(1.0f, 0.0f, 0.0f);
        }
        else
        {
            dir.Normalize();
        }

        // pick the face the approach comes from, then stand off outside that face
        const float half_x = (max.x - min.x) * 0.5f;
        const float half_z = (max.z - min.z) * 0.5f;
        math::Vector3 approach = center;
        if (std::fabs(dir.x) * half_z >= std::fabs(dir.z) * half_x)
        {
            approach.x = center.x + dir.x * (half_x + standoff);
            approach.z = std::clamp(from.z, min.z, max.z);
        }
        else
        {
            approach.z = center.z + dir.z * (half_z + standoff);
            approach.x = std::clamp(from.x, min.x, max.x);
        }
        approach.y = y;

        // if still inside due to clamp, push fully outside on the dominant axis
        if (point_in_box_xz(approach, box))
        {
            approach = center + dir * (std::max(half_x, half_z) + standoff);
            approach.y = y;
        }
        return approach;
    }

    bool landmark_approach_point(Entity* landmark, const math::Vector3& from, float standoff, math::Vector3& out_point)
    {
        if (landmark == nullptr)
        {
            return false;
        }

        math::BoundingBox bounds;
        if (!subtree_render_bounds(landmark, bounds))
        {
            out_point = landmark->GetPosition();
            return true;
        }

        const math::Vector3 size = bounds.GetSize();
        // tiny landmarks can use their origin; districts must be approached at the edge
        if (size.x < 8.0f && size.z < 8.0f)
        {
            out_point = landmark->GetPosition();
            return true;
        }

        out_point = approach_point_outside_box(from, bounds, standoff, landmark->GetPosition().y);
        return true;
    }

    bool segment_intersects_box_xz(const math::Vector3& a, const math::Vector3& b, const math::BoundingBox& box)
    {
        if (point_in_box_xz(a, box) || point_in_box_xz(b, box))
        {
            return true;
        }

        const float min_x = box.GetMin().x;
        const float max_x = box.GetMax().x;
        const float min_z = box.GetMin().z;
        const float max_z = box.GetMax().z;
        const math::Vector3 corners[4] = {
            { min_x, 0.0f, min_z },
            { max_x, 0.0f, min_z },
            { max_x, 0.0f, max_z },
            { min_x, 0.0f, max_z },
        };

        auto orient = [](const math::Vector3& p, const math::Vector3& q, const math::Vector3& r)
        {
            const float value = (q.z - p.z) * (r.x - q.x) - (q.x - p.x) * (r.z - q.z);
            if (std::fabs(value) < 0.0001f)
            {
                return 0;
            }
            return value > 0.0f ? 1 : 2;
        };
        auto on_segment = [](const math::Vector3& p, const math::Vector3& q, const math::Vector3& r)
        {
            return q.x <= std::max(p.x, r.x) && q.x >= std::min(p.x, r.x) &&
                   q.z <= std::max(p.z, r.z) && q.z >= std::min(p.z, r.z);
        };
        auto segments_intersect = [&](const math::Vector3& p1, const math::Vector3& q1, const math::Vector3& p2, const math::Vector3& q2)
        {
            const int o1 = orient(p1, q1, p2);
            const int o2 = orient(p1, q1, q2);
            const int o3 = orient(p2, q2, p1);
            const int o4 = orient(p2, q2, q1);
            if (o1 != o2 && o3 != o4)
            {
                return true;
            }
            if (o1 == 0 && on_segment(p1, p2, q1))
            {
                return true;
            }
            if (o2 == 0 && on_segment(p1, q2, q1))
            {
                return true;
            }
            if (o3 == 0 && on_segment(p2, p1, q2))
            {
                return true;
            }
            if (o4 == 0 && on_segment(p2, q1, q2))
            {
                return true;
            }
            return false;
        };

        for (uint32_t i = 0; i < 4; i++)
        {
            if (segments_intersect(a, b, corners[i], corners[(i + 1) % 4]))
            {
                return true;
            }
        }
        return false;
    }

    bool segment_blocked(const math::Vector3& a, const math::Vector3& b, const std::vector<RoadObstacle>& obstacles)
    {
        for (const RoadObstacle& obstacle : obstacles)
        {
            if (segment_intersects_box_xz(a, b, obstacle.box))
            {
                return true;
            }
        }
        return false;
    }

    std::vector<RoadObstacle> collect_road_obstacles(const std::vector<uint64_t>& ignore_ids, float clearance)
    {
        std::vector<RoadObstacle> obstacles;
        auto is_ignored = [&](uint64_t id)
        {
            for (uint64_t ignore_id : ignore_ids)
            {
                if (id == ignore_id)
                {
                    return true;
                }
            }
            return false;
        };

        for (Entity* entity : World::GetEntities())
        {
            if (entity == nullptr || entity->GetParent() != nullptr)
            {
                continue;
            }
            if (is_landmark_noise(entity) || is_ignored(entity->GetObjectId()))
            {
                continue;
            }

            math::BoundingBox bounds;
            if (Spline* spline = entity->GetComponent<Spline>())
            {
                // other roads are obstacles too, use control point span so we do not cross them
                const std::vector<math::Vector3> points = spline->GetControlPoints();
                if (points.size() < 2)
                {
                    continue;
                }
                bounds = math::BoundingBox(points.data(), static_cast<uint32_t>(points.size()));
                math::Vector3 min = bounds.GetMin();
                math::Vector3 max = bounds.GetMax();
                const float half_width = std::max(2.0f, spline->GetRoadWidth() * 0.5f + clearance * 0.25f);
                min.x -= half_width;
                min.z -= half_width;
                max.x += half_width;
                max.z += half_width;
                min.y -= 2.0f;
                max.y += 2.0f;
                bounds = math::BoundingBox(min, max);
            }
            else if (!subtree_render_bounds(entity, bounds))
            {
                continue;
            }

            const math::Vector3 size = bounds.GetSize();
            if (size.x < 4.0f && size.z < 4.0f)
            {
                continue;
            }
            if (size.x > 5000.0f || size.z > 5000.0f)
            {
                continue;
            }

            RoadObstacle obstacle;
            obstacle.box = expand_box_xz(bounds, clearance);
            obstacle.name = entity->GetObjectName();
            obstacle.entity_id = entity->GetObjectId();
            obstacles.push_back(obstacle);
        }
        return obstacles;
    }

    bool find_detour_point(const math::Vector3& a, const math::Vector3& b, const std::vector<RoadObstacle>& obstacles, float clearance, math::Vector3& out_point)
    {
        math::Vector3 delta = b - a;
        delta.y = 0.0f;
        const float length = delta.Length();
        if (length < 0.1f)
        {
            return false;
        }

        const math::Vector3 dir = delta / length;
        const math::Vector3 perp(-dir.z, 0.0f, dir.x);
        const math::Vector3 mid((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f);

        float max_extent = clearance * 8.0f;
        for (const RoadObstacle& obstacle : obstacles)
        {
            if (!segment_intersects_box_xz(a, b, obstacle.box))
            {
                continue;
            }
            const math::Vector3 size = obstacle.box.GetMax() - obstacle.box.GetMin();
            max_extent = std::max(max_extent, std::max(size.x, size.z) * 0.75f + clearance);
        }

        const float offsets[10] = {
            clearance * 1.5f, -clearance * 1.5f,
            clearance * 3.0f, -clearance * 3.0f,
            clearance * 5.0f, -clearance * 5.0f,
            max_extent * 0.5f, -max_extent * 0.5f,
            max_extent, -max_extent
        };

        for (float offset : offsets)
        {
            math::Vector3 candidate = mid + perp * offset;
            candidate.y = mid.y;
            if (!segment_blocked(a, candidate, obstacles) && !segment_blocked(candidate, b, obstacles))
            {
                out_point = candidate;
                return true;
            }
        }

        // fall back to expanded box corners of the first blocking obstacle
        for (const RoadObstacle& obstacle : obstacles)
        {
            if (!segment_intersects_box_xz(a, b, obstacle.box))
            {
                continue;
            }

            const math::Vector3 min = obstacle.box.GetMin();
            const math::Vector3 max = obstacle.box.GetMax();
            const float pad = std::max(clearance, 4.0f);
            const math::Vector3 corners[8] = {
                { min.x - pad, mid.y, min.z - pad },
                { max.x + pad, mid.y, min.z - pad },
                { max.x + pad, mid.y, max.z + pad },
                { min.x - pad, mid.y, max.z + pad },
                { min.x - pad, mid.y, (min.z + max.z) * 0.5f },
                { max.x + pad, mid.y, (min.z + max.z) * 0.5f },
                { (min.x + max.x) * 0.5f, mid.y, min.z - pad },
                { (min.x + max.x) * 0.5f, mid.y, max.z + pad },
            };

            float best_score = std::numeric_limits<float>::max();
            bool found = false;
            for (const math::Vector3& corner : corners)
            {
                if (segment_blocked(a, corner, obstacles) || segment_blocked(corner, b, obstacles))
                {
                    continue;
                }
                const float score = (corner - a).Length() + (b - corner).Length();
                if (score < best_score)
                {
                    best_score = score;
                    out_point = corner;
                    found = true;
                }
            }
            if (found)
            {
                return true;
            }
        }
        return false;
    }

    std::vector<math::Vector3> avoid_obstacles_on_path(const std::vector<math::Vector3>& input, const std::vector<RoadObstacle>& obstacles, float clearance)
    {
        if (input.size() < 2 || obstacles.empty())
        {
            return input;
        }

        std::vector<math::Vector3> path = input;
        for (uint32_t pass = 0; pass < 6; pass++)
        {
            bool changed = false;
            std::vector<math::Vector3> next;
            next.reserve(path.size() * 2);
            next.push_back(path.front());
            for (size_t i = 0; i + 1 < path.size(); i++)
            {
                const math::Vector3& a = next.back();
                const math::Vector3& b = path[i + 1];
                if (segment_blocked(a, b, obstacles))
                {
                    math::Vector3 detour;
                    if (find_detour_point(a, b, obstacles, clearance, detour))
                    {
                        next.push_back(detour);
                        changed = true;
                    }
                }
                next.push_back(b);
            }
            path = std::move(next);
            if (!changed)
            {
                break;
            }
        }
        return path;
    }

    std::string flat_points_string(const std::vector<math::Vector3>& points)
    {
        std::string flat;
        for (size_t i = 0; i < points.size(); i++)
        {
            if (i > 0)
            {
                flat += ",";
            }
            flat += std::to_string(points[i].x) + "," + std::to_string(points[i].y) + "," + std::to_string(points[i].z);
        }
        return flat;
    }

    std::string command_world_landmarks(const McpRequest& request)
    {
        if (ProgressTracker::IsLoading())
        {
            return json_error("world is loading");
        }

        uint32_t limit = 200;
        if (const std::optional<std::string> limit_arg = get_argument(request, "limit"))
        {
            uint64_t parsed = 0;
            if (!parse_uint64(*limit_arg, parsed) || parsed == 0 || parsed > 1000)
            {
                return json_error("limit must be between 1 and 1000");
            }
            limit = static_cast<uint32_t>(parsed);
        }

        bool include_tagged = true;
        if (const std::optional<std::string> tagged = get_argument(request, "include_tagged"))
        {
            if (!parse_bool(*tagged, include_tagged))
            {
                return json_error("invalid include_tagged");
            }
        }

        std::string json = "{\"ok\":true,\"landmarks\":[";
        bool first = true;
        uint32_t count = 0;
        for (Entity* entity : World::GetEntities())
        {
            if (entity == nullptr || count >= limit)
            {
                continue;
            }

            const bool is_root = entity->GetParent() == nullptr;
            const bool is_tagged = include_tagged && entity->HasTag("landmark");
            if (!is_root && !is_tagged)
            {
                continue;
            }
            if (is_landmark_noise(entity) && !is_tagged)
            {
                continue;
            }

            entity->AcquireChildren();
            math::BoundingBox bounds;
            const bool has_bounds = subtree_render_bounds(entity, bounds);
            if (!first)
            {
                json += ",";
            }
            first = false;
            count++;
            json += "{";
            json += "\"id\":" + json_string(std::to_string(entity->GetObjectId()));
            json += ",\"name\":" + json_string(entity->GetObjectName());
            json += ",\"position\":" + json_vector3(entity->GetPosition());
            json += ",\"child_count\":" + std::to_string(entity->GetChildrenCount());
            json += ",\"is_root\":" + json_bool(is_root);
            json += ",\"tagged_landmark\":" + json_bool(is_tagged);
            if (!entity->GetTags().empty())
            {
                json += ",\"tags\":" + entity_tags_json(entity);
            }
            if (has_bounds)
            {
                json += ",\"bounding_box\":" + json_bounding_box(bounds);
            }
            json += "}";
        }
        json += "],\"count\":" + std::to_string(count) + "}";
        return json;
    }

    std::string command_spline_create_road(const McpRequest& request)
    {
        if (ProgressTracker::IsLoading())
        {
            return json_error("world is loading");
        }
        if (!is_edit_mode())
        {
            return json_error("road creation requires edit mode");
        }

        const std::optional<std::string> points_arg = get_argument(request, "points");
        if (!points_arg || points_arg->empty())
        {
            return json_error("missing points");
        }

        std::vector<math::Vector3> world_points;
        if (!parse_vector3_list(*points_arg, world_points) || world_points.size() < 2)
        {
            return json_error("points must be a flat xyz list with at least 2 points");
        }

        Entity* parent = nullptr;
        if (const std::optional<std::string> parent_id = get_argument(request, "parent_id"))
        {
            uint64_t parsed_parent_id = 0;
            if (!parse_uint64(*parent_id, parsed_parent_id))
            {
                return json_error("invalid parent_id");
            }
            parent = World::GetEntityById(parsed_parent_id);
            if (parent == nullptr)
            {
                return json_error("parent entity not found");
            }
        }

        Entity* entity = World::CreateEntity();
        if (entity == nullptr)
        {
            return json_error("failed to create entity");
        }

        if (const std::optional<std::string> name = get_argument(request, "name"))
        {
            entity->SetObjectName(*name);
        }
        else
        {
            entity->SetObjectName("spline_road");
        }

        if (parent != nullptr)
        {
            entity->SetParent(parent);
        }

        // place the road root at the first point so local control points stay readable
        entity->SetPosition(world_points[0]);

        Spline* spline = entity->AddComponent<Spline>();
        if (spline == nullptr)
        {
            return json_error("failed to add spline component");
        }

        std::string error;
        if (!apply_spline_road_properties(spline, request, error, true))
        {
            return json_error(error);
        }
        if (!set_spline_control_points_world(entity, spline, world_points, false, error))
        {
            return json_error(error);
        }

        return spline_road_receipt(entity, spline);
    }

    std::string command_spline_set_control_points(const McpRequest& request)
    {
        if (ProgressTracker::IsLoading())
        {
            return json_error("world is loading");
        }
        if (!is_edit_mode())
        {
            return json_error("spline edits require edit mode");
        }

        std::string error;
        Entity* entity = get_entity_from_request(request, error);
        if (entity == nullptr)
        {
            return json_error(error);
        }

        Spline* spline = entity->GetComponent<Spline>();
        if (spline == nullptr)
        {
            return json_error("entity does not have a spline component");
        }

        const std::optional<std::string> points_arg = get_argument(request, "points");
        if (!points_arg || points_arg->empty())
        {
            return json_error("missing points");
        }

        std::vector<math::Vector3> world_points;
        if (!parse_vector3_list(*points_arg, world_points))
        {
            return json_error("points must be a flat xyz list");
        }

        bool append = false;
        if (const std::optional<std::string> append_arg = get_argument(request, "append"))
        {
            if (!parse_bool(*append_arg, append))
            {
                return json_error("invalid append");
            }
        }

        if (!apply_spline_road_properties(spline, request, error, false))
        {
            return json_error(error);
        }
        if (!set_spline_control_points_world(entity, spline, world_points, append, error))
        {
            return json_error(error);
        }

        return spline_road_receipt(entity, spline);
    }

    struct SplineChildPlacement
    {
        Entity* entity = nullptr;
        float lateral = 0.0f;
        float height = 0.0f;
        float old_arc = 0.0f;
    };

    std::vector<SplineChildPlacement> capture_spline_child_placements(Entity* entity, Spline* spline)
    {
        std::vector<SplineChildPlacement> placements;
        if (entity == nullptr || spline == nullptr || spline->GetLength() <= 0.1f)
        {
            return placements;
        }

        const uint32_t sample_count = 512;
        std::vector<math::Vector3> samples(sample_count + 1);
        std::vector<float> arc_lengths(sample_count + 1, 0.0f);
        for (uint32_t i = 0; i <= sample_count; i++)
        {
            samples[i] = spline->GetPoint(static_cast<float>(i) / static_cast<float>(sample_count));
            if (i > 0)
            {
                arc_lengths[i] = arc_lengths[i - 1] + samples[i].Distance(samples[i - 1]);
            }
        }
        auto closest_index = [&](const math::Vector3& position)
        {
            uint32_t best = 0;
            float best_dist = std::numeric_limits<float>::max();
            for (uint32_t i = 0; i <= sample_count; i++)
            {
                const float dist = samples[i].Distance(position);
                if (dist < best_dist)
                {
                    best_dist = dist;
                    best = i;
                }
            }
            return best;
        };

        auto already_captured = [&](Entity* candidate)
        {
            for (const SplineChildPlacement& placement : placements)
            {
                if (placement.entity == candidate)
                {
                    return true;
                }
            }
            return false;
        };

        auto add_placement = [&](Entity* child)
        {
            if (child == nullptr || !World::EntityExists(child) || already_captured(child))
            {
                return;
            }
            const math::Vector3 position = child->GetPosition();
            const uint32_t index = closest_index(position);
            const float t = static_cast<float>(index) / static_cast<float>(sample_count);
            const math::Vector3 point = samples[index];
            math::Vector3 tangent = spline->GetTangent(t);
            tangent.y = 0.0f;
            if (tangent.LengthSquared() < 0.0001f)
            {
                tangent = math::Vector3(1.0f, 0.0f, 0.0f);
            }
            else
            {
                tangent.Normalize();
            }
            math::Vector3 right = tangent.Cross(math::Vector3::Up);
            if (right.LengthSquared() < 0.0001f)
            {
                right = math::Vector3(1.0f, 0.0f, 0.0f);
            }
            else
            {
                right.Normalize();
            }

            SplineChildPlacement placement;
            placement.entity = child;
            placement.lateral = right.Dot(position - point);
            placement.height = position.y - point.y;
            placement.old_arc = arc_lengths[index];
            placements.push_back(placement);
        };

        // 1) every direct child except control points (poles carry nested lights)
        entity->AcquireChildren();
        for (Entity* child : entity->GetChildren())
        {
            if (child == nullptr)
            {
                continue;
            }
            if (child->GetObjectName().find("spline_point_") == 0)
            {
                continue;
            }
            add_placement(child);
        }

        // 2) reclaim road furniture near the current path, and stranded road_* furniture left behind by earlier edits
        const float grab_radius = std::max(18.0f, spline->GetRoadWidth() * 2.0f + 8.0f);
        for (Entity* candidate : World::GetEntities())
        {
            if (candidate == nullptr || candidate == entity || !World::EntityExists(candidate))
            {
                continue;
            }
            if (candidate->GetParent() == entity)
            {
                continue;
            }

            Entity* root = candidate;
            while (root->GetParent() != nullptr)
            {
                root = root->GetParent();
            }
            if (
                root != entity &&
                root->GetComponent<Spline>() != nullptr
            )
            {
                continue;
            }
            if (root != candidate && root != entity)
            {
                const std::string root_name = to_lower_copy(root->GetObjectName());
                if (root->HasTag("landmark") || root->HasTag("district") ||
                    root_name.find("gas") != std::string::npos || root_name.find("dock") != std::string::npos ||
                    root_name.find("airway") != std::string::npos || root_name.find("market") != std::string::npos ||
                    root_name.find("downtown") != std::string::npos || root_name.find("park") != std::string::npos ||
                    root_name.find("industrial") != std::string::npos || root_name.find("residential") != std::string::npos)
                {
                    continue;
                }
            }

            const std::string name = to_lower_copy(candidate->GetObjectName());
            const bool is_road_named =
                name.find("road_light") != std::string::npos ||
                name.find("road_prop") != std::string::npos ||
                name.find("street_light") != std::string::npos ||
                name.find("streetlight") != std::string::npos;
            const bool looks_like_furniture =
                is_road_named ||
                candidate->GetComponent<Camera>() != nullptr ||
                (candidate->GetComponent<Light>() != nullptr && (name.find("light") != std::string::npos || name.find("pole") != std::string::npos)) ||
                (name.find("pole") != std::string::npos && name.find("light") != std::string::npos) ||
                name.find("camera") != std::string::npos;
            if (!looks_like_furniture)
            {
                continue;
            }

            const uint32_t index = closest_index(candidate->GetPosition());
            const float distance = samples[index].Distance(candidate->GetPosition());
            // named road furniture is reclaimed even if stranded far from the new path
            if (!is_road_named && distance > grab_radius)
            {
                continue;
            }

            Entity* move_root = candidate;
            if (candidate->GetParent() != nullptr && candidate->GetParent() != entity)
            {
                Entity* parent = candidate->GetParent();
                const std::string parent_name = to_lower_copy(parent->GetObjectName());
                if (parent->GetComponent<Render>() != nullptr || parent_name.find("pole") != std::string::npos ||
                    parent_name.find("road_light") != std::string::npos)
                {
                    move_root = parent;
                }
            }
            if (move_root->GetParent() != entity)
            {
                move_root->SetParent(entity);
            }
            add_placement(move_root);
        }

        std::sort(placements.begin(), placements.end(), [](const SplineChildPlacement& a, const SplineChildPlacement& b)
        {
            return a.old_arc < b.old_arc;
        });
        return placements;
    }

    uint32_t apply_spline_child_placements(Spline* spline, const std::vector<SplineChildPlacement>& placements)
    {
        if (spline == nullptr || placements.empty())
        {
            return 0;
        }

        const float length = spline->GetLength();
        if (length <= 0.1f)
        {
            return 0;
        }

        const float step = spline->GetClosedLoop()
            ? length / static_cast<float>(placements.size())
            : (placements.size() == 1 ? length * 0.5f : length / static_cast<float>(placements.size() - 1));

        uint32_t moved = 0;
        for (size_t i = 0; i < placements.size(); i++)
        {
            Entity* target = placements[i].entity;
            if (target == nullptr || !World::EntityExists(target))
            {
                continue;
            }
            const float new_distance = placements.size() == 1 ? step : static_cast<float>(i) * step;
            const float new_t = spline->GetTAtDistance(new_distance, 32);
            const math::Vector3 new_point = spline->GetPoint(new_t);
            math::Vector3 new_tangent = spline->GetTangent(new_t);
            new_tangent.y = 0.0f;
            if (new_tangent.LengthSquared() < 0.0001f)
            {
                new_tangent = math::Vector3(1.0f, 0.0f, 0.0f);
            }
            else
            {
                new_tangent.Normalize();
            }
            math::Vector3 new_right = new_tangent.Cross(math::Vector3::Up);
            if (new_right.LengthSquared() < 0.0001f)
            {
                new_right = math::Vector3(1.0f, 0.0f, 0.0f);
            }
            else
            {
                new_right.Normalize();
            }
            target->SetPosition(new_point + new_right * placements[i].lateral + math::Vector3::Up * placements[i].height);
            target->SetRotation(math::Quaternion::FromLookRotation(new_tangent, math::Vector3::Up));
            moved++;
        }
        return moved;
    }

    std::string command_spline_reroute(const McpRequest& request)
    {
        if (ProgressTracker::IsLoading())
        {
            return json_error("world is loading");
        }
        if (!is_edit_mode())
        {
            return json_error("spline reroute requires edit mode");
        }

        std::string error;
        Entity* entity = nullptr;
        if (get_argument(request, "id") || get_argument(request, "entity_id"))
        {
            entity = get_entity_from_request(request, error);
        }
        if (entity == nullptr)
        {
            if (const std::optional<std::string> name = get_argument(request, "name"))
            {
                entity = resolve_entity_token(*name, error);
            }
            else if (const std::optional<std::string> id_as_name = get_argument(request, "id"))
            {
                entity = resolve_entity_token(*id_as_name, error);
            }
        }
        if (entity == nullptr)
        {
            // last resort: first root named spline_road
            entity = resolve_entity_token("spline_road", error);
        }
        if (entity == nullptr)
        {
            return json_error(error.empty() ? "missing spline id or name" : error);
        }

        Spline* spline = entity->GetComponent<Spline>();
        if (spline == nullptr)
        {
            return json_error("entity does not have a spline component");
        }

        std::vector<math::Vector3> world_points = spline->GetControlPoints();
        if (world_points.size() < 2)
        {
            return json_error("spline needs at least 2 control points");
        }

        if (const std::optional<std::string> from_arg = get_argument(request, "from"))
        {
            Entity* from_entity = resolve_entity_token(*from_arg, error);
            if (from_entity == nullptr)
            {
                return json_error("from landmark not found: " + *from_arg);
            }
            math::Vector3 approach;
            if (!landmark_approach_point(from_entity, world_points.back(), 16.0f, approach))
            {
                approach = from_entity->GetPosition();
            }
            world_points.front() = approach;
        }
        if (const std::optional<std::string> to_arg = get_argument(request, "to"))
        {
            Entity* to_entity = resolve_entity_token(*to_arg, error);
            if (to_entity == nullptr)
            {
                return json_error("to landmark not found: " + *to_arg);
            }
            math::Vector3 approach;
            if (!landmark_approach_point(to_entity, world_points.front(), 16.0f, approach))
            {
                approach = to_entity->GetPosition();
            }
            world_points.back() = approach;
        }

        if (const std::optional<std::string> via = get_argument(request, "via"))
        {
            std::vector<math::Vector3> via_points;
            if (!parse_vector3_list(*via, via_points))
            {
                return json_error("via must be a flat xyz list");
            }
            if (!via_points.empty())
            {
                std::vector<math::Vector3> merged;
                merged.push_back(world_points.front());
                merged.insert(merged.end(), via_points.begin(), via_points.end());
                merged.push_back(world_points.back());
                world_points = std::move(merged);
            }
        }

        float clearance = 14.0f;
        if (const std::optional<std::string> clearance_arg = get_argument(request, "clearance"))
        {
            if (!parse_float(*clearance_arg, clearance) || clearance < 0.0f || clearance > 200.0f)
            {
                return json_error("clearance must be between 0 and 200");
            }
        }

        bool keep_children = true;
        if (const std::optional<std::string> keep_arg = get_argument(request, "keep_children"))
        {
            if (!parse_bool(*keep_arg, keep_children))
            {
                return json_error("invalid keep_children");
            }
        }

        const std::vector<SplineChildPlacement> child_placements = keep_children
            ? capture_spline_child_placements(entity, spline)
            : std::vector<SplineChildPlacement>{};

        const size_t points_before = world_points.size();
        const std::vector<uint64_t> ignore_ids = { entity->GetObjectId() };
        const std::vector<RoadObstacle> obstacles = collect_road_obstacles(ignore_ids, clearance);
        world_points = avoid_obstacles_on_path(world_points, obstacles, clearance);
        world_points = avoid_obstacles_on_path(world_points, obstacles, clearance);

        if (!set_spline_control_points_world(entity, spline, world_points, false, error))
        {
            return json_error(error);
        }

        const uint32_t redistributed = apply_spline_child_placements(spline, child_placements);

        std::string result = spline_road_receipt(entity, spline);
        if (!result.empty() && result.back() == '}')
        {
            result.pop_back();
            result += ",\"clearance\":" + std::to_string(clearance);
            result += ",\"obstacle_count\":" + std::to_string(obstacles.size());
            result += ",\"detour_points_added\":" + std::to_string(world_points.size() > points_before ? world_points.size() - points_before : 0);
            result += ",\"children_redistributed\":" + std::to_string(redistributed);
            result += "}";
        }
        return result;
    }

    std::string command_spline_connect(const McpRequest& request)
    {
        if (ProgressTracker::IsLoading())
        {
            return json_error("world is loading");
        }
        if (!is_edit_mode())
        {
            return json_error("road creation requires edit mode");
        }

        const std::optional<std::string> landmarks_arg = get_argument(request, "landmarks");
        if (!landmarks_arg || landmarks_arg->empty())
        {
            return json_error("missing landmarks");
        }

        const std::vector<std::string> tokens = split_csv_tokens(*landmarks_arg);
        if (tokens.size() < 2)
        {
            return json_error("landmarks needs at least 2 names or ids");
        }

        struct LandmarkStop
        {
            Entity* entity = nullptr;
            std::string name;
            uint64_t id = 0;
            math::Vector3 position;
        };

        std::vector<LandmarkStop> stops;
        stops.reserve(tokens.size());
        for (const std::string& token : tokens)
        {
            std::string resolve_error;
            Entity* landmark = resolve_entity_token(token, resolve_error);
            if (landmark == nullptr)
            {
                return json_error("landmark not found: " + token + (resolve_error.empty() ? "" : (", " + resolve_error)));
            }
            LandmarkStop stop;
            stop.entity = landmark;
            stop.name = landmark->GetObjectName();
            stop.id = landmark->GetObjectId();
            stop.position = landmark->GetPosition();
            stops.push_back(std::move(stop));
        }

        bool avoid_obstacles = true;
        if (const std::optional<std::string> avoid_arg = get_argument(request, "avoid_obstacles"))
        {
            if (!parse_bool(*avoid_arg, avoid_obstacles))
            {
                return json_error("invalid avoid_obstacles");
            }
        }

        float clearance = 12.0f;
        if (const std::optional<std::string> clearance_arg = get_argument(request, "clearance"))
        {
            if (!parse_float(*clearance_arg, clearance) || clearance < 0.0f || clearance > 200.0f)
            {
                return json_error("clearance must be between 0 and 200");
            }
        }

        float standoff = clearance + 4.0f;
        if (const std::optional<std::string> standoff_arg = get_argument(request, "standoff"))
        {
            if (!parse_float(*standoff_arg, standoff) || standoff < 0.0f || standoff > 200.0f)
            {
                return json_error("standoff must be between 0 and 200");
            }
        }

        // approach district edges, never aim at landmark centers (that drives through runways/yards)
        std::vector<math::Vector3> world_points;
        std::vector<std::string> resolved_names;
        world_points.reserve(stops.size());
        resolved_names.reserve(stops.size());
        for (size_t i = 0; i < stops.size(); i++)
        {
            math::Vector3 toward = stops[i].position;
            if (i + 1 < stops.size())
            {
                toward = stops[i + 1].position;
            }
            else if (i > 0)
            {
                toward = stops[i - 1].position;
            }

            math::Vector3 approach;
            if (!landmark_approach_point(stops[i].entity, toward, standoff, approach))
            {
                approach = stops[i].position;
            }
            world_points.push_back(approach);
            resolved_names.push_back(stops[i].name);
        }

        if (const std::optional<std::string> via = get_argument(request, "via"))
        {
            std::vector<math::Vector3> via_points;
            if (!parse_vector3_list(*via, via_points))
            {
                return json_error("via must be a flat xyz list");
            }
            if (world_points.size() >= 2 && !via_points.empty())
            {
                std::vector<math::Vector3> merged;
                merged.push_back(world_points.front());
                merged.insert(merged.end(), via_points.begin(), via_points.end());
                for (size_t i = 1; i < world_points.size(); i++)
                {
                    merged.push_back(world_points[i]);
                }
                world_points = std::move(merged);
            }
        }

        const size_t points_before_avoid = world_points.size();
        uint32_t obstacle_count = 0;
        if (avoid_obstacles)
        {
            // include destination districts as obstacles so paths skirt them instead of crossing
            const std::vector<uint64_t> ignore_ids;
            const std::vector<RoadObstacle> obstacles = collect_road_obstacles(ignore_ids, clearance);
            obstacle_count = static_cast<uint32_t>(obstacles.size());
            world_points = avoid_obstacles_on_path(world_points, obstacles, clearance);
        }

        McpRequest create_request = request;
        create_request.arguments["points"] = flat_points_string(world_points);
        if (!get_argument(create_request, "name"))
        {
            std::string auto_name = "road";
            for (const std::string& name : resolved_names)
            {
                auto_name += "_";
                auto_name += name;
            }
            create_request.arguments["name"] = auto_name;
        }

        std::string result = command_spline_create_road(create_request);
        if (result.find("\"ok\":true") == std::string::npos)
        {
            return result;
        }

        if (!result.empty() && result.back() == '}')
        {
            result.pop_back();
            result += ",\"landmarks\":[";
            for (size_t i = 0; i < resolved_names.size(); i++)
            {
                if (i > 0)
                {
                    result += ",";
                }
                result += json_string(resolved_names[i]);
            }
            result += "],\"avoid_obstacles\":" + json_bool(avoid_obstacles);
            result += ",\"clearance\":" + std::to_string(clearance);
            result += ",\"standoff\":" + std::to_string(standoff);
            result += ",\"obstacle_count\":" + std::to_string(obstacle_count);
            result += ",\"detour_points_added\":" + std::to_string(world_points.size() > points_before_avoid ? world_points.size() - points_before_avoid : 0);
            result += "}";
        }
        return result;
    }

    std::string command_spline_junction(const McpRequest& request)
    {
        if (ProgressTracker::IsLoading())
        {
            return json_error("world is loading");
        }
        if (!is_edit_mode())
        {
            return json_error("junction edits require edit mode");
        }

        const std::optional<std::string> roads_arg = get_argument(request, "roads");
        if (!roads_arg || roads_arg->empty())
        {
            return json_error("missing roads");
        }

        const std::vector<std::string> tokens = split_csv_tokens(*roads_arg);
        if (tokens.size() < 2)
        {
            return json_error("roads needs at least 2 names or ids");
        }

        struct RoadEnd
        {
            Entity* entity = nullptr;
            Spline* spline = nullptr;
            std::vector<math::Vector3> points;
            std::string name;
        };

        std::vector<RoadEnd> roads;
        roads.reserve(tokens.size());
        for (const std::string& token : tokens)
        {
            std::string resolve_error;
            Entity* entity = resolve_entity_token(token, resolve_error);
            if (entity == nullptr)
            {
                return json_error("road not found: " + token + (resolve_error.empty() ? "" : (", " + resolve_error)));
            }
            Spline* spline = entity->GetComponent<Spline>();
            if (spline == nullptr)
            {
                return json_error("entity is not a spline road: " + token);
            }
            std::vector<math::Vector3> points = spline->GetControlPoints();
            if (points.size() < 2)
            {
                return json_error("road needs at least 2 control points: " + token);
            }
            RoadEnd road;
            road.entity = entity;
            road.spline = spline;
            road.points = std::move(points);
            road.name = entity->GetObjectName();
            roads.push_back(std::move(road));
        }

        math::Vector3 junction;
        bool has_explicit_point = false;
        std::vector<bool> snap_start(roads.size(), false);

        if (const std::optional<std::string> point_arg = get_argument(request, "point"))
        {
            std::vector<math::Vector3> points;
            if (!parse_vector3_list(*point_arg, points) || points.size() != 1)
            {
                return json_error("point must be a single xyz triple");
            }
            junction = points[0];
            has_explicit_point = true;
            for (size_t i = 0; i < roads.size(); i++)
            {
                const float dist_start = (roads[i].points.front() - junction).Length();
                const float dist_end = (roads[i].points.back() - junction).Length();
                snap_start[i] = dist_start <= dist_end;
            }
        }
        else
        {
            // pick the closest endpoint pair across the first two roads, then fold in the rest
            float best_dist = std::numeric_limits<float>::max();
            bool best_a_start = true;
            bool best_b_start = true;
            const math::Vector3 ends_a[2] = { roads[0].points.front(), roads[0].points.back() };
            const math::Vector3 ends_b[2] = { roads[1].points.front(), roads[1].points.back() };
            for (size_t ia = 0; ia < 2; ia++)
            {
                for (size_t ib = 0; ib < 2; ib++)
                {
                    const float dist = (ends_a[ia] - ends_b[ib]).Length();
                    if (dist < best_dist)
                    {
                        best_dist = dist;
                        best_a_start = ia == 0;
                        best_b_start = ib == 0;
                    }
                }
            }
            snap_start[0] = best_a_start;
            snap_start[1] = best_b_start;

            for (size_t i = 2; i < roads.size(); i++)
            {
                math::Vector3 seed = math::Vector3::Zero;
                for (size_t j = 0; j < i; j++)
                {
                    seed += snap_start[j] ? roads[j].points.front() : roads[j].points.back();
                }
                seed = seed / static_cast<float>(i);
                const float dist_start = (roads[i].points.front() - seed).Length();
                const float dist_end = (roads[i].points.back() - seed).Length();
                snap_start[i] = dist_start <= dist_end;
            }

            junction = math::Vector3::Zero;
            for (size_t i = 0; i < roads.size(); i++)
            {
                junction += snap_start[i] ? roads[i].points.front() : roads[i].points.back();
            }
            junction = junction / static_cast<float>(roads.size());
        }

        std::string json = "{\"ok\":true,\"junction\":{\"x\":" + std::to_string(junction.x) +
            ",\"y\":" + std::to_string(junction.y) +
            ",\"z\":" + std::to_string(junction.z) +
            "},\"explicit_point\":" + json_bool(has_explicit_point) +
            ",\"roads\":[";

        for (size_t i = 0; i < roads.size(); i++)
        {
            RoadEnd& road = roads[i];
            if (snap_start[i])
            {
                road.points.front() = junction;
            }
            else
            {
                road.points.back() = junction;
            }

            std::string error;
            if (!set_spline_control_points_world(road.entity, road.spline, road.points, false, error))
            {
                return json_error("failed to update " + road.name + ": " + error);
            }

            if (i > 0)
            {
                json += ",";
            }
            json += "{";
            json += "\"id\":" + json_string(std::to_string(road.entity->GetObjectId()));
            json += ",\"name\":" + json_string(road.name);
            json += ",\"snapped\":" + json_string(snap_start[i] ? "start" : "end");
            json += ",\"point_count\":" + std::to_string(road.spline->GetControlPointCount());
            json += ",\"length\":" + std::to_string(road.spline->GetLength());
            json += "}";
        }

        json += "],\"count\":" + std::to_string(roads.size()) + "}";
        return json;
    }

    void clear_road_decoration(Entity* entity)
    {
        if (entity == nullptr)
        {
            return;
        }

        for (uint32_t pass = 0; pass < 64; pass++)
        {
            entity->AcquireChildren();
            std::vector<Entity*> to_remove;
            for (Entity* child : entity->GetChildren())
            {
                if (child == nullptr || !World::EntityExists(child))
                {
                    continue;
                }
                const std::string& name = child->GetObjectName();
                if (name.find("road_light_") == 0 || name.find("road_prop_") == 0)
                {
                    to_remove.push_back(child);
                }
            }
            if (to_remove.empty())
            {
                break;
            }
            for (Entity* child : to_remove)
            {
                World::RemoveEntity(child);
            }
        }
    }

    std::string command_spline_decorate(const McpRequest& request)
    {
        if (ProgressTracker::IsLoading())
        {
            return json_error("world is loading");
        }
        if (!is_edit_mode())
        {
            return json_error("road decoration requires edit mode");
        }

        std::string error;
        Entity* entity = get_entity_from_request(request, error);
        if (entity == nullptr)
        {
            return json_error(error);
        }

        Spline* spline = entity->GetComponent<Spline>();
        if (spline == nullptr)
        {
            return json_error("entity does not have a spline component");
        }

        const float length = spline->GetLength();
        if (length <= 1.0f)
        {
            return json_error("spline has no usable length");
        }

        float spacing = 28.0f;
        if (const std::optional<std::string> spacing_arg = get_argument(request, "spacing"))
        {
            if (!parse_float(*spacing_arg, spacing) || spacing < 5.0f || spacing > 200.0f)
            {
                return json_error("spacing must be between 5 and 200");
            }
        }

        bool lights = true;
        if (const std::optional<std::string> lights_arg = get_argument(request, "lights"))
        {
            if (!parse_bool(*lights_arg, lights))
            {
                return json_error("invalid lights");
            }
        }

        bool props = true;
        if (const std::optional<std::string> props_arg = get_argument(request, "props"))
        {
            if (!parse_bool(*props_arg, props))
            {
                return json_error("invalid props");
            }
        }

        bool sidewalks = true;
        if (const std::optional<std::string> sidewalks_arg = get_argument(request, "sidewalks"))
        {
            if (!parse_bool(*sidewalks_arg, sidewalks))
            {
                return json_error("invalid sidewalks");
            }
        }

        bool replace = true;
        if (const std::optional<std::string> replace_arg = get_argument(request, "replace"))
        {
            if (!parse_bool(*replace_arg, replace))
            {
                return json_error("invalid replace");
            }
        }

        if (const std::optional<std::string> road_width = get_argument(request, "road_width"))
        {
            float parsed = 0.0f;
            if (!parse_float(*road_width, parsed) || parsed <= 0.0f)
            {
                return json_error("invalid road_width");
            }
            spline->SetRoadWidth(parsed);
        }

        if (sidewalks)
        {
            spline->SetSidewalkEnabled(true);
            float sidewalk_width = 2.0f;
            if (const std::optional<std::string> width_arg = get_argument(request, "sidewalk_width"))
            {
                if (!parse_float(*width_arg, sidewalk_width) || sidewalk_width < 0.0f)
                {
                    return json_error("invalid sidewalk_width");
                }
            }
            spline->SetSidewalkWidth(sidewalk_width);
        }

        if (spline->GetMeshEnabled())
        {
            spline->GenerateRoadMesh();
        }

        if (replace)
        {
            clear_road_decoration(entity);
        }

        const float half_width = spline->GetRoadWidth() * 0.5f;
        const float light_lateral = half_width + (spline->GetSidewalkEnabled() ? spline->GetSidewalkWidth() : 0.0f) + 1.2f;
        const float prop_lateral = half_width + 0.6f;
        const uint32_t count = std::max(2u, static_cast<uint32_t>(std::ceil(length / spacing)) + 1u);

        uint32_t light_count = 0;
        uint32_t prop_count = 0;
        for (uint32_t i = 0; i < count; i++)
        {
            const float t = static_cast<float>(i) / static_cast<float>(count - 1);
            const math::Vector3 point = spline->GetPoint(t);
            math::Vector3 tangent = spline->GetTangent(t);
            tangent.y = 0.0f;
            if (tangent.LengthSquared() < 0.0001f)
            {
                tangent = math::Vector3(1.0f, 0.0f, 0.0f);
            }
            else
            {
                tangent.Normalize();
            }
            math::Vector3 right = tangent.Cross(math::Vector3::Up);
            if (right.LengthSquared() < 0.0001f)
            {
                right = math::Vector3(1.0f, 0.0f, 0.0f);
            }
            else
            {
                right.Normalize();
            }

            const float side = (i % 2 == 0) ? 1.0f : -1.0f;
            if (lights)
            {
                Entity* pole = World::CreateEntity();
                pole->SetObjectName("road_light_pole_" + std::to_string(light_count));
                pole->SetParent(entity);
                pole->SetPosition(point + right * (light_lateral * side) + math::Vector3(0.0f, 3.0f, 0.0f));
                pole->SetScale(math::Vector3(0.25f, 6.0f, 0.25f));
                if (Render* render = pole->AddComponent<Render>())
                {
                    render->SetMesh(MeshType::Cylinder);
                    render->SetDefaultMaterial();
                }

                Entity* lamp = World::CreateEntity();
                lamp->SetObjectName("road_light_" + std::to_string(light_count));
                lamp->SetParent(pole);
                lamp->SetPositionLocal(math::Vector3(0.0f, 0.55f, 0.0f));
                if (Light* light = lamp->AddComponent<Light>())
                {
                    light->SetLightType(LightType::Point);
                    light->SetColor(Color(1.0f, 0.92f, 0.78f, 1.0f));
                    light->SetTemperature(3200.0f);
                    light->SetIntensity(8500.0f);
                    light->SetRange(30.0f);
                    light->SetFlag(LightFlags::Shadows, true);
                    light->SetDrawDistance(60.0f);
                    light->SetShadowDistance(45.0f);
                }
                light_count++;
            }

            if (props && i > 0 && i + 1 < count && (i % 2 == 1))
            {
                Entity* barrier = World::CreateEntity();
                barrier->SetObjectName("road_prop_barrier_" + std::to_string(prop_count));
                barrier->SetParent(entity);
                barrier->SetPosition(point + right * (prop_lateral * -side) + math::Vector3(0.0f, 0.4f, 0.0f));
                barrier->SetScale(math::Vector3(0.35f, 0.8f, 1.6f));
                barrier->SetRotation(math::Quaternion::FromLookRotation(tangent, math::Vector3::Up));
                if (Render* render = barrier->AddComponent<Render>())
                {
                    render->SetMesh(MeshType::Cube);
                    render->SetDefaultMaterial();
                }
                prop_count++;
            }
        }

        std::string json = "{\"ok\":true";
        json += ",\"entity\":" + entity_to_json_compact(entity);
        json += ",\"length\":" + std::to_string(length);
        json += ",\"spacing\":" + std::to_string(spacing);
        json += ",\"lights\":" + std::to_string(light_count);
        json += ",\"props\":" + std::to_string(prop_count);
        json += ",\"sidewalks\":" + json_bool(spline->GetSidewalkEnabled());
        json += ",\"road_width\":" + std::to_string(spline->GetRoadWidth());
        json += "}";
        return json;
    }

    enum class DistrictPreset
    {
        Market,
        Downtown,
        Park,
        Industrial,
        Residential,
        Parking,
        Plaza,
        GasStation
    };

    std::optional<DistrictPreset> district_preset_from_name(const std::string& name)
    {
        const std::string value = to_lower_copy(name);
        if (value == "market")
        {
            return DistrictPreset::Market;
        }
        if (value == "downtown" || value == "skyscrapers" || value == "towers")
        {
            return DistrictPreset::Downtown;
        }
        if (value == "park")
        {
            return DistrictPreset::Park;
        }
        if (value == "industrial" || value == "dockyard")
        {
            return DistrictPreset::Industrial;
        }
        if (value == "residential" || value == "housing")
        {
            return DistrictPreset::Residential;
        }
        if (value == "parking" || value == "parking_lot")
        {
            return DistrictPreset::Parking;
        }
        if (value == "plaza" || value == "civic")
        {
            return DistrictPreset::Plaza;
        }
        if (value == "gas_station" || value == "gasstation")
        {
            return DistrictPreset::GasStation;
        }
        return std::nullopt;
    }

    std::string district_preset_to_name(DistrictPreset preset)
    {
        switch (preset)
        {
        case DistrictPreset::Market: return "market";
        case DistrictPreset::Downtown: return "downtown";
        case DistrictPreset::Park: return "park";
        case DistrictPreset::Industrial: return "industrial";
        case DistrictPreset::Residential: return "residential";
        case DistrictPreset::Parking: return "parking";
        case DistrictPreset::Plaza: return "plaza";
        case DistrictPreset::GasStation: return "gas_station";
        }
        return "market";
    }

    float district_density_scale(const std::string& density)
    {
        const std::string value = to_lower_copy(density);
        if (value == "low")
        {
            return 0.65f;
        }
        if (value == "high")
        {
            return 1.35f;
        }
        return 1.0f;
    }

    struct DistrictRng
    {
        uint32_t state = 1;

        explicit DistrictRng(uint32_t seed)
        {
            state = seed == 0 ? 1u : seed;
        }

        uint32_t next_u32()
        {
            state = state * 1664525u + 1013904223u;
            return state;
        }

        float next_01()
        {
            return static_cast<float>(next_u32() & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
        }

        float range(float min_value, float max_value)
        {
            return min_value + (max_value - min_value) * next_01();
        }

        int range_int(int min_value, int max_value)
        {
            if (max_value <= min_value)
            {
                return min_value;
            }
            return min_value + static_cast<int>(next_u32() % static_cast<uint32_t>(max_value - min_value + 1));
        }
    };

    struct DistrictBuildContext
    {
        Entity* parent = nullptr;
        DistrictRng* rng = nullptr;
        std::shared_ptr<Material> surface_material;
        std::shared_ptr<Material> structure_material;
        std::shared_ptr<Material> accent_material;
        float width = 40.0f;
        float depth = 40.0f;
        float density = 1.0f;
        bool lights = true;
        uint32_t part_count = 0;
        uint32_t light_count = 0;
    };

    std::shared_ptr<Material> district_blockout_material(
        const DistrictPreset preset,
        const std::string& role
    )
    {
        const std::string preset_name =
            district_preset_to_name(preset);
        const std::string path =
            World::GetGeneratedResourceDirectory() +
            "materials/" +
            "district_" +
            preset_name +
            "_" +
            role +
            EXTENSION_MATERIAL;
        if (
            std::shared_ptr<Material> existing =
                ResourceCache::GetByPath<Material>(path)
        )
        {
            return existing;
        }
        if (FileSystem::IsFile(path))
        {
            if (
                std::shared_ptr<Material> loaded =
                    ResourceCache::Load<Material>(path)
            )
            {
                return loaded;
            }
        }

        Color color(0.38f, 0.4f, 0.42f, 1.0f);
        if (role == "surface")
        {
            color = preset == DistrictPreset::Park
                ? Color(0.11f, 0.24f, 0.1f, 1.0f)
                : Color(0.18f, 0.19f, 0.2f, 1.0f);
        }
        else if (role == "accent")
        {
            color = preset == DistrictPreset::Industrial
                ? Color(0.48f, 0.2f, 0.06f, 1.0f)
                : Color(0.16f, 0.3f, 0.52f, 1.0f);
        }
        else if (preset == DistrictPreset::Residential)
        {
            color = Color(0.5f, 0.38f, 0.27f, 1.0f);
        }
        else if (preset == DistrictPreset::Market)
        {
            color = Color(0.52f, 0.4f, 0.22f, 1.0f);
        }

        const std::filesystem::path file_path(path);
        if (file_path.has_parent_path())
        {
            std::filesystem::create_directories(
                file_path.parent_path()
            );
        }
        std::shared_ptr<Material> material =
            std::make_shared<Material>();
        material->SetResourceFilePath(path);
        material->SetObjectName(
            "district_" +
            preset_name +
            "_" +
            role
        );
        material->ApplyPaintPreset(
            MaterialPaintPreset::Matte,
            color,
            false
        );
        material->SetProperty(
            MaterialProperty::Roughness,
            role == "accent" ? 0.55f : 0.82f
        );
        material->SaveToFile(path);
        return ResourceCache::Cache(material);
    }

    void clear_entity_children(Entity* entity)
    {
        if (entity == nullptr)
        {
            return;
        }
        for (uint32_t pass = 0; pass < 64; pass++)
        {
            entity->AcquireChildren();
            std::vector<Entity*> children = entity->GetChildren();
            if (children.empty())
            {
                break;
            }
            bool deleted_any = false;
            for (Entity* child : children)
            {
                if (child == nullptr || !World::EntityExists(child))
                {
                    continue;
                }
                World::RemoveEntityImmediate(child);
                deleted_any = true;
            }
            if (!deleted_any)
            {
                break;
            }
        }
        entity->AcquireChildren();
    }

    Entity* blockout_part(DistrictBuildContext& ctx, const std::string& name, MeshType mesh, const math::Vector3& local_pos, const math::Vector3& local_scale, const math::Vector3& local_euler = math::Vector3::Zero)
    {
        Entity* entity = World::CreateEntity();
        if (entity == nullptr || ctx.parent == nullptr)
        {
            return nullptr;
        }
        entity->SetObjectName(name);
        entity->SetParent(ctx.parent);
        entity->SetPositionLocal(local_pos);
        entity->SetScaleLocal(local_scale);
        if (local_euler.x != 0.0f || local_euler.y != 0.0f || local_euler.z != 0.0f)
        {
            entity->SetRotationLocal(math::Quaternion::FromEulerAngles(local_euler));
        }
        if (Render* render = entity->AddComponent<Render>())
        {
            render->SetMesh(mesh);
            const std::string lower_name =
                to_lower_copy(name);
            const bool is_surface =
                lower_name.find("pad") != std::string::npos ||
                lower_name.find("path") != std::string::npos ||
                lower_name.find("parking") != std::string::npos ||
                lower_name.find("courtyard") != std::string::npos ||
                lower_name.find("road") != std::string::npos;
            const bool is_accent =
                lower_name.find("canopy") != std::string::npos ||
                lower_name.find("container") != std::string::npos ||
                lower_name.find("sign") != std::string::npos ||
                lower_name.find("bench") != std::string::npos;
            const std::shared_ptr<Material> material =
                is_surface
                    ? ctx.surface_material
                    : (
                        is_accent
                            ? ctx.accent_material
                            : ctx.structure_material
                    );
            if (material)
            {
                render->SetMaterial(material);
            }
            else
            {
                render->SetDefaultMaterial();
            }
        }
        if (Physics* physics = entity->AddComponent<Physics>())
        {
            const BodyType body_type =
                mesh == MeshType::Sphere
                    ? BodyType::Sphere
                    : (
                        mesh == MeshType::Cylinder
                            ? BodyType::Capsule
                            : (
                                mesh == MeshType::Quad
                                    ? BodyType::Plane
                                    : BodyType::Box
                            )
                    );
            physics->SetBodyType(body_type);
            physics->SetStatic(true);
        }
        ctx.part_count++;
        return entity;
    }

    Entity* blockout_light(DistrictBuildContext& ctx, const std::string& name, const math::Vector3& local_pos, float intensity = 8500.0f, float range = 30.0f)
    {
        if (!ctx.lights || ctx.parent == nullptr)
        {
            return nullptr;
        }
        Entity* entity = World::CreateEntity();
        if (entity == nullptr)
        {
            return nullptr;
        }
        entity->SetObjectName(name);
        entity->SetParent(ctx.parent);
        entity->SetPositionLocal(local_pos);
        if (Light* light = entity->AddComponent<Light>())
        {
            light->SetLightType(LightType::Point);
            light->SetColor(Color(1.0f, 0.92f, 0.78f, 1.0f));
            light->SetTemperature(3200.0f);
            light->SetIntensity(intensity);
            light->SetRange(range);
            light->SetFlag(LightFlags::Shadows, true);
            light->SetDrawDistance(std::max(60.0f, range * 2.0f));
            light->SetShadowDistance(std::max(45.0f, range * 1.5f));
        }
        ctx.light_count++;
        return entity;
    }

    void build_district_pad(DistrictBuildContext& ctx, float thickness = 0.2f)
    {
        blockout_part(ctx, "pad", MeshType::Cube, math::Vector3(0.0f, thickness * 0.5f, 0.0f), math::Vector3(ctx.width, thickness, ctx.depth));
    }

    void build_district_market(DistrictBuildContext& ctx)
    {
        build_district_pad(ctx, 0.15f);
        const int stall_cols = std::max(2, static_cast<int>(3.0f * ctx.density));
        const int stall_rows = std::max(2, static_cast<int>(2.0f * ctx.density));
        const float stall_w = ctx.width * 0.18f;
        const float stall_d = ctx.depth * 0.16f;
        const float gap_x = ctx.width / static_cast<float>(stall_cols + 1);
        const float gap_z = ctx.depth / static_cast<float>(stall_rows + 1);
        uint32_t index = 0;
        for (int row = 0; row < stall_rows; row++)
        {
            for (int col = 0; col < stall_cols; col++)
            {
                const float x = -ctx.width * 0.5f + gap_x * static_cast<float>(col + 1);
                const float z = -ctx.depth * 0.5f + gap_z * static_cast<float>(row + 1);
                const float h = ctx.rng->range(2.2f, 3.4f);
                blockout_part(ctx, "stall_" + std::to_string(index), MeshType::Cube, math::Vector3(x, h * 0.5f, z), math::Vector3(stall_w, h, stall_d));
                blockout_part(ctx, "canopy_" + std::to_string(index), MeshType::Cube, math::Vector3(x, h + 0.15f, z), math::Vector3(stall_w * 1.15f, 0.2f, stall_d * 1.15f));
                if ((index % 2) == 0)
                {
                    blockout_light(ctx, "stall_light_" + std::to_string(index), math::Vector3(x, h + 1.5f, z), 7000.0f, 18.0f);
                }
                index++;
            }
        }
        blockout_part(ctx, "shop_a", MeshType::Cube, math::Vector3(-ctx.width * 0.38f, 2.5f, -ctx.depth * 0.38f), math::Vector3(ctx.width * 0.22f, 5.0f, ctx.depth * 0.18f));
        blockout_part(ctx, "shop_b", MeshType::Cube, math::Vector3(ctx.width * 0.36f, 2.2f, ctx.depth * 0.34f), math::Vector3(ctx.width * 0.2f, 4.4f, ctx.depth * 0.16f));
        blockout_light(ctx, "market_center_light", math::Vector3(0.0f, 8.0f, 0.0f), 12000.0f, 40.0f);
    }

    void build_district_downtown(DistrictBuildContext& ctx)
    {
        build_district_pad(ctx, 0.25f);
        const int towers = std::max(3, static_cast<int>(5.0f * ctx.density));
        for (int i = 0; i < towers; i++)
        {
            const float x = ctx.rng->range(-ctx.width * 0.35f, ctx.width * 0.35f);
            const float z = ctx.rng->range(-ctx.depth * 0.35f, ctx.depth * 0.35f);
            const float w = ctx.rng->range(ctx.width * 0.08f, ctx.width * 0.14f);
            const float d = ctx.rng->range(ctx.depth * 0.08f, ctx.depth * 0.14f);
            const float h = ctx.rng->range(18.0f, 55.0f) * ctx.density;
            blockout_part(ctx, "tower_" + std::to_string(i), MeshType::Cube, math::Vector3(x, h * 0.5f, z), math::Vector3(w, h, d));
            blockout_light(ctx, "tower_light_" + std::to_string(i), math::Vector3(x, h + 2.0f, z), 9000.0f, 28.0f);
        }
        blockout_part(ctx, "podium", MeshType::Cube, math::Vector3(0.0f, 2.0f, 0.0f), math::Vector3(ctx.width * 0.35f, 4.0f, ctx.depth * 0.28f));
        blockout_light(ctx, "downtown_plaza_light", math::Vector3(0.0f, 10.0f, 0.0f), 14000.0f, 45.0f);
    }

    void build_district_park(DistrictBuildContext& ctx)
    {
        build_district_pad(ctx, 0.12f);
        blockout_part(ctx, "path_x", MeshType::Cube, math::Vector3(0.0f, 0.18f, 0.0f), math::Vector3(ctx.width * 0.9f, 0.08f, ctx.depth * 0.08f));
        blockout_part(ctx, "path_z", MeshType::Cube, math::Vector3(0.0f, 0.18f, 0.0f), math::Vector3(ctx.width * 0.08f, 0.08f, ctx.depth * 0.9f));
        const int trees = std::max(6, static_cast<int>(10.0f * ctx.density));
        for (int i = 0; i < trees; i++)
        {
            const float x = ctx.rng->range(-ctx.width * 0.4f, ctx.width * 0.4f);
            const float z = ctx.rng->range(-ctx.depth * 0.4f, ctx.depth * 0.4f);
            if (std::fabs(x) < ctx.width * 0.06f || std::fabs(z) < ctx.depth * 0.06f)
            {
                continue;
            }
            const float trunk_h = ctx.rng->range(1.8f, 3.2f);
            blockout_part(ctx, "tree_trunk_" + std::to_string(i), MeshType::Cylinder, math::Vector3(x, trunk_h * 0.5f, z), math::Vector3(0.35f, trunk_h, 0.35f));
            blockout_part(ctx, "tree_crown_" + std::to_string(i), MeshType::Sphere, math::Vector3(x, trunk_h + 1.2f, z), math::Vector3(2.2f, 2.2f, 2.2f));
        }
        blockout_part(ctx, "bench_a", MeshType::Cube, math::Vector3(-ctx.width * 0.2f, 0.45f, ctx.depth * 0.12f), math::Vector3(2.2f, 0.5f, 0.55f));
        blockout_part(ctx, "bench_b", MeshType::Cube, math::Vector3(ctx.width * 0.18f, 0.45f, -ctx.depth * 0.15f), math::Vector3(2.2f, 0.5f, 0.55f), math::Vector3(0.0f, 90.0f, 0.0f));
        blockout_light(ctx, "park_light_a", math::Vector3(-ctx.width * 0.25f, 5.0f, -ctx.depth * 0.25f), 6500.0f, 25.0f);
        blockout_light(ctx, "park_light_b", math::Vector3(ctx.width * 0.25f, 5.0f, ctx.depth * 0.25f), 6500.0f, 25.0f);
    }

    void build_district_industrial(DistrictBuildContext& ctx)
    {
        build_district_pad(ctx, 0.2f);
        blockout_part(ctx, "warehouse", MeshType::Cube, math::Vector3(-ctx.width * 0.22f, 5.0f, 0.0f), math::Vector3(ctx.width * 0.42f, 10.0f, ctx.depth * 0.55f));
        blockout_part(ctx, "office", MeshType::Cube, math::Vector3(ctx.width * 0.32f, 2.5f, -ctx.depth * 0.28f), math::Vector3(ctx.width * 0.18f, 5.0f, ctx.depth * 0.2f));
        const int containers = std::max(4, static_cast<int>(8.0f * ctx.density));
        for (int i = 0; i < containers; i++)
        {
            const float x = ctx.width * 0.18f + static_cast<float>(i % 3) * 3.2f;
            const float z = -ctx.depth * 0.05f + static_cast<float>(i / 3) * 3.0f;
            const float y = 1.3f + static_cast<float>((i / 3) % 2) * 2.6f;
            blockout_part(ctx, "container_" + std::to_string(i), MeshType::Cube, math::Vector3(x, y, z), math::Vector3(6.0f, 2.6f, 2.5f), math::Vector3(0.0f, (i % 2) * 90.0f, 0.0f));
        }
        blockout_part(ctx, "crane_mast", MeshType::Cube, math::Vector3(ctx.width * 0.05f, 9.0f, ctx.depth * 0.28f), math::Vector3(1.0f, 18.0f, 1.0f));
        blockout_part(ctx, "crane_arm", MeshType::Cube, math::Vector3(ctx.width * 0.05f + 6.0f, 17.5f, ctx.depth * 0.28f), math::Vector3(14.0f, 0.8f, 0.8f));
        const float fence_h = 2.2f;
        blockout_part(ctx, "fence_n", MeshType::Cube, math::Vector3(0.0f, fence_h * 0.5f, -ctx.depth * 0.5f), math::Vector3(ctx.width, fence_h, 0.2f));
        blockout_part(ctx, "fence_s", MeshType::Cube, math::Vector3(0.0f, fence_h * 0.5f, ctx.depth * 0.5f), math::Vector3(ctx.width, fence_h, 0.2f));
        blockout_part(ctx, "fence_w", MeshType::Cube, math::Vector3(-ctx.width * 0.5f, fence_h * 0.5f, 0.0f), math::Vector3(0.2f, fence_h, ctx.depth));
        blockout_part(ctx, "fence_e", MeshType::Cube, math::Vector3(ctx.width * 0.5f, fence_h * 0.5f, 0.0f), math::Vector3(0.2f, fence_h, ctx.depth));
        blockout_light(ctx, "yard_light_a", math::Vector3(-ctx.width * 0.3f, 12.0f, -ctx.depth * 0.3f), 11000.0f, 40.0f);
        blockout_light(ctx, "yard_light_b", math::Vector3(ctx.width * 0.3f, 12.0f, ctx.depth * 0.2f), 11000.0f, 40.0f);
    }

    void build_district_residential(DistrictBuildContext& ctx)
    {
        build_district_pad(ctx, 0.15f);
        const int rows = std::max(2, static_cast<int>(2.0f * ctx.density));
        const int cols = std::max(3, static_cast<int>(3.0f * ctx.density));
        uint32_t index = 0;
        for (int row = 0; row < rows; row++)
        {
            for (int col = 0; col < cols; col++)
            {
                const float x = -ctx.width * 0.35f + static_cast<float>(col) * (ctx.width * 0.7f / std::max(1, cols - 1));
                const float z = -ctx.depth * 0.3f + static_cast<float>(row) * (ctx.depth * 0.6f / std::max(1, rows - 1));
                const float h = ctx.rng->range(6.0f, 14.0f) * (0.85f + 0.15f * ctx.density);
                blockout_part(ctx, "house_" + std::to_string(index), MeshType::Cube, math::Vector3(x, h * 0.5f, z), math::Vector3(ctx.width * 0.12f, h, ctx.depth * 0.14f));
                if ((index % 3) == 0)
                {
                    blockout_light(ctx, "house_light_" + std::to_string(index), math::Vector3(x, h + 1.5f, z), 6000.0f, 20.0f);
                }
                index++;
            }
        }
        blockout_part(ctx, "courtyard", MeshType::Cube, math::Vector3(0.0f, 0.2f, 0.0f), math::Vector3(ctx.width * 0.25f, 0.1f, ctx.depth * 0.25f));
    }

    void build_district_parking(DistrictBuildContext& ctx)
    {
        build_district_pad(ctx, 0.12f);
        const int rows = std::max(3, static_cast<int>(4.0f * ctx.density));
        for (int i = 0; i < rows; i++)
        {
            const float z = -ctx.depth * 0.4f + static_cast<float>(i) * (ctx.depth * 0.8f / std::max(1, rows - 1));
            blockout_part(ctx, "stall_strip_" + std::to_string(i), MeshType::Cube, math::Vector3(0.0f, 0.16f, z), math::Vector3(ctx.width * 0.85f, 0.05f, 0.25f));
        }
        const int poles = std::max(2, static_cast<int>(3.0f * ctx.density));
        for (int i = 0; i < poles; i++)
        {
            const float x = -ctx.width * 0.35f + static_cast<float>(i) * (ctx.width * 0.7f / std::max(1, poles - 1));
            blockout_part(ctx, "light_pole_" + std::to_string(i), MeshType::Cylinder, math::Vector3(x, 4.0f, ctx.depth * 0.42f), math::Vector3(0.2f, 8.0f, 0.2f));
            blockout_light(ctx, "lot_light_" + std::to_string(i), math::Vector3(x, 8.2f, ctx.depth * 0.42f), 9000.0f, 32.0f);
        }
    }

    void build_district_plaza(DistrictBuildContext& ctx)
    {
        build_district_pad(ctx, 0.18f);
        blockout_part(ctx, "wall_n", MeshType::Cube, math::Vector3(0.0f, 1.0f, -ctx.depth * 0.48f), math::Vector3(ctx.width * 0.9f, 2.0f, 0.35f));
        blockout_part(ctx, "wall_s", MeshType::Cube, math::Vector3(0.0f, 1.0f, ctx.depth * 0.48f), math::Vector3(ctx.width * 0.9f, 2.0f, 0.35f));
        blockout_part(ctx, "wall_w", MeshType::Cube, math::Vector3(-ctx.width * 0.48f, 1.0f, 0.0f), math::Vector3(0.35f, 2.0f, ctx.depth * 0.9f));
        blockout_part(ctx, "wall_e", MeshType::Cube, math::Vector3(ctx.width * 0.48f, 1.0f, 0.0f), math::Vector3(0.35f, 2.0f, ctx.depth * 0.9f));
        blockout_part(ctx, "monument_base", MeshType::Cube, math::Vector3(0.0f, 0.6f, 0.0f), math::Vector3(4.0f, 1.2f, 4.0f));
        blockout_part(ctx, "monument", MeshType::Cylinder, math::Vector3(0.0f, 5.0f, 0.0f), math::Vector3(1.2f, 8.0f, 1.2f));
        blockout_light(ctx, "plaza_light_a", math::Vector3(-ctx.width * 0.3f, 6.0f, -ctx.depth * 0.3f), 8000.0f, 28.0f);
        blockout_light(ctx, "plaza_light_b", math::Vector3(ctx.width * 0.3f, 6.0f, ctx.depth * 0.3f), 8000.0f, 28.0f);
        blockout_light(ctx, "plaza_center_light", math::Vector3(0.0f, 10.0f, 0.0f), 12000.0f, 35.0f);
    }

    void build_district_gas_station(DistrictBuildContext& ctx)
    {
        const float w = std::max(24.0f, ctx.width);
        const float d = std::max(18.0f, ctx.depth);
        ctx.width = w;
        ctx.depth = d;
        build_district_pad(ctx, 0.1f);
        blockout_part(ctx, "store", MeshType::Cube, math::Vector3(-w * 0.28f, 2.0f, -d * 0.2f), math::Vector3(8.0f, 4.0f, 6.0f));
        blockout_part(ctx, "store_roof", MeshType::Cube, math::Vector3(-w * 0.28f, 4.2f, -d * 0.2f), math::Vector3(8.6f, 0.35f, 6.6f));
        blockout_part(ctx, "canopy", MeshType::Cube, math::Vector3(w * 0.12f, 5.0f, d * 0.05f), math::Vector3(14.0f, 0.4f, 10.0f));
        blockout_part(ctx, "pillar_a", MeshType::Cylinder, math::Vector3(w * 0.12f - 5.0f, 2.5f, d * 0.05f - 3.5f), math::Vector3(0.4f, 5.0f, 0.4f));
        blockout_part(ctx, "pillar_b", MeshType::Cylinder, math::Vector3(w * 0.12f + 5.0f, 2.5f, d * 0.05f - 3.5f), math::Vector3(0.4f, 5.0f, 0.4f));
        blockout_part(ctx, "pillar_c", MeshType::Cylinder, math::Vector3(w * 0.12f - 5.0f, 2.5f, d * 0.05f + 3.5f), math::Vector3(0.4f, 5.0f, 0.4f));
        blockout_part(ctx, "pillar_d", MeshType::Cylinder, math::Vector3(w * 0.12f + 5.0f, 2.5f, d * 0.05f + 3.5f), math::Vector3(0.4f, 5.0f, 0.4f));
        for (int i = 0; i < 4; i++)
        {
            const float x = w * 0.12f - 3.0f + static_cast<float>(i) * 2.0f;
            blockout_part(ctx, "pump_" + std::to_string(i), MeshType::Cube, math::Vector3(x, 1.0f, d * 0.05f), math::Vector3(0.7f, 2.0f, 0.7f));
        }
        blockout_part(ctx, "sign_pole", MeshType::Cylinder, math::Vector3(w * 0.4f, 4.0f, -d * 0.35f), math::Vector3(0.25f, 8.0f, 0.25f));
        blockout_part(ctx, "sign_board", MeshType::Cube, math::Vector3(w * 0.4f, 7.5f, -d * 0.35f), math::Vector3(3.5f, 2.0f, 0.3f));
        blockout_light(ctx, "gs_light_canopy", math::Vector3(w * 0.12f, 4.7f, d * 0.05f), 12000.0f, 35.0f);
        blockout_light(ctx, "gs_light_store", math::Vector3(-w * 0.28f, 3.5f, -d * 0.05f), 8500.0f, 25.0f);
        blockout_light(ctx, "gs_light_sign", math::Vector3(w * 0.4f, 8.0f, -d * 0.35f), 7000.0f, 20.0f);
    }

    void build_district_preset(DistrictBuildContext& ctx, DistrictPreset preset)
    {
        switch (preset)
        {
        case DistrictPreset::Market: build_district_market(ctx); break;
        case DistrictPreset::Downtown: build_district_downtown(ctx); break;
        case DistrictPreset::Park: build_district_park(ctx); break;
        case DistrictPreset::Industrial: build_district_industrial(ctx); break;
        case DistrictPreset::Residential: build_district_residential(ctx); break;
        case DistrictPreset::Parking: build_district_parking(ctx); break;
        case DistrictPreset::Plaza: build_district_plaza(ctx); break;
        case DistrictPreset::GasStation: build_district_gas_station(ctx); break;
        }
    }

    math::Vector2 default_footprint_for_preset(DistrictPreset preset)
    {
        switch (preset)
        {
        case DistrictPreset::Market: return math::Vector2(48.0f, 40.0f);
        case DistrictPreset::Downtown: return math::Vector2(70.0f, 70.0f);
        case DistrictPreset::Park: return math::Vector2(60.0f, 50.0f);
        case DistrictPreset::Industrial: return math::Vector2(80.0f, 55.0f);
        case DistrictPreset::Residential: return math::Vector2(55.0f, 45.0f);
        case DistrictPreset::Parking: return math::Vector2(45.0f, 35.0f);
        case DistrictPreset::Plaza: return math::Vector2(40.0f, 40.0f);
        case DistrictPreset::GasStation: return math::Vector2(28.0f, 22.0f);
        }
        return math::Vector2(40.0f, 40.0f);
    }

    std::string district_receipt_json(Entity* entity, DistrictPreset preset, uint32_t part_count, uint32_t light_count)
    {
        std::string json = "{";
        json += "\"entity\":" + entity_to_json_compact(entity);
        json += ",\"preset\":" + json_string(district_preset_to_name(preset));
        json += ",\"created_count\":" + std::to_string(part_count);
        json += ",\"lights_count\":" + std::to_string(light_count);
        math::BoundingBox bounds;
        if (subtree_render_bounds(entity, bounds))
        {
            json += ",\"bounding_box\":" + json_bounding_box(bounds);
        }
        json += "}";
        return json;
    }

    bool boxes_overlap_xz(const math::BoundingBox& a, const math::BoundingBox& b, float gap)
    {
        return !(a.GetMax().x + gap < b.GetMin().x || b.GetMax().x + gap < a.GetMin().x ||
                 a.GetMax().z + gap < b.GetMin().z || b.GetMax().z + gap < a.GetMin().z);
    }

    math::BoundingBox footprint_box_at(const math::Vector3& center, float width, float depth)
    {
        const math::Vector3 half(width * 0.5f, 20.0f, depth * 0.5f);
        return math::BoundingBox(center - half, center + half);
    }

    std::vector<math::BoundingBox> collect_existing_landmark_boxes()
    {
        std::vector<math::BoundingBox> boxes;
        for (Entity* entity : World::GetEntities())
        {
            if (entity == nullptr || entity->GetParent() != nullptr)
            {
                continue;
            }
            if (is_landmark_noise(entity))
            {
                continue;
            }
            math::BoundingBox bounds;
            if (!subtree_render_bounds(entity, bounds))
            {
                continue;
            }
            const math::Vector3 size = bounds.GetSize();
            if (size.x < 4.0f && size.z < 4.0f)
            {
                continue;
            }
            if (size.x > 5000.0f || size.z > 5000.0f)
            {
                continue;
            }
            boxes.push_back(bounds);
        }
        return boxes;
    }

    bool footprint_blocked(const math::Vector3& center, float width, float depth, const std::vector<math::BoundingBox>& reserved, float gap)
    {
        const math::BoundingBox candidate = footprint_box_at(center, width, depth);
        for (const math::BoundingBox& box : reserved)
        {
            if (boxes_overlap_xz(candidate, box, gap))
            {
                return true;
            }
        }
        return false;
    }

    Entity* create_district_entity(const std::string& name, const math::Vector3& position, float rotation_y, DistrictPreset preset, float width, float depth, float density, bool lights, uint32_t seed, bool replace, uint32_t& part_count, uint32_t& light_count, std::string& error)
    {
        Entity* parent = nullptr;
        {
            std::string resolve_error;
            parent = find_entity_by_name_unique(name, true, resolve_error);
        }
        if (parent != nullptr)
        {
            if (!replace)
            {
                error = "entity already exists: " + name;
                return nullptr;
            }
            clear_entity_children(parent);
        }
        else
        {
            parent = World::CreateEntity();
            if (parent == nullptr)
            {
                error = "failed to create district entity";
                return nullptr;
            }
            parent->SetObjectName(name);
        }

        parent->SetPosition(position);
        if (std::fabs(rotation_y) > 0.001f)
        {
            parent->SetRotation(math::Quaternion::FromEulerAngles(math::Vector3(0.0f, rotation_y, 0.0f)));
        }
        parent->AddTag("landmark");
        parent->AddTag("district");
        parent->AddTag(district_preset_to_name(preset));

        DistrictRng rng(seed);
        DistrictBuildContext ctx;
        ctx.parent = parent;
        ctx.rng = &rng;
        ctx.surface_material =
            district_blockout_material(
                preset,
                "surface"
            );
        ctx.structure_material =
            district_blockout_material(
                preset,
                "structure"
            );
        ctx.accent_material =
            district_blockout_material(
                preset,
                "accent"
            );
        ctx.width = width;
        ctx.depth = depth;
        ctx.density = density;
        ctx.lights = lights;
        build_district_preset(ctx, preset);
        part_count = ctx.part_count;
        light_count = ctx.light_count;
        return parent;
    }

    std::string command_district_blockout(const McpRequest& request)
    {
        if (ProgressTracker::IsLoading())
        {
            return json_error("world is loading");
        }
        if (!is_edit_mode())
        {
            return json_error("district blockout requires edit mode");
        }

        const std::optional<std::string> preset_arg = get_argument(request, "preset");
        if (!preset_arg)
        {
            return json_error("missing preset");
        }
        const std::optional<DistrictPreset> preset = district_preset_from_name(*preset_arg);
        if (!preset)
        {
            return json_error("invalid preset, expected market, downtown, park, industrial, residential, parking, plaza, or gas_station");
        }

        math::Vector3 position = math::Vector3::Zero;
        if (const std::optional<std::string> position_arg = get_argument(request, "position"))
        {
            if (!parse_vector3(*position_arg, position))
            {
                return json_error("invalid position");
            }
        }

        math::Vector2 footprint = default_footprint_for_preset(*preset);
        if (const std::optional<std::string> footprint_arg = get_argument(request, "footprint"))
        {
            if (!parse_vector2(*footprint_arg, footprint) || footprint.x <= 1.0f || footprint.y <= 1.0f)
            {
                return json_error("footprint must be width,depth meters");
            }
        }

        float rotation_y = 0.0f;
        if (const std::optional<std::string> rotation_arg = get_argument(request, "rotation_y"))
        {
            if (!parse_float(*rotation_arg, rotation_y))
            {
                return json_error("invalid rotation_y");
            }
        }

        uint32_t seed = 1;
        if (const std::optional<std::string> seed_arg = get_argument(request, "seed"))
        {
            uint64_t parsed = 0;
            if (!parse_uint64(*seed_arg, parsed))
            {
                return json_error("invalid seed");
            }
            seed = static_cast<uint32_t>(parsed);
        }

        bool lights = true;
        if (const std::optional<std::string> lights_arg = get_argument(request, "lights"))
        {
            if (!parse_bool(*lights_arg, lights))
            {
                return json_error("invalid lights");
            }
        }

        bool replace = true;
        if (const std::optional<std::string> replace_arg = get_argument(request, "replace"))
        {
            if (!parse_bool(*replace_arg, replace))
            {
                return json_error("invalid replace");
            }
        }

        float density = 1.0f;
        if (const std::optional<std::string> density_arg = get_argument(request, "density"))
        {
            density = district_density_scale(*density_arg);
        }

        const std::string name = get_argument(request, "name").value_or(district_preset_to_name(*preset));
        uint32_t part_count = 0;
        uint32_t light_count = 0;
        std::string error;
        Entity* entity = create_district_entity(name, position, rotation_y, *preset, footprint.x, footprint.y, density, lights, seed, replace, part_count, light_count, error);
        if (entity == nullptr)
        {
            return json_error(error);
        }

        std::string json = "{\"ok\":true";
        json += ",\"entity\":" + entity_to_json_compact(entity);
        json += ",\"preset\":" + json_string(district_preset_to_name(*preset));
        json += ",\"created_count\":" + std::to_string(part_count);
        json += ",\"lights_count\":" + std::to_string(light_count);
        json += ",\"footprint\":[" + std::to_string(footprint.x) + "," + std::to_string(footprint.y) + "]";
        math::BoundingBox bounds;
        if (subtree_render_bounds(entity, bounds))
        {
            json += ",\"bounding_box\":" + json_bounding_box(bounds);
        }
        json += "}";
        return json;
    }

    std::string command_city_blockout(const McpRequest& request)
    {
        if (ProgressTracker::IsLoading())
        {
            return json_error("world is loading");
        }
        if (!is_edit_mode())
        {
            return json_error("city blockout requires edit mode");
        }

        math::Vector3 center = math::Vector3::Zero;
        if (const std::optional<std::string> center_arg = get_argument(request, "center"))
        {
            if (!parse_vector3(*center_arg, center))
            {
                return json_error("invalid center");
            }
        }

        float extent = 220.0f;
        if (const std::optional<std::string> extent_arg = get_argument(request, "extent"))
        {
            if (!parse_float(*extent_arg, extent) || extent < 40.0f || extent > 2000.0f)
            {
                return json_error("extent must be between 40 and 2000");
            }
        }

        uint32_t seed = 7;
        if (const std::optional<std::string> seed_arg = get_argument(request, "seed"))
        {
            uint64_t parsed = 0;
            if (!parse_uint64(*seed_arg, parsed))
            {
                return json_error("invalid seed");
            }
            seed = static_cast<uint32_t>(parsed);
        }

        bool avoid_existing = true;
        if (const std::optional<std::string> avoid_arg = get_argument(request, "avoid_existing"))
        {
            if (!parse_bool(*avoid_arg, avoid_existing))
            {
                return json_error("invalid avoid_existing");
            }
        }

        bool lights = true;
        if (const std::optional<std::string> lights_arg = get_argument(request, "lights"))
        {
            if (!parse_bool(*lights_arg, lights))
            {
                return json_error("invalid lights");
            }
        }

        bool replace = true;
        if (const std::optional<std::string> replace_arg = get_argument(request, "replace"))
        {
            if (!parse_bool(*replace_arg, replace))
            {
                return json_error("invalid replace");
            }
        }

        bool connect_roads = false;
        if (const std::optional<std::string> roads_arg = get_argument(request, "connect_roads"))
        {
            if (!parse_bool(*roads_arg, connect_roads))
            {
                return json_error("invalid connect_roads");
            }
        }

        float density = 1.0f;
        if (const std::optional<std::string> density_arg = get_argument(request, "density"))
        {
            density = district_density_scale(*density_arg);
        }

        float corridor = 28.0f;
        if (const std::optional<std::string> corridor_arg = get_argument(request, "corridor"))
        {
            if (!parse_float(*corridor_arg, corridor) || corridor < 8.0f || corridor > 120.0f)
            {
                return json_error("corridor must be between 8 and 120");
            }
        }

        struct PlannedDistrict
        {
            std::string name;
            DistrictPreset preset = DistrictPreset::Market;
            math::Vector3 position = math::Vector3::Zero;
            math::Vector2 footprint = math::Vector2(40.0f, 40.0f);
            bool has_position = false;
        };

        std::vector<PlannedDistrict> planned;
        if (const std::optional<std::string> districts_arg = get_argument(request, "districts"))
        {
            const std::vector<std::string> tokens = split_csv_tokens(*districts_arg);
            for (const std::string& token : tokens)
            {
                std::string preset_name = token;
                std::string district_name;
                const size_t colon = token.find(':');
                if (colon != std::string::npos)
                {
                    preset_name = token.substr(0, colon);
                    district_name = token.substr(colon + 1);
                }
                const std::optional<DistrictPreset> preset = district_preset_from_name(preset_name);
                if (!preset)
                {
                    return json_error("invalid district preset: " + preset_name);
                }
                PlannedDistrict entry;
                entry.preset = *preset;
                entry.name = district_name.empty() ? district_preset_to_name(*preset) : district_name;
                entry.footprint = default_footprint_for_preset(*preset);
                planned.push_back(std::move(entry));
            }
        }
        else
        {
            const DistrictPreset defaults[] = {
                DistrictPreset::Downtown,
                DistrictPreset::Market,
                DistrictPreset::Park,
                DistrictPreset::Industrial,
                DistrictPreset::Residential,
                DistrictPreset::Parking
            };
            for (DistrictPreset preset : defaults)
            {
                PlannedDistrict entry;
                entry.preset = preset;
                entry.name = district_preset_to_name(preset);
                entry.footprint = default_footprint_for_preset(preset);
                planned.push_back(std::move(entry));
            }
        }

        if (planned.empty())
        {
            return json_error("no districts to place");
        }

        if (const std::optional<std::string> names_arg = get_argument(request, "names"))
        {
            const std::vector<std::string> names = split_csv_tokens(*names_arg);
            for (size_t i = 0; i < planned.size() && i < names.size(); i++)
            {
                if (!names[i].empty())
                {
                    planned[i].name = names[i];
                }
            }
        }

        if (const std::optional<std::string> footprints_arg = get_argument(request, "footprints"))
        {
            std::vector<float> values;
            std::stringstream stream(*footprints_arg);
            std::string part;
            while (std::getline(stream, part, ','))
            {
                float parsed = 0.0f;
                if (!parse_float(part, parsed))
                {
                    return json_error("footprints must be flat width,depth pairs");
                }
                values.push_back(parsed);
            }
            if (values.size() < 2 || (values.size() % 2) != 0)
            {
                return json_error("footprints must be flat width,depth pairs");
            }
            for (size_t i = 0; i < planned.size() && (i * 2 + 1) < values.size(); i++)
            {
                planned[i].footprint = math::Vector2(values[i * 2], values[i * 2 + 1]);
            }
        }

        if (const std::optional<std::string> positions_arg = get_argument(request, "positions"))
        {
            std::vector<math::Vector3> positions;
            if (!parse_vector3_list(*positions_arg, positions))
            {
                return json_error("positions must be a flat xyz list");
            }
            for (size_t i = 0; i < planned.size() && i < positions.size(); i++)
            {
                planned[i].position = positions[i];
                planned[i].has_position = true;
            }
        }

        std::vector<math::BoundingBox> reserved;
        if (avoid_existing)
        {
            reserved = collect_existing_landmark_boxes();
        }

        DistrictRng rng(seed);
        const float ring = extent * 0.55f;
        for (size_t i = 0; i < planned.size(); i++)
        {
            PlannedDistrict& entry = planned[i];
            if (entry.has_position)
            {
                continue;
            }

            bool placed = false;
            for (int attempt = 0; attempt < 48; attempt++)
            {
                const float angle = (static_cast<float>(i) / static_cast<float>(planned.size())) * 6.2831853f + rng.range(-0.35f, 0.35f);
                const float radius = ring * rng.range(0.55f, 1.05f);
                math::Vector3 candidate = center + math::Vector3(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);
                candidate.y = center.y;
                if (!footprint_blocked(candidate, entry.footprint.x, entry.footprint.y, reserved, corridor))
                {
                    entry.position = candidate;
                    entry.has_position = true;
                    reserved.push_back(footprint_box_at(candidate, entry.footprint.x, entry.footprint.y));
                    placed = true;
                    break;
                }
            }
            if (!placed)
            {
                // fall back to a spaced slot even if tight
                const float angle = (static_cast<float>(i) / static_cast<float>(planned.size())) * 6.2831853f;
                entry.position = center + math::Vector3(std::cos(angle) * ring, 0.0f, std::sin(angle) * ring);
                entry.has_position = true;
                reserved.push_back(footprint_box_at(entry.position, entry.footprint.x, entry.footprint.y));
            }
        }

        std::string json = "{\"ok\":true,\"districts\":[";
        std::vector<std::string> created_names;
        created_names.reserve(planned.size());
        for (size_t i = 0; i < planned.size(); i++)
        {
            const PlannedDistrict& entry = planned[i];
            uint32_t part_count = 0;
            uint32_t light_count = 0;
            std::string error;
            Entity* entity = create_district_entity(
                entry.name,
                entry.position,
                0.0f,
                entry.preset,
                entry.footprint.x,
                entry.footprint.y,
                density,
                lights,
                seed + static_cast<uint32_t>(i) * 97u,
                replace,
                part_count,
                light_count,
                error
            );
            if (entity == nullptr)
            {
                return json_error(error);
            }
            if (i > 0)
            {
                json += ",";
            }
            json += district_receipt_json(entity, entry.preset, part_count, light_count);
            created_names.push_back(entry.name);
        }
        json += "]";

        // road hint points: approach edges facing city center for a later arterial pass
        json += ",\"road_hints\":[";
        for (size_t i = 0; i < planned.size(); i++)
        {
            const PlannedDistrict& entry = planned[i];
            math::Vector3 toward = center;
            if ((center - entry.position).LengthSquared() < 1.0f && planned.size() > 1)
            {
                toward = planned[(i + 1) % planned.size()].position;
            }
            math::Vector3 approach = entry.position;
            {
                std::string resolve_error;
                Entity* entity = find_entity_by_name_unique(entry.name, true, resolve_error);
                if (entity != nullptr)
                {
                    landmark_approach_point(entity, toward, corridor * 0.35f, approach);
                }
            }
            if (i > 0)
            {
                json += ",";
            }
            json += "{\"district\":" + json_string(entry.name);
            json += ",\"point\":" + json_vector3(approach) + "}";
        }
        json += "]";

        uint32_t roads_created = 0;
        if (connect_roads && created_names.size() >= 2)
        {
            json += ",\"roads\":[";
            bool first_road = true;
            for (size_t i = 0; i + 1 < created_names.size(); i++)
            {
                McpRequest connect_request;
                connect_request.command = "spline_connect";
                connect_request.arguments["landmarks"] = created_names[i] + "," + created_names[i + 1];
                connect_request.arguments["name"] = "road_" + created_names[i] + "_" + created_names[i + 1];
                connect_request.arguments["avoid_obstacles"] = "true";
                connect_request.arguments["conform_to_terrain"] = "true";
                connect_request.arguments["sidewalk_enabled"] = "true";
                const std::string road_result = command_spline_connect(connect_request);
                if (road_result.find("\"ok\":true") == std::string::npos)
                {
                    continue;
                }
                if (!first_road)
                {
                    json += ",";
                }
                first_road = false;
                json += "{\"from\":" + json_string(created_names[i]) + ",\"to\":" + json_string(created_names[i + 1]) + "}";
                roads_created++;
            }
            json += "]";
        }

        json += ",\"count\":" + std::to_string(created_names.size());
        json += ",\"roads_created\":" + std::to_string(roads_created);
        json += ",\"center\":" + json_vector3(center);
        json += ",\"extent\":" + std::to_string(extent);
        json += ",\"corridor\":" + std::to_string(corridor);
        json += "}";
        return json;
    }

}
