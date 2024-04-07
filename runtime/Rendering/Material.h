/*
Copyright(c) 2016-2024 Panos Karabelas

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

#pragma once

//= INCLUDES ======================
#include <memory>
#include <array>
#include "../RHI/RHI_Definitions.h"
#include "../Resource/IResource.h"
//=================================

namespace Spartan
{
    // each texture type has multiple copies to allow for complex materials
    enum class MaterialTexture {
        Color,     Color2,     Color3,     Color4,
        Roughness, Roughness2, Roughness3, Roughness4,
        Metalness, Metalness2, Metalness3, Metalness4,
        Normal,    Normal2,    Normal3,    Normal4,
        Occlusion, Occlusion2, Occlusion3, Occlusion4,
        Emission,  Emission2,  Emission3,  Emission4,
        Height,    Height2,    Height3,    Height4,
        AlphaMask, AlphaMask2, AlphaMask3, AlphaMask4,
        Max
    };
    const uint32_t material_texture_slots_per_type = 4;

    enum class MaterialProperty
    {
        CanBeEdited,
        SingleTextureRoughnessMetalness,
        WorldSpaceHeight,    // height of the mesh the material is applied to
        Clearcoat,           // white specular layer on top of standard one
        Clearcoat_Roughness, // roughness of clearcoat specular
        Anisotropic,         // amount of anisotropy for specular reflection
        AnisotropicRotation, // rotates the direction of anisotropy, with 1.0 going full circle
        Sheen ,              // amount of soft velvet like reflection near edges
        SheenTint,           // mix between white and using base color for sheen reflection
        ColorTint,           // diffuse or metal surface color
        ColorR,
        ColorG,
        ColorB,
        ColorA,
        Roughness,
        Metalness,
        Normal,
        Height,
        Ior,
        SubsurfaceScattering,
        TextureTilingX,
        TextureTilingY,
        TextureOffsetX,
        TextureOffsetY,
        TextureSlopeBased,
        VertexAnimateWind,
        VertexAnimateWater,
        CullMode, // 0 - none, 1 - front, 2 - back - Same as RHI_CullMode
        Max
    };

    const uint32_t material_texture_type_count     = 8;
    const uint32_t material_texture_count_per_type = 4;
    const uint32_t material_texture_count_support  = static_cast<uint32_t>(MaterialTexture::Max);
    const uint32_t material_property_count         = static_cast<uint32_t>(MaterialTexture::Max);

    class SP_CLASS Material : public IResource
    {
    public:
        Material();
        ~Material() = default;

        // iresource
        bool LoadFromFile(const std::string& file_path) override;
        bool SaveToFile(const std::string& file_path) override;

        // textures
        void SetTexture(const MaterialTexture texture_type, RHI_Texture* texture);
        void SetTexture(const MaterialTexture texture_type, std::shared_ptr<RHI_Texture> texture);
        void SetTexture(const MaterialTexture texture_type, std::shared_ptr<RHI_Texture2D> texture);
        void SetTexture(const MaterialTexture texture_type, std::shared_ptr<RHI_TextureCube> texture);
        void SetTexture(const MaterialTexture texture_type, const std::string& file_path);
        bool HasTexture(const std::string& path) const;
        bool HasTexture(const MaterialTexture texture_type) const;
        std::string GetTexturePathByType(const MaterialTexture texture_type);
        std::vector<std::string> GetTexturePaths();
        RHI_Texture* GetTexture(const MaterialTexture texture_type);
        std::shared_ptr<RHI_Texture>& GetTexture_PtrShared(const MaterialTexture texturtexture_type);
        uint32_t GetArraySize();

        // properties
        float GetProperty(const MaterialProperty property_type) const { return m_properties[static_cast<uint32_t>(property_type)]; }
        void SetProperty(const MaterialProperty property_type, const float value);
        void SetColor(const Color& color);

        bool IsTessellated() const
        {
            return HasTexture(MaterialTexture::Height) ||
            HasTexture(MaterialTexture::Height2) ||
            HasTexture(MaterialTexture::Height3) ||
            HasTexture(MaterialTexture::Height4);
        }

        // index
        void SetIndex(const uint32_t index) { m_index = index; }
        uint32_t GetIndex() const           { return m_index; }

    private:
        std::array<std::shared_ptr<RHI_Texture>, material_texture_count_support> m_textures;
        std::array<float, material_property_count> m_properties;
        uint32_t m_index = 0;
    };
}
