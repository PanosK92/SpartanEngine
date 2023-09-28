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

#pragma once

//= INCLUDES ======================
#include <memory>
#include <array>
#include "../RHI/RHI_Definitions.h"
#include "../Resource/IResource.h"
//=================================

namespace Spartan
{
    enum class MaterialTexture
    {
        Color,
        Color2,    // A second color for blending purposes
        Roughness, // Specifies microfacet roughness of the surface for diffuse and specular reflection
        Metalness, // Blends between a non-metallic and metallic material model
        Normal,
        Normal2,   // A second normal for blending purposes
        Occlusion, // A texture that will be mixed with ssao.
        Emission,  // A texture that will cause a surface to be lit, works nice with bloom.
        Height,    // Perceived depth for parallax mapping.
        AlphaMask, // A texture which will use pixel shader discards for transparent pixels.
        Undefined
    };

    enum class MaterialProperty
    {
        Clearcoat,           // Extra white specular layer on top of others
        Clearcoat_Roughness, // Roughness of clearcoat specular
        Anisotropic,         // Amount of anisotropy for specular reflection
        AnisotropicRotation, // Rotates the direction of anisotropy, with 1.0 going full circle
        Sheen ,              // Amount of soft velvet like reflection near edges
        SheenTint,           // Mix between white and using base color for sheen reflection
        ColorTint,           // Diffuse or metal surface color
        ColorR,
        ColorG,
        ColorB,
        ColorA,
        RoughnessMultiplier,
        MetalnessMultiplier,
        NormalMultiplier,
        HeightMultiplier,
        UvTilingX,
        UvTilingY,
        UvOffsetX,
        UvOffsetY,
        SingleTextureRoughnessMetalness,
        CanBeEdited,
        IsTerrain,
        IsWater,
        Undefined
    };

    class SP_CLASS Material : public IResource
    {
    public:
        Material();
        ~Material() = default;

        //= IResource ===========================================
        bool LoadFromFile(const std::string& file_path) override;
        bool SaveToFile(const std::string& file_path) override;
        //=======================================================

        //= TEXTURES  ================================================================================
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
        //============================================================================================
        
        float GetProperty(const MaterialProperty property_type) const { return m_properties[static_cast<uint32_t>(property_type)]; }
        void SetProperty(const MaterialProperty property_type, const float value);

        void SetColor(const Color& color);
 
    private:
        std::array<std::shared_ptr<RHI_Texture>, 11> m_textures;
        std::array<float, 23> m_properties;
    };
}
