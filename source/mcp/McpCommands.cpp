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
#include "McpCommandsComponents.h"
#include "McpCommandsDiagnostics.h"
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
#include "../input/Input.h"
#include "../core/Window.h"
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

namespace spartan
{
    namespace
    {
        // the json, argument and entity helpers now live in McpCommandsCommon so the world build commands can
        // reach them from their own file, this keeps every call site here written the same way as before
        using namespace mcp_common;
        using namespace mcp_components;

        bool parse_float_array(
            const std::string& value,
            std::vector<float>& values,
            size_t max_count
        )
        {
            if (value.empty())
            {
                return false;
            }

            std::stringstream stream(value);
            std::string part;
            while (std::getline(stream, part, ','))
            {
                if (values.size() >= max_count)
                {
                    return false;
                }

                float parsed = 0.0f;
                if (!parse_float(part, parsed))
                {
                    return false;
                }
                values.emplace_back(parsed);
            }

            return !values.empty();
        }

        bool parse_index_array(
            const std::string& value,
            std::vector<uint32_t>& values,
            size_t max_count
        )
        {
            if (value.empty())
            {
                return false;
            }

            std::stringstream stream(value);
            std::string part;
            while (std::getline(stream, part, ','))
            {
                if (values.size() >= max_count || part.empty())
                {
                    return false;
                }
                if (
                    !std::all_of(
                        part.begin(),
                        part.end(),
                        [](unsigned char character)
                        {
                            return std::isdigit(character) != 0;
                        }
                    )
                )
                {
                    return false;
                }

                char* end = nullptr;
                const unsigned long long parsed =
                    std::strtoull(part.c_str(), &end, 10);
                if (
                    end == part.c_str() ||
                    *end != '\0' ||
                    parsed > UINT32_MAX
                )
                {
                    return false;
                }
                values.emplace_back(static_cast<uint32_t>(parsed));
            }

            return !values.empty();
        }

        std::string active_mcp_resource_directory()
        {
            return World::GetGeneratedResourceDirectory();
        }

        std::optional<std::string> resolve_mcp_mesh_path(
            const McpRequest& request,
            std::string& error
        )
        {
            const std::optional<std::string> path_arg =
                get_argument(request, "path");
            const std::optional<std::string> name_arg =
                get_argument(request, "name");
            if (
                (!path_arg || path_arg->empty()) &&
                (!name_arg || name_arg->empty())
            )
            {
                error = "missing path or name";
                return std::nullopt;
            }

            const std::filesystem::path meshes_directory =
                std::filesystem::path(
                    active_mcp_resource_directory()
                ) /
                "meshes";
            std::filesystem::path requested =
                path_arg && !path_arg->empty()
                ? std::filesystem::path(*path_arg)
                : std::filesystem::path(*name_arg);

            std::filesystem::path resolved;
            if (path_is_within(requested, meshes_directory))
            {
                resolved = requested;
            }
            else
            {
                resolved =
                    meshes_directory /
                    requested.filename();
            }

            if (resolved.extension() != EXTENSION_MESH)
            {
                resolved += EXTENSION_MESH;
            }
            resolved = std::filesystem::absolute(
                resolved
            ).lexically_normal();
            if (!path_is_within(resolved, meshes_directory))
            {
                error =
                    "mesh path must be inside the shared project mcp/meshes directory";
                return std::nullopt;
            }

            return FileSystem::GetRelativePath(
                resolved.generic_string()
            );
        }

        std::optional<std::string> resolve_mcp_texture_path(
            const McpRequest& request,
            std::string& error
        )
        {
            const std::optional<std::string> path_arg =
                get_argument(request, "path");
            const std::optional<std::string> name_arg =
                get_argument(request, "name");
            if (
                (!path_arg || path_arg->empty()) &&
                (!name_arg || name_arg->empty())
            )
            {
                error = "missing path or name";
                return std::nullopt;
            }

            const std::filesystem::path textures_directory =
                std::filesystem::path(
                    active_mcp_resource_directory()
                ) /
                "textures";
            std::filesystem::path requested =
                path_arg && !path_arg->empty()
                ? std::filesystem::path(*path_arg)
                : std::filesystem::path(*name_arg);

            std::filesystem::path resolved;
            if (path_is_within(requested, textures_directory))
            {
                resolved = requested;
            }
            else
            {
                resolved =
                    textures_directory /
                    requested.filename();
            }

            if (to_lower_copy(resolved.extension().string()) != ".png")
            {
                resolved.replace_extension(".png");
            }
            resolved = std::filesystem::absolute(
                resolved
            ).lexically_normal();
            if (!path_is_within(resolved, textures_directory))
            {
                error =
                    "texture path must be inside the shared project mcp/textures directory";
                return std::nullopt;
            }

            return FileSystem::GetRelativePath(
                resolved.generic_string()
            );
        }

        std::string primitive_types_json()
        {
            return "["
                "{\"name\":\"cube\",\"aliases\":[\"box\"],\"default_body_type\":\"box\"},"
                "{\"name\":\"quad\",\"aliases\":[\"plane\"],\"default_body_type\":\"plane\"},"
                "{\"name\":\"sphere\",\"aliases\":[\"ball\"],\"default_body_type\":\"sphere\"},"
                "{\"name\":\"cylinder\",\"aliases\":[],\"default_body_type\":\"capsule\"},"
                "{\"name\":\"cone\",\"aliases\":[],\"default_body_type\":\"box\"}"
            "]";
        }

        void add_entity_tags(
            Entity* entity,
            const std::string& comma_separated
        )
        {
            size_t start = 0;
            while (start <= comma_separated.size())
            {
                const size_t end =
                    comma_separated.find(',', start);
                std::string tag = comma_separated.substr(
                    start,
                    end == std::string::npos
                        ? std::string::npos
                        : end - start
                );
                const size_t first = tag.find_first_not_of(
                    " \t\r\n"
                );
                const size_t last = tag.find_last_not_of(
                    " \t\r\n"
                );
                tag = first == std::string::npos
                    ? ""
                    : tag.substr(first, last - first + 1);
                if (!tag.empty())
                {
                    entity->AddTag(tag);
                }
                if (end == std::string::npos)
                {
                    break;
                }
                start = end + 1;
            }
        }

        void apply_entity_identity(
            Entity* entity,
            const McpRequest& request
        )
        {
            if (
                const std::optional<std::string> tags =
                    get_argument(request, "tags")
            )
            {
                add_entity_tags(entity, *tags);
            }

            const std::array<std::string, 2> keys =
            {
                "semantic_id",
                "plan_element"
            };
            for (const std::string& key : keys)
            {
                if (
                    const std::optional<std::string> value =
                        get_argument(request, key);
                    value && !value->empty()
                )
                {
                    entity->AddTag(key + "=" + *value);
                }
            }

            if (
                const std::optional<std::string> semantic_tags =
                    get_argument(request, "semantic_tags")
            )
            {
                add_entity_tags(entity, *semantic_tags);
            }
        }

        bool is_world_path_valid(const std::string& path)
        {
            std::filesystem::path file_path(path);
            return file_path.is_absolute() && file_path.extension() == ".world";
        }

        std::string normalize_screenshot_path(const std::optional<std::string>& path)
        {
            std::filesystem::path file_name =
                path && !path->empty()
                ? std::filesystem::path(*path).filename()
                : std::filesystem::path(
                    "mcp_screenshot_" +
                    std::to_string(
                        Renderer::GetFrameNumber()
                    ) +
                    ".png"
                );
            if (file_name.extension().empty())
            {
                file_name.replace_extension(".png");
            }
            return std::filesystem::absolute(
                std::filesystem::path(
                    World::GetGeneratedResourceDirectory()
                ) /
                "thumbnails" /
                file_name
            ).lexically_normal().generic_string();
        }

        bool is_screenshot_path_valid(const std::string& path)
        {
            const std::filesystem::path screenshot_root =
                std::filesystem::absolute(
                    std::filesystem::path(
                        World::GetGeneratedResourceDirectory()
                    ) /
                    "thumbnails"
                ).lexically_normal();
            const std::filesystem::path file_path =
                std::filesystem::path(path).lexically_normal();
            const std::filesystem::path relative =
                file_path.lexically_relative(screenshot_root);
            if (relative.empty())
            {
                return false;
            }
            const auto first = relative.begin();
            return (
                first != relative.end() &&
                (*first).generic_string() != ".."
            );
        }

        std::optional<ResourceType> resource_type_from_name(const std::string& name)
        {
            if (name == "all" || name == "max" || name.empty())
            {
                return ResourceType::Max;
            }
            if (name == "texture")
            {
                return ResourceType::Texture;
            }
            if (name == "audio")
            {
                return ResourceType::Audio;
            }
            if (name == "material")
            {
                return ResourceType::Material;
            }
            if (name == "mesh")
            {
                return ResourceType::Mesh;
            }
            if (name == "cubemap")
            {
                return ResourceType::Cubemap;
            }
            if (name == "animation")
            {
                return ResourceType::Animation;
            }
            if (name == "font")
            {
                return ResourceType::Font;
            }
            if (name == "shader")
            {
                return ResourceType::Shader;
            }
            if (name == "unknown")
            {
                return ResourceType::Unknown;
            }

            return std::nullopt;
        }

        std::string material_texture_type_to_name(MaterialTextureType type)
        {
            switch (type)
            {
            case MaterialTextureType::Color:
                return "color";
            case MaterialTextureType::Roughness:
                return "roughness";
            case MaterialTextureType::Metalness:
                return "metalness";
            case MaterialTextureType::Normal:
                return "normal";
            case MaterialTextureType::Occlusion:
                return "occlusion";
            case MaterialTextureType::Emission:
                return "emission";
            case MaterialTextureType::Height:
                return "height";
            case MaterialTextureType::AlphaMask:
                return "alpha_mask";
            case MaterialTextureType::Packed:
                return "packed";
            default:
                return "unknown";
            }
        }

        std::optional<MaterialTextureType> material_texture_type_from_name(const std::string& name)
        {
            if (name == "color" || name == "albedo" || name == "base_color")
            {
                return MaterialTextureType::Color;
            }
            if (name == "roughness")
            {
                return MaterialTextureType::Roughness;
            }
            if (name == "metalness" || name == "metallic")
            {
                return MaterialTextureType::Metalness;
            }
            if (name == "normal")
            {
                return MaterialTextureType::Normal;
            }
            if (name == "occlusion" || name == "ao")
            {
                return MaterialTextureType::Occlusion;
            }
            if (name == "emission" || name == "emissive")
            {
                return MaterialTextureType::Emission;
            }
            if (name == "height")
            {
                return MaterialTextureType::Height;
            }
            if (name == "alpha_mask" || name == "alpha")
            {
                return MaterialTextureType::AlphaMask;
            }
            if (name == "packed")
            {
                return MaterialTextureType::Packed;
            }

            return std::nullopt;
        }

        std::string material_property_to_name(MaterialProperty property)
        {
            switch (property)
            {
            case MaterialProperty::Gltf:
                return "gltf";
            case MaterialProperty::WorldHeight:
                return "world_space_height";
            case MaterialProperty::WorldWidth:
                return "world_space_width";
            case MaterialProperty::WorldSpaceUv:
                return "world_space_uv";
            case MaterialProperty::Tessellation:
                return "tessellation";
            case MaterialProperty::ColorR:
                return "color_r";
            case MaterialProperty::ColorG:
                return "color_g";
            case MaterialProperty::ColorB:
                return "color_b";
            case MaterialProperty::ColorA:
                return "color_a";
            case MaterialProperty::Roughness:
                return "roughness";
            case MaterialProperty::Metalness:
                return "metalness";
            case MaterialProperty::Normal:
                return "normal";
            case MaterialProperty::Height:
                return "height";
            case MaterialProperty::Clearcoat:
                return "clearcoat";
            case MaterialProperty::Clearcoat_Roughness:
                return "clearcoat_roughness";
            case MaterialProperty::Anisotropic:
                return "anisotropic";
            case MaterialProperty::AnisotropicRotation:
                return "anisotropic_rotation";
            case MaterialProperty::Sheen:
                return "sheen";
            case MaterialProperty::SubsurfaceScattering:
                return "subsurface_scattering";
            case MaterialProperty::FlakeStrength:
                return "flake_strength";
            case MaterialProperty::FlakeScale:
                return "flake_scale";
            case MaterialProperty::PearlStrength:
                return "pearl_strength";
            case MaterialProperty::PearlColorR:
                return "pearl_color_r";
            case MaterialProperty::PearlColorG:
                return "pearl_color_g";
            case MaterialProperty::PearlColorB:
                return "pearl_color_b";
            case MaterialProperty::CoatTintR:
                return "coat_tint_r";
            case MaterialProperty::CoatTintG:
                return "coat_tint_g";
            case MaterialProperty::CoatTintB:
                return "coat_tint_b";
            case MaterialProperty::CoatTintStrength:
                return "coat_tint_strength";
            case MaterialProperty::Ior:
                return "ior";
            case MaterialProperty::Absorption:
                return "absorption";
            case MaterialProperty::Thickness:
                return "thickness";
            case MaterialProperty::PaintPreset:
                return "paint_preset";
            case MaterialProperty::SurfacePreset:
                return "surface_preset";
            case MaterialProperty::NormalFromAlbedo:
                return "normal_from_albedo";
            case MaterialProperty::EmissiveFromAlbedo:
                return "emissive_from_albedo";
            case MaterialProperty::TextureTilingX:
                return "texture_tiling_x";
            case MaterialProperty::TextureTilingY:
                return "texture_tiling_y";
            case MaterialProperty::TextureOffsetX:
                return "texture_offset_x";
            case MaterialProperty::TextureOffsetY:
                return "texture_offset_y";
            case MaterialProperty::TextureInvertX:
                return "texture_invert_x";
            case MaterialProperty::TextureInvertY:
                return "texture_invert_y";
            case MaterialProperty::TextureRotation:
                return "texture_rotation";
            case MaterialProperty::IsTerrain:
                return "texture_slope_based";
            case MaterialProperty::IsGrassBlade:
                return "is_grass_blade";
            case MaterialProperty::IsFoliage:
                return "is_foliage";
            case MaterialProperty::IsFlower:
                return "is_flower";
            case MaterialProperty::WindAnimation:
                return "wind_animation";
            case MaterialProperty::ColorVariationFromInstance:
                return "color_variation_from_instance";
            case MaterialProperty::IsWater:
                return "vertex_animate_water";
            case MaterialProperty::MotionBlurRadial:
                return "motion_blur_radial";
            case MaterialProperty::IsSkidMark:
                return "is_skid_mark";
            case MaterialProperty::CullMode:
                return "cull_mode";
            default:
                return "unknown";
            }
        }

        std::optional<MaterialProperty> material_property_from_name(const std::string& name)
        {
            for (uint32_t i = 0; i < static_cast<uint32_t>(MaterialProperty::Max); i++)
            {
                const MaterialProperty property = static_cast<MaterialProperty>(i);
                if (name == material_property_to_name(property))
                {
                    return property;
                }
            }

            if (name == "world_height")
            {
                return MaterialProperty::WorldHeight;
            }
            if (name == "world_width")
            {
                return MaterialProperty::WorldWidth;
            }
            if (name == "world_uv")
            {
                return MaterialProperty::WorldSpaceUv;
            }
            if (name == "base_color_r")
            {
                return MaterialProperty::ColorR;
            }
            if (name == "base_color_g")
            {
                return MaterialProperty::ColorG;
            }
            if (name == "base_color_b")
            {
                return MaterialProperty::ColorB;
            }
            if (name == "base_color_a")
            {
                return MaterialProperty::ColorA;
            }
            if (
                name == "alpha" ||
                name == "opacity"
            )
            {
                return MaterialProperty::ColorA;
            }
            if (name == "metallic")
            {
                return MaterialProperty::Metalness;
            }
            if (
                name == "ior" ||
                name == "refraction" ||
                name == "refractive_index" ||
                name == "index_of_refraction"
            )
            {
                return MaterialProperty::Ior;
            }
            if (
                name == "glass_thickness" ||
                name == "shell_thickness"
            )
            {
                return MaterialProperty::Thickness;
            }
            if (
                name == "dye_density" ||
                name == "tint_density"
            )
            {
                return MaterialProperty::Absorption;
            }
            if (name == "subsurface")
            {
                return MaterialProperty::SubsurfaceScattering;
            }
            if (name == "emissive")
            {
                return MaterialProperty::EmissiveFromAlbedo;
            }

            return std::nullopt;
        }

        // gltf style names whose convention is inverted relative to the engine property
        std::optional<MaterialProperty> material_property_inverted_from_name(
            const std::string& name
        )
        {
            if (
                name == "transmission" ||
                name == "transparency"
            )
            {
                return MaterialProperty::ColorA;
            }

            return std::nullopt;
        }

        std::string material_property_names_csv()
        {
            std::string names;
            for (uint32_t i = 0; i < static_cast<uint32_t>(MaterialProperty::Max); i++)
            {
                const std::string name =
                    material_property_to_name(static_cast<MaterialProperty>(i));
                if (name == "unknown")
                {
                    continue;
                }

                if (!names.empty())
                {
                    names += ", ";
                }
                names += name;
            }

            return names;
        }

        std::optional<MaterialPaintPreset> material_paint_preset_from_name(
            const std::string& name
        )
        {
            if (name == "gloss_solid")
            {
                return MaterialPaintPreset::GlossSolid;
            }
            if (name == "metallic")
            {
                return MaterialPaintPreset::Metallic;
            }
            if (name == "satin")
            {
                return MaterialPaintPreset::Satin;
            }
            if (name == "matte")
            {
                return MaterialPaintPreset::Matte;
            }
            if (name == "pearl")
            {
                return MaterialPaintPreset::Pearl;
            }
            if (name == "candy")
            {
                return MaterialPaintPreset::Candy;
            }
            if (name == "chameleon")
            {
                return MaterialPaintPreset::Chameleon;
            }

            return std::nullopt;
        }

        std::optional<MaterialSurfacePreset>
        material_surface_preset_from_name(
            const std::string& name
        )
        {
            if (name == "glass_clear")
            {
                return MaterialSurfacePreset::GlassClear;
            }
            if (name == "glass_tinted")
            {
                return MaterialSurfacePreset::GlassTinted;
            }
            if (name == "headlight_lens")
            {
                return MaterialSurfacePreset::HeadlightLens;
            }
            if (name == "taillight_lens")
            {
                return MaterialSurfacePreset::TaillightLens;
            }
            if (name == "rubber")
            {
                return MaterialSurfacePreset::RubberTire;
            }
            if (name == "carbon_fiber")
            {
                return MaterialSurfacePreset::CarbonFiber;
            }
            if (name == "chrome")
            {
                return MaterialSurfacePreset::Chrome;
            }
            if (name == "polished_metal")
            {
                return MaterialSurfacePreset::PolishedMetal;
            }
            if (name == "brake_disc")
            {
                return MaterialSurfacePreset::BrakeDisc;
            }
            if (name == "leather")
            {
                return MaterialSurfacePreset::Leather;
            }
            if (name == "black_plastic")
            {
                return MaterialSurfacePreset::BlackPlastic;
            }
            if (name == "emissive_red")
            {
                return MaterialSurfacePreset::EmissiveRedLight;
            }
            if (name == "emissive_white")
            {
                return MaterialSurfacePreset::EmissiveWhiteLight;
            }

            return std::nullopt;
        }

        IResource* get_resource_by_name_or_path(const std::string& name_or_path, ResourceType type)
        {
            for (const std::shared_ptr<IResource>& resource : ResourceCache::GetResourcesSnapshot())
            {
                if (!resource || (type != ResourceType::Max && resource->GetResourceType() != type))
                {
                    continue;
                }

                if (resource->GetObjectName() == name_or_path || resource->GetResourceFilePath() == name_or_path)
                {
                    return resource.get();
                }
            }

            return nullptr;
        }

        Material* get_material_from_request(const McpRequest& request, std::string& error)
        {
            const std::optional<std::string> name = get_argument(request, "name");
            const std::optional<std::string> path = get_argument(request, "path");
            const std::string key = name ? *name : (path ? *path : "");
            if (key.empty())
            {
                error = "missing material name or path";
                return nullptr;
            }

            Material* material = static_cast<Material*>(get_resource_by_name_or_path(key, ResourceType::Material));
            if (material == nullptr)
            {
                error = "material not found";
            }

            return material;
        }

        std::string material_to_json(Material* material)
        {
            if (material == nullptr)
            {
                return "null";
            }

            std::string json = "{";
            json += "\"resource\":" + resource_to_json(material);
            json += ",\"properties\":{";
            bool first_property = true;
            for (uint32_t i = 0; i < static_cast<uint32_t>(MaterialProperty::Max); i++)
            {
                const MaterialProperty property = static_cast<MaterialProperty>(i);
                const std::string name = material_property_to_name(property);
                if (name == "unknown")
                {
                    continue;
                }

                if (!first_property)
                {
                    json += ",";
                }
                first_property = false;
                const float value =
                    material->GetProperty(property);
                json += json_string(name) + ":";
                json += std::isfinite(value)
                    ? std::to_string(value)
                    : "null";
            }
            json += "}";

            json += ",\"textures\":{";
            bool first_texture = true;
            for (uint32_t i = 0; i < static_cast<uint32_t>(MaterialTextureType::Max); i++)
            {
                const MaterialTextureType texture_type = static_cast<MaterialTextureType>(i);
                if (!first_texture)
                {
                    json += ",";
                }
                first_texture = false;
                json += json_string(material_texture_type_to_name(texture_type)) + ":[";
                for (uint32_t slot = 0; slot < Material::slots_per_texture; slot++)
                {
                    if (slot != 0)
                    {
                        json += ",";
                    }
                    json += json_string(material->GetTexturePathByType(texture_type, static_cast<uint8_t>(slot)));
                }
                json += "]";
            }
            json += "}}";
            return json;
        }

