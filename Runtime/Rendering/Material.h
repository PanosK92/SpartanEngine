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

	class SPARTAN_CLASS Material : public IResource
	{
	public:
		Material(Context* context);
        ~Material() = default;

		//= IResource ===========================================
		bool LoadFromFile(const std::string& file_path) override;
		bool SaveToFile(const std::string& file_path) override;
		//=======================================================

		//= TEXTURES  ======================================================================================================
		void SetTextureSlot(const Texture_Type type, const std::shared_ptr<RHI_Texture>& texture);
        void SetTextureSlot(const Texture_Type type, const std::shared_ptr<RHI_Texture2D>& texture);
        void SetTextureSlot(const Texture_Type type, const std::shared_ptr<RHI_TextureCube>& texture);
		bool HasTexture(const std::string& path) const;
        bool HasTexture(const Texture_Type type) const { return m_texture_flags & type; }
		std::string GetTexturePathByType(Texture_Type type);
		std::vector<std::string> GetTexturePaths();
		RHI_Texture* GetTexture_Ptr(const Texture_Type type) { return HasTexture(type) ? m_textures[type].get() : nullptr; }
        std::shared_ptr<RHI_Texture>& GetTexture_PtrShared(const Texture_Type type);
		//==================================================================================================================

		//= SHADER =================================================================================
		std::shared_ptr<ShaderVariation> GetOrCreateShader(const uint8_t shader_flags);
		const std::shared_ptr<ShaderVariation>& GetShader() const { return m_shader; }
		bool HasShader()		                            const { return GetShader() != nullptr; }
		//==========================================================================================

		//= PROPERTIES ==========================================================================================
		const auto& GetColorAlbedo() const									{ return m_color_albedo; }
        void SetColorAlbedo(const Math::Vector4& color);
		
		const auto& GetTiling() const										{ return m_uv_tiling; }
		void SetTiling(const Math::Vector2& tiling)							{ m_uv_tiling = tiling; }

		const auto& GetOffset() const										{ return m_uv_offset; }
		void SetOffset(const Math::Vector2& offset)							{ m_uv_offset = offset; }

		auto IsEditable() const { return m_is_editable; }
		void SetIsEditable(const bool is_editable)							{ m_is_editable = is_editable; }

		auto& GetMultiplier(const Texture_Type type)                        { return m_multipliers[type];}
		void SetMultiplier(const Texture_Type type, const float multiplier)	{ m_multipliers[type] = multiplier; }

		static Texture_Type TextureTypeFromString(const std::string& type);
		//=======================================================================================================

	private:
		Math::Vector4 m_color_albedo	= Math::Vector4(1.0f, 1.0f, 1.0f, 1.0f);
		Math::Vector2 m_uv_tiling		= Math::Vector2(1.0f, 1.0f);
		Math::Vector2 m_uv_offset		= Math::Vector2(0.0f, 0.0f);
		bool m_is_editable				= true;
        uint8_t m_texture_flags         = 0;
		std::unordered_map<Texture_Type, std::shared_ptr<RHI_Texture>> m_textures;
		std::unordered_map<Texture_Type, float> m_multipliers;
		std::shared_ptr<ShaderVariation> m_shader;	
		std::shared_ptr<RHI_Device> m_rhi_device;
	};
}
