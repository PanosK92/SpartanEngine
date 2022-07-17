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
    }

    bool Material::LoadFromFile(const string& file_path)
    {
        auto xml = make_unique<XmlDocument>();
        if (!xml->Load(file_path))
            return false;

        SetResourceFilePath(file_path);

        xml->GetAttribute("Material", "Color",                           &m_color_albedo);
        xml->GetAttribute("Material", "Roughness_Multiplier",            &GetProperty(MaterialProperty::RoughnessMultiplier));
        xml->GetAttribute("Material", "Metallic_Multiplier",             &GetProperty(MaterialProperty::MetallnessMultiplier));
        xml->GetAttribute("Material", "Normal_Multiplier",               &GetProperty(MaterialProperty::NormalMultiplier));
        xml->GetAttribute("Material", "Height_Multiplier",               &GetProperty(MaterialProperty::HeightMultiplier));
        xml->GetAttribute("Material", "Clearcoat_Multiplier",            &GetProperty(MaterialProperty::Clearcoat));
        xml->GetAttribute("Material", "Clearcoat_Roughness_Multiplier",  &GetProperty(MaterialProperty::Clearcoat_Roughness));
        xml->GetAttribute("Material", "Anisotropi_Multiplier",           &GetProperty(MaterialProperty::Anisotropic));
        xml->GetAttribute("Material", "Anisotropic_Rotation_Multiplier", &GetProperty(MaterialProperty::AnisotropicRotation));
        xml->GetAttribute("Material", "Sheen_Multiplier",                &GetProperty(MaterialProperty::Sheen));
        xml->GetAttribute("Material", "Sheen_Tint_Multiplier",           &GetProperty(MaterialProperty::SheenTint));
        xml->GetAttribute("Material", "IsEditable",                      &m_is_editable);
        xml->GetAttribute("Material", "UV_Tiling",                       &m_uv_tiling);
        xml->GetAttribute("Material", "UV_Offset",                       &m_uv_offset);

        const uint32_t texture_count = xml->GetAttributeAs<uint32_t>("Textures", "Count");
        for (uint32_t i = 0; i < texture_count; i++)
        {
            auto node_name                 = "Texture_" + to_string(i);
            const MaterialTexture tex_type = static_cast<MaterialTexture>(xml->GetAttributeAs<uint32_t>(node_name, "Texture_Type"));
            auto tex_name                  = xml->GetAttributeAs<string>(node_name, "Texture_Name");
            auto tex_path                  = xml->GetAttributeAs<string>(node_name, "Texture_Path");

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
        xml->AddAttribute("Material", "Color",                           m_color_albedo);
        xml->AddAttribute("Material", "Roughness_Multiplier",            GetProperty(MaterialProperty::RoughnessMultiplier));
        xml->AddAttribute("Material", "Metallic_Multiplier",             GetProperty(MaterialProperty::MetallnessMultiplier));
        xml->AddAttribute("Material", "Normal_Multiplier",               GetProperty(MaterialProperty::NormalMultiplier));
        xml->AddAttribute("Material", "Height_Multiplier",               GetProperty(MaterialProperty::HeightMultiplier));
        xml->AddAttribute("Material", "Clearcoat_Multiplier",            GetProperty(MaterialProperty::Clearcoat));
        xml->AddAttribute("Material", "Clearcoat_Roughness_Multiplier",  GetProperty(MaterialProperty::Clearcoat_Roughness));
        xml->AddAttribute("Material", "Anisotropi_Multiplier",           GetProperty(MaterialProperty::Anisotropic));
        xml->AddAttribute("Material", "Anisotropic_Rotation_Multiplier", GetProperty(MaterialProperty::AnisotropicRotation));
        xml->AddAttribute("Material", "Sheen_Multiplier",                GetProperty(MaterialProperty::Sheen));
        xml->AddAttribute("Material", "Sheen_Tint_Multiplier",           GetProperty(MaterialProperty::SheenTint));
        xml->AddAttribute("Material", "UV_Tiling",                       m_uv_tiling);
        xml->AddAttribute("Material", "UV_Offset",                       m_uv_offset);
        xml->AddAttribute("Material", "IsEditable",                      m_is_editable);

        xml->AddChildNode("Material", "Textures");
        xml->AddAttribute("Textures", "Count", static_cast<uint32_t>(m_textures.size()));
        uint32_t i = 0;
        for (const auto& texture : m_textures)
        {
            auto tex_node = "Texture_" + to_string(i);
            xml->AddChildNode("Textures", tex_node);
            xml->AddAttribute(tex_node, "Texture_Type", i++);
            xml->AddAttribute(tex_node, "Texture_Name", texture ? texture->GetResourceName() : "");
            xml->AddAttribute(tex_node, "Texture_Path", texture ? texture->GetResourceFilePathNative() : "");
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

    void Material::SetColorAlbedo(const Math::Vector4& color)
    {
        // If an object switches from opaque to transparent or vice versa, make the world update so that the renderer
        // goes through the entities and makes the ones that use this material, render in the correct mode.
        if ((m_color_albedo.w != 1.0f && color.w == 1.0f) || (m_color_albedo.w == 1.0f && color.w != 1.0f))
        {
            m_context->GetSubsystem<World>()->Resolve();
        }

        m_color_albedo = color;
    }
}
