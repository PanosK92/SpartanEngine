/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include <unordered_map>
#include "../RHI/RHI_Definition.h"
#include "../Resource/IResource.h"
#include "../Math/Vector2.h"
#include "../Math/Vector4.h"
//=================================

namespace Spartan
{
    // Should I split these into properties and multipliers ?
    enum Material_Property : uint16_t
    {
        Material_Unknown                = 0,
        Material_Clearcoat              = 1 << 0,   // Extra white specular layer on top of others
        Material_Clearcoat_Roughness    = 1 << 1,   // Roughness of clearcoat specular
        Material_Anisotropic            = 1 << 2,   // Amount of anisotropy for specular reflection
        Material_Anisotropic_Rotation   = 1 << 3,   // Rotates the direction of anisotropy, with 1.0 going full circle
        Material_Sheen                  = 1 << 4,   // Amount of soft velvet like reflection near edges
        Material_Sheen_Tint             = 1 << 5,   // Mix between white and using base color for sheen reflection
        Material_Color                  = 1 << 6,   // Diffuse or metal surface color
        Material_Roughness              = 1 << 7,   // Specifies microfacet roughness of the surface for diffuse and specular reflection
        Material_Metallic               = 1 << 8,   // Blends between a non-metallic and metallic material model
        Material_Normal                 = 1 << 9,   // Controls the normals of the base layers
        Material_Height                 = 1 << 10,  // Perceived depth for parallax mapping
        Material_Occlusion              = 1 << 11,  // Amount of light loss, can be complementary to SSAO
        Material_Emission               = 1 << 12,  // Light emission from the surface, works nice with bloom
        Material_Mask                   = 1 << 13   // Discards pixels
    };

    class SPARTAN_CLASS Material : public IResource
    {
    public:
        Material(Context* context);
        ~Material() = default;

        //= IResource ===========================================
        bool LoadFromFile(const std::string& file_path) override;
        bool SaveToFile(const std::string& file_path) override;
        //=======================================================

        //= TEXTURES  ===========================================================================================================
        void SetTextureSlot(const Material_Property type, const std::shared_ptr<RHI_Texture>& texture, float multiplier = 1.0f);
        void SetTextureSlot(const Material_Property type, const std::shared_ptr<RHI_Texture2D>& texture);
        void SetTextureSlot(const Material_Property type, const std::shared_ptr<RHI_TextureCube>& texture);
        bool HasTexture(const std::string& path) const;
        bool HasTexture(const Material_Property type) const { return m_flags & type; }
        std::string GetTexturePathByType(Material_Property type);
        std::vector<std::string> GetTexturePaths();
        RHI_Texture* GetTexture_Ptr(const Material_Property type) { return HasTexture(type) ? m_textures[type].get() : nullptr; }
        std::shared_ptr<RHI_Texture>& GetTexture_PtrShared(const Material_Property type);
        //=======================================================================================================================
        
        //= PROPERTIES =====================================================================================
        const Math::Vector4& GetColorAlbedo()                               const { return m_color_albedo; }
        void SetColorAlbedo(const Math::Vector4& color);

        const Math::Vector2& GetTiling()                                    const { return m_uv_tiling; }
        void SetTiling(const Math::Vector2& tiling)                         { m_uv_tiling = tiling; }

        const Math::Vector2& GetOffset()                                    const { return m_uv_offset; }
        void SetOffset(const Math::Vector2& offset)                         { m_uv_offset = offset; }

        auto IsEditable()                                                   const { return m_is_editable; }
        void SetIsEditable(const bool is_editable)                          { m_is_editable = is_editable; }

        auto& GetProperty(const Material_Property type)                     { return m_properties[type]; }
        void SetProperty(const Material_Property type, const float value)   { m_properties[type] = value; }

        uint16_t GetFlags()                                                 const { return m_flags; }
        //==================================================================================================

    private:
        Math::Vector4 m_color_albedo    = Math::Vector4(1.0f, 1.0f, 1.0f, 1.0f);
        Math::Vector2 m_uv_tiling        = Math::Vector2(1.0f, 1.0f);
        Math::Vector2 m_uv_offset        = Math::Vector2(0.0f, 0.0f);
        bool m_is_editable                = true;
        uint16_t m_flags                = 0;
        std::unordered_map<Material_Property, std::shared_ptr<RHI_Texture>> m_textures;
        std::unordered_map<Material_Property, float> m_properties;
        std::shared_ptr<RHI_Device> m_rhi_device;
    };
}
