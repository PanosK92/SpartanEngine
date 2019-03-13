/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES =====================
#include <vector>
#include <memory>
#include "../RHI/RHI_Definition.h"
#include "../Resource/IResource.h"
#include "../Math/Vector2.h"
#include "../Math/Vector4.h"
//================================

namespace Directus
{	
	class ShaderPool;
	class ShaderVariation;
	class TexturePool;

	enum TextureType
	{
		TextureType_Unknown,
		TextureType_Albedo,
		TextureType_Roughness,
		TextureType_Metallic,
		TextureType_Normal,
		TextureType_Height,
		TextureType_Occlusion,
		TextureType_Emission,
		TextureType_Mask
	};

	struct TextureSlot
	{
		TextureSlot()
		{
			type = TextureType_Unknown;
		}

		TextureSlot(const TextureType type, const std::shared_ptr<RHI_Texture>& ptr)
		{
			this->ptr	= ptr;
			this->type	= type;
		}

		std::shared_ptr<RHI_Texture> ptr;
		TextureType type;
	};

	class ENGINE_CLASS Material : public IResource
	{
	public:
		enum ShadingMode
		{
			Shading_Sky,
			Shading_PBR	
		};

		Material(Context* context);
		~Material();

		//= IResource ===========================================
		bool LoadFromFile(const std::string& file_path) override;
		bool SaveToFile(const std::string& file_path) override;
		//=======================================================

		//= TEXTURE SLOTS  ================================================================
		const TextureSlot& GetTextureSlotByType(TextureType type);
		void SetTextureSlot(TextureType type, const std::shared_ptr<RHI_Texture>& texture);
		bool HasTexture(TextureType type);
		bool HasTexture(const std::string& path);
		std::string GetTexturePathByType(TextureType type);
		std::vector<std::string> GetTexturePaths();
		//=================================================================================

		//= SHADER ====================================================================
		void AcquireShader();
		std::shared_ptr<ShaderVariation> GetOrCreateShader(unsigned long shader_flags);
		std::shared_ptr<ShaderVariation> GetShader() const { return m_shader; }
		bool HasShader() const { return GetShader() != nullptr; }
		void SetMultiplier(TextureType type, float value);
		//=============================================================================

		//= PROPERTIES ============================================================================
		RHI_Cull_Mode GetCullMode() const					{ return m_cull_mode; }
		void SetCullMode(const RHI_Cull_Mode cull_mode)		{ m_cull_mode = cull_mode; }

		float& GetRoughnessMultiplier()						{ return m_roughness_multiplier; }
		void SetRoughnessMultiplier(const float roughness)	{ m_roughness_multiplier = roughness; }

		float GetMetallicMultiplier() const					{ return m_metallic_multiplier; }
		void SetMetallicMultiplier(const float metallic)	{ m_metallic_multiplier = metallic; }

		float GetNormalMultiplier() const					{ return m_normal_multiplier; }
		void SetNormalMultiplier(const float normal)		{ m_normal_multiplier = normal; }

		float GetHeightMultiplier() const					{ return m_height_multiplier; }
		void SetHeightMultiplier(const float height)		{ m_height_multiplier = height; }

		ShadingMode GetShadingMode() const					{ return m_shading_mode; }
		void SetShadingMode(const ShadingMode shading_mode)	{ m_shading_mode = shading_mode; }

		const Math::Vector4& GetColorAlbedo() const			{ return m_color_albedo; }
		void SetColorAlbedo(const Math::Vector4& color)		{ m_color_albedo = color; }

		const Math::Vector2& GetTiling() const				{ return m_uv_tiling; }
		void SetTiling(const Math::Vector2& tiling)			{ m_uv_tiling = tiling; }

		const Math::Vector2& GetOffset() const				{ return m_uv_offset; }
		void SetOffset(const Math::Vector2& offset)			{ m_uv_offset = offset; }

		bool IsEditable() const { return m_is_editable; }
		void SetIsEditable(const bool is_editable)			{ m_is_editable = is_editable; }
		//=========================================================================================

		static TextureType TextureTypeFromString(const std::string& type);

	private:
		void TextureBasedMultiplierAdjustment();

		RHI_Cull_Mode m_cull_mode;
		ShadingMode m_shading_mode;
		Math::Vector4 m_color_albedo;
		float m_roughness_multiplier;
		float m_metallic_multiplier;
		float m_normal_multiplier;
		float m_height_multiplier;
		Math::Vector2 m_uv_tiling;
		Math::Vector2 m_uv_offset;	
		bool m_is_editable;
		std::shared_ptr<ShaderVariation> m_shader;
		std::vector<TextureSlot> m_texture_slots;
		TextureSlot m_empty_texture_slot;
		std::shared_ptr<RHI_Device> m_rhi_device;
	};
}