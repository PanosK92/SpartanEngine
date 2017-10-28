/*
Copyright(c) 2016-2017 Panos Karabelas

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
		Unknown_Texture,
		Albedo_Texture,
		Roughness_Texture,
		Metallic_Texture,
		Normal_Texture,
		Height_Texture,
		Occlusion_Texture,
		Emission_Texture,
		Mask_Texture,
		CubeMap_Texture,
	};

	enum TextureFormat
	{
		RGBA_32_FLOAT,
		RGBA_16_FLOAT,
		RGBA_8_UNORM,
		R_8_UNORM
	};

	class DLL_API Texture : public Resource
	{
	public:
		Texture(Context* context);
		~Texture();

		//= RESOURCE INTERFACE ========================
		bool SaveToFile(const std::string& filePath);
		bool LoadFromFile(const std::string& filePath);
		//=============================================

		//= PROPERTIES ============================================================
		int GetWidth() { return m_width; }
		void SetWidth(int width) { m_width = width; }

		int GetHeight() { return m_height; }
		void SetHeight(int height) { m_height = height; }

		TextureType GetTextureType() { return m_textureType; }
		void SetTextureType(TextureType type);

		bool GetGrayscale() { return m_grayscale; }
		void SetGrayscale(bool grayscale) { m_grayscale = grayscale; }

		bool GetTransparency() { return m_transparency; }
		void SetTransparency(bool transparency) { m_transparency = transparency; }

		void EnableMimaps(bool enable) { m_mimaps = enable; }

		void** GetShaderResource();
		//========================================================================

		// Creates a texture of the given type from memory
		bool CreateFromMemory(int width, int height, int channels, unsigned char* buffer, TextureFormat format);
		// Creates a texture with pre-generated mimaps from memory
		bool CreateFromMemory(int width, int height, int channels, const std::vector<std::vector<unsigned char>>& buffer, TextureFormat format);

	private:
		bool LoadFromForeignFormat(const std::string& filePath);
		bool LoadMetadata(const std::string& filePath);
		TextureType TextureTypeFromString(const std::string& type);
		int ToAPIFormat(TextureFormat format);

		int m_width;
		int m_height;
		int m_channels;
		TextureType m_textureType;
		bool m_grayscale;
		bool m_transparency;
		bool m_alphaIsTransparency;
		bool m_mimaps;
		std::unique_ptr<D3D11Texture> m_texture;
	};
}