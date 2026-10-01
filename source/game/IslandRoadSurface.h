// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
// https://github.com/PanosK92/SpartanEngine/blob/master/license.md
// Commercial use requires written permission and negotiated payment terms.
#pragma once
#include "GameWorld.h"
#include "resource/ResourceCache.h"
#include "rendering/Material.h"
#include "rhi/RHI_Texture.h"
namespace spartan::island_road_surface
{
    // The island opts into a layered road finish; generic spline materials keep
    // their existing UV/profile contract, including editor test fixtures.
    inline bool Enabled(Entity* entity)
    {
        return game::GetIslandFeatures() && entity && entity->GetParent()
            && entity->GetParent()->GetObjectName()=="roads";
    }

    inline std::shared_ptr<Material> MaterialFor(int layer)
    {
        const char* names[]={"island_asphalt","island_road_paint","island_road_shoulder"};
        auto material=ResourceCache::GetByName<Material>(names[layer]);
        if (material) return material;
        material=std::make_shared<Material>();
        material->SetObjectName(names[layer]);
        const std::string base="project/materials/island_roads/";
        const std::string asset=layer==2 ? "gravel_road" : "asphalt_track";
        const std::string size=layer==2 ? "2k" : "4k";
        auto texture=[&](MaterialTextureType type,const char* channel,const char* extension=".jpg")
        {
            material->SetTexture(type,base+asset+"_"+channel+"_"+size+extension);
        };
        // Paint samples the asphalt diffuse untinted, road_weathering composes it over the
        // asphalt where the paint is worn or ragged. Both share the height for parallax.
        texture(MaterialTextureType::Color,"diff");
        texture(MaterialTextureType::Normal,"nor_gl");
        texture(MaterialTextureType::Roughness,"rough");
        texture(MaterialTextureType::Occlusion,"ao");
        if (layer!=2) texture(MaterialTextureType::Height,"height",".png");
        material->SetColor(layer==1 ? Color(1,1,1,1) :
            layer==2 ? Color(.66f,.63f,.58f,1) : Color(1.6f,1.65f,1.7f,1));
        material->SetProperty(MaterialProperty::Metalness,0);
        material->SetProperty(MaterialProperty::Roughness,1.0f);
        material->SetProperty(MaterialProperty::Normal,layer==2 ? .55f : 1.0f);
        // POM depth is height * 0.04 uv, 1.5 m per uv, so ~7 mm of aggregate relief.
        if (layer!=2) material->SetProperty(MaterialProperty::Height,.12f);
        material->SetProperty(MaterialProperty::TerrainBlend,layer==2 ? .8f : 0);
        material->SetProperty(MaterialProperty::TerrainCoating,0);
        material->SetProperty(MaterialProperty::IsRoadSurface,layer==0 ? 1.0f : 0.0f);
        material->SetProperty(MaterialProperty::IsRoadPaint,layer==1 ? 1.0f : 0.0f);
        material->SetProperty(MaterialProperty::CullMode,static_cast<float>(RHI_CullMode::None));
        return material;
    }

}
