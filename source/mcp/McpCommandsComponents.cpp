/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===================================
#include "pch.h"
#ifdef SP_GAME
#include "../car/CarPhysics.h"
#endif
#include "editor/Selection.h"
#include "game/CameraController.h"
#include "../profiling/WorldWork.h"
#include "../core/ThreadPool.h"
#include "McpCommands.h"
#include "McpCommandsCommon.h"
#include "McpCommandsWorldBuild.h"
#include "McpCommandsMesh.h"
#include "McpGeometryKernel.h"
#include "McpTextureKernel.h"
#include "../commands/console/ConsoleCommands.h"
#include "../commands/CommandStack.h"
#include "../core/ProgressTracker.h"
#include "../logging/Log.h"
#include "../physics/PhysicsWorld.h"
#include "../profiling/Profiler.h"
#include "../memory/GpuMemory.h"
#include "../world/World.h"
#include "../world/Weather.h"
#include "../world/Entity.h"
#include "../world/components/Camera.h"
#include "../world/components/Component.h"
#include "../world/components/AudioSource.h"
#include "../world/components/Light.h"
#include "../world/components/ParticleSystem.h"
#include "../world/components/Physics.h"
#include "../world/components/Render.h"
#include "../world/components/Script.h"
#include "../world/components/Spline.h"
#include "../world/components/SplineFollower.h"
#include "../world/components/Terrain.h"
#include "../world/components/Text3D.h"
#include "../world/Prefab.h"
#include "../world/GameReady.h"
#include "../world/WorldHelpers.h"
#include "../io/pugixml.hpp"
#include "../game/components/RaceDriver.h"
#include "../resource/ResourceCache.h"
#include "../resource/import/ImageImporter.h"
#include "../animation/Animation.h"
#include "../geometry/GeometryGeneration.h"
#include "../geometry/Mesh.h"
#include "../rhi/RHI_Texture.h"
#include "../rhi/RHI_Buffer.h"
#include "../rhi/RHI_Device.h"
#include "../rhi/RHI_Shader.h"
#include "../rendering/Material.h"
#include "../rendering/Renderer.h"
#include "../math/Vector2.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <optional>
#include <sstream>
#include <typeinfo>
#include <unordered_map>
//==============================================

#include "McpCommandsComponents.h"

namespace spartan::mcp_components
{
    using namespace mcp_common;
        std::optional<Text3DAlignment>
        text_3d_alignment_from_name(const std::string& name)
        {
            const std::string normalized = to_lower_copy(name);
            if (normalized == "left" || normalized == "0")
            {
                return Text3DAlignment::Left;
            }
            if (normalized == "center" || normalized == "1")
            {
                return Text3DAlignment::Center;
            }
            if (normalized == "right" || normalized == "2")
            {
                return Text3DAlignment::Right;
            }

            return std::nullopt;
        }


        struct ComponentMetadata
        {
            std::string property;
            std::string member;
            std::string type;
            bool writable = true;
            std::string unit;
            std::string range;
            std::string enum_values;
            std::vector<std::string> side_effects;
            std::string read_only_reason;
            std::string recommended_default;
            std::string note;
        };

        std::string json_string_array(const std::vector<std::string>& values)
        {
            std::string json = "[";
            for (size_t i = 0; i < values.size(); i++)
            {
                if (i != 0)
                {
                    json += ",";
                }
                json += json_string(values[i]);
            }
            json += "]";
            return json;
        }

        std::string json_vector2(const math::Vector2& value)
        {
            return "[" + json_number(value.x) + "," + json_number(value.y) + "]";
        }

        std::string json_quaternion(const math::Quaternion& value)
        {
            return "[" + json_number(value.x) + "," + json_number(value.y) + "," + json_number(value.z) + "," + json_number(value.w) + "]";
        }

        std::string json_color(const Color& value)
        {
            return "[" + json_number(value.r) + "," + json_number(value.g) + "," + json_number(value.b) + "," + json_number(value.a) + "]";
        }

        std::string json_matrix(const math::Matrix& value)
        {
            return "["
                + json_number(value.m00) + "," + json_number(value.m01) + "," + json_number(value.m02) + "," + json_number(value.m03) + ","
                + json_number(value.m10) + "," + json_number(value.m11) + "," + json_number(value.m12) + "," + json_number(value.m13) + ","
                + json_number(value.m20) + "," + json_number(value.m21) + "," + json_number(value.m22) + "," + json_number(value.m23) + ","
                + json_number(value.m30) + "," + json_number(value.m31) + "," + json_number(value.m32) + "," + json_number(value.m33) + "]";
        }

        bool parse_quaternion(const std::string& value, math::Quaternion& result)
        {
            std::vector<float> values;
            if (!parse_float_list(value, values, 4))
            {
                return false;
            }

            result = math::Quaternion(values[0], values[1], values[2], values[3]);
            return result.IsFinite();
        }

        bool parse_matrix(const std::string& value, math::Matrix& result)
        {
            std::vector<float> values;
            if (!parse_float_list(value, values, 16))
            {
                return false;
            }

            result = math::Matrix(values.data());
            return true;
        }

        bool parse_bounding_box(const std::string& value, math::BoundingBox& result)
        {
            std::vector<float> values;
            if (!parse_float_list(value, values, 6))
            {
                return false;
            }

            const math::Vector3 min(values[0], values[1], values[2]);
            const math::Vector3 max(values[3], values[4], values[5]);
            if (!min.IsFinite() || !max.IsFinite())
            {
                return false;
            }

            result = math::BoundingBox(min, max);
            return true;
        }

        bool parse_color(const std::string& value, Color& result)
        {
            std::vector<float> values;
            if (!parse_float_list(value, values, 3))
            {
                values.clear();
                if (!parse_float_list(value, values, 4))
                {
                    return false;
                }
            }

            result = values.size() == 3 ? Color(values[0], values[1], values[2], 1.0f) : Color(values[0], values[1], values[2], values[3]);
            return std::isfinite(result.r) && std::isfinite(result.g) && std::isfinite(result.b) && std::isfinite(result.a);
        }

        std::optional<MeshType> mesh_type_from_name(const std::string& name)
        {
            const std::string normalized = to_lower_copy(name);
            if (
                normalized == "cube" ||
                normalized == "box" ||
                normalized == "standard_cube"
            )
            {
                return MeshType::Cube;
            }
            if (
                normalized == "quad" ||
                normalized == "plane" ||
                normalized == "standard_quad"
            )
            {
                return MeshType::Quad;
            }
            if (
                normalized == "sphere" ||
                normalized == "ball" ||
                normalized == "standard_sphere"
            )
            {
                return MeshType::Sphere;
            }
            if (
                normalized == "cylinder" ||
                normalized == "standard_cylinder"
            )
            {
                return MeshType::Cylinder;
            }
            if (
                normalized == "cone" ||
                normalized == "standard_cone"
            )
            {
                return MeshType::Cone;
            }

            return std::nullopt;
        }

        std::optional<BodyType> body_type_from_name(const std::string& name)
        {
            if (name == "box")
            {
                return BodyType::Box;
            }
            if (name == "sphere")
            {
                return BodyType::Sphere;
            }
            if (name == "plane")
            {
                return BodyType::Plane;
            }
            if (name == "capsule")
            {
                return BodyType::Capsule;
            }
            if (name == "mesh")
            {
                return BodyType::Mesh;
            }
            if (name == "mesh_convex")
            {
                return BodyType::MeshConvex;
            }
            if (name == "controller")
            {
                return BodyType::Controller;
            }
            if (name == "vehicle")
            {
                return BodyType::Custom;
            }
            if (name == "cloth")
            {
                return BodyType::Cloth;
            }
            if (name == "heightfield")
            {
                return BodyType::Heightfield;
            }

            return std::nullopt;
        }

        std::string body_type_to_name(BodyType type)
        {
            switch (type)
            {
            case BodyType::Box:
                return "box";
            case BodyType::Sphere:
                return "sphere";
            case BodyType::Plane:
                return "plane";
            case BodyType::Capsule:
                return "capsule";
            case BodyType::Mesh:
                return "mesh";
            case BodyType::MeshConvex:
                return "mesh_convex";
            case BodyType::Controller:
                return "controller";
            case BodyType::Custom:
                return "vehicle";
            case BodyType::Cloth:
                return "cloth";
            case BodyType::Heightfield:
                return "heightfield";
            default:
                return "unknown";
            }
        }

        std::optional<LightType> light_type_from_name(const std::string& name)
        {
            if (name == "directional")
            {
                return LightType::Directional;
            }
            if (name == "point")
            {
                return LightType::Point;
            }
            if (name == "spot")
            {
                return LightType::Spot;
            }
            if (name == "area")
            {
                return LightType::Area;
            }

            return std::nullopt;
        }

        std::string light_type_to_name(LightType type)
        {
            switch (type)
            {
            case LightType::Directional:
                return "directional";
            case LightType::Point:
                return "point";
            case LightType::Spot:
                return "spot";
            case LightType::Area:
                return "area";
            default:
                return "unknown";
            }
        }

