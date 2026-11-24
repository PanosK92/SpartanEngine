// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
// https://github.com/PanosK92/SpartanEngine/blob/master/license.md
// Commercial use requires written permission and negotiated payment terms.
#pragma once
#include "WorldSystems.h"
#include "resource/ResourceCache.h"
#include "rendering/Material.h"
#include "rhi/RHI_Texture.h"
namespace spartan::layered_road_surface
{
    // A world opts into a layered road finish for the splines under one parent; generic
    // spline materials keep their existing UV/profile contract, including editor test fixtures.
    inline bool Enabled(Entity* entity)
    {
        const world_systems::Config& config=world_systems::GetConfig();
        return !config.layered_roads_parent.empty() && entity && entity->GetParent()
            && entity->GetParent()->GetObjectName()==config.layered_roads_parent;
    }

    inline std::shared_ptr<Material> MaterialFor(int layer)
    {
        const world_systems::Config& config=world_systems::GetConfig();
        const char* suffixes[]={"_asphalt_baked","_road_paint_baked","_road_shoulder"};
        const std::string name=config.layered_roads_material_prefix+suffixes[layer];
        auto material=ResourceCache::GetByName<Material>(name);
        if (material) return material;
        material=std::make_shared<Material>();
        material->SetObjectName(name);

        std::filesystem::path directory=config.layered_roads_material_dir;
        std::string prefix, suffix;
        if (layer==2)
        {
            prefix="gravel_road_";
            suffix="_2k.jpg";
        }
        else
        {
            directory /= "baked";
            if (!std::filesystem::is_regular_file(directory / "asphalt_diff.jpg"))
                directory=std::filesystem::path(ResourceCache::GetResourceDirectory(ResourceDirectory::Textures)) / "island_roads";
            prefix=layer==0 ? "asphalt_" : "paint_";
            suffix=".jpg";
        }
        auto path_for = [&](const char* channel)
        {
            return (directory/(prefix+channel+suffix)).generic_string();
        };
        material->SetTexture(MaterialTextureType::Color, path_for("diff"));
        material->SetTexture(MaterialTextureType::Normal, path_for("nor_gl"));
        material->SetPackedSources(
            0,
            path_for("ao"),
            path_for("rough"),
            std::string{},
            layer != 2 ? path_for("height") : std::string{}
        );
        if (layer!=2)
        {
            const std::filesystem::path detail_dir = config.layered_roads_material_dir;
            if (std::filesystem::is_regular_file(detail_dir / "asphalt_track_diff_4k.jpg"))
            {
                material->SetTexture(MaterialTextureType::Color,  (detail_dir / "asphalt_track_diff_4k.jpg").generic_string(),   1);
                material->SetTexture(MaterialTextureType::Normal, (detail_dir / "asphalt_track_nor_gl_4k.jpg").generic_string(), 1);
                material->SetPackedSources(
                    1,
                    (detail_dir / "asphalt_track_ao_4k.jpg").generic_string(),
                    (detail_dir / "asphalt_track_rough_4k.jpg").generic_string(),
                    std::string{},
                    (detail_dir / "asphalt_track_height_4k.png").generic_string()
                );
            }
        }
        material->SetColor(layer==2 ? Color(.66f,.63f,.58f,1) : Color(1,1,1,1));
        material->SetProperty(MaterialProperty::Metalness,0);
        material->SetProperty(MaterialProperty::Roughness,1.0f);
        material->SetProperty(MaterialProperty::Normal,layer==2 ? .55f : 1.0f);
        if (layer!=2) material->SetProperty(MaterialProperty::Height,0.5f);
        material->SetProperty(MaterialProperty::TerrainBlend,layer==2 ? .8f : 0);
        material->SetProperty(MaterialProperty::TerrainCoating,layer==2 ? 1.0f : 0);
        material->SetProperty(MaterialProperty::IsRoadSurface,layer==0 ? 1.0f : 0.0f);
        material->SetProperty(MaterialProperty::IsRoadPaint,layer==1 ? 1.0f : 0.0f);
        material->SetProperty(MaterialProperty::CullMode,static_cast<float>(RHI_CullMode::None));
        return ResourceCache::Cache(material);
    }

}
