#/*
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
    enum class MaterialTextureType
    {
        Color,
        Roughness,
        Metalness,
        Normal,
        Occlusion,
        Emission,
        Height,
        AlphaMask,
        Packed, // occlusion, roughness, metalness, height
        Max
    };

    enum class MaterialProperty
    {
        CanBeEdited,          // indicates if the material properties can be modified
        WorldSpaceHeight,     // height of the mesh to which the material is applied
        Clearcoat,            // additional specular layer on top of the base specular
        Clearcoat_Roughness,  // roughness level of the clearcoat layer
        Anisotropic,          // controls the anisotropy level of specular reflections
        AnisotropicRotation,  // adjusts the anisotropy direction, with 1.0 being a full rotation
        Sheen,                // adds a soft, velvet-like reflection at edges
        SheenTint,            // blends sheen reflection between white and the base color
        ColorTint,            // modifies the surface color for diffuse or metallic materials
        ColorR,               // red component of the material color
        ColorG,               // green component of the material color
        ColorB,               // blue component of the material color
        ColorA,               // alpha (transparency) component of the material color
        Roughness,            // controls the roughness aspect of the surface reflection
        Metalness,            // defines the surface as dielectric or metallic
        Normal,               // normal map texture for simulating surface details
        Height,               // height map texture for surface tessellation
        Ior,                  // index of refraction for the material
        SubsurfaceScattering, // simulates light passing through translucent materials
        TextureTilingX,       // tiling factor of the texture along the X-axis
        TextureTilingY,       // tiling factor of the texture along the Y-axis
        TextureOffsetX,       // offset of the texture along the X-axis
        TextureOffsetY,       // offset of the texture along the Y-axis
        TextureSlopeBased,    // applies texture mapping based on the mesh slope
        VertexAnimateWind,    // applies vertex-based animation to simulate wind
        VertexAnimateWater,   // applies vertex-based animation to simulate water flow
        CullMode,             // sets the culling mode based on RHI_CullMode enum values
        Max                   // total number of properties, used to size arrays
    };

    enum class MaterialIor
    {
        Air,
        Water,
        Eyes,
        Glass,
        Sapphire,
        Diamond,
        Max
    };

    class Material : public IResource
    {
    public:
        Material();
        ~Material() = default;

        // iresource
        bool LoadFromFile(const std::string& file_path) override;
        bool SaveToFile(const std::string& file_path) override;

        // textures
        void SetTexture(const MaterialTextureType texture_type, RHI_Texture* texture, const uint8_t slot = 0);
        void SetTexture(const MaterialTextureType texture_type, std::shared_ptr<RHI_Texture> texture, const uint8_t slot = 0);
        void SetTexture(const MaterialTextureType texture_type, const std::string& file_path, const uint8_t slot = 0);
        bool HasTextureOfType(const std::string& path) const;
        bool HasTextureOfType(const MaterialTextureType texture_type) const;
        std::string GetTexturePathByType(const MaterialTextureType texture_type, const uint8_t slot = 0);
        std::vector<std::string> GetTexturePaths();
        RHI_Texture* GetTexture(const MaterialTextureType texture_type, const uint8_t slot = 0);


        // index of refraction
        static float EnumToIor(const MaterialIor ior);
        static MaterialIor IorToEnum(const float ior);

        // properties
        float GetProperty(const MaterialProperty property_type) const { return m_properties[static_cast<uint32_t>(property_type)]; }
        void SetProperty(const MaterialProperty property_type, const float value);
        void SetColor(const Color& color);
        bool IsTransparent() const { return GetProperty(MaterialProperty::ColorA) < 1.0f; }
        bool IsVisible()     const { return GetProperty(MaterialProperty::ColorA) > 0.0f; }
        bool IsAlphaTested();
        bool IsTessellated() const
        {
            // the gbuffer will tesselate in one of these cases
            return HasTextureOfType(MaterialTextureType::Height) || GetProperty(MaterialProperty::VertexAnimateWater);
        }

        // misc
        void PrepareForGPU();
        uint32_t GetUsedSlotCount() const;
        void SetIndex(const uint32_t index) { m_index = index; }
        uint32_t GetIndex() const           { return m_index; }

        static const uint32_t slots_per_texture_type = 4;

    private:
        std::array<RHI_Texture*, static_cast<uint32_t>(MaterialTextureType::Max) * slots_per_texture_type> m_textures;
        std::array<float, static_cast<uint32_t>(MaterialProperty::Max)> m_properties;
        uint32_t m_index = 0;
        std::shared_ptr<RHI_Texture> texture_packed = nullptr; // temporary
    };
}
