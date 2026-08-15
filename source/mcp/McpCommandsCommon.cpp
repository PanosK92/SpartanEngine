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

//= INCLUDES ==========================
#include "pch.h"
#include "McpCommandsCommon.h"
#include "../Core/Engine.h"
#include "../World/World.h"
#include "../World/Entity.h"
#include "../World/Components/Component.h"
#include "../Math/Vector2.h"
#include "../Math/Vector3.h"
#include "../Math/BoundingBox.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
//=====================================

namespace spartan::mcp_common
{
    //= JSON ======================================================================
    std::string json_escape(const std::string& value)
    {
        std::ostringstream stream;
        for (const unsigned char c : value)
        {
            switch (c)
            {
            case '\\':
                stream << "\\\\";
                break;
            case '"':
                stream << "\\\"";
                break;
            case '\n':
                stream << "\\n";
                break;
            case '\r':
                stream << "\\r";
                break;
            case '\t':
                stream << "\\t";
                break;
            default:
                if (c < 0x20)
                {
                    stream << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                }
                else
                {
                    stream << c;
                }
                break;
            }
        }

        return stream.str();
    }

    std::string json_string(const std::string& value)
    {
        return "\"" + json_escape(value) + "\"";
    }

    std::string json_bool(bool value)
    {
        return value ? "true" : "false";
    }

    std::string json_number(double value)
    {
        return std::isfinite(value)
            ? std::to_string(value)
            : "null";
    }

    std::string json_error(const std::string& error)
    {
        return "{\"ok\":false,\"error\":" + json_string(error) + "}";
    }

    std::string json_vector3(const math::Vector3& value)
    {
        return "[" + json_number(value.x) + "," + json_number(value.y) + "," + json_number(value.z) + "]";
    }

    std::string json_bounding_box(const math::BoundingBox& value)
    {
        return "{\"min\":" + json_vector3(value.GetMin()) + ",\"max\":" + json_vector3(value.GetMax()) + ",\"center\":" + json_vector3(value.GetCenter()) + ",\"size\":" + json_vector3(value.GetSize()) + "}";
    }

    //==============================================================================

    //= ARGUMENTS =================================================================
    std::optional<std::string> get_argument(const McpRequest& request, const std::string& name)
    {
        const auto it = request.arguments.find(name);
        if (it == request.arguments.end())
        {
            return std::nullopt;
        }

        return it->second;
    }

    bool parse_bool(const std::string& value, bool& result)
    {
        if (value == "true" || value == "1")
        {
            result = true;
            return true;
        }
        if (value == "false" || value == "0")
        {
            result = false;
            return true;
        }

        return false;
    }

    bool parse_uint64(const std::string& value, uint64_t& result)
    {
        char* end = nullptr;
        const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
        if (end == value.c_str() || *end != '\0')
        {
            return false;
        }

        result = static_cast<uint64_t>(parsed);
        return true;
    }

    bool parse_float(const std::string& value, float& result)
    {
        char* end = nullptr;
        const float parsed = std::strtof(value.c_str(), &end);
        if (end == value.c_str() || *end != '\0')
        {
            return false;
        }

        result = parsed;
        return std::isfinite(result);
    }

    bool parse_float_list(const std::string& value, std::vector<float>& values, uint32_t expected_count)
    {
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

        return values.size() == expected_count;
    }

    bool parse_vector2(const std::string& value, math::Vector2& result)
    {
        std::vector<float> values;
        if (!parse_float_list(value, values, 2))
        {
            return false;
        }

        result = math::Vector2(values[0], values[1]);
        return result.IsFinite();
    }

    bool parse_vector3(const std::string& value, math::Vector3& result)
    {
        std::vector<float> values;
        if (!parse_float_list(value, values, 3))
        {
            return false;
        }

        result = math::Vector3(values[0], values[1], values[2]);
        return result.IsFinite();
    }

