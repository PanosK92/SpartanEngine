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
#include <memory>
#include <map>
#include "../RHI/RHI_Definition.h"
#include "../Resource/IResource.h"
#include "../Math/Vector2.h"
#include "../Math/Vector4.h"
#include "../Math/Vector3.h"
//================================

namespace Spartan
{	
	class ShaderVariation;

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

	enum ShadingMode
	{
		Shading_Sky,
		Shading_PBR
	};

	class SPARTAN_CLASS Material : public IResource
	{
	public:
		Material(Context* context);
		~Material();

		//= IResource ===========================================
		bool LoadFromFile(const std::string& file_path) override;
		bool SaveToFile(const std::string& file_path) override;
		//=======================================================

		//= TEXTURES  ==================================================================================================
		void SetTextureSlot(const TextureType type, const std::shared_ptr<RHI_Texture>& texture);
		void SetTextureSlot(const TextureType type, const std::shared_ptr<RHI_Texture2D>& texture);
		void SetTextureSlot(const TextureType type, const std::shared_ptr<RHI_TextureCube>& texture);
		auto GetResources() const { return m_resources; }
		bool HasTexture(const std::string& path);
		std::string GetTexturePathByType(TextureType type);
		std::vector<std::string> GetTexturePaths();
		bool HasTexture(const TextureType type) { return m_textures.find(type) != m_textures.end(); }
		const auto& GetTexture(const TextureType type) { return HasTexture(type) ? m_textures[type] : m_texture_empty; }
		//==============================================================================================================

		//= SHADER ====================================================================
		void AcquireShader();
		std::shared_ptr<ShaderVariation> GetOrCreateShader(unsigned long shader_flags);
		const auto& GetShader() const { return m_shader; }
		auto HasShader()		const { return GetShader() != nullptr; }
		//=============================================================================

		//= CONSTANT BUFFER ===================================================
		bool UpdateConstantBuffer();
		const auto& GetConstantBuffer() const { return m_constant_buffer_gpu; }
		//=====================================================================

		//= PROPERTIES ==========================================================================================
		auto GetCullMode() const											{ return m_cull_mode; }
		void SetCullMode(const RHI_Cull_Mode cull_mode)						{ m_cull_mode = cull_mode; }

		auto GetShadingMode() const											{ return m_shading_mode; }
		void SetShadingMode(const ShadingMode shading_mode)					{ m_shading_mode = shading_mode; }

		const auto& GetColorAlbedo() const									{ return m_color_albedo; }
		void SetColorAlbedo(const Math::Vector4& color)						{ m_color_albedo = color; }
		
		const auto& GetTiling() const										{ return m_uv_tiling; }
		void SetTiling(const Math::Vector2& tiling)							{ m_uv_tiling = tiling; }

		const auto& GetOffset() const										{ return m_uv_offset; }
		void SetOffset(const Math::Vector2& offset)							{ m_uv_offset = offset; }

		auto IsEditable() const { return m_is_editable; }
		void SetIsEditable(const bool is_editable)							{ m_is_editable = is_editable; }

		auto& GetMultiplier(const TextureType type)							{ return m_multipliers[type];}
		void SetMultiplier(const TextureType type, const float multiplier)	{ m_multipliers[type] = multiplier; }

		static TextureType TextureTypeFromString(const std::string& type);
		//=======================================================================================================

	private:
		void UpdateResourceArray();

		RHI_Cull_Mode m_cull_mode		= Cull_Back;
		ShadingMode m_shading_mode		= Shading_PBR;
		Math::Vector4 m_color_albedo	= Math::Vector4(1.0f, 1.0f, 1.0f, 1.0f);
		Math::Vector2 m_uv_tiling		= Math::Vector2(1.0f, 1.0f);
		Math::Vector2 m_uv_offset		= Math::Vector2(0.0f, 0.0f);
		bool m_is_editable				= true;
		const void* m_resources[8]		= {};
		std::map<TextureType, std::shared_ptr<RHI_Texture>> m_textures;
		std::map<TextureType, float> m_multipliers;
		std::shared_ptr<ShaderVariation> m_shader;	
		std::shared_ptr<RHI_Texture> m_texture_empty;
		std::shared_ptr<RHI_Device> m_rhi_device;

		// BUFFER
		struct ConstantBufferData
		{
			Math::Vector4 mat_albedo	= Math::Vector4(1.0f, 1.0f, 1.0f, 1.0f);
			Math::Vector2 mat_tiling_uv	= Math::Vector2(1.0f, 1.0f);
			Math::Vector2 mat_offset_uv	= Math::Vector2(0.0f, 0.0f);
			float mat_roughness_mul		= 0.0f;
			float mat_metallic_mul		= 0.0f;
			float mat_normal_mul		= 0.0f;
			float mat_height_mul		= 0.0f;
			float mat_shading_mode		= 0.0f;
			Math::Vector3 padding		= Math::Vector3::Zero;
		};
		ConstantBufferData m_constant_buffer_cpu;
		std::shared_ptr<RHI_ConstantBuffer> m_constant_buffer_gpu;
	};
}
