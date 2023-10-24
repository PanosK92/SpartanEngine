/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES =========================
#include "pch.h"
#include "Material.h"
#include "Renderer.h"
#include "../Resource/ResourceCache.h"
#include "../RHI/RHI_Texture2D.h"
#include "../RHI/RHI_TextureCube.h"
#include "../World/World.h"
SP_WARNINGS_OFF
#include "../IO/pugixml.hpp"
SP_WARNINGS_ON
//====================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    namespace
    {
        const char* material_property_to_char_ptr(MaterialProperty material_property)
        {
            switch (material_property)
            {
                case MaterialProperty::CanBeEdited:                     return "can_be_edited";
                case MaterialProperty::SingleTextureRoughnessMetalness: return "single_texture_roughness_metalness";
                case MaterialProperty::Clearcoat:                       return "clearcoat_multiplier";
                case MaterialProperty::Clearcoat_Roughness:             return "clearcoat_roughness_multiplier";
                case MaterialProperty::Anisotropic:                     return "anisotropic_multiplier";
                case MaterialProperty::AnisotropicRotation:             return "anisotropic_rotation_multiplier";
                case MaterialProperty::Sheen:                           return "sheen_multiplier";
                case MaterialProperty::SheenTint:                       return "sheen_tint_multiplier";
                case MaterialProperty::ColorTint:                       return "color_tint";
                case MaterialProperty::ColorR:                          return "color_r";
                case MaterialProperty::ColorG:                          return "color_g";
                case MaterialProperty::ColorB:                          return "color_b";
                case MaterialProperty::ColorA:                          return "color_a";
                case MaterialProperty::MultiplierRoughness:             return "multiplier_roughness";
                case MaterialProperty::MultiplierMetalness:             return "multiplier_metalness";
                case MaterialProperty::MultiplierNormal:                return "multiplier_normal";
                case MaterialProperty::MultiplierHeight:                return "multiplier_height";
                case MaterialProperty::TextureTilingX:                  return "texture_tiling_x";
                case MaterialProperty::TextureTilingY:                  return "texture_tiling_y";
                case MaterialProperty::TextureOffsetX:                  return "texture_offset_x";
                case MaterialProperty::TextureOffsetY:                  return "texture_offset_y";
                case MaterialProperty::TextureSlopeBased:               return "texture_slope_based";
                case MaterialProperty::TextureAnimate:                  return "texture_animate";
                case MaterialProperty::VertexAnimateWind:               return "vertex_animate_wind";
                case MaterialProperty::Undefined:                       return "undefined";
                default:
                {
                    SP_ASSERT_MSG(false, "Unknown material property");
                    return nullptr;
                }
            }
        }
     }

    Material::Material() : IResource(ResourceType::Material)
    {
        m_textures.fill(nullptr);
        m_properties.fill(0.0f);

        // initialize properties
        SetProperty(MaterialProperty::CanBeEdited,         1.0f);
        SetProperty(MaterialProperty::ColorR,              1.0f);
        SetProperty(MaterialProperty::ColorG,              1.0f);
        SetProperty(MaterialProperty::ColorB,              1.0f);
        SetProperty(MaterialProperty::ColorA,              1.0f);
        SetProperty(MaterialProperty::MultiplierRoughness, 1.0f);
        SetProperty(MaterialProperty::TextureTilingX,      1.0f);
        SetProperty(MaterialProperty::TextureTilingY,      1.0f);
    }

    bool Material::LoadFromFile(const std::string& file_path)
    {
        pugi::xml_document doc;
        if (!doc.load_file(file_path.c_str()))
        {
            SP_LOG_ERROR("Failed to load XML file");
            return false;
        }

        SetResourceFilePath(file_path);

        pugi::xml_node node_material = doc.child("Material");

        // load properties
        for (uint32_t i = 0; i < static_cast<uint32_t>(MaterialProperty::Undefined); ++i)
        {
            const char* attribute_name = material_property_to_char_ptr(static_cast<MaterialProperty>(i));
            m_properties[i] = node_material.child(attribute_name).text().as_float();
        }

        // load textures
        uint32_t texture_count = node_material.child("textures").attribute("count").as_uint();
        for (uint32_t i = 0; i < texture_count; ++i)
        {
            string node_name            = "texture_" + to_string(i);
            pugi::xml_node node_texture = node_material.child("textures").child(node_name.c_str());

            MaterialTexture tex_type = static_cast<MaterialTexture>(node_texture.attribute("texture_type").as_uint());
            string tex_name         = node_texture.attribute("texture_name").as_string();
            string tex_path         = node_texture.attribute("texture_path").as_string();

            // If the texture happens to be loaded, get a reference to it
            auto texture = ResourceCache::GetByName<RHI_Texture2D>(tex_name);
            // If there is not texture (it's not loaded yet), load it
            if (!texture)
            {
                texture = ResourceCache::Load<RHI_Texture2D>(tex_path);
            }

            SetTexture(tex_type, texture);
        }

        m_object_size_cpu = sizeof(*this);

        return true;
    }

    bool Material::SaveToFile(const string& file_path)
    {
        SetResourceFilePath(file_path);

        pugi::xml_document doc;
        pugi::xml_node materialNode = doc.append_child("Material");

        // save properties
        for (uint32_t i = 0; i < static_cast<uint32_t>(MaterialProperty::Undefined); ++i)
        {
            const char* attributeName = material_property_to_char_ptr(static_cast<MaterialProperty>(i));
            materialNode.append_child(attributeName).text().set(m_properties[i]);
        }

        // save textures
        pugi::xml_node texturesNode = materialNode.append_child("textures");
        texturesNode.append_attribute("count").set_value(static_cast<uint32_t>(m_textures.size()));

        for (uint32_t i = 0; i < m_textures.size(); ++i)
        {
            string node_name = "texture_" + to_string(i);
            pugi::xml_node textureNode = texturesNode.append_child(node_name.c_str());

            textureNode.append_attribute("texture_type").set_value(i);
            textureNode.append_attribute("texture_name").set_value(m_textures[i] ? m_textures[i]->GetObjectName().c_str() : "");
            textureNode.append_attribute("texture_path").set_value(m_textures[i] ? m_textures[i]->GetResourceFilePathNative().c_str() : "");
        }

        return doc.save_file(file_path.c_str());
    }

    void Material::SetTexture(const MaterialTexture texture_type, RHI_Texture* texture)
    {
        uint32_t type_int = static_cast<uint32_t>(texture_type);

        if (texture)
        {
            // cache the texture to ensure scene serialization/deserialization
            m_textures[type_int] = ResourceCache::Cache(texture->GetSharedPtr());
        }
        else
        {
            m_textures[type_int] = nullptr;
        }

        // Set the correct multiplier
        float multiplier = texture != nullptr;
        if (texture_type == MaterialTexture::Roughness)
        {
            SetProperty(MaterialProperty::MultiplierRoughness, multiplier);
        }
        else if (texture_type == MaterialTexture::Metalness)
        {
            SetProperty(MaterialProperty::MultiplierMetalness, multiplier);
        }
        else if (texture_type == MaterialTexture::Normal)
        {
            SetProperty(MaterialProperty::MultiplierNormal, multiplier);
        }
        else if (texture_type == MaterialTexture::Height)
        {
            SetProperty(MaterialProperty::MultiplierHeight, multiplier);
        }
    }

    void Material::SetTexture(const MaterialTexture texture_type, shared_ptr<RHI_Texture> texture)
    {
        SetTexture(texture_type, texture.get());
    }

    void Material::SetTexture(const MaterialTexture type, shared_ptr<RHI_Texture2D> texture)
    {
        SetTexture(type, static_pointer_cast<RHI_Texture>(texture));
    }

    void Material::SetTexture(const MaterialTexture type, shared_ptr<RHI_TextureCube> texture)
    {
        SetTexture(type, static_pointer_cast<RHI_Texture>(texture));
    }

    void Material::SetTexture(const MaterialTexture texture_type, const string& file_path)
    {
        SetTexture(texture_type, ResourceCache::Load<RHI_Texture2D>(file_path, RHI_Texture_Srv | RHI_Texture_Mips | RHI_Texture_Compressed));
    }
 
    bool Material::HasTexture(const string& path) const
    {
        for (const auto& texture : m_textures)
        {
            if (!texture)
                continue;

            if (texture->GetResourceFilePathNative() == path)
                return true;
        }

        return false;
    }

    bool Material::HasTexture(const MaterialTexture texture_type) const
    {
        return m_textures[static_cast<uint32_t>(texture_type)] != nullptr;
    }

    string Material::GetTexturePathByType(const MaterialTexture texture_type)
    {
        if (!HasTexture(texture_type))
            return "";

        return m_textures[static_cast<uint32_t>(texture_type)]->GetResourceFilePathNative();
    }

    vector<string> Material::GetTexturePaths()
    {
        vector<string> paths;
        for (const auto& texture : m_textures)
        {
            if (!texture)
                continue;

            paths.emplace_back(texture->GetResourceFilePathNative());
        }

        return paths;
    }

    RHI_Texture* Material::GetTexture(const MaterialTexture texture_type)
    {
        return GetTexture_PtrShared(texture_type).get();
    }

    shared_ptr<RHI_Texture>& Material::GetTexture_PtrShared(const MaterialTexture texture_type)
    {
        static shared_ptr<RHI_Texture> texture_empty;
        return HasTexture(texture_type) ? m_textures[static_cast<uint32_t>(texture_type)] : texture_empty;
    }

    void Material::SetProperty(const MaterialProperty property_type, const float value)
    {
        if (m_properties[static_cast<uint32_t>(property_type)] == value)
            return;

        if (property_type == MaterialProperty::ColorA)
        {
            // If an object switches from opaque to transparent or vice versa, make the world update so that the renderer
            // goes through the entities and makes the ones that use this material, render in the correct mode.
            float current_alpha = m_properties[static_cast<uint32_t>(property_type)];
            if ((current_alpha != 1.0f && value == 1.0f) || (current_alpha == 1.0f && value != 1.0f))
            {
                World::Resolve();
            }

            // Transparent objects are typically see-through (low roughness) so use the alpha as the roughness multiplier.
            m_properties[static_cast<uint32_t>(MaterialProperty::MultiplierRoughness)] = value * 0.5f;
        }

        m_properties[static_cast<uint32_t>(property_type)] = value;
    }

    void Material::SetColor(const Color& color)
    {
        SetProperty(MaterialProperty::ColorR, color.r);
        SetProperty(MaterialProperty::ColorG, color.g);
        SetProperty(MaterialProperty::ColorB, color.b);
        SetProperty(MaterialProperty::ColorA, color.a);
    }
}
