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
#include "../Core/Spartan_Object.h"
//=================================

namespace Spartan
{	
	class ShaderVariation;

    enum RHI_Material_Property : uint16_t
	{
		RHI_Material_Unknown                = 0,
        RHI_Material_Clearcoat              = 1 << 0,   // Extra white specular layer on top of others
        RHI_Material_Clearcoat_Roughness    = 1 << 1,   // Roughness of clearcoat specular
        RHI_Material_Anisotropic            = 1 << 2,   // Amount of anisotropy for specular reflection
        RHI_Material_Anisotropic_Rotation   = 1 << 3,   // Rotates the direction of anisotropy, with 1.0 going full circle
        RHI_Material_Sheen                  = 1 << 4,   // Amount of soft velvet like reflection near edges
        RHI_Material_Sheen_Tint             = 1 << 5,   // Mix between white and using base color for sheen reflection
		RHI_Material_Color                  = 1 << 6,   // Diffuse or metal surface color
		RHI_Material_Roughness              = 1 << 7,   // Specifies microfacet roughness of the surface for diffuse and specular reflection
		RHI_Material_Metallic               = 1 << 8,   // Blends between a non-metallic and metallic material model
		RHI_Material_Normal                 = 1 << 9,   // Controls the normals of the base layers
		RHI_Material_Height                 = 1 << 10,  // Perceived depth for parallax mapping
		RHI_Material_Occlusion              = 1 << 11,  // Amount of light loss, can be complementary to SSAO
		RHI_Material_Emission               = 1 << 12,  // Light emission from the surface, works nice with bloom
		RHI_Material_Mask                   = 1 << 13   // Discards pixels
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

		//= TEXTURES  ===============================================================================================================
		void SetTextureSlot(const RHI_Material_Property type, const std::shared_ptr<RHI_Texture>& texture);
        void SetTextureSlot(const RHI_Material_Property type, const std::shared_ptr<RHI_Texture2D>& texture);
        void SetTextureSlot(const RHI_Material_Property type, const std::shared_ptr<RHI_TextureCube>& texture);
		bool HasTexture(const std::string& path) const;
        bool HasTexture(const RHI_Material_Property type) const { return m_texture_flags & type; }
		std::string GetTexturePathByType(RHI_Material_Property type);
		std::vector<std::string> GetTexturePaths();
		RHI_Texture* GetTexture_Ptr(const RHI_Material_Property type) { return HasTexture(type) ? m_textures[type].get() : nullptr; }
        std::shared_ptr<RHI_Texture>& GetTexture_PtrShared(const RHI_Material_Property type);
		//===========================================================================================================================

		//= SHADER =================================================================================
		std::shared_ptr<ShaderVariation> GetOrCreateShader(const uint16_t shader_flags);
		const std::shared_ptr<ShaderVariation>& GetShader() const { return m_shader; }
		bool HasShader()		                            const { return GetShader() != nullptr; }
		//==========================================================================================

		//= PROPERTIES =========================================================================================
		const auto& GetColorAlbedo()                                            const { return m_color_albedo; }
        void SetColorAlbedo(const Math::Vector4& color);
		
		const auto& GetTiling()                                                 const { return m_uv_tiling; }
		void SetTiling(const Math::Vector2& tiling)                             { m_uv_tiling = tiling; }

		const auto& GetOffset()                                                 const { return m_uv_offset; }
		void SetOffset(const Math::Vector2& offset)                             { m_uv_offset = offset; }

		auto IsEditable() const                                                 { return m_is_editable; }
		void SetIsEditable(const bool is_editable)                              { m_is_editable = is_editable; }

		auto& GetProperty(const RHI_Material_Property type)                     { return m_properties[type];}
		void SetProperty(const RHI_Material_Property type, const float value)   { m_properties[type] = value; }
		//======================================================================================================

	private:
		Math::Vector4 m_color_albedo	= Math::Vector4(1.0f, 1.0f, 1.0f, 1.0f);
		Math::Vector2 m_uv_tiling		= Math::Vector2(1.0f, 1.0f);
		Math::Vector2 m_uv_offset		= Math::Vector2(0.0f, 0.0f);
		bool m_is_editable				= true;
        uint16_t m_texture_flags        = 0;
		std::unordered_map<RHI_Material_Property, std::shared_ptr<RHI_Texture>> m_textures;
		std::unordered_map<RHI_Material_Property, float> m_properties;
		std::shared_ptr<ShaderVariation> m_shader;	
		std::shared_ptr<RHI_Device> m_rhi_device;
	};
}
