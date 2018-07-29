/*
Copyright(c) 2016-2018 Panos Karabelas

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
#include <map>
#include <memory>
#include "../RHI/IRHI_Definition.h"
#include "../Resource/IResource.h"
#include "../Math/Vector2.h"
#include "../Math/Vector4.h"
//================================

namespace Directus
{	
	class ShaderPool;
	class ShaderVariation;
	class TexturePool;

	class ENGINE_CLASS Material : public IResource
	{
	public:
		enum ShadingMode
		{
			Shading_PBR,
			Shading_Unlit,
			Shading_Skybox
		};

		Material(Context* context);
		~Material();

		//= IResource ==================================================
		bool LoadFromFile(const std::string& filePath) override;
		bool SaveToFile(const std::string& filePath) override;
		unsigned int GetMemoryUsage() override;
		//==============================================================

		//= TEXTURES =====================================================================
		void SetTexture(const std::weak_ptr<RHI_Texture>& textureWeak, bool autoCache = true);
		std::weak_ptr<RHI_Texture> GetTextureByType(TextureType type) { return m_textures[type]; }
		bool HasTextureOfType(TextureType type);
		bool HasTexture(const std::string& path);
		std::string GetTexturePathByType(TextureType type);
		std::vector<std::string> GetTexturePaths();
		//================================================================================

		//= SHADER ===========================================================================
		void AcquireShader();
		std::weak_ptr<ShaderVariation> FindMatchingShader(unsigned long shaderFlags);
		std::weak_ptr<ShaderVariation> GetOrCreateShader(unsigned long shaderFlags);
		std::weak_ptr<ShaderVariation> GetShader() { return m_shader; }
		bool HasShader() { return GetShader().expired() ? false : true; }
		const std::vector<void*>& GetShaderResources();
		//====================================================================================

		//= PROPERTIES ================================================================	
		unsigned int GetModelID() { return m_modelID; }
		void SetModelID(unsigned int ID) { m_modelID = ID; }

		Cull_Mode GetCullMode() { return m_cullMode; }
		void SetCullMode(Cull_Mode cullMode) { m_cullMode = cullMode; }

		float GetOpacity() { return m_opacity; }
		void SetOpacity(float opacity)
		{
			m_opacity = opacity;
			m_alphaBlending = bool(opacity) ? true : false;
		}

		bool GetAlphaBlending() { return m_alphaBlending; }
		void SetAlphaBlending(bool alphaBlending) { m_alphaBlending = alphaBlending; }

		float& GetRoughnessMultiplier() { return m_roughnessMultiplier; }
		void SetRoughnessMultiplier(float roughness) { m_roughnessMultiplier = roughness; }

		float GetMetallicMultiplier() { return m_metallicMultiplier; }
		void SetMetallicMultiplier(float metallic) { m_metallicMultiplier = metallic; }

		float GetNormalMultiplier() { return m_normalMultiplier; }
		void SetNormalMultiplier(float normal) { m_normalMultiplier = normal; }

		float GetHeightMultiplier() { return m_heightMultiplier; }
		void SetHeightMultiplier(float height) { m_heightMultiplier = height; }

		void SetMultiplier(TextureType type, float value);

		ShadingMode GetShadingMode() { return m_shadingMode; }
		void SetShadingMode(ShadingMode shadingMode) { m_shadingMode = shadingMode; }

		const Math::Vector4& GetColorAlbedo() { return m_colorAlbedo; }
		void SetColorAlbedo(const Math::Vector4& color) { m_colorAlbedo = color; }

		const Math::Vector2& GetTiling() { return m_uvTiling; }
		void SetTiling(const Math::Vector2& tiling) { m_uvTiling = tiling; }

		const Math::Vector2& GetOffset() { return m_uvOffset; }
		void SetOffset(const Math::Vector2& offset) { m_uvOffset = offset; }

		bool IsEditable() { return m_isEditable; }
		void SetIsEditable(bool isEditable) { m_isEditable = isEditable; }
		//=============================================================================

	private:
		void TextureBasedMultiplierAdjustment();

		unsigned int m_modelID;	
		float m_opacity;
		bool m_alphaBlending;
		Cull_Mode m_cullMode;
		ShadingMode m_shadingMode;
		Math::Vector4 m_colorAlbedo;
		float m_roughnessMultiplier;
		float m_metallicMultiplier;
		float m_normalMultiplier;
		float m_heightMultiplier;
		Math::Vector2 m_uvTiling;
		Math::Vector2 m_uvOffset;	
		bool m_isEditable;
		std::weak_ptr<ShaderVariation> m_shader;
		// <tex_type, <tex,	tex_path>>
		std::map<TextureType, std::weak_ptr<RHI_Texture>> m_textures;
		std::vector<void*> m_shaderResources;
	};
}