        std::string entity_to_json_list_item(Entity* entity)
        {
            std::string json = "{";
            json += "\"id\":" + json_string(std::to_string(entity->GetObjectId()));
            json += ",\"name\":" + json_string(entity->GetObjectName());

            Entity* parent = entity->GetParent();
            json += ",\"parent_id\":";
            json += parent ? json_string(std::to_string(parent->GetObjectId())) : "null";

            json += ",\"components\":" + entity_components_json(entity);
            if (!entity->GetTags().empty())
            {
                json += ",\"tags\":" + entity_tags_json(entity);
            }
            json += "}";
            return json;
        }

        std::string entity_to_json(Entity* entity, bool include_children)
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
            json += ",\"position_local\":" + json_vector3(entity->GetPositionLocal());
            json += ",\"rotation_euler\":" + json_vector3(entity->GetRotation().ToEulerAngles());
            json += ",\"rotation_euler_local\":" + json_vector3(entity->GetRotationLocal().ToEulerAngles());
            json += ",\"scale\":" + json_vector3(entity->GetScale());
            json += ",\"scale_local\":" + json_vector3(entity->GetScaleLocal());

            if (include_children)
            {
                json += ",\"children\":[";
                bool first_child = true;
                for (Entity* child : entity->GetChildren())
                {
                    if (child == nullptr)
                    {
                        continue;
                    }

                    if (!first_child)
                    {
                        json += ",";
                    }
                    first_child = false;
                    json += json_string(std::to_string(child->GetObjectId()));
                }
                json += "]";
            }

            json += "}";
            return json;
        }

        std::string command_ping()
        {
            return "{\"ok\":true,\"version\":" + json_string(version::c_str()) + "}";
        }

        std::string command_engine_status()
        {
            std::string json = "{\"ok\":true";
            json += ",\"version\":" + json_string(version::c_str());
            json += ",\"editor_visible\":" + json_bool(Engine::IsFlagSet(EngineMode::EditorVisible));
            json += ",\"headless\":" + json_bool(Engine::IsHeadless());
            json += ",\"playing\":" + json_bool(Engine::IsFlagSet(EngineMode::Playing));
            json += ",\"paused\":" + json_bool(Engine::IsFlagSet(EngineMode::Paused));
            json += ",\"loading\":" + json_bool(ProgressTracker::IsLoading());
            json += ",\"fps\":" + std::to_string(Profiler::GetFps());
            json += ",\"frame_ms\":" + std::to_string(Profiler::GetFrameDurationMs());
            json += ",\"time_seconds\":" + std::to_string(Timer::GetTimeSec());
            json += ",\"frame_number\":" + std::to_string(Renderer::GetFrameNumber());
            json += ",\"jobs_running\":" + json_bool(ThreadPool::AreTasksRunning());
            json += ",\"working_threads\":" + std::to_string(ThreadPool::GetWorkingThreadCount());
            json += "}";
            return json;
        }

        std::string command_camera_snapshot()
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            Camera* camera = World::GetCamera();
            if (camera == nullptr || camera->GetEntity() == nullptr)
            {
                return json_error("camera not found");
            }

