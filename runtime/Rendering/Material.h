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
        Color2,    // a second color for blending purposes
        Roughness, // specifies microfacet roughness of the surface for diffuse and specular reflection
        Metalness, // blends between a non-metallic and metallic material model
        Normal,
        Normal2,   // a second normal for blending purposes
        Occlusion, // a texture that will be mixed with ssgi.
        Emission,  // a texture that will cause a surface to be lit, works nice with bloom.
        Height,    // perceived depth for parallax mapping.
        AlphaMask, // a texture which will use pixel shader discards for transparent pixels.
        Undefined
    };

    enum class MaterialProperty
    {
        CanBeEdited,
        SingleTextureRoughnessMetalness,
        WorldSpaceHeight,    // height of the mesh the material is applied to
        Clearcoat,           // extra white specular layer on top of others
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
        MultiplierRoughness,
        MultiplierMetalness,
        MultiplierNormal,
        MultiplierHeight,
        TextureTilingX,
        TextureTilingY,
        TextureOffsetX,
        TextureOffsetY,
        TextureSlopeBased,
        VertexAnimateWind,
        VertexAnimateWater,
        Undefined
    };

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

        // properties
        float GetProperty(const MaterialProperty property_type) const { return m_properties[static_cast<uint32_t>(property_type)]; }
        void SetProperty(const MaterialProperty property_type, const float value);

        // properties - color
        void SetColor(const Color& color);
 
    private:
        std::array<std::shared_ptr<RHI_Texture>, 11> m_textures;
        std::array<float, 26> m_properties;
    };
}