        std::optional<ComponentType> component_type_from_name(const std::string& name)
        {
            #define X(type, str) \
                if (name == #str) \
                { \
                    return ComponentType::type; \
                }
            SP_COMPONENT_LIST
            #undef X

            return std::nullopt;
        }

        std::string component_types_json()
        {
            std::string json = "[";
            bool first = true;

            #define X(type, str) \
                if (!first) \
                { \
                    json += ","; \
                } \
                first = false; \
                json += json_string(#str);
            SP_COMPONENT_LIST
            #undef X

            json += "]";
            return json;
        }

        std::string camel_to_snake(const std::string& value)
        {
            std::string result;
            for (size_t i = 0; i < value.size(); i++)
            {
                const unsigned char c = static_cast<unsigned char>(value[i]);
                if (std::isupper(c))
                {
                    if (i != 0 && !result.empty() && result.back() != '_' && (std::islower(static_cast<unsigned char>(value[i - 1])) || std::isdigit(static_cast<unsigned char>(value[i - 1]))))
                    {
                        result.push_back('_');
                    }
                    result.push_back(static_cast<char>(std::tolower(c)));
                }
                else
                {
                    result.push_back(static_cast<char>(c));
                }
            }

            return result;
        }

        std::string attribute_property_name(const Attribute& attribute)
        {
            if (attribute.name.rfind("m_", 0) == 0)
            {
                return attribute.name.substr(2);
            }
            if (attribute.name.rfind("Get", 0) == 0 && attribute.name.size() > 3)
            {
                return camel_to_snake(attribute.name.substr(3));
            }

            return camel_to_snake(attribute.name);
        }

        bool attribute_matches_property(const Attribute& attribute, const std::string& property)
        {
            const std::string query = to_lower_copy(property);
            return query == to_lower_copy(attribute.name) || query == to_lower_copy(attribute_property_name(attribute));
        }

        std::string projection_type_to_name(ProjectionType type)
        {
            return type == Projection_Orthographic ? "orthographic" : "perspective";
        }

        std::optional<ProjectionType> projection_type_from_name(const std::string& name)
        {
            if (name == "perspective" || name == "0")
            {
                return Projection_Perspective;
            }
            if (name == "orthographic" || name == "1")
            {
                return Projection_Orthographic;
            }

            return std::nullopt;
        }

        std::string spline_attach_mode_to_name(SplineAttachMode mode)
        {
            switch (mode)
            {
            case SplineAttachMode::None:
                return "none";
            case SplineAttachMode::Centerline:
                return "centerline";
            case SplineAttachMode::LeftEdge:
                return "left_edge";
            case SplineAttachMode::RightEdge:
                return "right_edge";
            case SplineAttachMode::LeftOuter:
                return "left_outer";
            case SplineAttachMode::RightOuter:
                return "right_outer";
            default:
                return "unknown";
            }
        }

        std::optional<SplineAttachMode> spline_attach_mode_from_name(const std::string& name)
        {
            if (name == "none" || name == "0")
            {
                return SplineAttachMode::None;
            }
            if (name == "centerline" || name == "1")
            {
                return SplineAttachMode::Centerline;
            }
            if (name == "left_edge" || name == "2")
            {
                return SplineAttachMode::LeftEdge;
            }
            if (name == "right_edge" || name == "3")
            {
                return SplineAttachMode::RightEdge;
            }
            if (name == "left_outer" || name == "4")
            {
                return SplineAttachMode::LeftOuter;
            }
            if (name == "right_outer" || name == "5")
            {
                return SplineAttachMode::RightOuter;
            }

            return std::nullopt;
        }

        std::string text_3d_alignment_to_name(
            const Text3DAlignment alignment
        )
        {
            switch (alignment)
            {
            case Text3DAlignment::Left:
                return "left";
            case Text3DAlignment::Center:
                return "center";
            case Text3DAlignment::Right:
                return "right";
            default:
                return "unknown";
            }
        }

        std::optional<SplineFollowMode> spline_follow_mode_from_name(const std::string& name)
        {
            if (name == "clamp" || name == "0")
            {
                return SplineFollowMode::Clamp;
            }
            if (name == "loop" || name == "1")
            {
                return SplineFollowMode::Loop;
            }
            if (name == "ping_pong" || name == "2")
            {
                return SplineFollowMode::PingPong;
            }

            return std::nullopt;
        }

        std::optional<ParticlePreset> particle_preset_from_name(const std::string& name)
        {
            if (name == "custom" || name == "0")
            {
                return ParticlePreset::Custom;
            }
            if (name == "fire" || name == "1")
            {
                return ParticlePreset::Fire;
            }
            if (name == "smoke" || name == "2")
            {
                return ParticlePreset::Smoke;
            }
            if (name == "steam" || name == "3")
            {
                return ParticlePreset::Steam;
            }
            if (name == "sparks" || name == "4")
            {
                return ParticlePreset::Sparks;
            }
            if (name == "dust" || name == "5")
            {
                return ParticlePreset::Dust;
            }
            if (name == "snow" || name == "6")
            {
                return ParticlePreset::Snow;
            }
            if (name == "rain" || name == "7")
            {
                return ParticlePreset::Rain;
            }
            if (name == "confetti" || name == "8")
            {
                return ParticlePreset::Confetti;
            }
            if (name == "fireflies" || name == "9")
            {
                return ParticlePreset::Fireflies;
            }
            if (name == "blood" || name == "10")
            {
                return ParticlePreset::Blood;
            }
            if (name == "magic" || name == "11")
            {
                return ParticlePreset::Magic;
            }
            if (name == "explosion" || name == "12")
            {
                return ParticlePreset::Explosion;
            }
            if (name == "waterfall" || name == "13")
            {
                return ParticlePreset::Waterfall;
            }
            if (name == "embers" || name == "14")
            {
                return ParticlePreset::Embers;
            }
            if (name == "tire_smoke" || name == "tiresmoke" || name == "15")
            {
                return ParticlePreset::TireSmoke;
            }
            if (name == "exhaust" || name == "16")
            {
                return ParticlePreset::Exhaust;
            }

            return std::nullopt;
        }

        std::optional<PhysicsForce> physics_force_from_name(const std::string& name)
        {
            if (name == "constant" || name == "force" || name == "0")
            {
                return PhysicsForce::Constant;
            }
            if (name == "impulse" || name == "1")
            {
                return PhysicsForce::Impulse;
            }

            return std::nullopt;
        }

        std::shared_ptr<IResource> get_resource_shared_by_name_or_path(const std::string& name_or_path, ResourceType type)
        {
            for (const std::shared_ptr<IResource>& resource : ResourceCache::GetResourcesSnapshot())
            {
                if (!resource || (type != ResourceType::Max && resource->GetResourceType() != type))
                {
                    continue;
                }

                if (resource->GetObjectName() == name_or_path || resource->GetResourceFilePath() == name_or_path)
                {
                    return resource;
                }
            }

            return nullptr;
        }

        std::string range_json(std::optional<float> min, std::optional<float> max)
        {
            std::string json = "{";
            bool first = true;
            if (min)
            {
                json += "\"min\":" + std::to_string(*min);
                first = false;
            }
            if (max)
            {
                if (!first)
                {
                    json += ",";
                }
                json += "\"max\":" + std::to_string(*max);
            }
            json += "}";
            return json;
        }

        std::string enum_values_json(std::initializer_list<std::pair<std::string, std::string>> values)
        {
            std::string json = "[";
            size_t index = 0;
            for (const std::pair<std::string, std::string>& value : values)
            {
                if (index != 0)
                {
                    json += ",";
                }
                json += "{\"name\":" + json_string(value.first) + ",\"value\":" + value.second + "}";
                index++;
            }
            json += "]";
            return json;
        }

        std::string component_metadata_to_json(const ComponentMetadata& metadata)
        {
            std::string json = "{";
            json += "\"property\":" + json_string(metadata.property);
            if (!metadata.member.empty())
            {
                json += ",\"member\":" + json_string(metadata.member);
            }
            json += ",\"type\":" + json_string(metadata.type);
            json += ",\"writable\":" + json_bool(metadata.writable);
            if (!metadata.unit.empty())
            {
                json += ",\"unit\":" + json_string(metadata.unit);
            }
            if (!metadata.range.empty())
            {
                json += ",\"range\":" + metadata.range;
            }
            if (!metadata.enum_values.empty())
            {
                json += ",\"enum_values\":" + metadata.enum_values;
            }
            if (!metadata.side_effects.empty())
            {
                json += ",\"side_effects\":" + json_string_array(metadata.side_effects);
            }
            if (!metadata.read_only_reason.empty())
            {
                json += ",\"read_only_reason\":" + json_string(metadata.read_only_reason);
            }
            if (!metadata.recommended_default.empty())
            {
                json += ",\"recommended_default\":" + metadata.recommended_default;
            }
            if (!metadata.note.empty())
            {
                json += ",\"note\":" + json_string(metadata.note);
            }
            json += "}";
            return json;
        }

        std::string projection_enum_values_json()
        {
            return enum_values_json({ { "perspective", json_string("perspective") }, { "orthographic", json_string("orthographic") } });
        }

        std::string camera_exposure_mode_enum_values_json()
        {
            return enum_values_json(
                {
                    { "manual", json_string("manual") },
                    { "automatic", json_string("automatic") }
                }
            );
        }

        std::string body_type_enum_values_json()
        {
            return enum_values_json({
                { "box", json_string("box") },
                { "sphere", json_string("sphere") },
                { "plane", json_string("plane") },
                { "capsule", json_string("capsule") },
                { "mesh", json_string("mesh") },
                { "mesh_convex", json_string("mesh_convex") },
                { "controller", json_string("controller") },
                { "vehicle", json_string("vehicle") },
                { "cloth", json_string("cloth") }
            });
        }

        std::string light_type_enum_values_json()
        {
            return enum_values_json({
                { "directional", json_string("directional") },
                { "point", json_string("point") },
                { "spot", json_string("spot") },
                { "area", json_string("area") }
            });
        }

        std::string spline_profile_enum_values_json()
        {
            return enum_values_json({
                { "road", json_string("road") },
                { "wall", json_string("wall") },
                { "tube", json_string("tube") },
                { "fence", json_string("fence") },
                { "channel", json_string("channel") }
            });
        }

        std::string spline_attach_mode_enum_values_json()
        {
            return enum_values_json({
                { "none", json_string("none") },
                { "centerline", json_string("centerline") },
                { "left_edge", json_string("left_edge") },
                { "right_edge", json_string("right_edge") },
                { "left_outer", json_string("left_outer") },
                { "right_outer", json_string("right_outer") }
            });
        }

        std::string spline_follow_mode_enum_values_json()
        {
            return enum_values_json({ { "clamp", "0" }, { "loop", "1" }, { "ping_pong", "2" } });
        }

        std::string text_3d_alignment_enum_values_json()
        {
            return enum_values_json({
                { "left", json_string("left") },
                { "center", json_string("center") },
                { "right", json_string("right") }
            });
        }

        std::string particle_preset_enum_values_json()
        {
            return enum_values_json({
                { "custom", "0" },
                { "fire", "1" },
                { "smoke", "2" },
                { "steam", "3" },
                { "sparks", "4" },
                { "dust", "5" },
                { "snow", "6" },
                { "rain", "7" },
                { "confetti", "8" },
                { "fireflies", "9" },
                { "blood", "10" },
                { "magic", "11" },
                { "explosion", "12" },
                { "waterfall", "13" },
                { "embers", "14" },
                { "tire_smoke", "15" },
                { "exhaust", "16" }
            });
        }

        std::string particle_blend_mode_enum_values_json()
        {
            return enum_values_json({ { "alpha", "0" }, { "premultiplied", "1" }, { "additive", "2" } });
        }

        std::string particle_lighting_mode_enum_values_json()
        {
            return enum_values_json({ { "lit", "0" }, { "unlit", "1" }, { "emissive", "2" } });
        }

        std::string attribute_value_to_json(const std::any& value, const std::string& type_name)
        {
            const std::type_info& type = value.type();

            if (type == typeid(bool))
            {
                return json_bool(std::any_cast<bool>(value));
            }
            if (type == typeid(float))
            {
                return json_number(std::any_cast<float>(value));
            }
            if (type == typeid(double))
            {
                return json_number(std::any_cast<double>(value));
            }
            if (type == typeid(int32_t))
            {
                return std::to_string(std::any_cast<int32_t>(value));
            }
            if (type == typeid(uint32_t))
            {
                return std::to_string(std::any_cast<uint32_t>(value));
            }
            if (type == typeid(uint64_t))
            {
                // as a string, uint64 entity ids survive json parsers that use doubles
                return json_string(std::to_string(std::any_cast<uint64_t>(value)));
            }
            if (type == typeid(std::string))
            {
                return json_string(std::any_cast<std::string>(value));
            }
            if (type == typeid(math::Vector2))
            {
                return json_vector2(std::any_cast<math::Vector2>(value));
            }
            if (type == typeid(math::Vector3))
            {
                return json_vector3(std::any_cast<math::Vector3>(value));
            }
            if (type == typeid(math::Quaternion))
            {
                return json_quaternion(std::any_cast<math::Quaternion>(value));
            }
            if (type == typeid(Color))
            {
                return json_color(std::any_cast<Color>(value));
            }
            if (type == typeid(math::BoundingBox))
            {
                return json_bounding_box(std::any_cast<math::BoundingBox>(value));
            }
            if (type == typeid(math::Matrix))
            {
                return json_matrix(std::any_cast<math::Matrix>(value));
            }
            if (type == typeid(ProjectionType))
            {
                return json_string(projection_type_to_name(std::any_cast<ProjectionType>(value)));
            }
            if (type == typeid(BodyType))
            {
                return json_string(body_type_to_name(std::any_cast<BodyType>(value)));
            }
            if (type == typeid(LightType))
            {
                return json_string(light_type_to_name(std::any_cast<LightType>(value)));
            }
            if (type == typeid(SplineProfile))
            {
                return json_string(spline_profile_to_name(std::any_cast<SplineProfile>(value)));
            }
            if (type == typeid(SplineAttachMode))
            {
                return json_string(spline_attach_mode_to_name(std::any_cast<SplineAttachMode>(value)));
            }
            if (type == typeid(SplineFollowMode))
            {
                return json_string(spline_follow_mode_to_name(std::any_cast<SplineFollowMode>(value)));
            }
            if (type == typeid(Text3DAlignment))
            {
                return json_string(
                    text_3d_alignment_to_name(
                        std::any_cast<Text3DAlignment>(value)
                    )
                );
            }
            if (type == typeid(ParticlePreset))
            {
                return std::to_string(static_cast<uint32_t>(std::any_cast<ParticlePreset>(value)));
            }
            if (type == typeid(ParticleBlendMode))
            {
                return std::to_string(static_cast<uint32_t>(std::any_cast<ParticleBlendMode>(value)));
            }
            if (type == typeid(ParticleLightingMode))
            {
                return std::to_string(static_cast<uint32_t>(std::any_cast<ParticleLightingMode>(value)));
            }
            if (type == typeid(Material*))
            {
                Material* material = std::any_cast<Material*>(value);
                return material ? json_string(material->GetObjectName()) : "null";
            }
            if (type == typeid(Mesh*))
            {
                Mesh* mesh = std::any_cast<Mesh*>(value);
                return mesh ? json_string(mesh->GetObjectName()) : "null";
            }
            if (type == typeid(std::vector<Instance>))
            {
                const std::vector<Instance>& instances = std::any_cast<const std::vector<Instance>&>(value);
                return "{\"count\":" + std::to_string(instances.size()) + "}";
            }

            return "{\"unsupported_type\":" + json_string(type_name) + "}";
        }

        bool attribute_type_is_writable(const std::any& value)
        {
            const std::type_info& type = value.type();
            return type == typeid(bool) ||
                type == typeid(float) ||
                type == typeid(double) ||
                type == typeid(int32_t) ||
                type == typeid(uint32_t) ||
                type == typeid(uint64_t) ||
                type == typeid(std::string) ||
                type == typeid(math::Vector2) ||
                type == typeid(math::Vector3) ||
                type == typeid(math::Quaternion) ||
                type == typeid(Color) ||
                type == typeid(math::BoundingBox) ||
                type == typeid(math::Matrix) ||
                type == typeid(ProjectionType) ||
                type == typeid(BodyType) ||
                type == typeid(LightType) ||
                type == typeid(SplineProfile) ||
                type == typeid(SplineAttachMode) ||
                type == typeid(SplineFollowMode) ||
                type == typeid(Text3DAlignment) ||
                type == typeid(ParticlePreset) ||
                type == typeid(ParticleBlendMode) ||
                type == typeid(ParticleLightingMode);
        }

        bool parse_attribute_value(const Attribute& attribute, const std::string& value, std::any& parsed, std::string& error)
        {
            const std::type_info& type = attribute.getter().type();

            if (type == typeid(bool))
            {
                bool result = false;
                if (!parse_bool(value, result))
                {
                    error = "invalid bool";
                    return false;
                }
                parsed = result;
                return true;
            }
            if (type == typeid(float))
            {
                float result = 0.0f;
                if (!parse_float(value, result))
                {
                    error = "invalid float";
                    return false;
                }
                parsed = result;
                return true;
            }
            if (type == typeid(double))
            {
                float result = 0.0f;
                if (!parse_float(value, result))
                {
                    error = "invalid double";
                    return false;
                }
                parsed = static_cast<double>(result);
                return true;
            }
            if (type == typeid(int32_t))
            {
                int32_t result = 0;
                if (!parse_int32(value, result))
                {
                    error = "invalid int32";
                    return false;
                }
                parsed = result;
                return true;
            }
            if (type == typeid(uint32_t))
            {
                uint32_t result = 0;
                if (!parse_uint32(value, result))
                {
                    error = "invalid uint32";
                    return false;
                }
                parsed = result;
                return true;
            }
            if (type == typeid(uint64_t))
            {
                uint64_t result = 0;
                if (!parse_uint64(value, result))
                {
                    error = "invalid uint64";
                    return false;
                }
                parsed = result;
                return true;
            }
            if (type == typeid(std::string))
            {
                parsed = value;
                return true;
            }
            if (type == typeid(math::Vector2))
            {
                math::Vector2 result;
                if (!parse_vector2(value, result))
                {
                    error = "invalid vector2";
                    return false;
                }
                parsed = result;
                return true;
            }
            if (type == typeid(math::Vector3))
            {
                math::Vector3 result;
                if (!parse_vector3(value, result))
                {
                    error = "invalid vector3";
                    return false;
                }
                parsed = result;
                return true;
            }
            if (type == typeid(math::Quaternion))
            {
                math::Quaternion result;
                if (!parse_quaternion(value, result))
                {
                    error = "invalid quaternion";
                    return false;
                }
                parsed = result;
                return true;
            }
            if (type == typeid(Color))
            {
                Color result;
                if (!parse_color(value, result))
                {
                    error = "invalid color";
                    return false;
                }
                parsed = result;
                return true;
            }
            if (type == typeid(math::Matrix))
            {
                math::Matrix result;
                if (!parse_matrix(value, result))
                {
                    error = "invalid matrix";
                    return false;
                }
                parsed = result;
                return true;
            }
            if (type == typeid(math::BoundingBox))
            {
                math::BoundingBox result;
                if (!parse_bounding_box(value, result))
                {
                    error = "invalid bounding_box";
                    return false;
                }
                parsed = result;
                return true;
            }
            if (type == typeid(ProjectionType))
            {
                const std::optional<ProjectionType> result = projection_type_from_name(value);
                if (!result)
                {
                    error = "invalid projection";
                    return false;
                }
                parsed = *result;
                return true;
            }
            if (type == typeid(BodyType))
            {
                const std::optional<BodyType> result = body_type_from_name(value);
                if (!result)
                {
                    error = "invalid body_type";
                    return false;
                }
                parsed = *result;
                return true;
            }
            if (type == typeid(LightType))
            {
                const std::optional<LightType> result = light_type_from_name(value);
                if (!result)
                {
                    error = "invalid light_type";
                    return false;
                }
                parsed = *result;
                return true;
            }
            if (type == typeid(SplineProfile))
            {
                const std::optional<SplineProfile> result = spline_profile_from_name(value);
                if (!result)
                {
                    error = "invalid spline_profile";
                    return false;
                }
                parsed = *result;
                return true;
            }
            if (type == typeid(SplineAttachMode))
            {
                const std::optional<SplineAttachMode> result = spline_attach_mode_from_name(value);
                if (!result)
                {
                    error = "invalid spline_attach_mode";
                    return false;
                }
                parsed = *result;
                return true;
            }
            if (type == typeid(SplineFollowMode))
            {
                const std::optional<SplineFollowMode> result = spline_follow_mode_from_name(value);
                if (!result)
                {
                    error = "invalid spline_follow_mode";
                    return false;
                }
                parsed = *result;
                return true;
            }
            if (type == typeid(Text3DAlignment))
            {
                const std::optional<Text3DAlignment> result =
                    text_3d_alignment_from_name(value);
                if (!result)
                {
                    error = "invalid text_3d_alignment";
                    return false;
                }
                parsed = *result;
                return true;
            }
            if (type == typeid(ParticlePreset))
            {
                uint32_t result = 0;
                if (!parse_uint32(value, result) || result >= static_cast<uint32_t>(ParticlePreset::Count))
                {
                    error = "invalid particle_preset";
                    return false;
                }
                parsed = static_cast<ParticlePreset>(result);
                return true;
            }
            if (type == typeid(ParticleBlendMode))
            {
                uint32_t result = 0;
                if (!parse_uint32(value, result) || result >= static_cast<uint32_t>(ParticleBlendMode::Count))
                {
                    error = "invalid particle_blend_mode";
                    return false;
                }
                parsed = static_cast<ParticleBlendMode>(result);
                return true;
            }
            if (type == typeid(ParticleLightingMode))
            {
                uint32_t result = 0;
                if (!parse_uint32(value, result) || result >= static_cast<uint32_t>(ParticleLightingMode::Count))
                {
                    error = "invalid particle_lighting_mode";
                    return false;
                }
                parsed = static_cast<ParticleLightingMode>(result);
                return true;
            }

            error = "member is read-only through MCP";
            return false;
        }

        std::string component_member_names_json(Component* component)
        {
            std::string json = "[";
            bool first = true;

            for (const Attribute& attribute : component->GetAttributes())
            {
                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += json_string(attribute_property_name(attribute));
            }

            json += "]";
            return json;
        }

        std::string component_members_to_json(Component* component)
        {
            std::string json = "[";
            bool first = true;

            for (const Attribute& attribute : component->GetAttributes())
            {
                const std::any value = attribute.getter();
                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += "{";
                json += "\"property\":" + json_string(attribute_property_name(attribute));
                json += ",\"member\":" + json_string(attribute.name);
                json += ",\"type\":" + json_string(attribute.type);
                json += ",\"writable\":" + json_bool(attribute.writable && attribute_type_is_writable(value));
                json += ",\"value\":" + attribute_value_to_json(value, attribute.type);
                json += "}";
            }

            json += "]";
            return json;
        }

        void apply_common_member_metadata(ComponentMetadata& metadata)
        {
            const std::string property = metadata.property;
            if (property == "projection_type")
            {
                metadata.type = "enum";
                metadata.enum_values = projection_enum_values_json();
            }
            else if (property == "body_type")
            {
                metadata.type = "enum";
                metadata.enum_values = body_type_enum_values_json();
                metadata.side_effects.emplace_back("recreates physics body and shapes");
            }
            else if (property == "light_type")
            {
                metadata.type = "enum";
                metadata.enum_values = light_type_enum_values_json();
                metadata.side_effects.emplace_back("resets sensible range and shadow mode compatibility");
            }
            else if (property == "profile")
            {
                metadata.type = "enum";
                metadata.enum_values = spline_profile_enum_values_json();
                metadata.side_effects.emplace_back("changes generated spline cross section");
            }
            else if (property == "attach_mode")
            {
                metadata.type = "enum";
                metadata.enum_values = spline_attach_mode_enum_values_json();
                metadata.side_effects.emplace_back("changes how the spline samples its source spline");
            }
            else if (property == "follow_mode")
            {
                metadata.type = "enum";
                metadata.enum_values = spline_follow_mode_enum_values_json();
                metadata.side_effects.emplace_back("changes what happens when the follower reaches the end of the spline");
            }
            else if (property == "alignment")
            {
                metadata.type = "enum";
                metadata.enum_values =
                    text_3d_alignment_enum_values_json();
                metadata.side_effects.emplace_back(
                    "regenerates 3d text geometry"
                );
            }
            else if (property == "preset")
            {
                metadata.type = "enum";
                metadata.enum_values = particle_preset_enum_values_json();
                metadata.side_effects.emplace_back("overwrites multiple particle properties");
            }
            else if (property == "blend_mode")
            {
                metadata.type = "enum";
                metadata.enum_values = particle_blend_mode_enum_values_json();
                metadata.side_effects.emplace_back("changes particle material blending");
            }
            else if (property == "lighting_mode")
            {
                metadata.type = "enum";
                metadata.enum_values = particle_lighting_mode_enum_values_json();
                metadata.side_effects.emplace_back("changes particle lighting path");
            }

            if (property.find("path") != std::string::npos || property.find("mesh") != std::string::npos || property.find("material") != std::string::npos)
            {
                if (metadata.unit.empty())
                {
                    metadata.unit = "path or resource name";
                }
            }
            if (property.find("distance") != std::string::npos || property.find("width") != std::string::npos || property.find("height") != std::string::npos || property.find("radius") != std::string::npos || property.find("offset") != std::string::npos)
            {
                if (metadata.unit.empty())
                {
                    metadata.unit = "meters";
                }
                if (metadata.range.empty())
                {
                    metadata.range = range_json(0.0f, std::nullopt);
                }
            }
            if (property.find("angle") != std::string::npos || property.find("yaw") != std::string::npos)
            {
                if (metadata.unit.empty())
                {
                    metadata.unit = "radians";
                }
            }
            if (property.find("fps") != std::string::npos)
            {
                metadata.unit = "frames per second";
                metadata.range = range_json(0.0f, std::nullopt);
            }
            if (property.find("rate") != std::string::npos)
            {
                metadata.unit = "per second";
                metadata.range = range_json(0.0f, std::nullopt);
            }
            if (property.find("count") != std::string::npos || property.find("resolution") != std::string::npos || property.find("segments") != std::string::npos || property.find("iterations") != std::string::npos)
            {
                metadata.range = range_json(0.0f, std::nullopt);
            }
            if (property.find("opacity") != std::string::npos || property.find("wet") != std::string::npos || property.find("blend") != std::string::npos || property.find("influence") != std::string::npos || property.find("stiffness") != std::string::npos || property.find("damping") != std::string::npos)
            {
                metadata.unit = "normalized";
                metadata.range = range_json(0.0f, 1.0f);
            }
            if (property == "mass")
            {
                metadata.unit = "kilograms";
                metadata.range = range_json(0.0f, std::nullopt);
                metadata.side_effects.emplace_back("updates rigid body mass");
            }
            if (property == "friction" || property == "friction_rolling" || property == "restitution")
            {
                metadata.unit = "coefficient";
                metadata.range = property == "restitution" ? range_json(0.0f, 1.0f) : range_json(0.0f, std::nullopt);
                metadata.side_effects.emplace_back("updates physics material");
            }
            if (property == "bounding_box" || property == "bounding_box_mesh" || property == "distance_squared" || property == "is_visible" || property == "lod_index" || property == "previous_lights" || property == "area_km2" || property == "height_samples" || property == "vertex_count" || property == "index_count" || property == "triangle_count")
            {
                metadata.note = "derived runtime state, edits can be overwritten by the component";
            }
            if (property == "spawn_burst")
            {
                metadata.side_effects.emplace_back("emits a particle burst");
            }
            if (property == "needs_road_regeneration")
            {
                metadata.note = "internal dirty flag, prefer component_action generate_road_mesh";
            }
            if (property == "source_spline_entity_id" || property == "instance_template_id" || property == "spline_entity_id")
            {
                metadata.unit = "entity id";
            }
            if (property == "progress")
            {
                metadata.unit = "normalized";
                metadata.range = range_json(0.0f, 1.0f);
            }
            if (property == "flip_forward")
            {
                metadata.note = "set true when the mesh drives backwards along the spline, rotates it 180 degrees";
            }
        }

        ComponentMetadata component_member_metadata(const Attribute& attribute)
        {
            const std::any value = attribute.getter();
            ComponentMetadata metadata;
            metadata.property = attribute_property_name(attribute);
            metadata.member   = attribute.name;
            metadata.type     = attribute.type;
            metadata.writable = attribute.writable && attribute_type_is_writable(value);
            if (!metadata.writable)
            {
                metadata.read_only_reason = attribute.writable ? "unsupported member type for component_set" : "member has no authoring setter";
            }

            apply_common_member_metadata(metadata);
            return metadata;
        }

        std::string component_member_metadata_json(Component* component)
        {
            std::string json = "[";
            bool first = true;

            for (const Attribute& attribute : component->GetAttributes())
            {
                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += component_metadata_to_json(component_member_metadata(attribute));
            }

            json += "]";
            return json;
        }

        std::string component_member_properties_to_json(Component* component)
        {
            std::string json = "{";
            bool first = true;

            for (const Attribute& attribute : component->GetAttributes())
            {
                const std::any value = attribute.getter();
                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += json_string(attribute_property_name(attribute)) + ":" + attribute_value_to_json(value, attribute.type);
            }

            json += "}";
            return json;
        }

        bool set_component_member(Component* component, const std::string& property, const std::string& value, std::string& error)
        {
            for (const Attribute& attribute : component->GetAttributes())
            {
                if (!attribute_matches_property(attribute, property))
                {
                    continue;
                }

                if (!attribute.writable)
                {
                    error = "member is read-only; use its authored property setter";
                    return false;
                }

                std::any parsed;
                if (!parse_attribute_value(attribute, value, parsed, error))
                {
                    return false;
                }

                try
                {
                    attribute.setter(parsed);
                    if (component->GetEntity()) component->GetEntity()->RefreshPreTickGate();
                }
                catch (const std::bad_any_cast&)
                {
                    error = "member type mismatch";
                    return false;
                }

                return true;
            }

            error = "unknown component property";
            return false;
        }

        std::string command_component_types()
        {
            return "{\"ok\":true,\"component_types\":" + component_types_json() + "}";
        }

        std::string command_entity_add_component(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("component edits require edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            const std::optional<std::string> type_name = get_argument(request, "type");
            if (!type_name)
            {
                return json_error("missing type");
            }

            const std::optional<ComponentType> type = component_type_from_name(*type_name);
            if (!type)
            {
                return json_error("unknown component type");
            }

            const bool component_exists =
                entity->GetComponentByType(*type) != nullptr;
            Component* component = entity->AddComponent(*type);
            if (component == nullptr)
            {
                return json_error("failed to add component");
            }
            if (
                *type == ComponentType::Render &&
                !component_exists
            )
            {
                Render* render =
                    static_cast<Render*>(component);
                render->SetMesh(MeshType::Cube);
                render->SetDefaultMaterial();
            }

            return "{\"ok\":true,\"entity\":" + entity_to_json_compact(entity) + "}";
        }

        std::string command_entity_remove_component(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("component edits require edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            const std::optional<std::string> type_name = get_argument(request, "type");
            if (!type_name)
            {
                return json_error("missing type");
            }

            const std::optional<ComponentType> type = component_type_from_name(*type_name);
            if (!type)
            {
                return json_error("unknown component type");
            }
            if (entity->GetComponentByType(*type) == nullptr)
            {
                return json_error("entity does not have component");
            }

            entity->RemoveComponentByType(*type);
            return "{\"ok\":true,\"entity\":" + entity_to_json_compact(entity) + "}";
        }

        std::string component_properties_to_json(Component* component)
        {
            if (component == nullptr)
            {
                return "{}";
            }

            const ComponentType type = component->GetType();
            std::string json = "{";

            if (type == ComponentType::Render)
            {
                Render* render = static_cast<Render*>(component);
                json += "\"mesh\":" + json_string(render->GetMeshName());
                json += ",\"material\":" + json_string(render->GetMaterialName());
                json += ",\"default_material\":" + json_bool(render->IsUsingDefaultMaterial());
                json += ",\"visible\":" + json_bool(render->IsVisible());
                json += ",\"casts_shadows\":" + json_bool(render->HasFlag(RenderFlags::CastsShadows));
                json += ",\"exclude_from_ray_tracing\":" + json_bool(render->HasFlag(RenderFlags::ExcludeFromRayTracing));
                json += ",\"max_render_distance\":" + std::to_string(render->GetMaxRenderDistance());
                json += ",\"max_shadow_distance\":" + std::to_string(render->GetMaxShadowDistance());
            }
            else if (type == ComponentType::Physics)
            {
                Physics* physics = static_cast<Physics*>(component);
                json += "\"body_type\":" + json_string(body_type_to_name(physics->GetBodyType()));
                json += ",\"static\":" + json_bool(physics->IsStatic());
                json += ",\"kinematic\":" + json_bool(physics->IsKinematic());
                json += ",\"enabled\":" + json_bool(physics->IsEnabled());
                json += ",\"mass\":" + std::to_string(physics->GetMass());
                json += ",\"friction\":" + std::to_string(physics->GetFriction());
                json += ",\"friction_rolling\":" + std::to_string(physics->GetFrictionRolling());
                json += ",\"restitution\":" + std::to_string(physics->GetRestitution());
                json += ",\"center_of_mass\":" + json_vector3(physics->GetCenterOfMass());
            }
            else if (type == ComponentType::Light)
            {
                Light* light = static_cast<Light*>(component);
                json += "\"light_type\":" + json_string(light_type_to_name(light->GetLightType()));
                json += ",\"color\":" + json_color(light->GetColor());
                json += ",\"temperature\":" + json_number(light->GetTemperature());
                json += ",\"intensity\":" + json_number(light->GetIntensityPhotometric());
                json += ",\"range\":" + json_number(light->GetRange());
                json += ",\"angle_degrees\":" + json_number(light->GetAngle() / math::deg_to_rad);
                json += ",\"area_width\":" + json_number(light->GetAreaWidth());
                json += ",\"area_height\":" + json_number(light->GetAreaHeight());
                json += ",\"shadows\":" + json_bool(light->GetFlag(LightFlags::Shadows));
                json += ",\"volumetric\":" + json_bool(light->GetFlag(LightFlags::Volumetric));
                json += ",\"draw_distance\":" + json_number(light->GetDrawDistance());
                json += ",\"shadow_distance\":" + json_number(light->GetShadowDistance());
                json += ",\"volumetric_distance\":" + json_number(light->GetVolumetricDistance());
                json += ",\"ies_profile\":" + json_string(light->GetIesProfile());
                if (light->GetIesSlot() != 0)
                {
                    json += ",\"ies_lumens\":" + json_number(light->GetIesLumens());
                    json += ",\"ies_peak_candela\":" + json_number(light->GetIesPeakCandela());
                }
            }
            else if (type == ComponentType::Camera)
            {
                Camera* camera = static_cast<Camera*>(component);
                json += "\"fov_degrees\":" + std::to_string(camera->GetFovHorizontalDeg());
                json += ",\"aperture\":" + std::to_string(camera->GetAperture());
                json += ",\"shutter_speed\":" + std::to_string(camera->GetShutterSpeed());
                json += ",\"iso\":" + std::to_string(camera->GetIso());
                json += ",\"exposure_mode\":" + json_string(
                    camera->GetExposureMode() == CameraExposureMode::automatic ?
                    "automatic" :
                    "manual"
                );
                json += ",\"auto_exposure_adaptation_speed\":" +
                    std::to_string(camera->GetAutoExposureAdaptationSpeed());
                json += ",\"auto_exposure_compensation\":" +
                    std::to_string(camera->GetAutoExposureCompensation());
                json += ",\"projection\":" + json_string(camera->GetProjectionType() == Projection_Perspective ? "perspective" : "orthographic");
                json += ",\"controllable\":" + json_bool(camera->GetFlag(CameraFlags::CanBeControlled));
                json += ",\"flashlight\":" + json_bool(camera->GetFlag(CameraFlags::Flashlight));
            }
            else if (type == ComponentType::AudioSource)
            {
                AudioSource* audio_source = static_cast<AudioSource*>(component);
                json += "\"clip\":" + json_string(audio_source->GetAudioClipName());
                json += ",\"mute\":" + json_bool(audio_source->GetMute());
                json += ",\"play_on_start\":" + json_bool(audio_source->GetPlayOnStart());
                json += ",\"loop\":" + json_bool(audio_source->GetLoop());
                json += ",\"is_3d\":" + json_bool(audio_source->GetIs3d());
                json += ",\"ambient\":" + json_bool(audio_source->GetAmbient());
                json += ",\"ambient_gain\":" + std::to_string(audio_source->GetAmbientGain());
                json += ",\"ambient_profile\":" + std::to_string(audio_source->GetAmbientProfile());
                json += ",\"habitat_gain\":" + std::to_string(audio_source->GetHabitatGain());
                json += ",\"is_playing\":" + json_bool(audio_source->IsPlaying());
                json += ",\"volume\":" + std::to_string(audio_source->GetVolume());
                json += ",\"pitch\":" + std::to_string(audio_source->GetPitch());
                json += ",\"reverb_enabled\":" + json_bool(audio_source->GetReverbEnabled());
                json += ",\"reverb_room_size\":" + std::to_string(audio_source->GetReverbRoomSize());
                json += ",\"reverb_decay\":" + std::to_string(audio_source->GetReverbDecay());
                json += ",\"reverb_wet\":" + std::to_string(audio_source->GetReverbWet());
            }
            else if (type == ComponentType::Script)
            {
                Script* script = static_cast<Script*>(component);
                json += "\"file_path\":" + json_string(script->file_path);
            }
            else if (type == ComponentType::Text3D)
            {
                Text3D* text_3d = static_cast<Text3D*>(component);
                json += "\"text\":" + json_string(text_3d->GetText());
                json += ",\"font_path\":" +
                    json_string(text_3d->GetFontPath());
                json += ",\"size\":" +
                    json_number(text_3d->GetSize());
                json += ",\"depth\":" +
                    json_number(text_3d->GetDepth());
                json += ",\"weight\":" +
                    json_number(text_3d->GetWeight());
                json += ",\"letter_spacing\":" +
                    json_number(text_3d->GetLetterSpacing());
                json += ",\"line_spacing\":" +
                    json_number(text_3d->GetLineSpacing());
                json += ",\"resolution\":" +
                    std::to_string(text_3d->GetResolution());
                json += ",\"alignment\":" +
                    json_string(
                        text_3d_alignment_to_name(
                            text_3d->GetAlignment()
                        )
                    );
                json += ",\"has_mesh\":" +
                    json_bool(text_3d->HasMesh());
            }
            else
            {
                return component_member_properties_to_json(component);
            }

            json += "}";
            return json;
        }

        std::string editable_properties_json(ComponentType type)
        {
            if (type == ComponentType::Render)
            {
                return "[\"mesh\",\"material\",\"default_material\",\"visible\",\"casts_shadows\",\"exclude_from_ray_tracing\",\"max_render_distance\",\"max_shadow_distance\"]";
            }
            if (type == ComponentType::Physics)
            {
                return "[\"body_type\",\"static\",\"kinematic\",\"enabled\",\"mass\",\"friction\",\"friction_rolling\",\"restitution\",\"center_of_mass\",\"linear_velocity\",\"angular_velocity\"]";
            }
            if (type == ComponentType::Light)
            {
                return "[\"light_type\",\"color\",\"temperature\",\"intensity\",\"range\",\"angle_degrees\",\"area_width\",\"area_height\",\"shadows\",\"volumetric\",\"draw_distance\",\"shadow_distance\",\"volumetric_distance\",\"ies_profile\"]";
            }
            if (type == ComponentType::Camera)
            {
                return
                    "[\"fov_degrees\",\"aperture\",\"shutter_speed\",\"iso\","
                    "\"exposure_mode\",\"auto_exposure_adaptation_speed\","
                    "\"auto_exposure_compensation\",\"projection\","
                    "\"controllable\",\"flashlight\"]";
            }
            if (type == ComponentType::AudioSource)
            {
                return "[\"clip\",\"mute\",\"play_on_start\",\"loop\",\"is_3d\",\"ambient\",\"ambient_profile\",\"volume\",\"pitch\",\"reverb_enabled\",\"reverb_room_size\",\"reverb_decay\",\"reverb_wet\"]";
            }
            if (type == ComponentType::Script)
            {
                return "[\"file_path\"]";
            }
            if (type == ComponentType::Text3D)
            {
                return "[\"text\",\"font_path\",\"size\",\"depth\",\"weight\",\"letter_spacing\",\"line_spacing\",\"resolution\",\"alignment\"]";
            }

            return "[]";
        }

        std::string component_actions_json(
            const ComponentType type
        )
        {
            if (type == ComponentType::Terrain)
            {
                return "[\"generate\"]";
            }
            if (type == ComponentType::Spline)
            {
                return "[\"generate_road_mesh\",\"clear_road_mesh\",\"spawn_instances\",\"clear_instances\",\"resample_control_points\",\"simplify_control_points\"]";
            }
            if (type == ComponentType::ParticleSystem)
            {
                return "[\"apply_preset\",\"trigger_burst\"]";
            }
            if (type == ComponentType::Physics)
            {
                return "[\"apply_force\",\"sync_wheel_offsets\",\"reset_tire_wear\",\"shift_up\",\"shift_down\",\"shift_to_neutral\"]";
            }
            if (type == ComponentType::AudioSource)
            {
                return "[\"play\",\"stop\"]";
            }
            if (type == ComponentType::Light)
            {
                return "[\"fit_to_mesh\"]";
            }
            if (type == ComponentType::Camera)
            {
                return "[\"focus_selected\"]";
            }
            if (type == ComponentType::Text3D)
            {
                return "[\"generate_mesh\",\"clear_mesh\"]";
            }

            return "[]";
        }

        std::string component_property_metadata_json(ComponentType type)
        {
            std::vector<ComponentMetadata> entries;
            const auto add = [&](ComponentMetadata metadata)
            {
                entries.emplace_back(std::move(metadata));
            };

            if (type == ComponentType::Render)
            {
                add({ "mesh", "", "string", true, "", "", "", { "loads or resolves render mesh", "updates render bounds and acceleration structure state" }, "", json_string("standard_cube") });
                add({ "material", "", "string", true, "", "", "", { "loads or resolves material resource", "changes rendered surface appearance" }, "", json_string("standard") });
                add({ "default_material", "", "bool", true, "", "", "", { "replaces the assigned material with the renderer default material" }, "", "false" });
                add({ "visible", "", "bool", true, "", "", "", {}, "", "true" });
                add({ "casts_shadows", "", "bool", true, "", "", "", { "affects shadow map participation" }, "", "true" });
                add({ "exclude_from_ray_tracing", "", "bool", true, "", "", "", { "affects blas and tlas participation" }, "", "false" });
                add({ "max_render_distance", "", "float", true, "meters", range_json(0.0f, std::nullopt), "", { "affects distance culling" }, "", "0" });
                add({ "max_shadow_distance", "", "float", true, "meters", range_json(0.0f, std::nullopt), "", { "affects shadow culling" }, "", "0" });
            }
            else if (type == ComponentType::Physics)
            {
                add({ "body_type", "", "enum", true, "", "", body_type_enum_values_json(), { "recreates physics body and shapes" }, "", json_string("box") });
                add({ "static", "", "bool", true, "", "", "", { "recreates or retags physics body" }, "", "false" });
                add({ "kinematic", "", "bool", true, "", "", "", { "changes simulation ownership of the body" }, "", "false" });
                add({ "enabled", "", "bool", true, "", "", "", { "enables or disables physics processing for the component" }, "", "true" });
                add({ "mass", "", "float", true, "kilograms", range_json(0.0f, std::nullopt), "", { "updates rigid body mass" }, "", "1" });
                add({ "friction", "", "float", true, "coefficient", range_json(0.0f, std::nullopt), "", { "updates physics material" }, "", "0.4" });
                add({ "friction_rolling", "", "float", true, "coefficient", range_json(0.0f, std::nullopt), "", { "updates rolling friction" }, "", "0.4" });
                add({ "restitution", "", "float", true, "coefficient", range_json(0.0f, 1.0f), "", { "updates physics material bounce" }, "", "0.2" });
                add({ "center_of_mass", "", "vector3", true, "meters local", "", "", { "changes rigid body center of mass" }, "", "[0,0,0]" });
                add({ "linear_velocity", "", "vector3", true, "meters per second", "", "", { "changes runtime rigid body velocity" }, "", "[0,0,0]" });
                add({ "angular_velocity", "", "vector3", true, "radians per second", "", "", { "changes runtime rigid body angular velocity" }, "", "[0,0,0]" });
            }
            else if (type == ComponentType::Light)
            {
                add({ "light_type", "", "enum", true, "", "", light_type_enum_values_json(), { "resets sensible range and shadow mode compatibility" }, "", json_string("point") });
                add({ "color", "", "color", true, "linear rgba", range_json(0.0f, std::nullopt), "", { "updates light color and marks renderer lighting data dirty" }, "", "[1,1,1,1]" });
                add({ "temperature", "", "float", true, "kelvin", range_json(1000.0f, 40000.0f), "", { "updates light color from blackbody temperature" }, "", "6500" });
                add({ "intensity", "", "float", true, "lux for directional, lumens otherwise", range_json(0.0f, std::nullopt), "", { "updates photometric and radiometric light intensity" }, "point/spot 8500, area 12000, directional 120000 for visible blockouts", "8500" });
                add({ "range", "", "float", true, "meters", range_json(0.0f, std::nullopt), "", { "affects light culling and shadow coverage" }, "point 30, spot 35, area 40", "30" });
                add({ "angle_degrees", "", "float", true, "degrees", range_json(0.1f, 179.0f), "", { "affects spot light cone and shadow projection" }, "", "45" });
                add({ "area_width", "", "float", true, "meters", range_json(0.0f, std::nullopt), "", { "affects area light emitter size" }, "", "6" });
                add({ "area_height", "", "float", true, "meters", range_json(0.0f, std::nullopt), "", { "affects area light emitter size" }, "", "3" });
                add({ "shadows", "", "bool", true, "", "", "", { "allocates and renders shadow maps when enabled" }, "", "true" });
                add({ "volumetric", "", "bool", true, "", "", "", { "enables volumetric lighting contribution" }, "", "false" });
                add({ "draw_distance", "", "float", true, "meters", range_json(0.0f, std::nullopt), "", { "affects light icon and debug drawing visibility" }, "", "60" });
                add({ "shadow_distance", "", "float", true, "meters", range_json(0.0f, std::nullopt), "", { "affects shadow rendering distance" }, "", "45" });
                add({ "volumetric_distance", "", "float", true, "meters", range_json(0.0f, std::nullopt), "", { "affects volumetric contribution distance" }, "", "0" });
                add({ "ies_profile", "", "string", true, "path relative to binaries", "", "", { "spot lights take the measured ies lm-63 distribution instead of the cone, angle becomes the profile extent, intensity stays lumens (ies_lumens is the file's flux)" }, "", json_string("") });
            }
            else if (type == ComponentType::Camera)
            {
                add({ "fov_degrees", "", "float", true, "degrees", range_json(1.0f, 179.0f), "", { "updates camera projection" }, "", "90" });
                add({ "aperture", "", "float", true, "f stop", range_json(0.01f, std::nullopt), "", { "changes manual exposure and depth of field behavior" }, "", "5.6" });
                add({ "shutter_speed", "", "float", true, "seconds", range_json(0.0001f, std::nullopt), "", { "changes manual exposure and motion blur behavior" }, "", "0.008" });
                add({ "iso", "", "float", true, "iso", range_json(1.0f, std::nullopt), "", { "changes manual exposure and film grain" }, "", "200" });
                add({
                    "exposure_mode",
                    "",
                    "enum",
                    true,
                    "",
                    "",
                    camera_exposure_mode_enum_values_json(),
                    { "selects physical or scene metered exposure" },
                    "",
                    json_string("automatic")
                });
                add({
                    "auto_exposure_adaptation_speed",
                    "",
                    "float",
                    true,
                    "",
                    range_json(0.0f, 10.0f),
                    "",
                    { "zero adapts immediately" },
                    "",
                    "1"
                });
                add({
                    "auto_exposure_compensation",
                    "",
                    "float",
                    true,
                    "ev stops",
                    range_json(-10.0f, 10.0f),
                    "",
                    { "positive values brighten automatic exposure" },
                    "",
                    "0"
                });
                add({ "projection", "", "enum", true, "", "", projection_enum_values_json(), { "updates camera projection" }, "", json_string("perspective") });
                add({ "controllable", "", "bool", true, "", "", "", { "enables editor fps camera controls" }, "", "true" });
                add({ "flashlight", "", "bool", true, "", "", "", { "creates or toggles transient camera flashlight entity" }, "", "false" });
            }
            else if (type == ComponentType::AudioSource)
            {
                add({ "clip", "", "string", true, "path or cached clip name", "", "", { "loads or resolves audio clip resource" }, "", json_string("") });
                add({ "mute", "", "bool", true, "", "", "", {}, "", "false" });
                add({ "play_on_start", "", "bool", true, "", "", "", { "changes behavior on component start" }, "", "false" });
                add({ "loop", "", "bool", true, "", "", "", { "changes playback looping behavior" }, "", "false" });
                add({ "is_3d", "", "bool", true, "", "", "", { "changes spatialization behavior" }, "", "true" });
                add({ "volume", "", "float", true, "linear gain", range_json(0.0f, 1.0f), "", { "updates playback gain" }, "", "1" });
                add({ "pitch", "", "float", true, "multiplier", range_json(0.0f, 4.0f), "", { "updates playback rate and pitch" }, "", "1" });
                add({ "reverb_enabled", "", "bool", true, "", "", "", { "enables reverb processing" }, "", "false" });
                add({ "reverb_room_size", "", "float", true, "normalized", range_json(0.0f, 1.0f), "", { "updates reverb parameters" }, "", "0.5" });
                add({ "reverb_decay", "", "float", true, "seconds", range_json(0.0f, std::nullopt), "", { "updates reverb parameters" }, "", "1" });
                add({ "reverb_wet", "", "float", true, "normalized", range_json(0.0f, 1.0f), "", { "updates reverb wet mix" }, "", "0.3" });
            }
            else if (type == ComponentType::Script)
            {
                add({ "file_path", "", "string", true, "path", "", "", { "changes script file loaded by the component" }, "", json_string("") });
            }
            else if (type == ComponentType::Text3D)
            {
                const std::vector<std::string> regeneration =
                {
                    "marks generated mesh dirty",
                    "regenerates geometry after a short debounce"
                };
                add({
                    "text",
                    "",
                    "string",
                    true,
                    "utf 8",
                    "",
                    "",
                    regeneration,
                    "",
                    json_string("Text")
                });
                add({
                    "font_path",
                    "",
                    "string",
                    true,
                    "font resource path",
                    "",
                    "",
                    regeneration,
                    "",
                    json_string("OpenSans/OpenSans-Medium.ttf")
                });
                add({
                    "size",
                    "",
                    "float",
                    true,
                    "meters",
                    range_json(0.01f, 1000.0f),
                    "",
                    regeneration,
                    "",
                    "1"
                });
                add({
                    "depth",
                    "",
                    "float",
                    true,
                    "meters",
                    range_json(0.001f, 1000.0f),
                    "",
                    regeneration,
                    "",
                    "0.1"
                });
                add({
                    "weight",
                    "",
                    "float",
                    true,
                    "meters",
                    range_json(0.0f, 1.0f),
                    "",
                    regeneration,
                    "",
                    "0"
                });
                add({
                    "letter_spacing",
                    "",
                    "float",
                    true,
                    "meters",
                    range_json(-10.0f, 100.0f),
                    "",
                    regeneration,
                    "",
                    "0"
                });
                add({
                    "line_spacing",
                    "",
                    "float",
                    true,
                    "multiplier",
                    range_json(0.1f, 10.0f),
                    "",
                    regeneration,
                    "",
                    "1.2"
                });
                add({
                    "resolution",
                    "",
                    "uint32",
                    true,
                    "pixels per em",
                    range_json(32.0f, 512.0f),
                    "",
                    regeneration,
                    "",
                    "128"
                });
                add({
                    "alignment",
                    "",
                    "enum",
                    true,
                    "",
                    "",
                    text_3d_alignment_enum_values_json(),
                    regeneration,
                    "",
                    json_string("left")
                });
                add({
                    "has_mesh",
                    "",
                    "bool",
                    false,
                    "",
                    "",
                    "",
                    {},
                    "derived generated mesh state",
                    "false"
                });
            }

            std::string json = "[";
            for (size_t i = 0; i < entries.size(); i++)
            {
                if (i != 0)
                {
                    json += ",";
                }
                json += component_metadata_to_json(entries[i]);
            }
            json += "]";
            return json;
        }

        std::string command_component_get(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            const std::optional<std::string> type_name = get_argument(request, "type");
            if (!type_name)
            {
                return json_error("missing type");
            }

            const std::optional<ComponentType> type = component_type_from_name(*type_name);
            if (!type)
            {
                return json_error("unknown component type");
            }

            Component* component = entity->GetComponentByType(*type);
            if (component == nullptr)
            {
                return json_error("entity does not have component");
            }

            std::string json = "{\"ok\":true,\"component\":{";
            json += "\"type\":" + json_string(*type_name);
            json += ",\"editable_properties\":" + editable_properties_json(*type);
            json += ",\"editable_members\":" + component_member_names_json(component);
            json += ",\"actions\":" + component_actions_json(*type);
            json += ",\"property_metadata\":" + component_property_metadata_json(*type);
            json += ",\"member_metadata\":" + component_member_metadata_json(component);
            json += ",\"properties\":" + component_properties_to_json(component);
            json += ",\"members\":" + component_members_to_json(component);
            json += "}}";
            return json;
        }

        bool assign_render_material(Render* render, const std::string& name_or_path, std::string& error)
        {
            if (name_or_path == "default")
            {
                render->SetDefaultMaterial();
                return true;
            }

            // prefer the cached resource so both resource names and paths bind
            if (std::shared_ptr<IResource> cached = get_resource_shared_by_name_or_path(name_or_path, ResourceType::Material))
            {
                render->SetMaterial(std::static_pointer_cast<Material>(cached));
                return true;
            }

            if (FileSystem::IsFile(name_or_path))
            {
                render->SetMaterial(name_or_path);
                return true;
            }

            error = "material not found by cached name, cached path, or file path: " + name_or_path;
            return false;
        }

        bool set_render_property(Render* render, const std::string& property, const std::string& value, std::string& error)
        {
            if (property == "mesh")
            {
                const std::optional<MeshType> parsed = mesh_type_from_name(value);
                if (!parsed)
                {
                    error = "invalid mesh";
                    return false;
                }
                render->SetMesh(*parsed);
                return true;
            }
            if (property == "material")
            {
                if (render->GetMesh() == nullptr)
                {
                    error =
                        "assign a mesh before assigning a render material";
                    return false;
                }
                return assign_render_material(render, value, error);
            }
            if (property == "default_material")
            {
                bool parsed = false;
                if (!parse_bool(value, parsed))
                {
                    error = "invalid default_material";
                    return false;
                }
                if (parsed)
                {
                    render->SetDefaultMaterial();
                }
                return true;
            }
            if (property == "visible")
            {
                bool parsed = false;
                if (!parse_bool(value, parsed))
                {
                    error = "invalid visible";
                    return false;
                }
                render->SetVisible(parsed);
                return true;
            }
            if (property == "casts_shadows" || property == "exclude_from_ray_tracing")
            {
                bool parsed = false;
                if (!parse_bool(value, parsed))
                {
                    error = "invalid render flag";
                    return false;
                }
                render->SetFlag(property == "casts_shadows" ? RenderFlags::CastsShadows : RenderFlags::ExcludeFromRayTracing, parsed);
                return true;
            }
            if (property == "max_render_distance" || property == "max_shadow_distance")
            {
                float parsed = 0.0f;
                if (!parse_float(value, parsed))
                {
                    error = "invalid render distance";
                    return false;
                }
                if (property == "max_render_distance")
                {
                    render->SetMaxRenderDistance(parsed);
                }
                else
                {
                    render->SetMaxShadowDistance(parsed);
                }
                return true;
            }

            error = "unsupported render property";
            return false;
        }

        bool set_physics_property(Physics* physics, const std::string& property, const std::string& value, std::string& error)
        {
            if (property == "body_type")
            {
                const std::optional<BodyType> parsed = body_type_from_name(value);
                if (!parsed)
                {
                    error = "invalid body_type";
                    return false;
                }
                physics->SetBodyType(*parsed);
                return true;
            }
            if (property == "static" || property == "kinematic" || property == "enabled")
            {
                bool parsed = false;
                if (!parse_bool(value, parsed))
                {
                    error = "invalid physics boolean";
                    return false;
                }
                if (property == "static")
                {
                    physics->SetStatic(parsed);
                }
                else if (property == "kinematic")
                {
                    physics->SetKinematic(parsed);
                }
                else
                {
                    physics->SetEnabled(parsed);
                }
                return true;
            }
            if (property == "mass" || property == "friction" || property == "friction_rolling" || property == "restitution")
            {
                float parsed = 0.0f;
                if (!parse_float(value, parsed))
                {
                    error = "invalid physics float";
                    return false;
                }
                if (property == "mass")
                {
                    physics->SetMass(parsed);
                }
                else if (property == "friction")
                {
                    physics->SetFriction(parsed);
                }
                else if (property == "friction_rolling")
                {
                    physics->SetFrictionRolling(parsed);
                }
                else
                {
                    physics->SetRestitution(parsed);
                }
                return true;
            }
            if (property == "center_of_mass" || property == "linear_velocity" || property == "angular_velocity")
            {
                math::Vector3 parsed;
                if (!parse_vector3(value, parsed))
                {
                    error = "invalid physics vector";
                    return false;
                }
                if (property == "center_of_mass")
                {
                    physics->SetCenterOfMass(parsed);
                }
                else if (property == "linear_velocity")
                {
                    physics->SetLinearVelocity(parsed);
                }
                else
                {
                    physics->SetAngularVelocity(parsed);
                }
                return true;
            }

            error = "unsupported physics property";
            return false;
        }

        bool set_light_property(Light* light, const std::string& property, const std::string& value, std::string& error)
        {
            if (property == "light_type")
            {
                const std::optional<LightType> parsed = light_type_from_name(value);
                if (!parsed)
                {
                    error = "invalid light_type";
                    return false;
                }
                light->SetLightType(*parsed);
                return true;
            }
            if (property == "color")
            {
                Color parsed;
                if (!parse_color(value, parsed))
                {
                    error = "invalid color";
                    return false;
                }
                light->SetColor(parsed);
                return true;
            }
            if (property == "ies_profile")
            {
                light->SetIesProfile(value);
                if (!value.empty() && light->GetIesProfile() != value)
                {
                    error = "ies profile could not be loaded, see console";
                    return false;
                }
                return true;
            }
            if (property == "shadows" || property == "volumetric")
            {
                bool parsed = false;
                if (!parse_bool(value, parsed))
                {
                    error = "invalid light flag";
                    return false;
                }
                light->SetFlag(property == "shadows" ? LightFlags::Shadows : LightFlags::Volumetric, parsed);
                return true;
            }
            if (
                property == "temperature" || property == "intensity" || property == "range" ||
                property == "angle_degrees" || property == "area_width" || property == "area_height" ||
                property == "draw_distance" || property == "shadow_distance" || property == "volumetric_distance"
            )
            {
                float parsed = 0.0f;
                if (!parse_float(value, parsed))
                {
                    error = "invalid light float";
                    return false;
                }
                if (property == "temperature")
                {
                    light->SetTemperature(parsed);
                }
                else if (property == "intensity")
                {
                    light->SetIntensity(parsed);
                }
                else if (property == "range")
                {
                    light->SetRange(parsed);
                }
                else if (property == "angle_degrees")
                {
                    light->SetAngle(parsed * math::deg_to_rad);
                }
                else if (property == "area_width")
                {
                    light->SetAreaWidth(parsed);
                }
                else if (property == "area_height")
                {
                    light->SetAreaHeight(parsed);
                }
                else if (property == "draw_distance")
                {
                    light->SetDrawDistance(parsed);
                }
                else if (property == "shadow_distance")
                {
                    light->SetShadowDistance(parsed);
                }
                else
                {
                    light->SetVolumetricDistance(parsed);
                }
                return true;
            }

            error = "unsupported light property";
            return false;
        }

        bool set_camera_property(Camera* camera, const std::string& property, const std::string& value, std::string& error)
        {
            if (property == "projection")
            {
                if (value == "perspective")
                {
                    camera->SetProjection(Projection_Perspective);
                    return true;
                }
                if (value == "orthographic")
                {
                    camera->SetProjection(Projection_Orthographic);
                    return true;
                }

                error = "invalid projection";
                return false;
            }
            if (property == "exposure_mode")
            {
                if (value == "manual")
                {
                    camera->SetExposureMode(CameraExposureMode::manual);
                    return true;
                }
                if (value == "automatic")
                {
                    camera->SetExposureMode(CameraExposureMode::automatic);
                    return true;
                }

                error = "invalid camera exposure mode";
                return false;
            }
            if (property == "controllable" || property == "flashlight")
            {
                bool parsed = false;
                if (!parse_bool(value, parsed))
                {
                    error = "invalid camera flag";
                    return false;
                }
                camera->SetFlag(property == "controllable" ? CameraFlags::CanBeControlled : CameraFlags::Flashlight, parsed);
                return true;
            }
            if (
                property == "fov_degrees" ||
                property == "aperture" ||
                property == "shutter_speed" ||
                property == "iso" ||
                property == "auto_exposure_adaptation_speed" ||
                property == "auto_exposure_compensation"
            )
            {
                float parsed = 0.0f;
                if (!parse_float(value, parsed))
                {
                    error = "invalid camera float";
                    return false;
                }
                if (property == "fov_degrees")
                {
                    camera->SetFovHorizontalDeg(parsed);
                }
                else if (property == "aperture")
                {
                    camera->SetAperture(parsed);
                }
                else if (property == "shutter_speed")
                {
                    camera->SetShutterSpeed(parsed);
                }
                else if (property == "iso")
                {
                    camera->SetIso(parsed);
                }
                else if (property == "auto_exposure_adaptation_speed")
                {
                    camera->SetAutoExposureAdaptationSpeed(parsed);
                }
                else
                {
                    camera->SetAutoExposureCompensation(parsed);
                }
                return true;
            }

            error = "unsupported camera property";
            return false;
        }

        bool set_audio_source_property(AudioSource* audio_source, const std::string& property, const std::string& value, std::string& error)
        {
            if (property == "ambient_profile")
            {
                if (value != "0" && value != "1" && value != "2" && value != "3")
                { error = "ambient_profile must be 0 (region), 1 (cicadas), 2 (birds) or 3 (wind)"; return false; }
                audio_source->SetAmbientProfile(static_cast<uint32_t>(value[0] - '0'));
                return true;
            }
            if (property == "ambient")
            {
                bool parsed = false;
                if (!parse_bool(value, parsed)) { error = "invalid ambient boolean"; return false; }
                audio_source->SetAmbient(parsed);
                return true;
            }
            if (property == "clip")
            {
                audio_source->SetAudioClip(value);
                return true;
            }
            if (property == "mute" || property == "play_on_start" || property == "loop" || property == "is_3d" || property == "reverb_enabled")
            {
                bool parsed = false;
                if (!parse_bool(value, parsed))
                {
                    error = "invalid audio boolean";
                    return false;
                }
                if (property == "mute")
                {
                    audio_source->SetMute(parsed);
                }
                else if (property == "play_on_start")
                {
                    audio_source->SetPlayOnStart(parsed);
                }
                else if (property == "loop")
                {
                    audio_source->SetLoop(parsed);
                }
                else if (property == "is_3d")
                {
                    audio_source->SetIs3d(parsed);
                }
                else
                {
                    audio_source->SetReverbEnabled(parsed);
                }
                return true;
            }
            if (property == "volume" || property == "pitch" || property == "reverb_room_size" || property == "reverb_decay" || property == "reverb_wet")
            {
                float parsed = 0.0f;
                if (!parse_float(value, parsed))
                {
                    error = "invalid audio float";
                    return false;
                }
                if (property == "volume")
                {
                    audio_source->SetVolume(parsed);
                }
                else if (property == "pitch")
                {
                    audio_source->SetPitch(parsed);
                }
                else if (property == "reverb_room_size")
                {
                    audio_source->SetReverbRoomSize(parsed);
                }
                else if (property == "reverb_decay")
                {
                    audio_source->SetReverbDecay(parsed);
                }
                else
                {
                    audio_source->SetReverbWet(parsed);
                }
                return true;
            }

            error = "unsupported audio_source property";
            return false;
        }

        bool set_text_3d_property(
            Text3D* text_3d,
            const std::string& property,
            const std::string& value,
            std::string& error
        )
        {
            if (property == "text")
            {
                text_3d->SetText(value);
                return true;
            }

            if (property == "font_path")
            {
                if (!FileSystem::IsSupportedFontFile(value))
                {
                    error = "unsupported font file";
                    return false;
                }

                std::string resolved_path = value;
                if (!FileSystem::IsFile(resolved_path))
                {
                    resolved_path =
                        ResourceCache::GetResourceDirectory(
                            ResourceDirectory::Fonts
                        ) +
                        "/" +
                        value;
                }
                if (!FileSystem::IsFile(resolved_path))
                {
                    error = "font file not found";
                    return false;
                }

                text_3d->SetFontPath(resolved_path);
                return true;
            }

            if (property == "alignment")
            {
                const std::optional<Text3DAlignment> alignment =
                    text_3d_alignment_from_name(value);
                if (!alignment)
                {
                    error = "invalid text_3d alignment";
                    return false;
                }

                text_3d->SetAlignment(*alignment);
                return true;
            }

            if (property == "resolution")
            {
                uint32_t resolution = 0;
                if (!parse_uint32(value, resolution))
                {
                    error = "invalid text_3d resolution";
                    return false;
                }

                text_3d->SetResolution(resolution);
                return true;
            }

            if (
                property == "size" ||
                property == "depth" ||
                property == "weight" ||
                property == "letter_spacing" ||
                property == "line_spacing"
            )
            {
                float parsed = 0.0f;
                if (!parse_float(value, parsed))
                {
                    error = "invalid text_3d float";
                    return false;
                }

                if (property == "size")
                {
                    text_3d->SetSize(parsed);
                }
                else if (property == "depth")
                {
                    text_3d->SetDepth(parsed);
                }
                else if (property == "weight")
                {
                    text_3d->SetWeight(parsed);
                }
                else if (property == "letter_spacing")
                {
                    text_3d->SetLetterSpacing(parsed);
                }
                else
                {
                    text_3d->SetLineSpacing(parsed);
                }

                return true;
            }

            error = "unsupported text_3d property";
            return false;
        }

        bool set_component_property(ComponentType type, Component* component, const std::string& property, const std::string& value, std::string& error)
        {
            bool changed = false;
            if (type == ComponentType::Render)
            {
                changed = set_render_property(static_cast<Render*>(component), property, value, error);
            }
            else if (type == ComponentType::Physics)
            {
                changed = set_physics_property(static_cast<Physics*>(component), property, value, error);
            }
            else if (type == ComponentType::Light)
            {
                changed = set_light_property(static_cast<Light*>(component), property, value, error);
            }
            else if (type == ComponentType::Camera)
            {
                changed = set_camera_property(static_cast<Camera*>(component), property, value, error);
            }
            else if (type == ComponentType::AudioSource)
            {
                changed = set_audio_source_property(static_cast<AudioSource*>(component), property, value, error);
            }
            else if (type == ComponentType::Text3D)
            {
                const std::string text_property =
                    property.rfind("m_", 0) == 0
                    ? property.substr(2)
                    : property;
                changed = set_text_3d_property(
                    static_cast<Text3D*>(component),
                    text_property,
                    value,
                    error
                );
            }
            else if (type == ComponentType::Script && property == "file_path")
            {
                static_cast<Script*>(component)->LoadScriptFile(value);
                changed = true;
            }
            else
            {
                changed = set_component_member(component, property, value, error);
            }

            if (!changed && (error.empty() || error.rfind("unsupported", 0) == 0))
            {
                std::string member_error;
                changed = set_component_member(component, property, value, member_error);
                if (!changed)
                {
                    error = member_error;
                }
            }

            return changed;
        }

        std::string command_component_set(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("component edits require edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            const std::optional<std::string> type_name = get_argument(request, "type");
            const std::optional<std::string> property = get_argument(request, "property");
            const std::optional<std::string> value = get_argument(request, "value");
            if (!type_name || !property || !value)
            {
                return json_error("missing type, property, or value");
            }

            const std::optional<ComponentType> type = component_type_from_name(*type_name);
            if (!type)
            {
                return json_error("unknown component type");
            }

            Component* component = entity->GetComponentByType(*type);
            if (component == nullptr)
            {
                return json_error("entity does not have component");
            }

            if (!set_component_property(*type, component, *property, *value, error))
            {
                return json_error(error.empty() ? "failed to set component property" : error);
            }

            std::string json = "{\"ok\":true,\"component\":{";
            json += "\"type\":" + json_string(*type_name);
            json += ",\"properties\":" + component_properties_to_json(component);
            json += ",\"members\":" + component_members_to_json(component);
            json += "}}";
            return json;
        }

        std::string command_component_set_batch(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("component edits require edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            const std::optional<std::string> type_name = get_argument(request, "type");
            const std::optional<std::string> count_arg = get_argument(request, "count");
            if (!type_name || !count_arg)
            {
                return json_error("missing type or count");
            }

            const std::optional<ComponentType> type = component_type_from_name(*type_name);
            if (!type)
            {
                return json_error("unknown component type");
            }

            uint64_t count = 0;
            if (!parse_uint64(*count_arg, count) || count == 0 || count > 128)
            {
                return json_error("count must be between 1 and 128");
            }

            Component* component = entity->GetComponentByType(*type);
            if (component == nullptr)
            {
                return json_error("entity does not have component");
            }

            for (uint64_t i = 0; i < count; i++)
            {
                const std::optional<std::string> property = get_argument(request, "property_" + std::to_string(i));
                const std::optional<std::string> value = get_argument(request, "value_" + std::to_string(i));
                if (!property || !value)
                {
                    return json_error("missing batch property or value at index " + std::to_string(i));
                }

                if (!set_component_property(*type, component, *property, *value, error))
                {
                    // the properties before this one are already set, so the count says how many took
                    const std::string message = error.empty()
                        ? "failed to set component property"
                        : error;
                    return json_batch_failure(
                        message,
                        "applied",
                        "[",
                        static_cast<uint32_t>(i),
                        i,
                        json_error(message)
                    );
                }
            }

            std::string json = "{\"ok\":true";
            json += ",\"updated_count\":" + std::to_string(count);
            json += ",\"component\":{";
            json += "\"type\":" + json_string(*type_name);
            json += ",\"properties\":" + component_properties_to_json(component);
            json += ",\"members\":" + component_members_to_json(component);
            json += "}}";
            return json;
        }

        std::string command_entity_find_by_component(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            const std::optional<std::string> type_name = get_argument(request, "type");
            if (!type_name)
            {
                return json_error("missing type");
            }

            const std::optional<ComponentType> type = component_type_from_name(*type_name);
            if (!type)
            {
                return json_error("unknown component type");
            }

            uint32_t limit = 100;
            if (const std::optional<std::string> limit_arg = get_argument(request, "limit"))
            {
                uint64_t parsed = 0;
                if (!parse_uint64(*limit_arg, parsed) || parsed == 0 || parsed > 1000)
                {
                    return json_error("limit must be between 1 and 1000");
                }

                limit = static_cast<uint32_t>(parsed);
            }

            uint32_t offset = 0;
            if (const std::optional<std::string> offset_arg = get_argument(request, "offset"))
            {
                uint64_t parsed = 0;
                if (!parse_uint64(*offset_arg, parsed))
                {
                    return json_error("invalid offset");
                }

                offset = static_cast<uint32_t>(parsed);
            }

            uint32_t total = 0;
            uint32_t emitted = 0;
            std::string json = "{\"ok\":true";
            json += ",\"type\":" + json_string(*type_name);
            json += ",\"offset\":" + std::to_string(offset);
            json += ",\"entities\":[";
            bool first = true;
            for (Entity* entity : World::GetEntities())
            {
                if (entity == nullptr || entity->GetComponentByType(*type) == nullptr)
                {
                    continue;
                }

                if (total++ < offset)
                {
                    continue;
                }

                if (emitted >= limit)
                {
                    continue;
                }

                if (!first)
                {
                    json += ",";
                }
                first = false;
                emitted++;
                json += entity_to_json_compact(entity);
            }

            json += "],\"total\":" + std::to_string(total);
            json += ",\"count\":" + std::to_string(emitted);
            json += ",\"truncated\":" + json_bool(total > offset + emitted);
            json += "}";
            return json;
        }

        std::string command_component_action(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            const std::optional<std::string> type_name = get_argument(request, "type");
            const std::optional<std::string> action_arg = get_argument(request, "action");
            if (!type_name || !action_arg)
            {
                return json_error("missing type or action");
            }

            const std::optional<ComponentType> type = component_type_from_name(*type_name);
            if (!type)
            {
                return json_error("unknown component type");
            }

            Component* component = entity->GetComponentByType(*type);
            if (component == nullptr)
            {
                return json_error("entity does not have component");
            }

            const std::string action = to_lower_copy(*action_arg);
            const bool runtime_action =
                (*type == ComponentType::Physics && (action == "apply_force" || action == "sync_wheel_offsets" || action == "reset_tire_wear" || action == "shift_up" || action == "shift_down" || action == "shift_to_neutral" || action == "draw_debug_visualization")) ||
                (*type == ComponentType::AudioSource && (action == "play" || action == "stop"));

            if (!runtime_action && !is_edit_mode())
            {
                return json_error("component action requires edit mode");
            }

            std::string result_json = "{}";
            if (*type == ComponentType::Terrain && action == "generate")
            {
                static_cast<Terrain*>(component)->Generate();
            }
            else if (*type == ComponentType::Spline && action == "generate_road_mesh")
            {
                static_cast<Spline*>(component)->GenerateRoadMesh();
            }
            else if (*type == ComponentType::Spline && action == "clear_road_mesh")
            {
                static_cast<Spline*>(component)->ClearRoadMesh();
            }
            else if (*type == ComponentType::Spline && action == "spawn_instances")
            {
                static_cast<Spline*>(component)->SpawnInstances();
            }
            else if (*type == ComponentType::Spline && action == "clear_instances")
            {
                static_cast<Spline*>(component)->ClearInstances();
            }
            else if (*type == ComponentType::Spline && action == "resample_control_points")
            {
                // value is the spacing in meters, 40 suits roads, smaller keeps tight curves
                const std::optional<std::string> value_arg = get_argument(request, "value");
                float spacing = 40.0f;
                if (value_arg && (!parse_float(*value_arg, spacing) || spacing <= 0.0f))
                {
                    return json_error("invalid spacing value");
                }
                Spline* spline = static_cast<Spline*>(component);
                spline->ResampleControlPoints(spacing);
                result_json = "{\"point_count\":" + std::to_string(spline->GetControlPointCount()) + "}";
            }
            else if (*type == ComponentType::Spline && action == "simplify_control_points")
            {
                // value is the tolerance in meters, points closer than this to the neighbour chord go
                const std::optional<std::string> value_arg = get_argument(request, "value");
                float tolerance = 2.0f;
                if (value_arg && (!parse_float(*value_arg, tolerance) || tolerance < 0.0f))
                {
                    return json_error("invalid tolerance value");
                }
                Spline* spline = static_cast<Spline*>(component);
                spline->SimplifyControlPoints(tolerance);
                result_json = "{\"point_count\":" + std::to_string(spline->GetControlPointCount()) + "}";
            }
            else if (
                *type == ComponentType::Text3D &&
                action == "generate_mesh"
            )
            {
                Text3D* text_3d = static_cast<Text3D*>(component);
                if (!text_3d->GenerateMesh())
                {
                    return json_error(
                        "failed to generate 3d text mesh"
                    );
                }
                result_json =
                    "{\"has_mesh\":" +
                    json_bool(text_3d->HasMesh()) +
                    "}";
            }
            else if (
                *type == ComponentType::Text3D &&
                action == "clear_mesh"
            )
            {
                Text3D* text_3d = static_cast<Text3D*>(component);
                text_3d->ClearMesh();
                result_json = "{\"has_mesh\":false}";
            }
            else if (*type == ComponentType::ParticleSystem && action == "apply_preset")
            {
                const std::optional<std::string> preset_arg = get_argument(request, "preset");
                const std::optional<std::string> value_arg = get_argument(request, "value");
                const std::string preset_name = preset_arg ? *preset_arg : (value_arg ? *value_arg : "");
                const std::optional<ParticlePreset> preset = particle_preset_from_name(to_lower_copy(preset_name));
                if (!preset)
                {
                    return json_error("invalid particle preset");
                }

                static_cast<ParticleSystem*>(component)->ApplyPreset(*preset);
            }
            else if (*type == ComponentType::ParticleSystem && action == "trigger_burst")
            {
                const std::optional<std::string> count_arg = get_argument(request, "count");
                const std::optional<std::string> value_arg = get_argument(request, "value");
                float count = 0.0f;
                if (!parse_float(count_arg ? *count_arg : (value_arg ? *value_arg : ""), count) || count <= 0.0f)
                {
                    return json_error("invalid burst count");
                }

                static_cast<ParticleSystem*>(component)->TriggerBurst(count);
            }
            else if (*type == ComponentType::Physics && action == "apply_force")
            {
                const std::optional<std::string> force_arg = get_argument(request, "force");
                if (!force_arg)
                {
                    return json_error("missing force");
                }

                math::Vector3 force;
                if (!parse_vector3(*force_arg, force))
                {
                    return json_error("invalid force");
                }

                PhysicsForce mode = PhysicsForce::Impulse;
                if (const std::optional<std::string> mode_arg = get_argument(request, "mode"))
                {
                    const std::optional<PhysicsForce> parsed = physics_force_from_name(to_lower_copy(*mode_arg));
                    if (!parsed)
                    {
                        return json_error("invalid force mode");
                    }
                    mode = *parsed;
                }

                static_cast<Physics*>(component)->ApplyForce(force, mode);
            }
            else if (*type == ComponentType::Physics && action == "sync_wheel_offsets")
            {
                CarPhysics::Get(*static_cast<Physics*>(component)).SyncWheelOffsetsFromEntities();
            }
            else if (*type == ComponentType::Physics && action == "reset_tire_wear")
            {
                CarPhysics::Get(*static_cast<Physics*>(component)).ResetTireWear();
            }
            else if (*type == ComponentType::Physics && action == "shift_up")
            {
                CarPhysics::Get(*static_cast<Physics*>(component)).ShiftUp();
            }
            else if (*type == ComponentType::Physics && action == "shift_down")
            {
                CarPhysics::Get(*static_cast<Physics*>(component)).ShiftDown();
            }
            else if (*type == ComponentType::Physics && action == "shift_to_neutral")
            {
                CarPhysics::Get(*static_cast<Physics*>(component)).ShiftToNeutral();
            }
            else if (*type == ComponentType::AudioSource && action == "play")
            {
                static_cast<AudioSource*>(component)->PlayClip();
            }
            else if (*type == ComponentType::AudioSource && action == "stop")
            {
                static_cast<AudioSource*>(component)->StopClip();
            }
            else if (*type == ComponentType::Light && action == "fit_to_mesh")
            {
                const bool fitted = static_cast<Light*>(component)->FitToMesh();
                result_json = "{\"fitted\":" + json_bool(fitted) + "}";
            }
            else if (*type == ComponentType::Camera && action == "focus_selected")
            {
                spartan::CameraController::Get(*static_cast<Camera*>(component)).Focus(spartan::Selection::GetSelectedEntity(), spartan::Selection::GetSelectedInstance());
            }
            else
            {
                return json_error("unsupported component action");
            }

            std::string json = "{\"ok\":true";
            json += ",\"entity\":" + entity_to_json_compact(entity);
            json += ",\"type\":" + json_string(*type_name);
            json += ",\"action\":" + json_string(action);
            json += ",\"result\":" + result_json;
            json += ",\"component\":{";
            json += "\"type\":" + json_string(*type_name);
            json += ",\"properties\":" + component_properties_to_json(component);
            json += ",\"members\":" + component_members_to_json(component);
            json += "}}";
            return json;
        }
    void Register()
    {
        RegisterMcpCommand("entity_find_by_component", command_entity_find_by_component);
        RegisterMcpCommand("component_types", [](const McpRequest&) { return command_component_types(); });
        RegisterMcpCommand("entity_add_component", command_entity_add_component);
        RegisterMcpCommand("entity_remove_component", command_entity_remove_component);
        RegisterMcpCommand("component_get", command_component_get);
        RegisterMcpCommand("component_set", command_component_set);
        RegisterMcpCommand("component_set_batch", command_component_set_batch);
        RegisterMcpCommand("component_action", command_component_action);
    }
}
