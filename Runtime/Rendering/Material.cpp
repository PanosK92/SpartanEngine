/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "Spartan.h"
#include "Material.h"
#include "Renderer.h"
#include "../Resource/ResourceCache.h"
#include "../IO/XmlDocument.h"
#include "../RHI/RHI_Texture2D.h"
#include "../RHI/RHI_TextureCube.h"
#include "../World/World.h"
//====================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    Material::Material(Context* context) : IResource(context, ResourceType::Material)
    {
        m_rhi_device = context->GetSubsystem<Renderer>()->GetRhiDevice();
        m_textures.fill(nullptr);
        m_properties.fill(1.0f);

        SetProperty(MaterialProperty::UvOffsetX, 0.0f);
        SetProperty(MaterialProperty::UvOffsetY, 0.0f);
    }

    bool Material::LoadFromFile(const string& file_path)
    {
        auto xml = make_unique<XmlDocument>();
        if (!xml->Load(file_path))
            return false;

        SetResourceFilePath(file_path);

        xml->GetAttribute("Material", "color_r",                         &m_properties[static_cast<uint32_t>(MaterialProperty::ColorR)]);
        xml->GetAttribute("Material", "color_g",                         &m_properties[static_cast<uint32_t>(MaterialProperty::ColorG)]);
        xml->GetAttribute("Material", "color_b",                         &m_properties[static_cast<uint32_t>(MaterialProperty::ColorB)]);
        xml->GetAttribute("Material", "color_a",                         &m_properties[static_cast<uint32_t>(MaterialProperty::ColorA)]);
        xml->GetAttribute("Material", "roughness_multiplier",            &m_properties[static_cast<uint32_t>(MaterialProperty::RoughnessMultiplier)]);
        xml->GetAttribute("Material", "metallic_multiplier",             &m_properties[static_cast<uint32_t>(MaterialProperty::MetallnessMultiplier)]);
        xml->GetAttribute("Material", "normal_multiplier",               &m_properties[static_cast<uint32_t>(MaterialProperty::NormalMultiplier)]);
        xml->GetAttribute("Material", "height_multiplier",               &m_properties[static_cast<uint32_t>(MaterialProperty::HeightMultiplier)]);
        xml->GetAttribute("Material", "clearcoat_multiplier",            &m_properties[static_cast<uint32_t>(MaterialProperty::Clearcoat)]);
        xml->GetAttribute("Material", "clearcoat_roughness_multiplier",  &m_properties[static_cast<uint32_t>(MaterialProperty::Clearcoat_Roughness)]);
        xml->GetAttribute("Material", "anisotropic_multiplier",          &m_properties[static_cast<uint32_t>(MaterialProperty::Anisotropic)]);
        xml->GetAttribute("Material", "anisotropic_rotation_multiplier", &m_properties[static_cast<uint32_t>(MaterialProperty::AnisotropicRotation)]);
        xml->GetAttribute("Material", "sheen_multiplier",                &m_properties[static_cast<uint32_t>(MaterialProperty::Sheen)]);
        xml->GetAttribute("Material", "sheen_tint_multiplier",           &m_properties[static_cast<uint32_t>(MaterialProperty::SheenTint)]);
        xml->GetAttribute("Material", "uv_tiling_x",                     &m_properties[static_cast<uint32_t>(MaterialProperty::UvTilingX)]);
        xml->GetAttribute("Material", "uv_tiling_y",                     &m_properties[static_cast<uint32_t>(MaterialProperty::UvTilingY)]);
        xml->GetAttribute("Material", "uv_offset_x",                     &m_properties[static_cast<uint32_t>(MaterialProperty::UvOffsetX)]);
        xml->GetAttribute("Material", "uv_offset_y",                     &m_properties[static_cast<uint32_t>(MaterialProperty::UvOffsetY)]);
        xml->GetAttribute("Material", "is_editable",                     &m_is_editable);

        const uint32_t texture_count = xml->GetAttributeAs<uint32_t>("textures", "count");
        for (uint32_t i = 0; i < texture_count; i++)
        {
            auto node_name                 = "texture_" + to_string(i);
            const MaterialTexture tex_type = static_cast<MaterialTexture>(xml->GetAttributeAs<uint32_t>(node_name, "texture_type"));
            auto tex_name                  = xml->GetAttributeAs<string>(node_name, "texture_name");
            auto tex_path                  = xml->GetAttributeAs<string>(node_name, "texture_path");

            // If the texture happens to be loaded, get a reference to it
            auto texture = m_context->GetSubsystem<ResourceCache>()->GetByName<RHI_Texture2D>(tex_name);
            // If there is not texture (it's not loaded yet), load it
            if (!texture)
            {
                texture = m_context->GetSubsystem<ResourceCache>()->Load<RHI_Texture2D>(tex_path);
            }

            SetTexture(tex_type, texture);
        }

        m_object_size_cpu = sizeof(*this);

        return true;
    }

    bool Material::SaveToFile(const string& file_path)
    {
        SetResourceFilePath(file_path);

        auto xml = make_unique<XmlDocument>();
        xml->AddNode("Material");
        xml->AddAttribute("Material", "color_r",                         GetProperty(MaterialProperty::ColorR));
        xml->AddAttribute("Material", "color_g",                         GetProperty(MaterialProperty::ColorG));
        xml->AddAttribute("Material", "color_b",                         GetProperty(MaterialProperty::ColorB));
        xml->AddAttribute("Material", "color_a",                         GetProperty(MaterialProperty::ColorA));
        xml->AddAttribute("Material", "roughness_multiplier",            GetProperty(MaterialProperty::RoughnessMultiplier));
        xml->AddAttribute("Material", "metallic_multiplier",             GetProperty(MaterialProperty::MetallnessMultiplier));
        xml->AddAttribute("Material", "normal_multiplier",               GetProperty(MaterialProperty::NormalMultiplier));
        xml->AddAttribute("Material", "height_multiplier",               GetProperty(MaterialProperty::HeightMultiplier));
        xml->AddAttribute("Material", "clearcoat_multiplier",            GetProperty(MaterialProperty::Clearcoat));
        xml->AddAttribute("Material", "clearcoat_roughness_multiplier",  GetProperty(MaterialProperty::Clearcoat_Roughness));
        xml->AddAttribute("Material", "anisotropic_multiplier",          GetProperty(MaterialProperty::Anisotropic));
        xml->AddAttribute("Material", "anisotropic_rotation_multiplier", GetProperty(MaterialProperty::AnisotropicRotation));
        xml->AddAttribute("Material", "sheen_multiplier",                GetProperty(MaterialProperty::Sheen));
        xml->AddAttribute("Material", "sheen_tint_multiplier",           GetProperty(MaterialProperty::SheenTint));
        xml->AddAttribute("Material", "uv_tiling_x",                     GetProperty(MaterialProperty::UvTilingX));
        xml->AddAttribute("Material", "uv_tiling_y",                     GetProperty(MaterialProperty::UvTilingY));
        xml->AddAttribute("Material", "uv_offset_x",                     GetProperty(MaterialProperty::UvOffsetX));
        xml->AddAttribute("Material", "uv_offset_y",                     GetProperty(MaterialProperty::UvOffsetY));
        xml->AddAttribute("Material", "is_editable",                     m_is_editable);

        xml->AddChildNode("Material", "textures");
        xml->AddAttribute("textures", "count", static_cast<uint32_t>(m_textures.size()));
        uint32_t i = 0;
        for (const auto& texture : m_textures)
        {
            auto tex_node = "texture_" + to_string(i);
            xml->AddChildNode("textures", tex_node);
            xml->AddAttribute(tex_node, "texture_type", i++);
            xml->AddAttribute(tex_node, "texture_name", texture ? texture->GetResourceName() : "");
            xml->AddAttribute(tex_node, "texture_path", texture ? texture->GetResourceFilePathNative() : "");
        }

        return xml->Save(GetResourceFilePathNative());
    }

    void Material::SetTexture(const MaterialTexture texture_type, const shared_ptr<RHI_Texture>& texture)
    {
        uint32_t type_int = static_cast<uint32_t>(texture_type);

        if (texture)
        {
            // Cache the texture to ensure scene serialization/deserialization
            m_textures[type_int] = m_context->GetSubsystem<ResourceCache>()->Cache(texture);
        }
        else
        {
            m_textures[type_int] = nullptr;
        }
    }

    void Material::SetTexture(const MaterialTexture type, const std::shared_ptr<RHI_Texture2D>& texture)
    {
        SetTexture(type, static_pointer_cast<RHI_Texture>(texture));
    }

    void Material::SetTexture(const MaterialTexture type, const std::shared_ptr<RHI_TextureCube>& texture)
    {
        SetTexture(type, static_pointer_cast<RHI_Texture>(texture));
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
        if (property_type == MaterialProperty::ColorA)
        {
            // If an object switches from opaque to transparent or vice versa, make the world update so that the renderer
            // goes through the entities and makes the ones that use this material, render in the correct mode.
            float current_alpha = m_properties[static_cast<uint32_t>(property_type)];
            if ((current_alpha != 1.0f && value == 1.0f) || (current_alpha == 1.0f && value != 1.0f))
            {
                m_context->GetSubsystem<World>()->Resolve();
            }
        }

        m_properties[static_cast<uint32_t>(property_type)] = value;
    }
}
