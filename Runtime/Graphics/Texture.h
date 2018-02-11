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

//= INCLUDES ====================
#include "../Resource/Resource.h"
#include <memory>
//===============================

namespace Directus
{
	class D3D11Texture;

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
		TextureType_Mask,
		TextureType_CubeMap,
	};

	enum TextureFormat
	{
		RGBA_32_FLOAT,
		RGBA_16_FLOAT,
		RGBA_8_UNORM,
		R_8_UNORM
	};

	enum TextureUsage
	{
		TextureUsage_Internal,
		// When set as external, no shader resource will automatically created and
		// no texture bits removed for memory. The creator has full control.
		TextureUsage_External
	};

	class ENGINE_CLASS Texture : public Resource
	{
	public:
		// Used by the engine for all textures
		Texture(Context* context);

		~Texture();

		//= RESOURCE INTERFACE =================================
		bool SaveToFile(const std::string& filePath) override;
		bool LoadFromFile(const std::string& filePath) override;
		unsigned int GetMemoryUsageKB() override;
		//======================================================

		//= PROPERTIES ==========================================================================================
		unsigned int GetWidth() { return m_width; }
		void SetWidth(unsigned int width) { m_width = width; }

		unsigned int GetHeight() { return m_height; }
		void SetHeight(unsigned int height) { m_height = height; }

		TextureType GetType() { return m_type; }
		void SetType(TextureType type);

		bool GetGrayscale() { return m_isGrayscale; }
		void SetGrayscale(bool isGrayscale) { m_isGrayscale = isGrayscale; }

		bool GetTransparency() { return m_isTransparent; }
		void SetTransparency(bool isTransparent) { m_isTransparent = isTransparent; }

		unsigned int GetBPP() { return m_bpp; }
		void SetBPP(unsigned int bpp) { m_bpp = bpp; }

		unsigned int GetChannels() { return m_channels; }
		void SetChannels(unsigned int channels) { m_channels = channels; }

		std::vector<std::vector<unsigned char>>& GetRGBA() { return m_textureBits; }
		void SetRGBA(const std::vector<std::vector<unsigned char>>& textureBits) { m_textureBits = textureBits; }

		void EnableMimaps(bool enable) { m_isUsingMipmaps = enable; }
		bool IsUsingMimmaps() { return m_isUsingMipmaps; }

		void SetUsage(TextureUsage use) { m_usage = use; }
		//=======================================================================================================

		//= TEXTURE BITS =========================================================
		void ClearTextureBits();
		void GetTextureBits(std::vector<std::vector<unsigned char>>* textureBits);
		//========================================================================
		
		//= SHADER RESOURCE ============================
		void** GetShaderResource();
		// Creates a shader resource from memory
		bool CreateShaderResource(
			unsigned int width, 
			unsigned int height, 
			unsigned int channels, 
			std::vector<unsigned char> rgba, 
			TextureFormat format
		);
		// Creates a shader resource from memory
		bool CreateShaderResource();
		//==============================================

	private:
		//= NATIVE TEXTURE HANDLING (BINARY) =========
		bool Serialize(const std::string& filePath);
		bool Deserialize(const std::string& filePath);
		//============================================

		bool LoadFromForeignFormat(const std::string& filePath);
		TextureType TextureTypeFromString(const std::string& type);

		TextureUsage m_usage = TextureUsage_Internal;
		std::shared_ptr<D3D11Texture> m_textureAPI;
		TextureFormat m_format = RGBA_8_UNORM;

		//= DATA =============================================
		unsigned int m_bpp = 0;
		unsigned int m_width = 0;
		unsigned int m_height = 0;
		unsigned int m_channels = 0;
		bool m_isGrayscale = false;
		bool m_isTransparent = false;
		bool m_isUsingMipmaps = false;
		std::vector<std::vector<unsigned char>> m_textureBits;
		TextureType m_type = TextureType_Unknown;
		//====================================================
	};
}