    std::string to_lower_copy(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    //==============================================================================

    //= ENTITIES ==================================================================
    bool is_edit_mode()
    {
        return !Engine::IsFlagSet(EngineMode::Playing);
    }

    Entity* find_entity_by_name_unique(const std::string& name, bool exact, std::string& error)
    {
        const std::string query = to_lower_copy(name);
        Entity* match = nullptr;

        for (Entity* entity : World::GetEntities())
        {
            if (entity == nullptr)
            {
                continue;
            }

            const std::string entity_name = to_lower_copy(entity->GetObjectName());
            const bool matches = exact ? entity_name == query : entity_name.find(query) != std::string::npos;
            if (!matches)
            {
                continue;
            }

            if (match != nullptr)
            {
                error = "multiple entities match name";
                return nullptr;
            }

            match = entity;
        }

        if (match == nullptr)
        {
            error = exact ? "entity name not found" : "entity name match not found";
        }

        return match;
    }

    Entity* get_entity_from_request(const McpRequest& request, std::string& error)
    {
        const std::optional<std::string> id_arg = get_argument(request, "id");
        if (!id_arg)
        {
            error = "missing id";
            return nullptr;
        }

        uint64_t id = 0;
        if (!parse_uint64(*id_arg, id))
        {
            Entity* entity = find_entity_by_name_unique(*id_arg, true, error);
            if (entity != nullptr)
            {
                return entity;
            }

            entity = find_entity_by_name_unique(*id_arg, false, error);
            if (entity != nullptr)
            {
                return entity;
            }

            return nullptr;
        }

        Entity* entity = World::GetEntityById(id);
        if (entity == nullptr)
        {
            error = "entity not found";
            return nullptr;
        }

        return entity;
    }

    std::string entity_components_json(Entity* entity)
    {
        std::string json = "[";
        bool first_component = true;
        for (const std::shared_ptr<Component>& component : entity->GetAllComponents())
        {
            if (component == nullptr)
            {
                continue;
            }

            if (!first_component)
            {
                json += ",";
            }
            first_component = false;
            json += json_string(Component::TypeToString(component->GetType()));
        }
        json += "]";
        return json;
    }

    std::string entity_tags_json(Entity* entity)
    {
        std::string json = "[";
        bool first = true;
        for (const std::string& tag : entity->GetTags())
        {
            if (!first)
            {
                json += ",";
            }
            first = false;
            json += json_string(tag);
        }
        json += "]";
        return json;
    }

    std::string entity_to_json_compact(Entity* entity)
    {
        std::string json = "{";
        json += "\"id\":" + json_string(std::to_string(entity->GetObjectId()));
        json += ",\"name\":" + json_string(entity->GetObjectName());
        json += ",\"active\":" + json_bool(entity->IsActive());

        Entity* parent = entity->GetParent();
        json += ",\"parent_id\":";
        json += parent ? json_string(std::to_string(parent->GetObjectId())) : "null";

        json += ",\"components\":" + entity_components_json(entity);
        if (!entity->GetTags().empty())
        {
            json += ",\"tags\":" + entity_tags_json(entity);
        }
        json += ",\"position\":" + json_vector3(entity->GetPosition());
        json += ",\"rotation_euler\":" + json_vector3(entity->GetRotation().ToEulerAngles());
        json += ",\"scale\":" + json_vector3(entity->GetScale());
        json += "}";
        return json;
    }

    //==============================================================================

    //= SPLINES ===================================================================
    std::string spline_profile_to_name(SplineProfile profile)
    {
        switch (profile)
        {
        case SplineProfile::Road:
            return "road";
        case SplineProfile::Wall:
            return "wall";
        case SplineProfile::Tube:
            return "tube";
        case SplineProfile::Fence:
            return "fence";
        case SplineProfile::Channel:
            return "channel";
        default:
            return "unknown";
        }
    }

    std::optional<SplineProfile> spline_profile_from_name(const std::string& name)
    {
        if (name == "road" || name == "0")
        {
            return SplineProfile::Road;
        }
        if (name == "wall" || name == "1")
        {
            return SplineProfile::Wall;
        }
        if (name == "tube" || name == "2")
        {
            return SplineProfile::Tube;
        }
        if (name == "fence" || name == "3")
        {
            return SplineProfile::Fence;
        }
        if (name == "channel" || name == "4")
        {
            return SplineProfile::Channel;
        }

        return std::nullopt;
    }

    std::string spline_follow_mode_to_name(SplineFollowMode mode)
    {
        switch (mode)
        {
        case SplineFollowMode::Clamp:
            return "clamp";
        case SplineFollowMode::Loop:
            return "loop";
        case SplineFollowMode::PingPong:
            return "ping_pong";
        default:
            return "unknown";
        }
    }

    //==============================================================================

}