            Entity* entity = camera->GetEntity();
            std::string json = "{\"ok\":true";
            json += ",\"entity_id\":" + json_string(std::to_string(entity->GetObjectId()));
            json += ",\"entity_name\":" + json_string(entity->GetObjectName());
            json += ",\"position\":" + json_vector3(entity->GetPosition());
            json += ",\"forward\":" + json_vector3(entity->GetForward());
            json += ",\"right\":" + json_vector3(entity->GetRight());
            json += ",\"up\":" + json_vector3(entity->GetUp());
            json += "}";
            return json;
        }

        std::string command_screenshot_take(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            const std::string path = normalize_screenshot_path(get_argument(request, "path"));
            if (!is_screenshot_path_valid(path))
            {
                return json_error(
                    "screenshot path must be inside project/mcp/blockout/thumbnails"
                );
            }
            std::string extension = std::filesystem::path(path).extension().generic_string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (extension != ".png")
            {
                return json_error("screenshot path must be a .png file");
            }
            std::filesystem::create_directories(
                std::filesystem::path(path).parent_path()
            );

            const bool accepted = Renderer::Screenshot(path);
            if (!accepted)
            {
                return json_error("screenshot already pending");
            }

            std::string json = "{\"ok\":true";
            json += ",\"path\":" + json_string(path);
            json += ",\"ready\":false";
            json += ",\"async\":true";
            json += ",\"note\":" + json_string("screenshot will be written after the next rendered frame");
            json += "}";
            return json;
        }

        std::string command_engine_set_mode(const McpRequest& request)
        {
            if (const std::optional<std::string> mode = get_argument(request, "mode"))
            {
                if (*mode == "edit")
                {
                    Engine::SetFlag(EngineMode::Playing, false);
                    Engine::SetFlag(EngineMode::Paused, false);
                }
                else if (*mode == "play")
                {
                    Engine::SetFlag(EngineMode::Playing, true);
                    Engine::SetFlag(EngineMode::Paused, false);
                }
                else if (*mode == "pause")
                {
                    Engine::SetFlag(EngineMode::Paused, true);
                }
                else if (*mode == "resume")
                {
                    Engine::SetFlag(EngineMode::Playing, true);
                    Engine::SetFlag(EngineMode::Paused, false);
                }
                else
                {
                    return json_error("invalid mode");
                }
            }

            const std::pair<const char*, EngineMode> flags[] =
            {
                { "playing", EngineMode::Playing },
                { "paused", EngineMode::Paused },
                { "editor_visible", EngineMode::EditorVisible }
            };

            for (const auto& [name, flag] : flags)
            {
                if (const std::optional<std::string> value = get_argument(request, name))
                {
                    bool parsed = false;
                    if (!parse_bool(*value, parsed))
                    {
                        return json_error(std::string("invalid ") + name);
                    }
                    Engine::SetFlag(flag, parsed);
                }
            }

            return command_engine_status();
        }

        // recompiles shaders whose source files (or includes) differ from what they were compiled from,
        // the same path the shader editor uses, pipelines pick the new modules up through the shader hash

        Terrain* find_terrain_from_request(const McpRequest& request, std::string& error)
        {
            if (get_argument(request, "id"))
            {
                Entity* entity = get_entity_from_request(request, error);
                if (entity == nullptr)
                {
                    return nullptr;
                }
                Terrain* terrain = entity->GetComponent<Terrain>();
                if (terrain == nullptr)
                {
                    error = "entity has no terrain component";
                }
                return terrain;
            }

            Terrain* found = nullptr;
            for (Entity* entity : World::GetEntities())
            {
                Terrain* terrain = entity ? entity->GetComponent<Terrain>() : nullptr;
                if (terrain == nullptr)
                {
                    continue;
                }
                if (found != nullptr)
                {
                    error = "the world has more than one terrain, pass id";
                    return nullptr;
                }
                found = terrain;
            }
            if (found == nullptr)
            {
                error = "the world has no terrain";
            }
            return found;
        }

        // pugi writes every attribute as text, hand numbers and booleans back as json values
        std::string xml_attribute_to_json(const pugi::xml_attribute& attribute)
        {
            const std::string value = attribute.as_string();
            if (value == "true" || value == "false")
            {
                return value;
            }
            char* end = nullptr;
            const double number = std::strtod(value.c_str(), &end);
            if (!value.empty() && end == value.c_str() + value.size() && std::isfinite(number))
            {
                return value;
            }
            return json_string(value);
        }

        std::string scatter_layer_json(const TerrainScatterLayer& layer, uint32_t index)
        {
            pugi::xml_document document;
            pugi::xml_node node = document.append_child("layer");
            Terrain::SaveScatterLayer(node, layer);

            std::string json = "{\"index\":" + std::to_string(index);
            json += ",\"instance_count\":" + std::to_string(layer.instance_count);
            json += ",\"coverage\":" + json_number(layer.coverage);
            json += ",\"attributes\":{";
            bool first = true;
            for (const pugi::xml_attribute& attribute : node.attributes())
            {
                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += json_string(attribute.name()) + ":" + xml_attribute_to_json(attribute);
            }
            json += "}}";
            return json;
        }

        std::atomic<bool> terrain_rescatter_running = false;

        std::string command_terrain_scatter_get(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            std::string error;
            Terrain* terrain = find_terrain_from_request(request, error);
            if (terrain == nullptr)
            {
                return json_error(error);
            }

            std::string json = "{\"ok\":true";
            json += ",\"terrain_id\":" + json_string(std::to_string(terrain->GetEntity()->GetObjectId()));
            json += ",\"mesh_rescatter_running\":" + json_bool(terrain_rescatter_running.load());
            json += ",\"kinds\":{\"0\":\"mesh\",\"1\":\"grass\",\"2\":\"detail\"}";
            json += ",\"layers\":[";
            const auto& layers = terrain->GetScatterLayers();
            for (uint32_t i = 0; i < layers.size(); i++)
            {
                if (i != 0)
                {
                    json += ",";
                }
                json += scatter_layer_json(layers[i], i);
            }
            json += "]}";
            return json;
        }

        std::string command_terrain_scatter_set(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("terrain scatter edits require edit mode");
            }

            std::string error;
            Terrain* terrain = find_terrain_from_request(request, error);
            if (terrain == nullptr)
            {
                return json_error(error);
            }

            auto& layers = terrain->GetScatterLayers();
            const std::optional<std::string> layer_arg = get_argument(request, "layer");
            if (!layer_arg)
            {
                return json_error("missing layer, pass a slot index or layer name");
            }
            uint32_t index = terrain_scatter_max;
            uint64_t parsed_index = 0;
            if (parse_uint64(*layer_arg, parsed_index))
            {
                index = parsed_index < terrain_scatter_max ? static_cast<uint32_t>(parsed_index) : terrain_scatter_max;
            }
            else
            {
                for (uint32_t i = 0; i < layers.size(); i++)
                {
                    if (layers[i].name == *layer_arg)
                    {
                        index = i;
                        break;
                    }
                }
            }
            if (index >= terrain_scatter_max)
            {
                return json_error("no scatter layer matches '" + *layer_arg + "', call terrain_scatter_get for names and indices");
            }

            uint64_t count = 0;
            const std::optional<std::string> count_arg = get_argument(request, "count");
            if (!count_arg || !parse_uint64(*count_arg, count) || count == 0 || count > 96)
            {
                return json_error("count must be between 1 and 96");
            }

            pugi::xml_document document;
            pugi::xml_node node = document.append_child("layer");
            Terrain::SaveScatterLayer(node, layers[index]);
            for (uint64_t i = 0; i < count; i++)
            {
                const std::optional<std::string> name  = get_argument(request, "attribute_" + std::to_string(i));
                const std::optional<std::string> value = get_argument(request, "value_" + std::to_string(i));
                if (!name || !value)
                {
                    return json_error("missing attribute or value at index " + std::to_string(i));
                }
                pugi::xml_attribute attribute = node.attribute(name->c_str());
                if (!attribute)
                {
                    return json_error("unknown scatter attribute '" + *name + "', call terrain_scatter_get for the attribute names");
                }
                attribute.set_value(value->c_str());
            }

            const TerrainScatterKind kind_before = layers[index].kind;
            TerrainScatterLayer updated          = layers[index];
            Terrain::LoadScatterLayer(node, updated, index);
            layers[index] = updated;

            // gpu layers own no entities, pushing their params is instant; mesh layers have to be placed again
            std::string rescatter = "none";
            bool rescatter_requested = true;
            if (const std::optional<std::string> rescatter_arg = get_argument(request, "rescatter"))
            {
                if (!parse_bool(*rescatter_arg, rescatter_requested))
                {
                    return json_error("rescatter must be true or false");
                }
            }
            const bool gpu_touched  = kind_before != TerrainScatterKind::Mesh || updated.kind != TerrainScatterKind::Mesh;
            const bool mesh_touched = kind_before == TerrainScatterKind::Mesh || updated.kind == TerrainScatterKind::Mesh;
            if (rescatter_requested && gpu_touched)
            {
                WorldHelpers::RefreshTerrainGpuScatter(terrain);
                rescatter = "gpu_refreshed";
            }
            if (rescatter_requested && mesh_touched)
            {
                bool expected = false;
                if (terrain_rescatter_running.compare_exchange_strong(expected, true))
                {
                    ThreadPool::AddTask([terrain]()
                    {
                        WorldHelpers::PopulateTerrainBiomeProps(terrain);
                        terrain_rescatter_running.store(false);
                    });
                    rescatter = gpu_touched ? "gpu_refreshed_and_mesh_rescatter_started" : "mesh_rescatter_started";
                }
                else
                {
                    rescatter = "mesh_rescatter_busy, call again with rescatter true once terrain_scatter_get shows mesh_rescatter_running false";
                }
            }

            std::string json = "{\"ok\":true";
            json += ",\"rescatter\":" + json_string(rescatter);
            json += ",\"note\":" + json_string("mesh rescatter runs in the background and re-rolls every mesh layer; world_save persists the layers with the world");
            json += ",\"layer\":" + scatter_layer_json(layers[index], index);
            json += "}";
            return json;
        }

        std::string command_world_summary()
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            const math::Vector3& wind = World::GetWind();
            std::string json = "{\"ok\":true";
            json += ",\"name\":" + json_string(World::GetName());
            json += ",\"file_path\":" + json_string(World::GetFilePath());
            json += ",\"description\":" + json_string(World::GetDescription());
            json += ",\"entity_count\":" + std::to_string(World::GetEntities().size());
            json += ",\"light_count\":" + std::to_string(World::GetLightCount());
            json += ",\"audio_source_count\":" + std::to_string(World::GetAudioSourceCount());
            json += ",\"time_of_day\":" + std::to_string(World::GetTimeOfDay(false));
            json += ",\"wind\":" + json_vector3(wind);
            json += ",\"puddliness\":" + std::to_string(World::GetPuddliness());
            json += ",\"rain\":" + std::to_string(Weather::GetRain());
            json += ",\"wetness\":" + std::to_string(Weather::GetWetness());
            json += ",\"rain_puddliness\":" + std::to_string(Weather::GetRainPuddliness());
            json += ",\"bounding_box\":" + json_bounding_box(World::GetBoundingBox());
            json += "}";
            return json;
        }

        std::string command_world_load(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            const std::optional<std::string> path = get_argument(request, "path");
            if (!path)
            {
                return json_error("missing path");
            }

            if (!is_world_path_valid(*path))
            {
                return json_error("path must be an absolute .world file");
            }

            if (!World::LoadFromFile(*path))
            {
                return json_error("failed to queue world load");
            }

            return "{\"ok\":true,\"queued\":true}";
        }

        std::string command_world_save(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            std::string path = World::GetFilePath();
            if (const std::optional<std::string> path_arg = get_argument(request, "path"))
            {
                path = *path_arg;
            }

            if (path.empty())
            {
                return json_error("missing path");
            }
            if (std::filesystem::path(path).is_relative())
            {
                path = std::filesystem::absolute(path)
                    .lexically_normal()
                    .string();
            }

            if (!is_world_path_valid(path))
            {
                return json_error("path must be an absolute .world file");
            }

            if (!World::SaveToFile(path))
            {
                return json_error("failed to save world");
            }

            std::string json = "{\"ok\":true,\"path\":" +
                json_string(path);
            json += ",\"resources_removed\":[";
            bool first = true;
            for (
                const std::string& file :
                World::GetLastResourceCleanup()
            )
            {
                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += json_string(file);
            }
            json += "]}";
            return json;
        }

        std::string command_world_resources_clean()
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error(
                    "world resource cleanup requires edit mode"
                );
            }

            std::string path = World::GetFilePath();
            if (path.empty())
            {
                return json_error("world has no file path");
            }
            if (std::filesystem::path(path).is_relative())
            {
                path = std::filesystem::absolute(path)
                    .lexically_normal()
                    .string();
            }
            if (!is_world_path_valid(path))
            {
                return json_error(
                    "world path must be an absolute .world file"
                );
            }

            const std::string directory =
                World::GetResourceDirectory(path);
            const std::vector<std::string> previous_cleanup =
                World::GetLastResourceCleanup();
            const std::vector<std::string> before =
                FileSystem::GetFilesInDirectory(directory);
            if (!World::SaveToFile(path))
            {
                return json_error(
                    "failed to save world and clean resources"
                );
            }
            const std::vector<std::string> after =
                FileSystem::GetFilesInDirectory(directory);
            const std::vector<std::string>& failures =
                World::GetLastResourceCleanupFailures();
            std::set<std::string> retained(
                after.begin(),
                after.end()
            );
            std::set<std::string> removed(
                previous_cleanup.begin(),
                previous_cleanup.end()
            );
            removed.insert(
                World::GetLastResourceCleanup().begin(),
                World::GetLastResourceCleanup().end()
            );
            for (const std::string& file : before)
            {
                if (retained.find(file) == retained.end())
                {
                    removed.insert(file);
                }
            }
            std::string json = "{\"ok\":true";
            json += ",\"path\":" + json_string(path);
            json += ",\"directory\":" + json_string(directory);
            json += ",\"removed\":[";
            bool first = true;
            for (const std::string& file : removed)
            {
                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += json_string(file);
            }
            json += "]";
            json += ",\"failed\":[";
            first = true;
            for (const std::string& file : failures)
            {
                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += json_string(file);
            }
            json += "]";
            json += ",\"orphan_count\":" +
                std::to_string(failures.size());
            json += ",\"retained_count\":" +
                std::to_string(after.size());
            json += "}";
            return json;
        }

        std::string command_world_resource_directory_get()
        {
            // generated assets live in a fixed directory that has nothing to do with the active world, so an
            // unsaved world only leaves the world specific fields blank, it does not make the answer unknown
            const std::string world_path = World::GetFilePath();
            const std::string resource_directory =
                world_path.empty()
                ? std::string()
                : World::GetResourceDirectory(world_path);
            const std::string mcp_blockout =
                World::GetGeneratedResourceDirectory();
            const std::string mcp_library =
                World::GetLibraryResourceDirectory();
            std::string json = "{\"ok\":true";
            json += ",\"world_saved\":" +
                std::string(world_path.empty() ? "false" : "true");
            json += ",\"world_path\":" +
                json_string(world_path);
            json += ",\"resource_directory\":" +
                json_string(resource_directory);
            json += ",\"mcp\":{";
            json += "\"blockout\":{";
            json += "\"root\":" +
                json_string(mcp_blockout);
            json += ",\"meshes\":" +
                json_string(mcp_blockout + "meshes/");
            json += ",\"materials\":" +
                json_string(mcp_blockout + "materials/");
            json += ",\"textures\":" +
                json_string(mcp_blockout + "textures/");
            json += ",\"prefabs\":" +
                json_string(mcp_blockout + "prefabs/");
            json += ",\"sources\":" +
                json_string(mcp_blockout + "sources/");
            json += ",\"thumbnails\":" +
                json_string(mcp_blockout + "thumbnails/");
            json += ",\"catalog\":" +
                json_string(mcp_blockout + "catalog.json");
            json += "}";
            json += ",\"library\":{";
            json += "\"root\":" +
                json_string(mcp_library);
            json += ",\"meshes\":" +
                json_string(mcp_library + "meshes/");
            json += ",\"materials\":" +
                json_string(mcp_library + "materials/");
            json += ",\"textures\":" +
                json_string(mcp_library + "textures/");
            json += ",\"prefabs\":" +
                json_string(mcp_library + "prefabs/");
            json += ",\"sources\":" +
                json_string(mcp_library + "sources/");
            json += ",\"thumbnails\":" +
                json_string(mcp_library + "thumbnails/");
            json += ",\"catalog\":" +
                json_string(mcp_library + "catalog.json");
            json += "}";
            // legacy root alias points at mcp blockout output
            json += ",\"root\":" +
                json_string(mcp_blockout);
            json += "}}";
            return json;
        }

        std::string command_world_set_environment(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("world environment edits require edit mode");
            }

            bool changed = false;
            if (const std::optional<std::string> time_of_day = get_argument(request, "time_of_day"))
            {
                float parsed = 0.0f;
                if (!parse_float(*time_of_day, parsed) || parsed < 0.0f || parsed > 1.0f)
                {
                    return json_error("invalid time_of_day");
                }

                World::SetTimeOfDay(parsed);
                changed = true;
            }

            if (const std::optional<std::string> wind = get_argument(request, "wind"))
            {
                math::Vector3 parsed;
                if (!parse_vector3(*wind, parsed))
                {
                    return json_error("invalid wind");
                }

                World::SetWind(parsed);
                changed = true;
            }

            if (const std::optional<std::string> puddliness = get_argument(request, "puddliness"))
            {
                float parsed = 0.0f;
                if (!parse_float(*puddliness, parsed) || parsed < 0.0f || parsed > 1.0f)
                {
                    return json_error("invalid puddliness");
                }

                World::SetPuddliness(parsed);
                changed = true;
            }

            if (const std::optional<std::string> rain = get_argument(request, "rain"))
            {
                float parsed = 0.0f;
                if (!parse_float(*rain, parsed) || parsed < 0.0f || parsed > 1.0f)
                {
                    return json_error("invalid rain");
                }

                Light* light = World::GetDirectionalLight();
                if (!light)
                {
                    return json_error("rain needs a directional light");
                }

                light->SetRain(parsed);
                changed = true;
            }

            if (const std::optional<std::string> description = get_argument(request, "description"))
            {
                World::SetDescription(*description);
                changed = true;
            }

            if (!changed)
            {
                return json_error("no environment values provided");
            }

            return command_world_summary();
        }

        std::string command_world_raycast(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            const std::optional<std::string> origin_arg = get_argument(request, "origin");
            const std::optional<std::string> direction_arg = get_argument(request, "direction");
            if (!origin_arg || !direction_arg)
            {
                return json_error("missing origin or direction");
            }

            math::Vector3 origin;
            math::Vector3 direction;
            if (!parse_vector3(*origin_arg, origin))
            {
                return json_error("invalid origin");
            }
            if (!parse_vector3(*direction_arg, direction) || direction == math::Vector3::Zero)
            {
                return json_error("invalid direction");
            }

            float max_distance = 1000.0f;
            if (const std::optional<std::string> max_distance_arg = get_argument(request, "max_distance"))
            {
                if (!parse_float(*max_distance_arg, max_distance) || max_distance <= 0.0f)
                {
                    return json_error("invalid max_distance");
                }
            }

            PhysicsRaycastHit hit_result;
            const bool hit = PhysicsWorld::RaycastStatic(
                origin,
                direction,
                max_distance,
                hit_result
            );

            std::string json = "{\"ok\":true";
            json += ",\"hit\":" + json_bool(hit);
            if (hit)
            {
                json += ",\"position\":" +
                    json_vector3(hit_result.position);
                json += ",\"normal\":" +
                    json_vector3(hit_result.normal);
                json += ",\"distance\":" +
                    std::to_string(hit_result.distance);
                if (hit_result.entity != nullptr)
                {
                    json += ",\"entity_id\":" +
                        json_string(
                            std::to_string(
                                hit_result.entity->GetObjectId()
                            )
                        );
                    json += ",\"entity_name\":" +
                        json_string(
                            hit_result.entity->GetObjectName()
                        );
                }
            }
            json += "}";
            return json;
        }

        std::string command_entity_snap(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("entity snapping requires edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            const std::string mode = to_lower_copy(
                get_argument(request, "mode").value_or("floor")
            );
            if (
                mode != "floor" &&
                mode != "ceiling" &&
                mode != "wall" &&
                mode != "surface"
            )
            {
                return json_error("invalid snap mode");
            }

            float max_distance = 1000.0f;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "max_distance")
            )
            {
                if (
                    !parse_float(*value, max_distance) ||
                    max_distance <= 0.0f
                )
                {
                    return json_error("invalid max_distance");
                }
            }

            float offset = 0.0f;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "offset")
            )
            {
                if (!parse_float(*value, offset))
                {
                    return json_error("invalid offset");
                }
            }

            bool align_to_surface =
                mode == "wall" || mode == "surface";
            if (
                const std::optional<std::string> value =
                    get_argument(request, "align_to_surface")
            )
            {
                if (!parse_bool(*value, align_to_surface))
                {
                    return json_error("invalid align_to_surface");
                }
            }

            math::Vector3 target = entity->GetPosition();
            if (
                const std::optional<std::string> value =
                    get_argument(request, "target")
            )
            {
                if (!parse_vector3(*value, target))
                {
                    return json_error("invalid target");
                }
            }

            math::Vector3 origin = entity->GetPosition();
            math::Vector3 direction = math::Vector3::Down;
            if (mode == "floor")
            {
                origin = target +
                    math::Vector3::Up * (max_distance * 0.5f);
                direction = math::Vector3::Down;
            }
            else if (mode == "ceiling")
            {
                origin = target +
                    math::Vector3::Down * (max_distance * 0.5f);
                direction = math::Vector3::Up;
            }
            else
            {
                origin = target;
                direction = mode == "wall"
                    ? entity->GetForward()
                    : math::Vector3::Down;
            }

            if (
                const std::optional<std::string> value =
                    get_argument(request, "origin")
            )
            {
                if (!parse_vector3(*value, origin))
                {
                    return json_error("invalid origin");
                }
            }
            if (
                const std::optional<std::string> value =
                    get_argument(request, "direction")
            )
            {
                if (
                    !parse_vector3(*value, direction) ||
                    direction == math::Vector3::Zero
                )
                {
                    return json_error("invalid direction");
                }
            }

            PhysicsRaycastHit hit;
            if (
                !PhysicsWorld::RaycastStatic(
                    origin,
                    direction,
                    max_distance,
                    hit,
                    entity
                )
            )
            {
                return json_error("snap ray did not hit static geometry");
            }

            if (align_to_surface)
            {
                if (mode == "wall")
                {
                    entity->SetRotation(
                        math::Quaternion::FromLookRotation(
                            hit.normal,
                            math::Vector3::Up
                        )
                    );
                }
                else
                {
                    entity->SetRotation(
                        math::Quaternion::FromRotation(
                            math::Vector3::Up,
                            hit.normal
                        )
                    );
                }
            }

            math::BoundingBox bounds;
            bool has_bounds = false;
            std::function<void(Entity*)> merge_bounds =
                [&](Entity* current)
            {
                if (Render* render =
                    current->GetComponent<Render>())
                {
                    auto merge_matrix =
                        [&](const math::Matrix& matrix)
                    {
                        const math::BoundingBox world_bounds =
                            render->GetBoundingBoxMesh() *
                            matrix;
                        if (!has_bounds)
                        {
                            bounds = world_bounds;
                            has_bounds = true;
                        }
                        else
                        {
                            bounds.Merge(world_bounds);
                        }
                    };
                    if (render->HasInstancing())
                    {
                        for (
                            uint32_t i = 0;
                            i < render->GetInstanceCount();
                            i++
                        )
                        {
                            merge_matrix(
                                render->GetInstance(i, true)
                            );
                        }
                    }
                    else
                    {
                        merge_matrix(current->GetMatrix());
                    }
                }
                for (Entity* child : current->GetChildren())
                {
                    merge_bounds(child);
                }
            };
            merge_bounds(entity);
            if (!has_bounds)
            {
                return json_error("entity has no render bounds");
            }

            const math::Vector3 pivot = entity->GetPosition();
            float support_toward_surface =
                -std::numeric_limits<float>::max();
            std::function<void(Entity*)> compute_support =
                [&](Entity* current)
            {
                if (Render* render =
                    current->GetComponent<Render>())
                {
                    std::array<math::Vector3, 8> corners;
                    render->GetBoundingBoxMesh().GetCorners(
                        &corners
                    );
                    auto accumulate_matrix =
                        [&](const math::Matrix& matrix)
                    {
                        for (const math::Vector3& corner : corners)
                        {
                            const math::Vector3 world_corner =
                                matrix * corner;
                            support_toward_surface = std::max(
                                support_toward_surface,
                                math::Vector3::Dot(
                                    world_corner - pivot,
                                    -hit.normal
                                )
                            );
                        }
                    };
                    if (render->HasInstancing())
                    {
                        for (
                            uint32_t i = 0;
                            i < render->GetInstanceCount();
                            i++
                        )
                        {
                            accumulate_matrix(
                                render->GetInstance(i, true)
                            );
                        }
                    }
                    else
                    {
                        accumulate_matrix(current->GetMatrix());
                    }
                }
                for (Entity* child : current->GetChildren())
                {
                    compute_support(child);
                }
            };
            compute_support(entity);
            if (
                support_toward_surface ==
                -std::numeric_limits<float>::max()
            )
            {
                return json_error(
                    "failed to compute entity support extent"
                );
            }
            const math::Vector3 final_position =
                hit.position +
                hit.normal *
                (support_toward_surface + offset);
            entity->SetPosition(final_position);

            has_bounds = false;
            merge_bounds(entity);
            std::string json = "{\"ok\":true";
            json += ",\"mode\":" + json_string(mode);
            json += ",\"position\":" +
                json_vector3(entity->GetPosition());
            json += ",\"rotation\":" +
                json_quaternion(entity->GetRotation());
            json += ",\"hit\":{";
            json += "\"position\":" +
                json_vector3(hit.position);
            json += ",\"normal\":" +
                json_vector3(hit.normal);
            json += ",\"distance\":" +
                std::to_string(hit.distance);
            if (hit.entity)
            {
                json += ",\"entity_id\":" +
                    json_string(
                        std::to_string(hit.entity->GetObjectId())
                    );
                json += ",\"entity_name\":" +
                    json_string(hit.entity->GetObjectName());
            }
            json += "}";
            json += ",\"bounding_box\":" +
                json_bounding_box(bounds);
            json += "}";
            return json;
        }

        std::string command_entity_spatial_snapshot(
            const McpRequest& request
        )
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            std::string error;
            Entity* root = get_entity_from_request(request, error);
            if (root == nullptr)
            {
                return json_error(error);
            }

            bool include_descendants = true;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "include_descendants")
            )
            {
                if (!parse_bool(*value, include_descendants))
                {
                    return json_error("invalid include_descendants");
                }
            }

            uint32_t limit = 1000;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "limit")
            )
            {
                uint64_t parsed = 0;
                if (
                    !parse_uint64(*value, parsed) ||
                    parsed == 0 ||
                    parsed > 5000
                )
                {
                    return json_error(
                        "limit must be between 1 and 5000"
                    );
                }
                limit = static_cast<uint32_t>(parsed);
            }

            uint32_t offset = 0;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "offset")
            )
            {
                uint64_t parsed = 0;
                if (
                    !parse_uint64(*value, parsed) ||
                    parsed > std::numeric_limits<uint32_t>::max()
                )
                {
                    return json_error("invalid offset");
                }
                offset = static_cast<uint32_t>(parsed);
            }

            std::vector<Entity*> entities;
            entities.emplace_back(root);
            if (include_descendants)
            {
                root->GetDescendants(&entities);
            }

            std::string json = "{\"ok\":true";
            json += ",\"root_id\":" +
                json_string(std::to_string(root->GetObjectId()));
            json += ",\"root_name\":" +
                json_string(root->GetObjectName());
            json += ",\"entities\":[";
            bool first = true;
            uint32_t emitted = 0;
            uint32_t skipped = 0;
            for (Entity* entity : entities)
            {
                if (entity == nullptr)
                {
                    continue;
                }
                if (skipped < offset)
                {
                    skipped++;
                    continue;
                }
                if (emitted >= limit)
                {
                    continue;
                }

                math::BoundingBox own_bounds;
                bool has_own_bounds = false;
                if (Render* render =
                    entity->GetComponent<Render>())
                {
                    own_bounds = render->GetBoundingBox();
                    has_own_bounds =
                        own_bounds.GetMin().IsFinite() &&
                        own_bounds.GetMax().IsFinite() &&
                        own_bounds.GetSize().LengthSquared() > 0.0f;
                }

                math::BoundingBox subtree_bounds;
                bool has_subtree_bounds = false;
                std::vector<Entity*> subtree;
                subtree.emplace_back(entity);
                entity->GetDescendants(&subtree);
                for (Entity* current : subtree)
                {
                    if (current == nullptr)
                    {
                        continue;
                    }
                    if (Render* render =
                        current->GetComponent<Render>())
                    {
                        const math::BoundingBox& bounds =
                            render->GetBoundingBox();
                        if (
                            !bounds.GetMin().IsFinite() ||
                            !bounds.GetMax().IsFinite() ||
                            bounds.GetSize().LengthSquared() <= 0.0f
                        )
                        {
                            continue;
                        }
                        if (!has_subtree_bounds)
                        {
                            subtree_bounds = bounds;
                            has_subtree_bounds = true;
                        }
                        else
                        {
                            subtree_bounds.Merge(bounds);
                        }
                    }
                }

                PhysicsRaycastHit support_hit;
                bool has_support_hit = false;
                float support_gap = 0.0f;
                PhysicsRaycastHit ceiling_hit;
                bool has_ceiling_hit = false;
                float ceiling_gap = 0.0f;
                PhysicsRaycastHit wall_hit;
                bool has_wall_hit = false;
                float wall_gap =
                    std::numeric_limits<float>::max();
                if (has_own_bounds)
                {
                    const math::Vector3 size = own_bounds.GetSize();
                    const float probe_offset = std::max(
                        0.05f,
                        std::min(0.25f, size.y * 0.1f)
                    );
                    math::Vector3 probe_origin =
                        own_bounds.GetCenter();
                    probe_origin.y =
                        own_bounds.GetMin().y + probe_offset;
                    has_support_hit = PhysicsWorld::RaycastStatic(
                        probe_origin,
                        math::Vector3::Down,
                        1000.0f,
                        support_hit,
                        entity
                    );
                    if (has_support_hit)
                    {
                        support_gap =
                            own_bounds.GetMin().y -
                            support_hit.position.y;
                    }

                    math::Vector3 ceiling_origin =
                        own_bounds.GetCenter();
                    ceiling_origin.y =
                        own_bounds.GetMax().y - probe_offset;
                    has_ceiling_hit = PhysicsWorld::RaycastStatic(
                        ceiling_origin,
                        math::Vector3::Up,
                        1000.0f,
                        ceiling_hit,
                        entity
                    );
                    if (has_ceiling_hit)
                    {
                        ceiling_gap =
                            ceiling_hit.position.y -
                            own_bounds.GetMax().y;
                    }

                    const std::array<math::Vector3, 4>
                        wall_directions =
                    {
                        math::Vector3::Left,
                        math::Vector3::Right,
                        math::Vector3::Forward,
                        math::Vector3::Backward
                    };
                    for (
                        const math::Vector3& wall_direction :
                        wall_directions
                    )
                    {
                        PhysicsRaycastHit candidate;
                        if (
                            !PhysicsWorld::RaycastStatic(
                                own_bounds.GetCenter(),
                                wall_direction,
                                1000.0f,
                                candidate,
                                entity
                            )
                        )
                        {
                            continue;
                        }
                        const float own_extent =
                            std::abs(wall_direction.x) > 0.5f
                            ? size.x * 0.5f
                            : size.z * 0.5f;
                        const float candidate_gap =
                            candidate.distance - own_extent;
                        if (
                            !has_wall_hit ||
                            candidate_gap < wall_gap
                        )
                        {
                            has_wall_hit = true;
                            wall_gap = candidate_gap;
                            wall_hit = candidate;
                        }
                    }
                }

                if (!first)
                {
                    json += ",";
                }
                first = false;
                emitted++;

                json += "{";
                json += "\"id\":" +
                    json_string(std::to_string(entity->GetObjectId()));
                json += ",\"name\":" +
                    json_string(entity->GetObjectName());
                Entity* parent = entity->GetParent();
                json += ",\"parent_id\":";
                json += parent
                    ? json_string(
                        std::to_string(parent->GetObjectId())
                    )
                    : "null";
                json += ",\"active\":" +
                    json_bool(entity->IsActive());
                json += ",\"components\":" +
                    entity_components_json(entity);
                if (!entity->GetTags().empty())
                {
                    json += ",\"tags\":" +
                        entity_tags_json(entity);
                }
                json += ",\"position\":" +
                    json_vector3(entity->GetPosition());
                json += ",\"rotation_euler\":" +
                    json_vector3(
                        entity->GetRotation().ToEulerAngles()
                    );
                json += ",\"forward\":" +
                    json_vector3(entity->GetForward());
                json += ",\"has_render_bounds\":" +
                    json_bool(has_own_bounds);
                if (has_own_bounds)
                {
                    json += ",\"bounding_box\":" +
                        json_bounding_box(own_bounds);
                }
                if (has_subtree_bounds)
                {
                    json += ",\"subtree_bounding_box\":" +
                        json_bounding_box(subtree_bounds);
                }
                json += ",\"support_hit\":" +
                    json_bool(has_support_hit);
                if (has_support_hit)
                {
                    json += ",\"support_gap\":" +
                        std::to_string(support_gap);
                    json += ",\"support\":{";
                    json += "\"position\":" +
                        json_vector3(support_hit.position);
                    json += ",\"normal\":" +
                        json_vector3(support_hit.normal);
                    json += ",\"distance\":" +
                        std::to_string(support_hit.distance);
                    if (support_hit.entity)
                    {
                        json += ",\"entity_id\":" +
                            json_string(
                                std::to_string(
                                    support_hit.entity->GetObjectId()
                                )
                            );
                        json += ",\"entity_name\":" +
                            json_string(
                                support_hit.entity->GetObjectName()
                            );
                    }
                    json += "}";
                }
                json += ",\"ceiling_hit\":" +
                    json_bool(has_ceiling_hit);
                if (has_ceiling_hit)
                {
                    json += ",\"ceiling_gap\":" +
                        std::to_string(ceiling_gap);
                    if (ceiling_hit.entity)
                    {
                        json += ",\"ceiling_entity_id\":" +
                            json_string(
                                std::to_string(
                                    ceiling_hit.entity->GetObjectId()
                                )
                            );
                    }
                }
                json += ",\"wall_hit\":" +
                    json_bool(has_wall_hit);
                if (has_wall_hit)
                {
                    json += ",\"wall_gap\":" +
                        std::to_string(wall_gap);
                    json += ",\"wall_normal\":" +
                        json_vector3(wall_hit.normal);
                    if (wall_hit.entity)
                    {
                        json += ",\"wall_entity_id\":" +
                            json_string(
                                std::to_string(
                                    wall_hit.entity->GetObjectId()
                                )
                            );
                    }
                }
                json += "}";
            }
            json += "],\"count\":" +
                std::to_string(emitted);
            json += ",\"offset\":" +
                std::to_string(offset);
            json += ",\"total\":" +
                std::to_string(entities.size());
            json += ",\"truncated\":" +
                json_bool(
                    entities.size() >
                    static_cast<size_t>(offset) + emitted
                );
            json += "}";
            return json;
        }

        std::string command_entity_list(const McpRequest& request)
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

            std::vector<Entity*> entities;
            for (Entity* entity : World::GetEntities())
            {
                if (entity != nullptr)
                {
                    entities.emplace_back(entity);
                }
            }

            const uint32_t total = static_cast<uint32_t>(entities.size());
            std::string json = "{\"ok\":true,\"total\":" + std::to_string(total);
            json += ",\"offset\":" + std::to_string(offset);
            json += ",\"entities\":[";
            uint32_t count = 0;
            bool first = true;
            for (uint32_t i = offset; i < total && count < limit; i++)
            {
                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += entity_to_json_list_item(entities[i]);
                count++;
            }
            json += "],\"count\":" + std::to_string(count);
            json += ",\"truncated\":" + json_bool(offset + count < total);
            json += "}";
            return json;
        }

        std::string command_entity_find(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            const std::optional<std::string> name = get_argument(request, "name");
            const std::optional<std::string> tag  = get_argument(request, "tag");
            if ((!name || name->empty()) && (!tag || tag->empty()))
            {
                return json_error("missing name or tag");
            }

            std::string match = get_argument(request, "match").value_or("contains");
            if (match != "exact" && match != "contains")
            {
                return json_error("match must be exact or contains");
            }

            uint32_t limit = 20;
            if (const std::optional<std::string> limit_arg = get_argument(request, "limit"))
            {
                uint64_t parsed = 0;
                if (!parse_uint64(*limit_arg, parsed) || parsed == 0 || parsed > 100)
                {
                    return json_error("limit must be between 1 and 100");
                }

                limit = static_cast<uint32_t>(parsed);
            }

            const std::string query = to_lower_copy(name.value_or(""));
            std::string json = "{\"ok\":true,\"matches\":[";
            bool first = true;
            uint32_t count = 0;
            for (Entity* entity : World::GetEntities())
            {
                if (entity == nullptr)
                {
                    continue;
                }

                if (!query.empty())
                {
                    const std::string entity_name = to_lower_copy(entity->GetObjectName());
                    const bool is_match = match == "exact" ? entity_name == query : entity_name.find(query) != std::string::npos;
                    if (!is_match)
                    {
                        continue;
                    }
                }

                if (tag && !tag->empty() && !entity->HasTag(*tag))
                {
                    continue;
                }

                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += entity_to_json_compact(entity);

                count++;
                if (count >= limit)
                {
                    break;
                }
            }

            json += "],\"truncated\":";
            json += json_bool(count >= limit);
            json += "}";
            return json;
        }

        std::string command_entity_get(const McpRequest& request)
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

            return "{\"ok\":true,\"entity\":" + entity_to_json(entity, true) + "}";
        }

        std::string command_entity_update(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("entity updates require edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            bool changed = false;
            if (const std::optional<std::string> name = get_argument(request, "name"))
            {
                if (name->empty())
                {
                    return json_error("name cannot be empty");
                }

                entity->SetObjectName(*name);
                changed = true;
            }

            if (const std::optional<std::string> active = get_argument(request, "active"))
            {
                bool parsed = false;
                if (!parse_bool(*active, parsed))
                {
                    return json_error("invalid active");
                }

                entity->SetActive(parsed);
                changed = true;
            }
            if (
                const std::optional<std::string> transient =
                    get_argument(request, "transient")
            )
            {
                bool parsed = false;
                if (!parse_bool(*transient, parsed))
                {
                    return json_error("invalid transient");
                }
                entity->SetTransient(parsed);
                changed = true;
            }

            if (const std::optional<std::string> parent_id = get_argument(request, "parent_id"))
            {
                Entity* parent = nullptr;
                if (!parent_id->empty() && *parent_id != "null" && *parent_id != "none" && *parent_id != "root" && *parent_id != "0")
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
                    if (parent == entity || parent->IsDescendantOf(entity))
                    {
                        return json_error("parent cannot be self or descendant");
                    }
                }

                entity->SetParent(parent);
                changed = true;
            }

            if (const std::optional<std::string> tags = get_argument(request, "tags"))
            {
                const std::string mode = to_lower_copy(
                    get_argument(request, "tags_mode").value_or(
                        "replace"
                    )
                );
                if (mode == "replace")
                {
                    entity->SetTagsString(*tags);
                }
                else if (mode == "merge")
                {
                    add_entity_tags(entity, *tags);
                }
                else
                {
                    return json_error(
                        "tags_mode must be replace or merge"
                    );
                }
                changed = true;
            }

            // "key=value,key=value" on a code prefab (e.g. a car's paint_color_r or camera_follows), the prefab is rebuilt with them on the next world load
            bool prefab_changed = false;
            if (const std::optional<std::string> prefab_attributes = get_argument(request, "prefab_attributes"))
            {
                if (entity->GetPrefabType().empty())
                {
                    return json_error("entity is not a code prefab");
                }
                auto trim = [](const std::string& value)
                {
                    const size_t first = value.find_first_not_of(" \t");
                    const size_t last  = value.find_last_not_of(" \t");
                    return first == std::string::npos ? std::string() : value.substr(first, last - first + 1);
                };
                std::unordered_map<std::string, std::string> attributes = entity->GetPrefabAttributes();
                std::stringstream stream(*prefab_attributes);
                std::string pair;
                while (std::getline(stream, pair, ','))
                {
                    const size_t equals = pair.find('=');
                    const std::string key = equals == std::string::npos ? "" : trim(pair.substr(0, equals));
                    if (key.empty() || key == "type" || key == "file")
                    {
                        return json_error("prefab_attributes expects key=value pairs separated by commas, type and file cannot change");
                    }
                    attributes[key] = trim(pair.substr(equals + 1));
                }
                entity->SetPrefabData(entity->GetPrefabType(), attributes);
                changed        = true;
                prefab_changed = true;
            }

            if (!changed)
            {
                return json_error("no entity values provided");
            }

            std::string json = "{\"ok\":true,\"entity\":" + entity_to_json_compact(entity);
            if (prefab_changed)
            {
                json += ",\"prefab_attributes\":{";
                bool first = true;
                for (const auto& [key, value] : std::map<std::string, std::string>(entity->GetPrefabAttributes().begin(), entity->GetPrefabAttributes().end()))
                {
                    json += (first ? "" : ",") + json_string(key) + ":" + json_string(value);
                    first = false;
                }
                json += "},\"note\":\"prefab attributes apply when the world is saved and loaded again\"";
            }
            return json + "}";
        }

        std::string command_entity_delete(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("entity deletion requires edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            const std::string deleted_id = std::to_string(entity->GetObjectId());
            World::RemoveEntity(entity);
            return "{\"ok\":true,\"deleted_id\":" + json_string(deleted_id) + "}";
        }

        std::string command_entity_delete_children(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("entity child deletion requires edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            uint32_t deleted_count = 0;
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
                    deleted_count++;
                    deleted_any = true;
                }

                if (!deleted_any)
                {
                    break;
                }
            }

            entity->AcquireChildren();

            std::string json = "{\"ok\":true,\"deleted_count\":" + std::to_string(deleted_count);
            json += ",\"id\":" + json_string(std::to_string(entity->GetObjectId()));
            json += ",\"name\":" + json_string(entity->GetObjectName());
            json += ",\"remaining_count\":" + std::to_string(entity->GetChildrenCount());
            json += ",\"remaining_children\":[";
            bool first_child = true;
            for (Entity* child : entity->GetChildren())
            {
                if (child == nullptr)
                {
                    continue;
                }

                if (!first_child)
                {
                    json += ",";
                }
                first_child = false;
                json += json_string(child->GetObjectName());
            }
            json += "]";
            json += "}";
            return json;
        }

        std::string command_primitive_types()
        {
            return "{\"ok\":true,\"primitive_types\":" + primitive_types_json() + "}";
        }

        void append_render_material_snapshot(std::string& json, Entity* entity, bool include_descendants, bool& first)
        {
            if (entity == nullptr)
            {
                return;
            }

            if (Render* render = entity->GetComponent<Render>())
            {
                if (!first)
                {
                    json += ",";
                }
                first = false;

                Entity* parent = entity->GetParent();
                json += "{";
                json += "\"id\":" + json_string(std::to_string(entity->GetObjectId()));
                json += ",\"name\":" + json_string(entity->GetObjectName());
                json += ",\"parent_id\":";
                json += parent ? json_string(std::to_string(parent->GetObjectId())) : "null";
                json += ",\"mesh\":" + json_string(render->GetMeshName());
                json += ",\"material\":" + json_string(render->GetMaterialName());
                json += ",\"default_material\":" + json_bool(render->IsUsingDefaultMaterial());
                json += "}";
            }

            if (!include_descendants)
            {
                return;
            }

            for (Entity* child : entity->GetChildren())
            {
                append_render_material_snapshot(json, child, include_descendants, first);
            }
        }

        std::string command_entity_render_materials(const McpRequest& request)
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

            bool include_descendants = true;
            if (const std::optional<std::string> value = get_argument(request, "include_descendants"))
            {
                if (!parse_bool(*value, include_descendants))
                {
                    return json_error("invalid include_descendants");
                }
            }

            std::string json = "{\"ok\":true";
            json += ",\"id\":" + json_string(std::to_string(entity->GetObjectId()));
            json += ",\"name\":" + json_string(entity->GetObjectName());
            json += ",\"materials\":[";
            bool first = true;
            append_render_material_snapshot(json, entity, include_descendants, first);
            json += "]}";
            return json;
        }

        std::string command_resource_list(const McpRequest& request)
        {
            ResourceType type = ResourceType::Max;
            if (const std::optional<std::string> type_arg = get_argument(request, "type"))
            {
                const std::optional<ResourceType> parsed = resource_type_from_name(to_lower_copy(*type_arg));
                if (!parsed)
                {
                    return json_error("invalid resource type");
                }
                type = *parsed;
            }

            uint32_t limit = 500;
            if (const std::optional<std::string> limit_arg = get_argument(request, "limit"))
            {
                uint64_t parsed = 0;
                if (!parse_uint64(*limit_arg, parsed) || parsed == 0 || parsed > 5000)
                {
                    return json_error("limit must be between 1 and 5000");
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
            json += ",\"type\":" + json_string(resource_type_to_name(type));
            json += ",\"offset\":" + std::to_string(offset);
            json += ",\"resources\":[";
            bool first = true;
            for (const std::shared_ptr<IResource>& resource : ResourceCache::GetResourcesSnapshot())
            {
                if (!resource || (type != ResourceType::Max && resource->GetResourceType() != type))
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
                json += resource_to_json(resource.get());
            }

            json += "],\"total\":" + std::to_string(total);
            json += ",\"count\":" + std::to_string(emitted);
            json += ",\"truncated\":" + json_bool(total > offset + emitted);
            json += "}";
            return json;
        }

        std::string command_material_get(const McpRequest& request)
        {
            std::string error;
            Material* material = get_material_from_request(request, error);
            if (material == nullptr)
            {
                return json_error(error);
            }

            return "{\"ok\":true,\"material\":" + material_to_json(material) + "}";
        }

        std::string command_material_set_property(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("material edits require edit mode");
            }

            std::string error;
            Material* material = get_material_from_request(request, error);
            if (material == nullptr)
            {
                return json_error(error);
            }

            const std::optional<std::string> property_arg = get_argument(request, "property");
            const std::optional<std::string> value_arg = get_argument(request, "value");
            if (!property_arg || !value_arg)
            {
                return json_error("missing property or value");
            }

            const std::string property_name = to_lower_copy(*property_arg);
            bool inverted = false;
            std::optional<MaterialProperty> property =
                material_property_from_name(property_name);
            if (!property)
            {
                property = material_property_inverted_from_name(property_name);
                inverted = property.has_value();
            }
            if (!property)
            {
                return json_error(
                    "invalid material property, valid names: " +
                    material_property_names_csv()
                );
            }

            float value = 0.0f;
            if (!parse_float(*value_arg, value))
            {
                return json_error("invalid material property value");
            }

            if (inverted)
            {
                value = std::clamp(1.0f - value, 0.0f, 1.0f);
            }

            material->SetProperty(*property, value);
            std::string json = "{\"ok\":true,\"applied_property\":";
            json += json_string(material_property_to_name(*property));
            json += ",\"applied_value\":" + std::to_string(value);
            json += ",\"material\":" + material_to_json(material) + "}";
            return json;
        }

        std::string command_material_set_texture(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("material edits require edit mode");
            }

            std::string error;
            Material* material = get_material_from_request(request, error);
            if (material == nullptr)
            {
                return json_error(error);
            }

            const std::optional<std::string> texture_type_arg = get_argument(request, "texture_type");
            const std::optional<std::string> texture_path = get_argument(request, "texture_path");
            if (!texture_type_arg || !texture_path)
            {
                return json_error("missing texture_type or texture_path");
            }

            const std::optional<MaterialTextureType> texture_type = material_texture_type_from_name(to_lower_copy(*texture_type_arg));
            if (!texture_type)
            {
                return json_error("invalid material texture type");
            }

            uint8_t slot = 0;
            if (const std::optional<std::string> slot_arg = get_argument(request, "slot"))
            {
                uint32_t parsed = 0;
                if (!parse_uint32(*slot_arg, parsed) || parsed >= Material::slots_per_texture)
                {
                    return json_error("invalid texture slot");
                }
                slot = static_cast<uint8_t>(parsed);
            }

            material->SetTexture(*texture_type, *texture_path, slot);
            return "{\"ok\":true,\"material\":" + material_to_json(material) + "}";
        }

        std::string command_material_apply_preset(
            const McpRequest& request
        )
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("material edits require edit mode");
            }

            std::string error;
            Material* material =
                get_material_from_request(request, error);
            if (material == nullptr)
            {
                return json_error(error);
            }

            const std::optional<std::string> kind_arg =
                get_argument(request, "kind");
            const std::optional<std::string> preset_arg =
                get_argument(request, "preset");
            if (!kind_arg || !preset_arg)
            {
                return json_error("missing kind or preset");
            }

            const std::string kind = to_lower_copy(*kind_arg);
            const std::string preset = to_lower_copy(*preset_arg);
            if (kind == "paint")
            {
                const std::optional<MaterialPaintPreset> parsed =
                    material_paint_preset_from_name(preset);
                if (!parsed)
                {
                    return json_error("invalid paint preset");
                }

                Color color(
                    material->GetProperty(MaterialProperty::ColorR),
                    material->GetProperty(MaterialProperty::ColorG),
                    material->GetProperty(MaterialProperty::ColorB),
                    material->GetProperty(MaterialProperty::ColorA)
                );
                if (
                    const std::optional<std::string> color_arg =
                        get_argument(request, "color")
                )
                {
                    if (!parse_color(*color_arg, color))
                    {
                        return json_error("invalid color");
                    }
                }
                material->ApplyPaintPreset(*parsed, color, true);
            }
            else if (kind == "surface")
            {
                const std::optional<MaterialSurfacePreset> parsed =
                    material_surface_preset_from_name(preset);
                if (!parsed)
                {
                    return json_error("invalid surface preset");
                }
                material->ApplySurfacePreset(*parsed, true);
            }
            else
            {
                return json_error("kind must be paint or surface");
            }

            return "{\"ok\":true,\"material\":" +
                material_to_json(material) +
                "}";
        }

        std::string command_material_semantic_create(
            const McpRequest& request
        )
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("material creation requires edit mode");
            }

            const std::optional<std::string> path_arg =
                get_argument(request, "path");
            const std::optional<std::string> semantic_arg =
                get_argument(request, "semantic");
            if (
                !path_arg ||
                path_arg->empty() ||
                !semantic_arg
            )
            {
                return json_error("missing path or semantic");
            }

            std::string path_error;
            const std::optional<std::string> resolved_path =
                resolve_mcp_output_path(
                    *path_arg,
                    "materials",
                    EXTENSION_MATERIAL,
                    path_error
                );
            if (!resolved_path)
            {
                return json_error(path_error);
            }
            const std::string path = *resolved_path;
            std::filesystem::create_directories(
                std::filesystem::path(path).parent_path()
            );
            const std::string semantic =
                to_lower_copy(*semantic_arg);
            std::shared_ptr<Material> material =
                ResourceCache::GetByPath<Material>(path);
            if (!material && FileSystem::IsFile(path))
            {
                material = ResourceCache::Load<Material>(path);
            }
            if (!material)
            {
                const std::filesystem::path file_path(path);
                if (file_path.has_parent_path())
                {
                    std::filesystem::create_directories(
                        file_path.parent_path()
                    );
                }
                material = std::make_shared<Material>();
                material->SetResourceFilePath(path);
                material = ResourceCache::Cache(material);
            }
            if (!material)
            {
                return json_error("failed to create material");
            }

            Color color(0.8f, 0.8f, 0.8f, 1.0f);
            if (semantic == "painted_wall")
            {
                color = Color(0.72f, 0.68f, 0.58f, 1.0f);
            }
            else if (semantic == "wood")
            {
                color = Color(0.32f, 0.14f, 0.055f, 1.0f);
            }
            else if (semantic == "fabric")
            {
                color = Color(0.55f, 0.42f, 0.3f, 1.0f);
            }
            else if (semantic == "concrete")
            {
                color = Color(0.42f, 0.44f, 0.43f, 1.0f);
            }
            else if (semantic == "asphalt")
            {
                color = Color(0.035f, 0.04f, 0.045f, 1.0f);
            }
            else if (semantic == "masonry")
            {
                color = Color(0.4f, 0.28f, 0.2f, 1.0f);
            }
            else if (semantic == "rubber")
            {
                color = Color(0.018f, 0.02f, 0.022f, 1.0f);
            }
            else if (semantic == "road_paint")
            {
                color = Color(0.78f, 0.76f, 0.62f, 1.0f);
            }
            else if (semantic == "painted_metal")
            {
                color = Color(0.32f, 0.38f, 0.44f, 1.0f);
            }
            else if (semantic == "screen")
            {
                color = Color(0.005f, 0.008f, 0.012f, 1.0f);
            }
            else if (semantic == "screen_on")
            {
                color = Color(0.08f, 0.22f, 0.5f, 1.0f);
            }
            bool color_overridden = false;
            if (
                const std::optional<std::string> color_arg =
                    get_argument(request, "color")
            )
            {
                if (!parse_color(*color_arg, color))
                {
                    return json_error("invalid color");
                }
                color_overridden = true;
            }

            if (semantic == "painted_wall" || semantic == "paint")
            {
                material->ApplyPaintPreset(
                    MaterialPaintPreset::Matte,
                    color,
                    false
                );
            }
            else if (semantic == "wood")
            {
                material->ApplyPaintPreset(
                    MaterialPaintPreset::Matte,
                    color,
                    false
                );
                material->SetProperty(
                    MaterialProperty::Roughness,
                    0.72f
                );
                material->SetProperty(
                    MaterialProperty::Sheen,
                    0.12f
                );
            }
            else if (
                semantic == "concrete" ||
                semantic == "asphalt" ||
                semantic == "masonry" ||
                semantic == "road_paint"
            )
            {
                material->ApplyPaintPreset(
                    MaterialPaintPreset::Matte,
                    color,
                    false
                );
                const float roughness =
                    semantic == "asphalt"
                    ? 0.92f
                    : semantic == "concrete"
                        ? 0.86f
                        : semantic == "masonry"
                            ? 0.82f
                            : 0.58f;
                material->SetProperty(
                    MaterialProperty::Roughness,
                    roughness
                );
            }
            else if (semantic == "rubber")
            {
                material->ApplySurfacePreset(
                    MaterialSurfacePreset::RubberTire,
                    false
                );
                material->SetColor(color);
            }
            else if (semantic == "painted_metal")
            {
                material->ApplyPaintPreset(
                    MaterialPaintPreset::Satin,
                    color,
                    false
                );
                material->SetProperty(
                    MaterialProperty::Metalness,
                    0.62f
                );
                material->SetProperty(
                    MaterialProperty::Roughness,
                    0.38f
                );
            }
            else if (semantic == "black_plastic")
            {
                material->ApplySurfacePreset(
                    MaterialSurfacePreset::BlackPlastic,
                    false
                );
                if (color_overridden)
                {
                    material->SetColor(color);
                }
            }
            else if (semantic == "fabric")
            {
                material->ApplyPaintPreset(
                    MaterialPaintPreset::Matte,
                    color,
                    false
                );
                material->SetProperty(
                    MaterialProperty::Roughness,
                    0.88f
                );
                material->SetProperty(
                    MaterialProperty::Sheen,
                    0.35f
                );
            }
            else if (semantic == "metal")
            {
                material->ApplySurfacePreset(
                    MaterialSurfacePreset::PolishedMetal,
                    false
                );
                material->SetColor(color);
            }
            else if (semantic == "chrome")
            {
                material->ApplySurfacePreset(
                    MaterialSurfacePreset::Chrome,
                    false
                );
                if (color_overridden)
                {
                    material->SetColor(color);
                }
            }
            else if (semantic == "glass")
            {
                material->ApplySurfacePreset(
                    MaterialSurfacePreset::GlassClear,
                    false
                );
                if (color_overridden)
                {
                    material->SetColor(color);
                }
            }
            else if (semantic == "screen")
            {
                material->ApplyPaintPreset(
                    MaterialPaintPreset::GlossSolid,
                    color,
                    false
                );
                material->SetProperty(
                    MaterialProperty::Roughness,
                    0.18f
                );
            }
            else if (semantic == "screen_on")
            {
                material->ApplyPaintPreset(
                    MaterialPaintPreset::GlossSolid,
                    color,
                    false
                );
                material->SetProperty(
                    MaterialProperty::EmissiveFromAlbedo,
                    0.08f
                );
            }
            else if (semantic == "emissive")
            {
                material->ApplySurfacePreset(
                    MaterialSurfacePreset::EmissiveWhiteLight,
                    false
                );
                material->SetColor(color);
            }
            else
            {
                return json_error("unsupported semantic material");
            }

            std::optional<std::filesystem::file_time_type>
                previous_write_time;
            if (FileSystem::IsFile(path))
            {
                previous_write_time =
                    std::filesystem::last_write_time(path);
            }
            material->SaveToFile(path);
            if (!FileSystem::IsFile(path))
            {
                return json_error("failed to save material");
            }
            if (
                previous_write_time &&
                std::filesystem::last_write_time(path) <=
                *previous_write_time
            )
            {
                return json_error("material file was not updated");
            }

            return "{\"ok\":true,\"semantic\":" +
                json_string(semantic) +
                ",\"material\":" +
                material_to_json(material.get()) +
                "}";
        }

        std::string command_undo_redo(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("undo and redo require edit mode");
            }

            const std::optional<std::string> action_arg = get_argument(request, "action");
            if (!action_arg)
            {
                return json_error("missing action");
            }

            const std::string action = to_lower_copy(*action_arg);
            if (action == "undo")
            {
                CommandStack::Undo();
            }
            else if (action == "redo")
            {
                CommandStack::Redo();
            }
            else
            {
                return json_error("unknown undo action");
            }

            return "{\"ok\":true,\"action\":" + json_string(action) + "}";
        }

        std::string command_resource_load(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            const std::optional<std::string> type_arg = get_argument(request, "type");
            const std::optional<std::string> path_arg = get_argument(request, "path");
            if (!type_arg || !path_arg || path_arg->empty())
            {
                return json_error("missing type or path");
            }

            const std::optional<ResourceType> type = resource_type_from_name(to_lower_copy(*type_arg));
            if (!type || *type == ResourceType::Max || *type == ResourceType::Unknown)
            {
                return json_error("invalid resource type");
            }

            uint32_t flags = 0;
            if (const std::optional<std::string> flags_arg = get_argument(request, "flags"))
            {
                if (!parse_uint32(*flags_arg, flags))
                {
                    return json_error("invalid flags");
                }
            }

            std::shared_ptr<IResource> resource;
            if (*type == ResourceType::Material)
            {
                resource = ResourceCache::Load<Material>(*path_arg, flags);
            }
            else if (*type == ResourceType::Texture)
            {
                resource = ResourceCache::Load<RHI_Texture>(*path_arg, flags != 0 ? flags : RHI_Texture_Srv);
            }
            else if (*type == ResourceType::Mesh)
            {
                resource = ResourceCache::Load<Mesh>(*path_arg, flags);
            }
            else if (*type == ResourceType::Animation)
            {
                resource = ResourceCache::Load<Animation>(*path_arg, flags);
            }
            else
            {
                return json_error("resource type is not loadable by MCP");
            }

            if (!resource)
            {
                return json_error("failed to load resource");
            }

            return "{\"ok\":true,\"resource\":" + resource_to_json(resource.get()) + "}";
        }

        std::string command_resource_reload(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            const std::optional<std::string> key_arg = get_argument(request, "name") ? get_argument(request, "name") : get_argument(request, "path");
            if (!key_arg || key_arg->empty())
            {
                return json_error("missing resource name or path");
            }

            ResourceType type = ResourceType::Max;
            if (const std::optional<std::string> type_arg = get_argument(request, "type"))
            {
                const std::optional<ResourceType> parsed = resource_type_from_name(to_lower_copy(*type_arg));
                if (!parsed)
                {
                    return json_error("invalid resource type");
                }
                type = *parsed;
            }

            std::shared_ptr<IResource> resource = get_resource_shared_by_name_or_path(*key_arg, type);
            if (!resource)
            {
                return json_error("resource not found");
            }
            if (resource->GetResourceFilePath().empty())
            {
                return json_error("resource has no file path");
            }

            if (!resource->LoadFromFile(resource->GetResourceFilePath()))
                return json_error("resource reload failed; see the engine log");
            return "{\"ok\":true,\"resource\":" + resource_to_json(resource.get()) + "}";
        }

        std::string command_resource_save(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("resource save requires edit mode");
            }

            const std::optional<std::string> key_arg = get_argument(request, "name") ? get_argument(request, "name") : get_argument(request, "path");
            if (!key_arg || key_arg->empty())
            {
                return json_error("missing resource name or path");
            }

            ResourceType type = ResourceType::Max;
            if (const std::optional<std::string> type_arg = get_argument(request, "type"))
            {
                const std::optional<ResourceType> parsed = resource_type_from_name(to_lower_copy(*type_arg));
                if (!parsed)
                {
                    return json_error("invalid resource type");
                }
                type = *parsed;
            }

            std::shared_ptr<IResource> resource = get_resource_shared_by_name_or_path(*key_arg, type);
            if (!resource)
            {
                return json_error("resource not found");
            }

            const std::optional<std::string> save_path = get_argument(request, "save_path");
            const std::string requested_path =
                save_path && !save_path->empty()
                ? *save_path
                : resource->GetResourceFilePath();
            if (requested_path.empty())
            {
                return json_error("resource has no save path");
            }

            const char* directory_name = "resources";
            std::string extension =
                std::filesystem::path(
                    requested_path
                ).extension().string();
            switch (resource->GetResourceType())
            {
            case ResourceType::Texture:
            case ResourceType::Cubemap:
                directory_name = "textures";
                if (extension.empty())
                {
                    extension = EXTENSION_TEXTURE;
                }
                break;
            case ResourceType::Audio:
                directory_name = "audio";
                if (extension.empty())
                {
                    extension = EXTENSION_AUDIO;
                }
                break;
            case ResourceType::Material:
                directory_name = "materials";
                extension = EXTENSION_MATERIAL;
                break;
            case ResourceType::Mesh:
                directory_name = "meshes";
                extension = EXTENSION_MESH;
                break;
            case ResourceType::Font:
                directory_name = "fonts";
                if (extension.empty())
                {
                    extension = EXTENSION_FONT;
                }
                break;
            case ResourceType::Shader:
                directory_name = "shaders";
                if (extension.empty())
                {
                    extension = EXTENSION_SHADER;
                }
                break;
            case ResourceType::Animation:
                directory_name = "animations";
                if (extension.empty())
                {
                    extension = ".animation";
                }
                break;
            default:
                if (extension.empty())
                {
                    extension = ".resource";
                }
                break;
            }
            std::string path_error;
            const std::optional<std::string> resolved_path =
                resolve_mcp_output_path(
                    requested_path,
                    directory_name,
                    extension,
                    path_error
                );
            if (!resolved_path)
            {
                return json_error(path_error);
            }
            const std::string path = *resolved_path;
            const std::filesystem::path file_path(path);
            if (file_path.has_parent_path())
            {
                std::filesystem::create_directories(file_path.parent_path());
            }
            resource->SetResourceFilePath(path);
            resource->SaveToFile(path);
            // SaveToFile cannot report a full disk or a denied write, so the file itself is the receipt,
            // reporting a save that did not happen sends the catalog off to register a missing file
            if (!FileSystem::IsFile(path))
            {
                return json_error("the resource did not reach " + path);
            }
            return "{\"ok\":true,\"path\":" + json_string(path) + ",\"resource\":" + resource_to_json(resource.get()) + "}";
        }

        std::string command_resource_remove(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("resource removal requires edit mode");
            }

            const std::optional<std::string> key_arg = get_argument(request, "name") ? get_argument(request, "name") : get_argument(request, "path");
            if (!key_arg || key_arg->empty())
            {
                return json_error("missing resource name or path");
            }

            bool delete_file = false;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "delete_file")
            )
            {
                if (!parse_bool(*value, delete_file))
                {
                    return json_error("delete_file must be boolean");
                }
            }

            ResourceType type = ResourceType::Max;
            if (const std::optional<std::string> type_arg = get_argument(request, "type"))
            {
                const std::optional<ResourceType> parsed = resource_type_from_name(to_lower_copy(*type_arg));
                if (!parsed)
                {
                    return json_error("invalid resource type");
                }
                type = *parsed;
            }

            std::vector<std::shared_ptr<IResource>> resources = ResourceCache::GetResourcesSnapshot();
            const auto it = std::find_if(resources.begin(), resources.end(), [&](const std::shared_ptr<IResource>& resource)
            {
                return resource && (type == ResourceType::Max || resource->GetResourceType() == type) && (resource->GetObjectName() == *key_arg || resource->GetResourceFilePath() == *key_arg);
            });

            if (it == resources.end())
            {
                return json_error("resource not found");
            }

            const std::string file_path =
                (*it)->GetResourceFilePath();
            if (delete_file)
            {
                if (
                    file_path.empty() ||
                    !path_is_within(
                        file_path,
                        World::GetGeneratedResourceDirectory()
                    )
                )
                {
                    return json_error(
                        "only shared MCP resource files can be deleted"
                    );
                }

                std::error_code error;
                const bool removed_file =
                    std::filesystem::remove(file_path, error);
                error.clear();
                const bool file_still_exists =
                    std::filesystem::exists(file_path, error);
                if (!removed_file && file_still_exists)
                {
                    return json_error(
                        "resource file could not be deleted"
                    );
                }
            }

            const std::string removed = resource_to_json(it->get());
            ResourceCache::Remove(*it);
            return
                "{\"ok\":true,\"file_deleted\":" +
                std::string(delete_file ? "true" : "false") +
                ",\"removed\":" +
                removed +
                "}";
        }

        std::string command_material_create(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("material creation requires edit mode");
            }

            const std::optional<std::string> path_arg = get_argument(request, "path");
            if (!path_arg || path_arg->empty())
            {
                return json_error("missing path");
            }
            std::string path_error;
            const std::optional<std::string> path =
                resolve_mcp_output_path(
                    *path_arg,
                    "materials",
                    EXTENSION_MATERIAL,
                    path_error
                );
            if (!path)
            {
                return json_error(path_error);
            }

            std::shared_ptr<Material> material = std::make_shared<Material>();
            const std::filesystem::path file_path(*path);
            if (file_path.has_parent_path())
            {
                std::filesystem::create_directories(file_path.parent_path());
            }
            material->SetResourceFilePath(*path);
            material->SaveToFile(*path);
            // caching a material whose file never landed hands back something that looks usable and then
            // resolves to nothing the next time the prefab is loaded
            if (!FileSystem::IsFile(*path))
            {
                return json_error("the material did not reach " + *path);
            }
            if (const std::optional<std::string> name = get_argument(request, "name"))
            {
                material->SetObjectName(*name);
            }

            material = ResourceCache::Cache(material);
            return "{\"ok\":true,\"material\":" + material_to_json(material.get()) + "}";
        }

        float fit_camera_distance_to_bounds(
            const math::BoundingBox& bounds,
            const math::Quaternion& rotation,
            const float fov_horizontal,
            const float fov_vertical,
            const float near_plane,
            const float padding
        )
        {
            const math::Vector3 center = bounds.GetCenter();
            const math::Vector3 forward =
                rotation * math::Vector3::Forward;
            const math::Vector3 right =
                rotation * math::Vector3::Right;
            const math::Vector3 up =
                rotation * math::Vector3::Up;
            const float tan_horizontal =
                std::tan(fov_horizontal * 0.5f);
            const float tan_vertical =
                std::tan(fov_vertical * 0.5f);
            float distance = near_plane * 2.0f;
            std::array<math::Vector3, 8> corners;
            bounds.GetCorners(&corners);
            for (const math::Vector3& corner : corners)
            {
                const math::Vector3 offset = corner - center;
                const float depth =
                    math::Vector3::Dot(offset, forward);
                const float horizontal =
                    std::abs(math::Vector3::Dot(offset, right));
                const float vertical =
                    std::abs(math::Vector3::Dot(offset, up));
                distance = std::max(
                    distance,
                    horizontal * padding /
                    tan_horizontal - depth
                );
                distance = std::max(
                    distance,
                    vertical * padding /
                    tan_vertical - depth
                );
                distance = std::max(
                    distance,
                    near_plane * 2.0f - depth
                );
            }
            return distance;
        }

        std::string command_viewport_frame(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("viewport frame requires edit mode");
            }

            Camera* camera = World::GetCamera();
            if (camera == nullptr)
            {
                return json_error("camera not found");
            }

            Entity* target_entity = nullptr;
            if (get_argument(request, "id"))
            {
                std::string error;
                target_entity = get_entity_from_request(request, error);
                if (target_entity == nullptr)
                {
                    return json_error(error);
                }
                spartan::Selection::ClearSelection();
                spartan::Selection::AddToSelection(target_entity);
            }
            else
            {
                target_entity = spartan::Selection::GetSelectedEntity();
            }
            if (target_entity == nullptr)
            {
                return json_error("no entity selected to frame");
            }

            math::BoundingBox bounds;
            bool has_bounds = false;
            std::vector<Entity*> entities = { target_entity };
            target_entity->GetDescendants(&entities);
            for (Entity* entity : entities)
            {
                if (entity == nullptr)
                {
                    continue;
                }
                if (Render* render = entity->GetComponent<Render>())
                {
                    const math::BoundingBox& render_bounds =
                        render->GetBoundingBox();
                    if (
                        !render_bounds.GetMin().IsFinite() ||
                        !render_bounds.GetMax().IsFinite() ||
                        render_bounds.GetSize().LengthSquared() <= 0.0f
                    )
                    {
                        continue;
                    }
                    if (!has_bounds)
                    {
                        bounds = render_bounds;
                        has_bounds = true;
                    }
                    else
                    {
                        bounds.Merge(render_bounds);
                    }
                }
            }

            const math::Vector3 target = has_bounds
                ? bounds.GetCenter()
                : target_entity->GetPosition();
            float padding = 1.2f;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "padding")
            )
            {
                if (
                    !parse_float(*value, padding) ||
                    padding < 1.0f ||
                    padding > 4.0f
                )
                {
                    return json_error(
                        "padding must be between 1 and 4"
                    );
                }
            }

            const std::string view = to_lower_copy(
                get_argument(request, "view").value_or("perspective")
            );
            math::Vector3 camera_direction;
            if (view == "perspective")
            {
                camera_direction =
                    math::Vector3(1.0f, 0.65f, -1.0f).Normalized();
            }
            else if (view == "front")
            {
                camera_direction = math::Vector3(0.0f, 0.0f, 1.0f);
            }
            else if (view == "back")
            {
                camera_direction = math::Vector3(0.0f, 0.0f, -1.0f);
            }
            else if (view == "left")
            {
                camera_direction = math::Vector3(-1.0f, 0.0f, 0.0f);
            }
            else if (view == "right")
            {
                camera_direction = math::Vector3(1.0f, 0.0f, 0.0f);
            }
            else if (view == "top")
            {
                camera_direction =
                    math::Vector3(0.0f, 1.0f, 0.001f).Normalized();
            }
            else
            {
                return json_error("unknown viewport frame view");
            }

            const float fov_horizontal =
                camera->GetFovHorizontalRad();
            const float fov_vertical =
                camera->GetFovVerticalRad();
            if (
                !std::isfinite(fov_horizontal) ||
                !std::isfinite(fov_vertical) ||
                fov_horizontal <= 0.01f ||
                fov_vertical <= 0.01f
            )
            {
                return json_error("camera field of view is invalid");
            }
            const math::Quaternion rotation =
                math::Quaternion::FromLookRotation(
                    camera_direction * -1.0f
                );
            const math::BoundingBox frame_bounds = has_bounds
                ? bounds
                : math::BoundingBox(
                    target - math::Vector3::One,
                    target + math::Vector3::One
                );
            const float distance =
                fit_camera_distance_to_bounds(
                    frame_bounds,
                    rotation,
                    fov_horizontal,
                    fov_vertical,
                    camera->GetNearPlane(),
                    padding
                );
            Entity* camera_entity = camera->GetEntity();
            const math::Vector3 position =
                target + camera_direction * distance;
            camera_entity->SetPosition(position);
            camera_entity->SetRotation(rotation);

            std::string json = command_camera_snapshot();
            if (!json.empty() && json.back() == '}')
            {
                json.pop_back();
                json += ",\"view\":" + json_string(view);
                json += ",\"target\":" + json_vector3(target);
                json += ",\"distance\":" + std::to_string(distance);
                json += ",\"padding\":" + std::to_string(padding);
                if (has_bounds)
                {
                    json += ",\"bounding_box\":" +
                        json_bounding_box(bounds);
                }
                json += "}";
            }
            return json;
        }

        // only headless instances can be closed remotely, a visible editor may hold a user's unsaved work
        std::string command_engine_quit(const McpRequest& request)
        {
            (void)request;
            if (!Engine::IsHeadless())
            {
                return json_error("engine_quit only closes headless instances, close a visible editor by hand");
            }

            Window::Close();
            return "{\"ok\":true,\"closing\":true}";
        }

        std::string input_status_json()
        {
            std::string held;
            for (uint32_t i = 0; i < Input::key_count; i++)
            {
                float seconds_left = 0.0f;
                if (Input::GetInjectedKey(static_cast<KeyCode>(i), seconds_left))
                {
                    held += held.empty() ? "" : ",";
                    held += "{\"key\":" + json_string(Input::GetKeyName(static_cast<KeyCode>(i))) + ",\"seconds_left\":" + json_number(seconds_left) + "}";
                }
            }

            const math::Vector2 motion = Input::GetInjectedMouseMotionRemaining();
            std::string json = "{\"ok\":true,\"held\":[" + held + "]";
            json += ",\"mouse_delta_remaining\":[" + json_number(motion.x) + "," + json_number(motion.y) + "]";
            json += ",\"blocked_by_ui\":" + json_bool(Input::IsBlockedByUi());
            json += ",\"gamepad_connected\":" + json_bool(Input::IsGamepadConnected());
            if (Camera* camera = World::GetCamera())
            {
                json += ",\"camera_controlled\":" + json_bool(camera->GetFlag(CameraFlags::IsControlled));
                json += ",\"degrees_per_pixel\":" + json_number(camera->GetMouseSensitivity());
            }
            json += "}";
            return json;
        }

        // virtual keyboard and mouse, see Input::InjectKey, nothing reaches the os so other windows are safe
        std::string command_input_inject(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            bool release = false;
            if (const std::optional<std::string> value = get_argument(request, "release"))
            {
                if (!parse_bool(*value, release))
                {
                    return json_error("invalid release");
                }
            }

            float duration = 0.1f;
            if (const std::optional<std::string> value = get_argument(request, "duration"))
            {
                if (!parse_float(*value, duration) || duration < 0.0f || duration > 60.0f)
                {
                    return json_error("duration must be 0 to 60 seconds");
                }
            }

            // validate everything before injecting anything
            std::vector<KeyCode> keys;
            if (const std::optional<std::string> value = get_argument(request, "keys"))
            {
                std::stringstream stream(*value);
                std::string part;
                while (std::getline(stream, part, ','))
                {
                    part.erase(0, part.find_first_not_of(" \t"));
                    part.erase(part.find_last_not_of(" \t") + 1);
                    if (part.empty())
                    {
                        continue;
                    }

                    KeyCode key;
                    if (!Input::GetKeyFromName(part, key))
                    {
                        return json_error("unknown key '" + part + "', use KeyCode names such as W, Shift_Left, Space, Arrow_Up, Click_Right");
                    }
                    keys.push_back(key);
                }
            }

            std::optional<math::Vector2> mouse_delta;
            if (const std::optional<std::string> value = get_argument(request, "mouse_delta"))
            {
                math::Vector2 parsed;
                if (!parse_vector2(*value, parsed))
                {
                    return json_error("invalid mouse_delta, expected [x, y] pixels");
                }
                mouse_delta = parsed;
            }

            std::optional<math::Vector2> wheel;
            if (const std::optional<std::string> value = get_argument(request, "wheel"))
            {
                math::Vector2 parsed;
                if (!parse_vector2(*value, parsed))
                {
                    return json_error("invalid wheel, expected [x, y] notches");
                }
                wheel = parsed;
            }

            if (release)
            {
                Input::ClearInjected();
            }
            for (KeyCode key : keys)
            {
                Input::InjectKey(key, duration);
            }
            if (mouse_delta)
            {
                Input::InjectMouseMotion(*mouse_delta, duration);
            }
            if (wheel)
            {
                Input::InjectMouseWheel(*wheel);
            }

            return input_status_json();
        }

        std::string command_camera_set_view(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            Camera* camera = World::GetCamera();
            if (camera == nullptr || camera->GetEntity() == nullptr)
            {
                return json_error("camera not found");
            }

            Entity* entity = camera->GetEntity();

            // in play mode the eye is a child of the player capsule, moving only the eye gets undone by the
            // controller and leaves the body behind (streaming then unloads its chunk and it falls), so move the body
            Physics* player = nullptr;
            if (!is_edit_mode())
            {
                if (Entity* parent = entity->GetParent())
                {
                    Physics* physics = parent->GetComponent<Physics>();
                    if (physics && physics->GetBodyType() == BodyType::Controller)
                    {
                        player = physics;
                    }
                }
                if (!player && !Engine::IsFlagSet(EngineMode::Paused))
                {
                    return json_error("camera view changes require edit mode, paused play mode, or a player controller camera");
                }
            }

            if (const std::optional<std::string> position = get_argument(request, "position"))
            {
                math::Vector3 parsed;
                if (!parse_vector3(*position, parsed))
                {
                    return json_error("invalid position");
                }
                if (player)
                {
                    Entity* body            = entity->GetParent();
                    const math::Vector3 eye = player->GetControllerTopLocal();
                    const math::Vector3 capsule_position = parsed - body->GetRotation() * eye;
                    player->SetBodyTransform(capsule_position, body->GetRotation(), false);
                    body->SetPosition(capsule_position);
                    entity->SetPositionLocal(eye);
                }
                else
                {
                    entity->SetPosition(parsed);
                }
            }

            if (const std::optional<std::string> rotation_euler = get_argument(request, "rotation_euler"))
            {
                math::Vector3 parsed;
                if (!parse_vector3(*rotation_euler, parsed))
                {
                    return json_error("invalid rotation_euler");
                }
                entity->SetRotation(math::Quaternion::FromEulerAngles(parsed));
            }
            else
            {
                // look_at is an accepted alias for target
                std::optional<std::string> target = get_argument(request, "target");
                if (!target)
                {
                    target = get_argument(request, "look_at");
                }

                if (target)
                {
                    math::Vector3 parsed;
                    if (!parse_vector3(*target, parsed))
                    {
                        return json_error("invalid target");
                    }
                    const math::Vector3 direction = parsed - entity->GetPosition();
                    if (direction.LengthSquared() <= std::numeric_limits<float>::epsilon())
                    {
                        return json_error("target must differ from camera position");
                    }
                    entity->SetRotation(math::Quaternion::FromLookRotation(direction));
                }
            }

            return command_camera_snapshot();
        }

        std::string command_physics_state(const McpRequest& request)
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

            Physics* physics = entity->GetComponent<Physics>();
            if (physics == nullptr)
            {
                return json_error("physics component not found");
            }

            std::string json = "{\"ok\":true";
            json += ",\"entity\":" + entity_to_json_compact(entity);
            json += ",\"body_type\":" + json_string(body_type_to_name(physics->GetBodyType()));
            json += ",\"enabled\":" + json_bool(physics->IsEnabled());
            json += ",\"static\":" + json_bool(physics->IsStatic());
            json += ",\"kinematic\":" + json_bool(physics->IsKinematic());
            json += ",\"mass\":" + std::to_string(physics->GetMass());
            json += ",\"friction\":" + std::to_string(physics->GetFriction());
            json += ",\"friction_rolling\":" + std::to_string(physics->GetFrictionRolling());
            json += ",\"restitution\":" + std::to_string(physics->GetRestitution());
            json += ",\"center_of_mass\":" + json_vector3(physics->GetCenterOfMass());
            json += ",\"linear_velocity\":" + json_vector3(physics->GetLinearVelocity());
            json += ",\"grounded\":" + json_bool(physics->IsGrounded());
            if (Entity* ground = physics->GetGroundEntity())
            {
                json += ",\"ground_entity\":" + entity_to_json_compact(ground);
            }

            if (physics->GetBodyType() == BodyType::Custom)
            {
                json += ",\"vehicle\":{";
                json += "\"throttle\":" + std::to_string(CarPhysics::Get(*physics).GetVehicleThrottle());
                json += ",\"brake\":" + std::to_string(CarPhysics::Get(*physics).GetVehicleBrake());
                json += ",\"steering\":" + std::to_string(CarPhysics::Get(*physics).GetVehicleSteering());
                json += ",\"handbrake\":" + std::to_string(CarPhysics::Get(*physics).GetVehicleHandbrake());
                json += ",\"gear\":" + json_string(CarPhysics::Get(*physics).GetCurrentGearString());
                json += ",\"manual_shifting\":" + json_bool(CarPhysics::Get(*physics).GetManualTransmission());
                json += ",\"engine_rpm\":" + std::to_string(CarPhysics::Get(*physics).GetEngineRPM());
                json += ",\"boost_pressure\":" + std::to_string(CarPhysics::Get(*physics).GetBoostPressure());
                json += ",\"abs_active\":" + json_bool(CarPhysics::Get(*physics).IsAbsActiveAny());
                json += ",\"tc_active\":" + json_bool(CarPhysics::Get(*physics).IsTcActive());
                json += ",\"wheels\":[";
                for (uint32_t i = 0; i < static_cast<uint32_t>(WheelIndex::Count); i++)
                {
                    if (i != 0)
                    {
                        json += ",";
                    }
                    const WheelIndex wheel = static_cast<WheelIndex>(i);
                    json += "{";
                    json += "\"index\":" + std::to_string(i);
                    json += ",\"grounded\":" + json_bool(CarPhysics::Get(*physics).IsWheelGrounded(wheel));
                    json += ",\"compression\":" + std::to_string(CarPhysics::Get(*physics).GetWheelCompression(wheel));
                    json += ",\"slip_angle\":" + std::to_string(CarPhysics::Get(*physics).GetWheelSlipAngle(wheel));
                    json += ",\"slip_ratio\":" + std::to_string(CarPhysics::Get(*physics).GetWheelSlipRatio(wheel));
                    json += ",\"rpm\":" + std::to_string(CarPhysics::Get(*physics).GetWheelRPM(wheel));
                    json += ",\"temperature\":" + std::to_string(CarPhysics::Get(*physics).GetWheelTemperature(wheel));
                    json += ",\"wear\":" + std::to_string(CarPhysics::Get(*physics).GetWheelWear(wheel));
                    json += ",\"contact_point\":" + json_vector3(CarPhysics::Get(*physics).GetWheelContactPoint(wheel));
                    json += ",\"contact_normal\":" + json_vector3(CarPhysics::Get(*physics).GetWheelContactNormal(wheel));
                    json += "}";
                }
                json += "]}";
            }

            json += "}";
            return json;
        }

        std::string command_selection_update(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("selection update requires edit mode");
            }

            Camera* camera = World::GetCamera();
            if (camera == nullptr)
            {
                return json_error("camera not found");
            }

            const std::optional<std::string> action_arg = get_argument(request, "action");
            if (!action_arg)
            {
                return json_error("missing action");
            }

            const std::string action = to_lower_copy(*action_arg);
            if (action == "clear")
            {
                spartan::Selection::ClearSelection();
            }
            else if (action == "set_by_component")
            {
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

                spartan::Selection::ClearSelection();
                for (Entity* entity : World::GetEntities())
                {
                    if (entity != nullptr && entity->GetComponentByType(*type) != nullptr)
                    {
                        spartan::Selection::AddToSelection(entity);
                    }
                }
            }
            else
            {
                std::string error;
                Entity* entity = get_entity_from_request(request, error);
                if (entity == nullptr)
                {
                    return json_error(error);
                }

                if (action == "set")
                {
                    spartan::Selection::ClearSelection();
                    spartan::Selection::AddToSelection(entity);
                }
                else if (action == "add")
                {
                    spartan::Selection::AddToSelection(entity);
                }
                else if (action == "remove")
                {
                    spartan::Selection::RemoveFromSelection(entity);
                }
                else if (action == "toggle")
                {
                    spartan::Selection::ToggleSelection(entity);
                }
                else
                {
                    return json_error("unknown selection action");
                }
            }

            std::string json = "{\"ok\":true,\"selected_ids\":[";
            bool first = true;
            for (Entity* selected_entity : spartan::Selection::GetSelectedEntities())
            {
                if (selected_entity == nullptr)
                {
                    continue;
                }

                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += json_string(std::to_string(selected_entity->GetObjectId()));
            }
            json += "]}";
            return json;
        }

        std::string command_entity_clone(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("entity clone requires edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            Entity* parent = nullptr;
            if (const std::optional<std::string> parent_id = get_argument(request, "parent_id"))
            {
                if (!parent_id->empty() && *parent_id != "null" && *parent_id != "none" && *parent_id != "root" && *parent_id != "0")
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
                    if (parent == entity || parent->IsDescendantOf(entity))
                    {
                        return json_error("parent cannot be self or descendant");
                    }
                }
            }

            Entity* clone = entity->Clone();
            if (clone == nullptr)
            {
                return json_error("failed to clone entity");
            }

            if (const std::optional<std::string> name = get_argument(request, "name"))
            {
                clone->SetObjectName(*name);
            }

            if (get_argument(request, "parent_id"))
            {
                clone->SetParent(parent);
            }

            bool select = false;
            if (const std::optional<std::string> select_arg = get_argument(request, "select"))
            {
                if (!parse_bool(*select_arg, select))
                {
                    return json_error("invalid select");
                }
            }
            if (select)
            {
                if (Camera* camera = World::GetCamera())
                {
                    spartan::Selection::ClearSelection();
                    spartan::Selection::AddToSelection(clone);
                }
            }

            return "{\"ok\":true,\"entity\":" + entity_to_json(clone, true) + "}";
        }

        std::string command_entity_move_index(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("entity move requires edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            const std::optional<std::string> index_arg = get_argument(request, "index");
            if (!index_arg)
            {
                return json_error("missing index");
            }

            uint32_t index = 0;
            if (!parse_uint32(*index_arg, index))
            {
                return json_error("invalid index");
            }

            if (Entity* parent = entity->GetParent())
            {
                parent->MoveChildToIndex(entity, index);
            }
            else
            {
                World::MoveEntityToIndex(entity, index);
            }

            return "{\"ok\":true,\"entity\":" + entity_to_json_compact(entity) + "}";
        }

        std::string command_prefab_types()
        {
            std::vector<std::string> types = Prefab::GetRegisteredTypes();
            std::string json = "{\"ok\":true,\"types\":[";
            bool first = true;
            for (const std::string& type : types)
            {
                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += json_string(type);
            }
            json += "]}";
            return json;
        }

        std::string command_entity_make_game_ready(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("making an entity game ready requires edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            bool generate_lods = true;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "generate_lods")
            )
            {
                if (!parse_bool(*value, generate_lods))
                {
                    return json_error("generate_lods must be a boolean");
                }
            }

            // one mesh file for the whole hierarchy, named after what it came from unless asked otherwise
            const std::optional<std::string> requested_path =
                get_argument(request, "path");
            std::string path_error;
            const std::optional<std::string> resolved_path =
                resolve_mcp_output_path(
                    requested_path && !requested_path->empty()
                        ? *requested_path
                        : entity->GetObjectName() + "_merged",
                    "meshes",
                    EXTENSION_MESH,
                    path_error
                );
            if (!resolved_path)
            {
                return json_error(path_error);
            }

            const game_ready::MergeReport report =
                game_ready::MergeRenderersByMaterial(
                    entity,
                    *resolved_path,
                    generate_lods
                );
            if (!report.ok)
            {
                return json_error(report.error);
            }

            std::string json = "{\"ok\":true";
            json += ",\"mesh_path\":" + json_string(report.mesh_path);
            json += ",\"renderers_before\":" +
                std::to_string(report.renderers_before);
            json += ",\"renderers_after\":" +
                std::to_string(report.renderers_after);
            json += ",\"entities_removed\":" +
                std::to_string(report.entities_removed);
            json += ",\"vertices_before\":" +
                std::to_string(report.vertices_before);
            json += ",\"vertices_after\":" +
                std::to_string(report.vertices_after);
            json += ",\"indices_before\":" +
                std::to_string(report.indices_before);
            json += ",\"indices_after\":" +
                std::to_string(report.indices_after);

            json += ",\"merged\":[";
            for (size_t index = 0; index < report.groups.size(); index++)
            {
                if (index != 0)
                {
                    json += ",";
                }
                const game_ready::MergeGroup& group = report.groups[index];
                json += "{\"material\":" + json_string(group.material_name);
                json += ",\"entity\":" + json_string(group.entity_name);
                json += ",\"sub_mesh_index\":" +
                    std::to_string(group.sub_mesh_index);
                json += ",\"parts\":" + std::to_string(group.source_count);
                json += ",\"vertex_count\":" +
                    std::to_string(group.vertex_count);
                json += ",\"index_count\":" +
                    std::to_string(group.index_count);
                json += "}";
            }
            json += "]";

            // naming what was left alone is the whole value of the reply, a hierarchy that did not
            // collapse as far as expected always has a reason and it is usually fixable
            json += ",\"skipped\":[";
            for (size_t index = 0; index < report.skipped.size(); index++)
            {
                if (index != 0)
                {
                    json += ",";
                }
                json += "{\"entity\":" +
                    json_string(report.skipped[index].entity_name);
                json += ",\"reason\":" +
                    json_string(report.skipped[index].reason);
                json += "}";
            }
            json += "]";
            json += ",\"entity\":" + entity_to_json_compact(entity);
            json += "}";
            return json;
        }

        std::string command_prefab_save(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("prefab save requires edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            const std::optional<std::string> path = get_argument(request, "path");
            if (!path || path->empty())
            {
                return json_error("missing path");
            }
            std::string path_error;
            const std::optional<std::string> resolved_path =
                resolve_mcp_output_path(
                    *path,
                    "prefabs",
                    EXTENSION_PREFAB,
                    path_error
                );
            if (!resolved_path)
            {
                return json_error(path_error);
            }
            std::filesystem::create_directories(
                std::filesystem::path(
                    *resolved_path
                ).parent_path()
            );

            const bool saved = Prefab::SaveToFile(
                entity,
                *resolved_path
            );
            if (!saved)
            {
                return json_error("failed to save prefab");
            }

            // a save that reports success without leaving a file is worse than a failure, because the run
            // carries on believing the asset exists. the whole point of the prefab is the file
            if (!FileSystem::IsFile(*resolved_path))
            {
                return json_error(
                    "prefab reported saved but no file was written to " +
                    *resolved_path
                );
            }

            return
                "{\"ok\":true,\"path\":" +
                json_string(*resolved_path) +
                ",\"entity\":" +
                entity_to_json_compact(entity) +
                "}";
        }

        std::string command_prefab_load(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("prefab load requires edit mode");
            }

            const std::optional<std::string> path = get_argument(request, "path");
            if (!path || path->empty())
            {
                return json_error("missing path");
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

            if (parent == nullptr)
            {
                parent = World::CreateEntity();
                if (parent == nullptr)
                {
                    return json_error("failed to create prefab root");
                }
                parent->SetObjectName("prefab");
            }

            if (const std::optional<std::string> name = get_argument(request, "name"))
            {
                parent->SetObjectName(*name);
            }

            const bool loaded = Prefab::LoadFromFile(*path, parent);
            if (!loaded)
            {
                return json_error("failed to load prefab");
            }

            parent->SetPrefabFilePath(*path);
            parent->MarkPrefabBaseline();
            return "{\"ok\":true,\"path\":" + json_string(*path) + ",\"entity\":" + entity_to_json(parent, true) + "}";
        }

        std::string command_selection_get()
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            Camera* camera = World::GetCamera();
            if (camera == nullptr)
            {
                return json_error("camera not found");
            }

            std::string json = "{\"ok\":true,\"selected_ids\":[";
            bool first = true;
            for (Entity* entity : spartan::Selection::GetSelectedEntities())
            {
                if (entity == nullptr)
                {
                    continue;
                }

                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += json_string(std::to_string(entity->GetObjectId()));
            }
            json += "]}";
            return json;
        }

        std::string command_context_snapshot()
        {
            std::string json = "{\"ok\":true";
            json += ",\"status\":" + command_engine_status();
            json += ",\"world\":" + command_world_summary();
            json += ",\"selection\":" + command_selection_get();
            json += ",\"camera\":" + command_camera_snapshot();
            json += "}";
            return json;
        }

        std::string command_entity_resolve(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            bool selected = false;
            if (const std::optional<std::string> selected_arg = get_argument(request, "selected"))
            {
                if (!parse_bool(*selected_arg, selected))
                {
                    return json_error("invalid selected");
                }
            }

            if (selected)
            {
                Camera* camera = World::GetCamera();
                if (camera == nullptr)
                {
                    return json_error("camera not found");
                }

                std::vector<Entity*> selected_entities;
                for (Entity* entity : spartan::Selection::GetSelectedEntities())
                {
                    if (entity != nullptr)
                    {
                        selected_entities.emplace_back(entity);
                    }
                }

                if (selected_entities.empty())
                {
                    return json_error("nothing selected");
                }
                if (selected_entities.size() > 1)
                {
                    return json_error("multiple entities selected");
                }

                return "{\"ok\":true,\"entity\":" + entity_to_json_compact(selected_entities.front()) + ",\"source\":\"selection\"}";
            }

            if (const std::optional<std::string> id = get_argument(request, "id"))
            {
                std::string error;
                Entity* entity = get_entity_from_request(request, error);
                if (entity == nullptr)
                {
                    return json_error(error);
                }

                return "{\"ok\":true,\"entity\":" + entity_to_json_compact(entity) + ",\"source\":\"id\"}";
            }

            const std::optional<std::string> name = get_argument(request, "name");
            if (!name || name->empty())
            {
                return json_error("missing id, name, or selected");
            }

            std::string error;
            Entity* entity = find_entity_by_name_unique(*name, true, error);
            if (entity != nullptr)
            {
                return "{\"ok\":true,\"entity\":" + entity_to_json_compact(entity) + ",\"source\":\"name_exact\"}";
            }

            entity = find_entity_by_name_unique(*name, false, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            return "{\"ok\":true,\"entity\":" + entity_to_json_compact(entity) + ",\"source\":\"name_contains\"}";
        }

        std::string command_entity_create_empty(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("entity creation requires edit mode");
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

            if (const std::optional<std::string> parent_id = get_argument(request, "parent_id"))
            {
                uint64_t parsed_parent_id = 0;
                if (!parse_uint64(*parent_id, parsed_parent_id))
                {
                    return json_error("invalid parent_id");
                }

                Entity* parent = World::GetEntityById(parsed_parent_id);
                if (parent == nullptr)
                {
                    return json_error("parent entity not found");
                }

                entity->SetParent(parent);
            }

            if (
                const std::optional<std::string> position =
                    get_argument(request, "position")
            )
            {
                math::Vector3 parsed;
                if (!parse_vector3(*position, parsed))
                {
                    return json_error("invalid position");
                }
                entity->SetPositionLocal(parsed);
            }
            if (
                const std::optional<std::string> rotation_euler =
                    get_argument(request, "rotation_euler")
            )
            {
                math::Vector3 parsed;
                if (!parse_vector3(*rotation_euler, parsed))
                {
                    return json_error("invalid rotation_euler");
                }
                entity->SetRotationLocal(
                    math::Quaternion::FromEulerAngles(parsed)
                );
            }
            if (
                const std::optional<std::string> scale =
                    get_argument(request, "scale")
            )
            {
                math::Vector3 parsed;
                if (!parse_vector3(*scale, parsed))
                {
                    return json_error("invalid scale");
                }
                entity->SetScaleLocal(parsed);
            }
            if (
                const std::optional<std::string> transient =
                    get_argument(request, "transient")
            )
            {
                bool parsed = false;
                if (!parse_bool(*transient, parsed))
                {
                    return json_error("invalid transient");
                }
                entity->SetTransient(parsed);
            }
            if (
                const std::optional<std::string> active =
                    get_argument(request, "active")
            )
            {
                bool parsed = false;
                if (!parse_bool(*active, parsed))
                {
                    return json_error("invalid active");
                }
                entity->SetActive(parsed);
            }

            apply_entity_identity(entity, request);
            return "{\"ok\":true,\"entity\":" + entity_to_json_compact(entity) + "}";
        }

        math::Vector3 fallback_tangent(
            const math::Vector3& normal
        )
        {
            const math::Vector3 reference =
                std::abs(normal.y) < 0.999f
                ? math::Vector3(0.0f, 1.0f, 0.0f)
                : math::Vector3(1.0f, 0.0f, 0.0f);
            return math::Vector3::Cross(
                reference,
                normal
            ).Normalized();
        }

        std::string command_mesh_raw_create(
            const McpRequest& request
        )
        {
            constexpr size_t max_vertex_count = 100000;
            constexpr size_t max_index_count  = 300000;
            constexpr size_t max_payload_size = 16 * 1024 * 1024;

            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error(
                    "raw mesh creation requires edit mode"
                );
            }

            std::string path_error;
            const std::optional<std::string> path =
                resolve_mcp_mesh_path(request, path_error);
            if (!path)
            {
                return json_error(path_error);
            }

            const std::optional<std::string> positions_arg =
                get_argument(request, "positions");
            const std::optional<std::string> indices_arg =
                get_argument(request, "indices");
            if (!positions_arg || !indices_arg)
            {
                return json_error(
                    "positions and indices are required"
                );
            }

            size_t payload_size =
                positions_arg->size() +
                indices_arg->size();
            const std::optional<std::string> normals_arg =
                get_argument(request, "normals");
            const std::optional<std::string> uv0_arg =
                get_argument(request, "uv0");
            const std::optional<std::string> colors_arg =
                get_argument(request, "colors");
            if (normals_arg)
            {
                payload_size += normals_arg->size();
            }
            if (uv0_arg)
            {
                payload_size += uv0_arg->size();
            }
            if (colors_arg)
            {
                return json_error(
                    "colors are not supported by RHI_Vertex_PosTexNorTan"
                );
            }
            if (payload_size > max_payload_size)
            {
                return json_error(
                    "raw mesh payload exceeds 16 MiB"
                );
            }

            std::vector<float> position_values;
            std::vector<uint32_t> indices;
            if (
                !parse_float_array(
                    *positions_arg,
                    position_values,
                    max_vertex_count * 3
                ) ||
                position_values.size() < 9 ||
                position_values.size() % 3 != 0
            )
            {
                return json_error(
                    "positions must be finite comma-separated triples with 3 to 100000 vertices"
                );
            }
            if (
                !parse_index_array(
                    *indices_arg,
                    indices,
                    max_index_count
                ) ||
                indices.size() < 3 ||
                indices.size() % 3 != 0
            )
            {
                return json_error(
                    "indices must be comma-separated unsigned triangle indices with at most 300000 values"
                );
            }

            const size_t vertex_count =
                position_values.size() / 3;
            std::vector<float> normal_values;
            if (
                normals_arg &&
                (
                    !parse_float_array(
                        *normals_arg,
                        normal_values,
                        max_vertex_count * 3
                    ) ||
                    normal_values.size() != vertex_count * 3
                )
            )
            {
                return json_error(
                    "normals must contain one finite triple per vertex"
                );
            }

            std::vector<float> uv_values;
            if (
                uv0_arg &&
                (
                    !parse_float_array(
                        *uv0_arg,
                        uv_values,
                        max_vertex_count * 2
                    ) ||
                    uv_values.size() != vertex_count * 2
                )
            )
            {
                return json_error(
                    "uv0 must contain one finite pair per vertex"
                );
            }

            std::vector<math::Vector3> positions(vertex_count);
            std::vector<math::Vector3> normals(
                vertex_count,
                math::Vector3::Zero
            );
            std::vector<math::Vector3> tangents(
                vertex_count,
                math::Vector3::Zero
            );
            std::vector<math::Vector2> uv0(
                vertex_count,
                math::Vector2::Zero
            );
            for (size_t i = 0; i < vertex_count; i++)
            {
                positions[i] = math::Vector3(
                    position_values[i * 3],
                    position_values[i * 3 + 1],
                    position_values[i * 3 + 2]
                );
                if (normals_arg)
                {
                    normals[i] = math::Vector3(
                        normal_values[i * 3],
                        normal_values[i * 3 + 1],
                        normal_values[i * 3 + 2]
                    );
                    if (
                        normals[i].LengthSquared() <=
                        0.000000000001f
                    )
                    {
                        return json_error(
                            "normals cannot contain zero-length vectors"
                        );
                    }
                    normals[i].Normalize();
                }
                if (uv0_arg)
                {
                    uv0[i] = math::Vector2(
                        uv_values[i * 2],
                        uv_values[i * 2 + 1]
                    );
                }
            }

            for (size_t i = 0; i < indices.size(); i += 3)
            {
                const uint32_t index_a = indices[i];
                const uint32_t index_b = indices[i + 1];
                const uint32_t index_c = indices[i + 2];
                if (
                    index_a >= vertex_count ||
                    index_b >= vertex_count ||
                    index_c >= vertex_count
                )
                {
                    return json_error(
                        "an index is outside the vertex range"
                    );
                }
                if (
                    index_a == index_b ||
                    index_b == index_c ||
                    index_c == index_a
                )
                {
                    return json_error(
                        "indices contain a degenerate triangle"
                    );
                }

                const math::Vector3 edge_a =
                    positions[index_b] - positions[index_a];
                const math::Vector3 edge_b =
                    positions[index_c] - positions[index_a];
                const math::Vector3 face_normal =
                    math::Vector3::Cross(edge_a, edge_b);
                if (
                    face_normal.LengthSquared() <=
                    0.000000000001f
                )
                {
                    return json_error(
                        "positions contain a zero-area triangle"
                    );
                }
                if (!normals_arg)
                {
                    normals[index_a] += face_normal;
                    normals[index_b] += face_normal;
                    normals[index_c] += face_normal;
                }

                if (uv0_arg)
                {
                    const math::Vector2 delta_a =
                        uv0[index_b] - uv0[index_a];
                    const math::Vector2 delta_b =
                        uv0[index_c] - uv0[index_a];
                    const float determinant =
                        delta_a.x * delta_b.y -
                        delta_a.y * delta_b.x;
                    if (std::abs(determinant) > 0.00000001f)
                    {
                        const math::Vector3 tangent =
                            (
                                edge_a * delta_b.y -
                                edge_b * delta_a.y
                            ) /
                            determinant;
                        tangents[index_a] += tangent;
                        tangents[index_b] += tangent;
                        tangents[index_c] += tangent;
                    }
                }
            }

            std::vector<RHI_Vertex_PosTexNorTan> vertices;
            vertices.reserve(vertex_count);
            for (size_t i = 0; i < vertex_count; i++)
            {
                if (!normals_arg)
                {
                    if (
                        normals[i].LengthSquared() <=
                        0.000000000001f
                    )
                    {
                        return json_error(
                            "a vertex has no valid triangle normal"
                        );
                    }
                    normals[i].Normalize();
                }

                tangents[i] -=
                    normals[i] *
                    math::Vector3::Dot(
                        normals[i],
                        tangents[i]
                    );
                if (
                    tangents[i].LengthSquared() <=
                    0.000000000001f
                )
                {
                    tangents[i] = fallback_tangent(normals[i]);
                }
                else
                {
                    tangents[i].Normalize();
                }

                vertices.emplace_back(
                    positions[i],
                    uv0[i],
                    normals[i],
                    tangents[i]
                );
            }

            if (
                ResourceCache::GetByPath<Mesh>(*path) ||
                FileSystem::IsFile(*path)
            )
            {
                return json_error(
                    "mesh path already exists"
                );
            }

            const std::filesystem::path file_path(*path);
            if (file_path.has_parent_path())
            {
                std::filesystem::create_directories(
                    file_path.parent_path()
                );
            }

            std::shared_ptr<Mesh> mesh =
                std::make_shared<Mesh>();
            mesh->SetResourceFilePath(*path);
            mesh->SetFlag(
                static_cast<uint32_t>(
                    MeshFlags::PostProcessOptimize
                ),
                false
            );
            mesh->AddGeometry(vertices, indices, false);
            mesh->SaveToFile(*path);
            if (!FileSystem::IsFile(*path))
            {
                return json_error("failed to save raw mesh");
            }

            std::shared_ptr<Mesh> cached =
                ResourceCache::Cache(mesh);
            if (!cached)
            {
                return json_error("failed to cache raw mesh");
            }
            cached->CreateGpuBuffers();

            std::string json = "{\"ok\":true";
            json += ",\"path\":" + json_string(*path);
            json += ",\"vertex_count\":" +
                std::to_string(vertex_count);
            json += ",\"index_count\":" +
                std::to_string(indices.size());
            json += ",\"generated_normals\":" +
                json_bool(!normals_arg);
            json += ",\"generated_tangents\":true";
            json += ",\"channels\":[\"positions\",\"indices\",\"normals\",\"uv0\",\"tangents\"]";
            json += ",\"resource\":" +
                resource_to_json(cached.get());
            json += "}";
            return json;
        }

        std::string command_texture_generate(
            const McpRequest& request
        )
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error(
                    "texture generation requires edit mode"
                );
            }

            std::string path_error;
            std::optional<std::string> path =
                resolve_mcp_texture_path(request, path_error);
            if (!path)
            {
                return json_error(path_error);
            }
            const std::string requested_path = *path;

            const std::optional<std::string> layers_arg =
                get_argument(request, "layers");
            if (!layers_arg || layers_arg->empty())
            {
                return json_error("layers are required");
            }

            mcp_texture_kernel::request settings;
            settings.font_directory =
                ResourceCache::GetResourceDirectory(
                    ResourceDirectory::Fonts
                );

            // a value that will not parse is refused rather than left at the default, silently handing back
            // a 512 texture when 2048 was asked for produces an asset nobody ordered and no error to explain it
            std::string argument_error;
            auto read_uint = [&request, &argument_error](
                const char* key,
                uint32_t& target
            )
            {
                if (
                    const std::optional<std::string> value =
                        get_argument(request, key)
                )
                {
                    uint32_t parsed = 0;
                    if (!parse_uint32(*value, parsed))
                    {
                        argument_error = std::string(key) + " must be a whole number";
                        return;
                    }
                    target = parsed;
                }
            };
            auto read_float = [&request, &argument_error](
                const char* key,
                float& target
            )
            {
                if (
                    const std::optional<std::string> value =
                        get_argument(request, key)
                )
                {
                    float parsed = 0.0f;
                    if (!parse_float(*value, parsed))
                    {
                        argument_error = std::string(key) + " must be a number";
                        return;
                    }
                    target = parsed;
                }
            };

            read_uint("width", settings.width);
            read_uint("height", settings.height);
            read_uint("seed", settings.seed);
            read_float("normal_strength", settings.normal_strength);
            read_float("normal_bevel", settings.normal_bevel);
            read_float("base_roughness", settings.base_roughness);
            read_float("base_metalness", settings.base_metalness);
            if (!argument_error.empty())
            {
                return json_error(argument_error);
            }
            if (
                const std::optional<std::string> value =
                    get_argument(request, "seamless")
            )
            {
                settings.seamless = to_lower_copy(*value) != "false";
            }

            std::string error;
            if (
                !mcp_texture_kernel::request_from_json(
                    *layers_arg,
                    settings,
                    error
                )
            )
            {
                return json_error(error);
            }

            bool contributes_surface = false;
            for (
                const mcp_texture_kernel::layer& entry :
                settings.layers
            )
            {
                contributes_surface =
                    contributes_surface ||
                    entry.roughness_value >= 0.0f ||
                    entry.metalness_value >= 0.0f ||
                    entry.occlusion > 0.0f;
            }

            mcp_texture_kernel::result generated;
            if (
                !mcp_texture_kernel::generate(
                    settings,
                    generated,
                    error
                )
            )
            {
                return json_error(error);
            }

            const std::filesystem::path color_path(*path);
            if (color_path.has_parent_path())
            {
                std::filesystem::create_directories(
                    color_path.parent_path()
                );
            }

            // the resource cache keeps serving a texture it already loaded from a path, so writing
            // over that file changes nothing on screen while the response still reports fresh stats,
            // the maps move to the next free suffix instead and the response carries the real path
            const std::string requested_stem =
                (
                    color_path.parent_path() /
                    color_path.stem()
                ).generic_string();
            std::string stem = requested_stem;
            std::string written_path = *path;
            const auto any_map_cached = [](const std::string& candidate_stem)
            {
                for (
                    const char* suffix :
                    { ".png", "_normal.png", "_roughness.png", "_packed.png" }
                )
                {
                    if (
                        ResourceCache::GetByPath<RHI_Texture>(
                            candidate_stem + suffix
                        )
                    )
                    {
                        return true;
                    }
                }
                return false;
            };
            bool relocated = false;
            for (uint32_t attempt = 2; any_map_cached(stem); attempt++)
            {
                stem = requested_stem + "_" + std::to_string(attempt);
                written_path = stem + ".png";
                relocated = true;
                if (attempt > 999)
                {
                    return json_error(
                        "every texture path derived from " + *path +
                        " is already loaded, pick a new name"
                    );
                }
            }
            path = written_path;
            const std::string normal_path    = stem + "_normal.png";
            const std::string roughness_path = stem + "_roughness.png";
            const std::string packed_path    = stem + "_packed.png";

            const bool write_normal =
                generated.stats.relief_range > 0.0001f;
            const bool write_packed =
                contributes_surface || write_normal;

            ImageImporter::SaveSdrRgba8(
                *path,
                generated.width,
                generated.height,
                generated.albedo.data()
            );
            ImageImporter::SaveSdrRgba8(
                roughness_path,
                generated.width,
                generated.height,
                generated.roughness.data()
            );
            if (write_normal)
            {
                ImageImporter::SaveSdrRgba8(
                    normal_path,
                    generated.width,
                    generated.height,
                    generated.normal.data()
                );
            }
            if (write_packed)
            {
                ImageImporter::SaveSdrRgba8(
                    packed_path,
                    generated.width,
                    generated.height,
                    generated.packed.data()
                );
            }

            if (!FileSystem::Exists(*path))
            {
                return json_error(
                    "texture could not be written to " + *path
                );
            }
            if (!FileSystem::Exists(roughness_path))
            {
                return json_error(
                    "roughness texture could not be written to " +
                    roughness_path
                );
            }

            // wiring the maps here keeps the agent from creating a material and
            // forgetting to attach what it just generated
            std::string assigned_material;
            if (
                const std::optional<std::string> material_path =
                    get_argument(request, "material_path")
            )
            {
                if (!material_path->empty())
                {
                    std::shared_ptr<Material> material =
                        ResourceCache::GetByPath<Material>(
                            *material_path
                        );
                    if (!material && FileSystem::IsFile(*material_path))
                    {
                        material = ResourceCache::Load<Material>(
                            *material_path
                        );
                    }
                    if (!material)
                    {
                        return json_error(
                            "material_path could not be resolved: " +
                            *material_path
                        );
                    }

                    material->SetTexture(
                        MaterialTextureType::Color,
                        *path,
                        0
                    );
                    material->SetTexture(
                        MaterialTextureType::Roughness,
                        roughness_path,
                        0
                    );
                    if (write_normal)
                    {
                        material->SetTexture(
                            MaterialTextureType::Normal,
                            normal_path,
                            0
                        );
                    }
                    if (write_packed)
                    {
                        material->SetTexture(
                            MaterialTextureType::Packed,
                            packed_path,
                            0
                        );
                    }
                    assigned_material = *material_path;
                }
            }

            std::string json = "{\"ok\":true,\"path\":";
            json += json_string(*path);
            if (relocated)
            {
                json += ",\"requested_path\":" +
                    json_string(requested_path);
                json += ",\"note\":\"requested path was already loaded, maps were written to a new name and the material rebound to it\"";
            }
            json += ",\"normal_path\":" +
                json_string(write_normal ? normal_path : "");
            json += ",\"roughness_path\":" +
                json_string(roughness_path);
            json += ",\"packed_path\":" +
                json_string(write_packed ? packed_path : "");
            json += ",\"material_path\":" +
                json_string(assigned_material);
            json += ",\"width\":" +
                std::to_string(generated.width);
            json += ",\"height\":" +
                std::to_string(generated.height);
            json += ",\"seamless\":" +
                json_bool(settings.seamless);
            json += ",\"layer_count\":" +
                std::to_string(settings.layers.size());
            json += ",\"stats\":{";
            json += "\"mean_color\":[" +
                std::to_string(generated.stats.mean_r) + "," +
                std::to_string(generated.stats.mean_g) + "," +
                std::to_string(generated.stats.mean_b) + "]";
            json += ",\"mean_luminance\":" +
                std::to_string(generated.stats.mean_luminance);
            json += ",\"contrast\":" +
                std::to_string(generated.stats.contrast);
            json += ",\"coverage\":" +
                std::to_string(generated.stats.coverage);
            json += ",\"seam_error\":" +
                std::to_string(generated.stats.seam_error);
            json += ",\"relief_range\":" +
                std::to_string(generated.stats.relief_range);
            json += "}}";
            return json;
        }

        std::string command_mesh_raw_get(
            const McpRequest& request
        )
        {
            constexpr uint32_t max_output_vertices = 25000;
            constexpr uint32_t max_output_indices  = 75000;

            std::string path_error;
            const std::optional<std::string> path =
                resolve_mcp_mesh_path(request, path_error);
            if (!path)
            {
                return json_error(path_error);
            }

            std::shared_ptr<Mesh> mesh =
                ResourceCache::GetByPath<Mesh>(*path);
            if (!mesh && FileSystem::IsFile(*path))
            {
                mesh = ResourceCache::Load<Mesh>(*path);
            }
            if (!mesh)
            {
                return json_error(
                    "mesh was not found in the active world's mcp resource library"
                );
            }

            uint32_t sub_mesh_index = 0;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "sub_mesh")
            )
            {
                if (!parse_uint32(*value, sub_mesh_index))
                {
                    return json_error("invalid sub_mesh");
                }
            }
            if (sub_mesh_index >= mesh->GetSubMeshCount())
            {
                return json_error(
                    "sub_mesh is outside the mesh range"
                );
            }

            uint32_t vertex_limit = max_output_vertices;
            uint32_t index_limit  = max_output_indices;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "max_vertices")
            )
            {
                if (
                    !parse_uint32(*value, vertex_limit) ||
                    vertex_limit == 0 ||
                    vertex_limit > max_output_vertices
                )
                {
                    return json_error(
                        "max_vertices must be between 1 and 25000"
                    );
                }
            }
            if (
                const std::optional<std::string> value =
                    get_argument(request, "max_indices")
            )
            {
                if (
                    !parse_uint32(*value, index_limit) ||
                    index_limit == 0 ||
                    index_limit > max_output_indices
                )
                {
                    return json_error(
                        "max_indices must be between 1 and 75000"
                    );
                }
            }

            std::vector<RHI_Vertex_PosTexNorTan> vertices;
            std::vector<uint32_t> indices;
            mesh->GetGeometry(
                sub_mesh_index,
                &indices,
                &vertices
            );
            if (
                vertices.size() > vertex_limit ||
                indices.size() > index_limit
            )
            {
                std::string error =
                    "mesh output exceeds requested limits, vertex_count=" +
                    std::to_string(vertices.size()) +
                    ", index_count=" +
                    std::to_string(indices.size()) +
                    ", max_vertices=" +
                    std::to_string(vertex_limit) +
                    ", max_indices=" +
                    std::to_string(index_limit);
                return json_error(error);
            }

            std::ostringstream json;
            json << std::setprecision(
                std::numeric_limits<float>::max_digits10
            );
            json << "{\"ok\":true,\"path\":"
                 << json_string(*path)
                 << ",\"sub_mesh\":" << sub_mesh_index
                 << ",\"vertex_count\":" << vertices.size()
                 << ",\"index_count\":" << indices.size()
                 << ",\"truncated\":false";

            json << ",\"positions\":[";
            for (size_t i = 0; i < vertices.size(); i++)
            {
                if (i != 0)
                {
                    json << ",";
                }
                const math::Vector3 value =
                    vertices[i].get_position();
                json << value.x << "," << value.y << "," << value.z;
            }
            json << "],\"indices\":[";
            for (size_t i = 0; i < indices.size(); i++)
            {
                if (i != 0)
                {
                    json << ",";
                }
                json << indices[i];
            }
            json << "],\"normals\":[";
            for (size_t i = 0; i < vertices.size(); i++)
            {
                if (i != 0)
                {
                    json << ",";
                }
                const math::Vector3 value =
                    vertices[i].get_normal();
                json << value.x << "," << value.y << "," << value.z;
            }
            json << "],\"uv0\":[";
            for (size_t i = 0; i < vertices.size(); i++)
            {
                if (i != 0)
                {
                    json << ",";
                }
                const math::Vector2 value =
                    vertices[i].get_uv();
                json << value.x << "," << value.y;
            }
            json << "],\"tangents\":[";
            for (size_t i = 0; i < vertices.size(); i++)
            {
                if (i != 0)
                {
                    json << ",";
                }
                const math::Vector3 value =
                    vertices[i].get_tangent();
                json << value.x << "," << value.y << "," << value.z;
            }
            json << "],\"unsupported_channels\":[\"colors\"]}";
            return json.str();
        }

        std::string command_render_set_mesh(
            const McpRequest& request
        )
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("mesh assignment requires edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            const std::optional<std::string> mesh_arg =
                get_argument(request, "mesh");
            if (!mesh_arg || mesh_arg->empty())
            {
                return json_error("missing mesh");
            }

            std::shared_ptr<IResource> resource =
                get_resource_shared_by_name_or_path(
                    *mesh_arg,
                    ResourceType::Mesh
                );
            if (!resource && FileSystem::IsFile(*mesh_arg))
            {
                resource = ResourceCache::Load<Mesh>(*mesh_arg);
            }
            if (!resource)
            {
                return json_error(
                    "mesh not found by cached name, cached path, or file path"
                );
            }

            const std::shared_ptr<Mesh> mesh =
                std::static_pointer_cast<Mesh>(resource);
            uint32_t sub_mesh_index = 0;
            if (
                const std::optional<std::string> sub_mesh_arg =
                    get_argument(request, "sub_mesh_index")
            )
            {
                if (
                    !parse_uint32(
                        *sub_mesh_arg,
                        sub_mesh_index
                    ) ||
                    sub_mesh_index >= mesh->GetSubMeshCount()
                )
                {
                    return json_error("invalid sub_mesh_index");
                }
            }

            Render* render = entity->GetComponent<Render>();
            if (render == nullptr)
            {
                render = entity->AddComponent<Render>();
            }
            if (render == nullptr)
            {
                return json_error("failed to add render component");
            }

            render->SetMesh(
                mesh.get(),
                sub_mesh_index
            );
            if (render->GetMaterial() == nullptr)
            {
                render->SetDefaultMaterial();
            }

            if (
                const std::optional<std::string> material =
                    get_argument(request, "material")
            )
            {
                std::string material_error;
                if (
                    !assign_render_material(
                        render,
                        *material,
                        material_error
                    )
                )
                {
                    return json_error(material_error);
                }
            }

            std::string json = "{\"ok\":true";
            json += ",\"entity\":" +
                entity_to_json_compact(entity);
            json += ",\"mesh\":" +
                resource_to_json(mesh.get());
            json += ",\"sub_mesh_index\":" +
                std::to_string(sub_mesh_index);
            json += "}";
            return json;
        }

        std::string command_entity_create_primitive(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("primitive creation requires edit mode");
            }

            MeshType mesh_type = MeshType::Cube;
            if (const std::optional<std::string> mesh = get_argument(request, "mesh"))
            {
                const std::optional<MeshType> parsed = mesh_type_from_name(*mesh);
                if (!parsed)
                {
                    return json_error("invalid mesh");
                }

                mesh_type = *parsed;
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

            std::optional<math::Vector3> parsed_position;
            if (const std::optional<std::string> position = get_argument(request, "position"))
            {
                math::Vector3 parsed;
                if (!parse_vector3(*position, parsed))
                {
                    return json_error("invalid position");
                }
                parsed_position = parsed;
            }

            std::optional<math::Vector3> parsed_rotation_euler;
            if (const std::optional<std::string> rotation_euler = get_argument(request, "rotation_euler"))
            {
                math::Vector3 parsed;
                if (!parse_vector3(*rotation_euler, parsed))
                {
                    return json_error("invalid rotation_euler");
                }
                parsed_rotation_euler = parsed;
            }

            std::optional<math::Vector3> parsed_scale;
            if (const std::optional<std::string> scale = get_argument(request, "scale"))
            {
                math::Vector3 parsed;
                if (!parse_vector3(*scale, parsed))
                {
                    return json_error("invalid scale");
                }
                parsed_scale = parsed;
            }

            bool with_physics = true;
            if (const std::optional<std::string> with_physics_arg = get_argument(request, "with_physics"))
            {
                if (!parse_bool(*with_physics_arg, with_physics))
                {
                    return json_error("invalid with_physics");
                }
            }

            BodyType body_type = BodyType::Box;
            if (const std::optional<std::string> body_type_arg = get_argument(request, "body_type"))
            {
                const std::optional<BodyType> parsed = body_type_from_name(*body_type_arg);
                if (!parsed)
                {
                    return json_error("invalid body_type");
                }

                body_type = *parsed;
                with_physics = true;
            }
            else if (mesh_type == MeshType::Sphere)
            {
                body_type = BodyType::Sphere;
            }
            else if (mesh_type == MeshType::Quad)
            {
                body_type = BodyType::Plane;
            }
            else if (mesh_type == MeshType::Cylinder)
            {
                body_type = BodyType::Capsule;
            }
            std::optional<bool> physics_static = true;
            if (const std::optional<std::string> value = get_argument(request, "physics_static"))
            {
                bool parsed = false;
                if (!parse_bool(*value, parsed))
                {
                    return json_error("invalid physics_static");
                }

                physics_static = parsed;
                with_physics = true;
            }

            std::optional<bool> physics_kinematic;
            if (const std::optional<std::string> value = get_argument(request, "physics_kinematic"))
            {
                bool parsed = false;
                if (!parse_bool(*value, parsed))
                {
                    return json_error("invalid physics_kinematic");
                }

                physics_kinematic = parsed;
                with_physics = true;
            }

            std::optional<float> physics_mass;
            if (const std::optional<std::string> value = get_argument(request, "physics_mass"))
            {
                float parsed = 0.0f;
                if (!parse_float(*value, parsed))
                {
                    return json_error("invalid physics_mass");
                }

                physics_mass = parsed;
                with_physics = true;
            }

            std::optional<float> physics_friction;
            if (const std::optional<std::string> value = get_argument(request, "physics_friction"))
            {
                float parsed = 0.0f;
                if (!parse_float(*value, parsed))
                {
                    return json_error("invalid physics_friction");
                }

                physics_friction = parsed;
                with_physics = true;
            }

            std::optional<float> physics_restitution;
            if (const std::optional<std::string> value = get_argument(request, "physics_restitution"))
            {
                float parsed = 0.0f;
                if (!parse_float(*value, parsed))
                {
                    return json_error("invalid physics_restitution");
                }

                physics_restitution = parsed;
                with_physics = true;
            }

            Stopwatch step_timer;
            Entity* entity = World::CreateEntity();
            float step_ms = step_timer.GetElapsedTimeMs();
            if (step_ms > 500.0f)
            {
                SP_LOG_WARNING("MCP entity_create_primitive: World::CreateEntity took %.1f ms", step_ms);
            }
            if (entity == nullptr)
            {
                return json_error("failed to create entity");
            }

            entity->SetObjectName(get_argument(request, "name").value_or("primitive"));
            if (parent != nullptr)
            {
                entity->SetParent(parent);
            }
            if (parsed_position)
            {
                entity->SetPositionLocal(*parsed_position);
            }
            if (parsed_rotation_euler)
            {
                entity->SetRotationLocal(math::Quaternion::FromEulerAngles(*parsed_rotation_euler));
            }
            if (parsed_scale)
            {
                entity->SetScaleLocal(*parsed_scale);
            }

            step_timer.Start();
            Render* render = entity->AddComponent<Render>();
            step_ms = step_timer.GetElapsedTimeMs();
            if (step_ms > 500.0f)
            {
                SP_LOG_WARNING("MCP entity_create_primitive: AddComponent<Render> took %.1f ms", step_ms);
            }
            if (render == nullptr)
            {
                return json_error("failed to add render component");
            }
            step_timer.Start();
            render->SetMesh(mesh_type);
            step_ms = step_timer.GetElapsedTimeMs();
            if (step_ms > 500.0f)
            {
                SP_LOG_WARNING("MCP entity_create_primitive: Render::SetMesh took %.1f ms", step_ms);
            }
            step_timer.Start();
            render->SetDefaultMaterial();
            step_ms = step_timer.GetElapsedTimeMs();
            if (step_ms > 500.0f)
            {
                SP_LOG_WARNING("MCP entity_create_primitive: Render::SetDefaultMaterial took %.1f ms", step_ms);
            }
            if (const std::optional<std::string> material = get_argument(request, "material"))
            {
                std::string material_error;
                if (!assign_render_material(render, *material, material_error))
                {
                    return json_error(material_error);
                }
            }

            if (with_physics)
            {
                step_timer.Start();
                Physics* physics = entity->AddComponent<Physics>();
                step_ms = step_timer.GetElapsedTimeMs();
                if (step_ms > 500.0f)
                {
                    SP_LOG_WARNING("MCP entity_create_primitive: AddComponent<Physics> took %.1f ms", step_ms);
                }
                if (physics == nullptr)
                {
                    return json_error("failed to add physics component");
                }

                step_timer.Start();
                physics->SetBodyType(body_type);
                step_ms = step_timer.GetElapsedTimeMs();
                if (step_ms > 500.0f)
                {
                    SP_LOG_WARNING("MCP entity_create_primitive: Physics::SetBodyType(%s) took %.1f ms", body_type_to_name(body_type).c_str(), step_ms);
                }
                if (physics_static)
                {
                    physics->SetStatic(*physics_static);
                }
                if (physics_kinematic)
                {
                    physics->SetKinematic(*physics_kinematic);
                }
                if (physics_mass)
                {
                    physics->SetMass(*physics_mass);
                }
                if (physics_friction)
                {
                    physics->SetFriction(*physics_friction);
                }
                if (physics_restitution)
                {
                    physics->SetRestitution(*physics_restitution);
                }
            }

            apply_entity_identity(entity, request);
            return "{\"ok\":true,\"entity\":" + entity_to_json_compact(entity) + "}";
        }

        std::string command_entity_create_primitive_batch(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("primitive creation requires edit mode");
            }

            const std::optional<std::string> count_arg = get_argument(request, "count");
            uint64_t count = 0;
            if (!count_arg || !parse_uint64(*count_arg, count) || count == 0 || count > 64)
            {
                return json_error("count must be between 1 and 64");
            }

            const std::vector<std::string> keys =
            {
                "mesh",
                "name",
                "parent_id",
                "position",
                "rotation_euler",
                "scale",
                "material",
                "with_physics",
                "body_type",
                "physics_static",
                "physics_kinematic",
                "physics_mass",
                "physics_friction",
                "physics_restitution",
                "tags",
                "semantic_id",
                "plan_element",
                "semantic_tags"
            };

            std::string created_json = "[";
            uint32_t created_count = 0;
            for (uint64_t i = 0; i < count; i++)
            {
                McpRequest item_request;
                item_request.command = "entity_create_primitive";
                for (const std::string& key : keys)
                {
                    const std::string batch_key = "item_" + std::to_string(i) + "_" + key;
                    const auto it = request.arguments.find(batch_key);
                    if (it != request.arguments.end())
                    {
                        item_request.arguments[key] = it->second;
                    }
                }

                const std::string item_result = command_entity_create_primitive(item_request);
                if (!item_succeeded(item_result))
                {
                    return json_batch_failure(
                        "failed to create primitive batch item",
                        "created",
                        created_json,
                        created_count,
                        i,
                        item_result
                    );
                }

                if (created_count > 0)
                {
                    created_json += ",";
                }
                created_json += item_result;
                created_count++;
            }

            std::string json = "{\"ok\":true,\"created\":" + created_json + "]";
            json += ",\"created_count\":" + std::to_string(created_count);
            json += "}";
            return json;
        }

        std::vector<std::pair<std::string, std::string>>
        calibrated_light_create_properties(
            const LightType type
        )
        {
            if (type == LightType::Directional)
            {
                return {
                    { "temperature", "5500" },
                    { "color", "1,0.96,0.9,1" },
                    { "intensity", "120000" },
                    { "shadows", "true" },
                    { "volumetric", "false" }
                };
            }
            if (type == LightType::Area)
            {
                return {
                    { "temperature", "3200" },
                    { "color", "1,0.93,0.82,1" },
                    { "intensity", "12000" },
                    { "range", "40" },
                    { "area_width", "6" },
                    { "area_height", "3" },
                    { "shadows", "true" },
                    { "volumetric", "false" },
                    { "draw_distance", "80" },
                    { "shadow_distance", "60" }
                };
            }
            if (type == LightType::Spot)
            {
                return {
                    { "temperature", "3500" },
                    { "color", "1,0.94,0.84,1" },
                    { "intensity", "8500" },
                    { "range", "35" },
                    { "angle_degrees", "45" },
                    { "shadows", "true" },
                    { "volumetric", "false" },
                    { "draw_distance", "70" },
                    { "shadow_distance", "50" }
                };
            }
            return {
                { "temperature", "3200" },
                { "color", "1,0.92,0.78,1" },
                { "intensity", "8500" },
                { "range", "30" },
                { "shadows", "true" },
                { "volumetric", "false" },
                { "draw_distance", "60" },
                { "shadow_distance", "45" }
            };
        }

        float calibrated_light_intensity_floor(
            const LightType type
        )
        {
            if (type == LightType::Directional)
            {
                return 1000.0f;
            }
            if (type == LightType::Area)
            {
                return 1600.0f;
            }
            return 800.0f;
        }

        std::string command_entity_create_light(
            const McpRequest& request
        )
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("light creation requires edit mode");
            }

            LightType light_type = LightType::Point;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "light_type")
            )
            {
                const std::optional<LightType> parsed =
                    light_type_from_name(to_lower_copy(*value));
                if (!parsed)
                {
                    return json_error("invalid light_type");
                }
                light_type = *parsed;
            }

            bool calibrated = true;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "calibrated")
            )
            {
                if (!parse_bool(*value, calibrated))
                {
                    return json_error("invalid calibrated");
                }
            }

            Entity* parent = nullptr;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "parent_id")
            )
            {
                uint64_t parent_id = 0;
                if (!parse_uint64(*value, parent_id))
                {
                    return json_error("invalid parent_id");
                }
                parent = World::GetEntityById(parent_id);
                if (parent == nullptr)
                {
                    return json_error("parent entity not found");
                }
            }

            std::optional<math::Vector3> position;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "position")
            )
            {
                math::Vector3 parsed;
                if (!parse_vector3(*value, parsed))
                {
                    return json_error("invalid position");
                }
                position = parsed;
            }

            std::optional<math::Vector3> rotation_euler;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "rotation_euler")
            )
            {
                math::Vector3 parsed;
                if (!parse_vector3(*value, parsed))
                {
                    return json_error("invalid rotation_euler");
                }
                rotation_euler = parsed;
            }

            std::optional<math::Vector3> scale;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "scale")
            )
            {
                math::Vector3 parsed;
                if (!parse_vector3(*value, parsed))
                {
                    return json_error("invalid scale");
                }
                scale = parsed;
            }

            Entity* entity = World::CreateEntity();
            if (entity == nullptr)
            {
                return json_error("failed to create entity");
            }
            entity->SetObjectName(
                get_argument(request, "name").value_or("light")
            );
            if (parent != nullptr)
            {
                entity->SetParent(parent);
            }
            if (position)
            {
                entity->SetPositionLocal(*position);
            }
            if (rotation_euler)
            {
                entity->SetRotationLocal(
                    math::Quaternion::FromEulerAngles(
                        *rotation_euler
                    )
                );
            }
            if (scale)
            {
                entity->SetScaleLocal(*scale);
            }
            apply_entity_identity(entity, request);

            Light* light = entity->AddComponent<Light>();
            if (light == nullptr)
            {
                return json_error("failed to add light component");
            }

            std::string error;
            if (
                !set_light_property(
                    light,
                    "light_type",
                    light_type_to_name(light_type),
                    error
                )
            )
            {
                return json_error(error);
            }

            if (calibrated)
            {
                for (
                    const auto& [property, value] :
                    calibrated_light_create_properties(light_type)
                )
                {
                    if (
                        !set_light_property(
                            light,
                            property,
                            value,
                            error
                        )
                    )
                    {
                        return json_error(error);
                    }
                }
            }

            const std::array<std::string, 11> property_order =
            {
                "temperature",
                "color",
                "intensity",
                "range",
                "angle_degrees",
                "area_width",
                "area_height",
                "shadows",
                "volumetric",
                "draw_distance",
                "shadow_distance"
            };
            for (const std::string& property : property_order)
            {
                const std::optional<std::string> value =
                    get_argument(request, property);
                if (!value)
                {
                    continue;
                }
                if (
                    calibrated &&
                    property == "intensity"
                )
                {
                    float intensity = 0.0f;
                    if (!parse_float(*value, intensity))
                    {
                        return json_error("invalid intensity");
                    }
                    if (
                        intensity <
                        calibrated_light_intensity_floor(light_type)
                    )
                    {
                        continue;
                    }
                }
                if (
                    !set_light_property(
                        light,
                        property,
                        *value,
                        error
                    )
                )
                {
                    return json_error(error);
                }
            }
            if (
                const std::optional<std::string> value =
                    get_argument(request, "volumetric_distance")
            )
            {
                if (
                    !set_light_property(
                        light,
                        "volumetric_distance",
                        *value,
                        error
                    )
                )
                {
                    return json_error(error);
                }
            }

            return
                "{\"ok\":true,\"entity\":" +
                entity_to_json_compact(entity) +
                "}";
        }

        std::string command_entity_create_light_batch(
            const McpRequest& request
        )
        {
            const std::optional<std::string> count_arg =
                get_argument(request, "count");
            uint64_t count = 0;
            if (
                !count_arg ||
                !parse_uint64(*count_arg, count) ||
                count == 0 ||
                count > 64
            )
            {
                return json_error(
                    "count must be between 1 and 64"
                );
            }

            const std::vector<std::string> keys =
            {
                "name",
                "parent_id",
                "position",
                "rotation_euler",
                "scale",
                "light_type",
                "color",
                "temperature",
                "intensity",
                "range",
                "angle_degrees",
                "area_width",
                "area_height",
                "shadows",
                "volumetric",
                "draw_distance",
                "shadow_distance",
                "volumetric_distance",
                "calibrated",
                "tags",
                "semantic_id",
                "plan_element",
                "semantic_tags"
            };

            std::string created_json = "[";
            uint32_t created_count = 0;
            for (uint64_t i = 0; i < count; i++)
            {
                McpRequest item_request;
                item_request.command = "entity_create_light";
                for (const std::string& key : keys)
                {
                    const std::string batch_key =
                        "item_" +
                        std::to_string(i) +
                        "_" +
                        key;
                    const auto it =
                        request.arguments.find(batch_key);
                    if (it != request.arguments.end())
                    {
                        item_request.arguments[key] = it->second;
                    }
                }

                const std::string item_result =
                    command_entity_create_light(item_request);
                if (!item_succeeded(item_result))
                {
                    return json_batch_failure(
                        "failed to create light batch item",
                        "created",
                        created_json,
                        created_count,
                        i,
                        item_result
                    );
                }

                if (created_count > 0)
                {
                    created_json += ",";
                }
                created_json += item_result;
                created_count++;
            }

            std::string json =
                "{\"ok\":true,\"created\":" +
                created_json +
                "]";
            json += ",\"created_count\":" +
                std::to_string(created_count);
            json += "}";
            return json;
        }

        std::string command_entity_select(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("selection requires edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            Camera* camera = World::GetCamera();
            if (camera == nullptr)
            {
                return json_error("camera not found");
            }

            spartan::Selection::SetSelectedEntity(entity);
            return command_selection_get();
        }

        std::string command_entity_set_transform(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("transform requires edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            bool changed = false;

            if (const std::optional<std::string> position = get_argument(request, "position"))
            {
                math::Vector3 parsed;
                if (!parse_vector3(*position, parsed))
                {
                    return json_error("invalid position");
                }
                entity->SetPositionLocal(parsed);
                changed = true;
            }

            if (const std::optional<std::string> rotation = get_argument(request, "rotation"))
            {
                math::Quaternion parsed;
                if (!parse_quaternion(*rotation, parsed))
                {
                    return json_error("invalid rotation");
                }
                entity->SetRotationLocal(parsed);
                changed = true;
            }

            if (const std::optional<std::string> rotation_euler = get_argument(request, "rotation_euler"))
            {
                math::Vector3 parsed;
                if (!parse_vector3(*rotation_euler, parsed))
                {
                    return json_error("invalid rotation_euler");
                }
                entity->SetRotationLocal(math::Quaternion::FromEulerAngles(parsed));
                changed = true;
            }

            if (const std::optional<std::string> scale = get_argument(request, "scale"))
            {
                math::Vector3 parsed;
                if (!parse_vector3(*scale, parsed))
                {
                    return json_error("invalid scale");
                }
                entity->SetScaleLocal(parsed);
                changed = true;
            }

            if (!changed)
            {
                return json_error("no transform values provided");
            }

            return "{\"ok\":true,\"entity\":" + entity_to_json_compact(entity) + "}";
        }

        std::string command_entity_set_transform_batch(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("transform requires edit mode");
            }

            const std::optional<std::string> count_arg = get_argument(request, "count");
            uint64_t count = 0;
            if (!count_arg || !parse_uint64(*count_arg, count) || count == 0 || count > 64)
            {
                return json_error("count must be between 1 and 64");
            }

            const std::vector<std::string> keys =
            {
                "id",
                "position",
                "rotation",
                "rotation_euler",
                "scale"
            };

            std::string updated_json = "[";
            uint32_t updated_count = 0;
            for (uint64_t i = 0; i < count; i++)
            {
                McpRequest item_request;
                item_request.command = "entity_set_transform";
                for (const std::string& key : keys)
                {
                    const std::string batch_key = "item_" + std::to_string(i) + "_" + key;
                    const auto it = request.arguments.find(batch_key);
                    if (it != request.arguments.end())
                    {
                        item_request.arguments[key] = it->second;
                    }
                }

                const std::string item_result = command_entity_set_transform(item_request);
                if (!item_succeeded(item_result))
                {
                    return json_batch_failure(
                        "failed to set transform batch item",
                        "updated",
                        updated_json,
                        updated_count,
                        i,
                        item_result
                    );
                }

                if (updated_count > 0)
                {
                    updated_json += ",";
                }
                updated_json += item_result;
                updated_count++;
            }

            std::string json = "{\"ok\":true,\"updated\":" + updated_json + "]";
            json += ",\"updated_count\":" + std::to_string(updated_count);
            json += "}";
            return json;
        }

        std::string command_execute_lua(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("lua execution requires edit mode");
            }

            const std::optional<std::string> code = get_argument(request, "code");
            if (!code || code->empty())
            {
                return json_error("missing code");
            }

            sol::state_view lua = World::GetLuaState();
            sol::protected_function_result result = lua.safe_script(*code, sol::script_pass_on_error);
            if (!result.valid())
            {
                const sol::error error = result;
                return json_error(std::string("lua error, ") + error.what());
            }

            std::string json = "{\"ok\":true";
            const sol::object return_value = result;
            if (return_value.valid() && return_value.get_type() != sol::type::nil)
            {
                const sol::protected_function to_string = lua["tostring"];
                const sol::protected_function_result to_string_result = to_string(return_value);
                if (to_string_result.valid())
                {
                    const sol::optional<std::string> as_string = to_string_result;
                    if (as_string)
                    {
                        json += ",\"result\":" + json_string(*as_string);
                    }
                }
            }
            json += "}";
            return json;
        }
    }

    namespace
    {
        // commands registered by higher layers like the editor
        std::unordered_map<std::string, McpCommandHandler>& get_external_commands()
        {
            static std::unordered_map<std::string, McpCommandHandler> commands;
            return commands;
        }
    }

    void RegisterMcpCommand(const std::string& name, McpCommandHandler handler)
    {
        get_external_commands()[name] = std::move(handler);
    }

    void UnregisterMcpCommand(const std::string& name)
    {
        get_external_commands().erase(name);
    }

    std::string GetMcpProgressSnapshot()
    {
        const auto display = ProgressTracker::GetDisplay();
        std::string json = "{\"ok\":true,\"loading\":" + json_bool(ProgressTracker::IsLoading());
        json += ",\"active_count\":" + std::to_string(display.active_count) + ",\"tasks\":[";
        constexpr const char* types[] = { "world", "download", "model", "terrain", "texture" };
        for (uint32_t i = 0; i < display.count; ++i)
        {
            const auto& task = display.tasks[i];
            if (i) json += ",";
            json += "{\"id\":" + json_string(std::to_string(task.id));
            json += ",\"type\":" + json_string(types[static_cast<size_t>(task.type)]);
            json += ",\"title\":" + json_string(task.title);
            json += ",\"step\":" + json_string(task.step);
            json += ",\"detail\":" + json_string(task.detail);
            json += ",\"fraction\":" + (task.fraction < 0 ? std::string("null") : std::to_string(task.fraction));
            json += ",\"elapsed_seconds\":" + std::to_string(task.elapsed_seconds) + "}";
        }
        return json + "]}";
    }

    std::string ExecuteMcpCommand(const McpRequest& request)
    {
        // a table rather than a chain of comparisons, so a command is one line to add and the cost of
        // dispatch does not depend on where in the list it happens to sit
        static const std::unordered_map<std::string, std::string (*)(const McpRequest&)> commands =
        {
            { "ping",                          [](const McpRequest&) { return command_ping(); } },
            { "engine_status",                 [](const McpRequest&) { return command_engine_status(); } },
            { "progress_snapshot",             [](const McpRequest&) { return GetMcpProgressSnapshot(); } },
            { "world_work_snapshot", [](const McpRequest&)
                {
                    std::string json = "{\"ok\":true,\"world_tick\":" + std::to_string(World::GetWorkCounterTick());
                    const auto& counts = World::GetWorkCounters().values;
                    for (size_t i = 0; i < counts.size(); ++i)
                        json += ",\"" + std::string(world_work_names[i]) + "\":" + std::to_string(counts[i]);
                    return json + "}";
                }
            },
            { "engine_set_mode",               command_engine_set_mode },
            { "undo_redo",                     command_undo_redo },
            { "terrain_scatter_get",           command_terrain_scatter_get },
            { "terrain_scatter_set",           command_terrain_scatter_set },
            { "world_summary",                 [](const McpRequest&) { return command_world_summary(); } },
            { "world_load",                    command_world_load },
            { "world_save",                    command_world_save },
            { "world_resources_clean",         [](const McpRequest&) { return command_world_resources_clean(); } },
            { "world_resource_directory_get",  [](const McpRequest&) { return command_world_resource_directory_get(); } },
            { "world_set_environment",         command_world_set_environment },
            { "world_raycast",                 command_world_raycast },
            { "entity_snap",                   command_entity_snap },
            { "entity_spatial_snapshot",       command_entity_spatial_snapshot },
            { "entity_list",                   command_entity_list },
            { "entity_find",                   command_entity_find },
            { "entity_get",                    command_entity_get },
            { "selection_get",                 [](const McpRequest&) { return command_selection_get(); } },
            { "context_snapshot",              [](const McpRequest&) { return command_context_snapshot(); } },
            { "camera_snapshot",               [](const McpRequest&) { return command_camera_snapshot(); } },
            { "camera_set_view",               command_camera_set_view },
            { "input_inject",                  command_input_inject },
            { "engine_quit",                   command_engine_quit },
            { "screenshot_take",               command_screenshot_take },
            { "entity_resolve",                command_entity_resolve },
            { "primitive_types",               [](const McpRequest&) { return command_primitive_types(); } },
            { "entity_create_empty",           command_entity_create_empty },
            { "mesh_generate",                 mcp_mesh::command_mesh_generate },
            { "mesh_raw_create",               command_mesh_raw_create },
            { "mesh_raw_get",                  command_mesh_raw_get },
            { "texture_generate",              command_texture_generate },
            { "mesh_generate_batch",           mcp_mesh::command_mesh_generate_batch },
            { "render_set_mesh",               command_render_set_mesh },
            { "entity_create_primitive",       command_entity_create_primitive },
            { "entity_create_primitive_batch", command_entity_create_primitive_batch },
            { "entity_create_light",           command_entity_create_light },
            { "entity_create_light_batch",     command_entity_create_light_batch },
            { "entity_update",                 command_entity_update },
            { "entity_delete",                 command_entity_delete },
            { "entity_delete_children",        command_entity_delete_children },
            { "entity_select",                 command_entity_select },
            { "selection_update",              command_selection_update },
            { "entity_clone",                  command_entity_clone },
            { "entity_move_index",             command_entity_move_index },
            { "viewport_frame",                command_viewport_frame },
            { "entity_set_transform",          command_entity_set_transform },
            { "entity_set_transform_batch",    command_entity_set_transform_batch },
            { "entity_render_materials",       command_entity_render_materials },
            { "resource_list",                 command_resource_list },
            { "resource_load",                 command_resource_load },
            { "resource_reload",               command_resource_reload },
            { "resource_save",                 command_resource_save },
            { "resource_remove",               command_resource_remove },
            { "material_get",                  command_material_get },
            { "material_create",               command_material_create },
            { "material_set_property",         command_material_set_property },
            { "material_set_texture",          command_material_set_texture },
            { "material_apply_preset",         command_material_apply_preset },
            { "material_semantic_create",      command_material_semantic_create },
            { "physics_state",                 command_physics_state },
            { "prefab_types",                  [](const McpRequest&) { return command_prefab_types(); } },
            { "entity_make_game_ready",        command_entity_make_game_ready },
            { "prefab_save",                   command_prefab_save },
            { "prefab_load",                   command_prefab_load },
            // these live in McpCommandsWorldBuild.cpp
            { "spline_query",                  mcp_world_build::command_spline_query },
            { "spline_distribute",             mcp_world_build::command_spline_distribute },
            { "world_landmarks",               mcp_world_build::command_world_landmarks },
            { "spline_create_road",            mcp_world_build::command_spline_create_road },
            { "spline_set_control_points",     mcp_world_build::command_spline_set_control_points },
            { "spline_reroute",                mcp_world_build::command_spline_reroute },
            { "spline_connect",                mcp_world_build::command_spline_connect },
            { "spline_junction",               mcp_world_build::command_spline_junction },
            { "spline_decorate",               mcp_world_build::command_spline_decorate },
            { "district_blockout",             mcp_world_build::command_district_blockout },
            { "city_blockout",                 mcp_world_build::command_city_blockout },
            { "execute_lua",                   command_execute_lua },
        };

        const auto command = commands.find(request.command);
        if (command != commands.end())
        {
            return command->second(request);
        }

        const auto& external_commands = get_external_commands();
        const auto it = external_commands.find(request.command);
        if (it != external_commands.end())
        {
            return it->second(request);
        }

        return json_error("unknown command");
    }
}
