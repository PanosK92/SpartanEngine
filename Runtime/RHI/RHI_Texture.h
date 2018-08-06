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
#include <memory>
#include "RHI_Definition.h"
#include "../Resource/IResource.h"
//================================

namespace Directus
{
	class ENGINE_CLASS RHI_Texture : public IResource
	{
	public:
		RHI_Texture(Context* context);
		~RHI_Texture();

		//= RESOURCE INTERFACE =================================
		bool SaveToFile(const std::string& filePath) override;
		bool LoadFromFile(const std::string& filePath) override;
		unsigned int GetMemoryUsage() override;
		//======================================================

		//= IMPLEMENTED BY GRAPHICS API  ========================
		bool CreateShaderResource(unsigned int width, unsigned int height, unsigned int channels, const std::vector<std::byte>& data, Texture_Format format);					// Simple texture, no mimaps
		bool CreateShaderResource(unsigned int width, unsigned int height, unsigned int channels, const std::vector<std::vector<std::byte>>& mipmaps, Texture_Format format); 	// With custom-generated mimaps
		bool CreateShaderResource(unsigned int width, int height, unsigned int channels, const std::vector<std::byte>& data, Texture_Format format);							// With auto-generated mimaps
		void* GetShaderResource()						{ return m_shaderResource; }
		void SetShaderResource(void* shaderResource)	{ m_shaderResource = shaderResource; }
		bool CreateShaderResource();
		//=======================================================

		//= PROPERTIES ==========================================================================================
		unsigned int GetWidth()						{ return m_width; }
		void SetWidth(unsigned int width)			{ m_width = width; }

		unsigned int GetHeight()					{ return m_height; }
		void SetHeight(unsigned int height)			{ m_height = height; }

		TextureType GetType()						{ return m_type; }
		void SetType(TextureType type);

		bool GetGrayscale()							{ return m_isGrayscale; }
		void SetGrayscale(bool isGrayscale)			{ m_isGrayscale = isGrayscale; }

		bool GetTransparency()						{ return m_isTransparent; }
		void SetTransparency(bool isTransparent)	{ m_isTransparent = isTransparent; }

		unsigned int GetBPP()						{ return m_bpp; }
		void SetBPP(unsigned int bpp)				{ m_bpp = bpp; }

		unsigned int GetChannels()					{ return m_channels; }
		void SetChannels(unsigned int channels)		{ m_channels = channels; }

		void EnableMimaps(bool enable) { m_isUsingMipmaps = enable; }
		bool IsUsingMimmaps() { return m_isUsingMipmaps; }

		std::vector<std::vector<std::byte>>& GetRGBA()							{ return m_textureBytes; }
		void SetRGBA(const std::vector<std::vector<std::byte>>& textureBits)	{ m_textureBytes = textureBits; }
		//=======================================================================================================

		//= TEXTURE BITS ======================================================
		void ClearTextureBytes();
		void GetTextureBytes(std::vector<std::vector<std::byte>>* textureBytes);
		//=====================================================================

	protected:
		//= NATIVE TEXTURE HANDLING (BINARY) =========
		bool Serialize(const std::string& filePath);
		bool Deserialize(const std::string& filePath);
		//============================================

		bool LoadFromForeignFormat(const std::string& filePath);
		TextureType TextureTypeFromString(const std::string& type);

		//= DATA ==========================================
		unsigned int m_bpp = 0;
		unsigned int m_width = 0;
		unsigned int m_height = 0;
		unsigned int m_channels = 0;
		bool m_isGrayscale = false;
		bool m_isTransparent = false;
		bool m_isUsingMipmaps = false;
		std::vector<std::vector<std::byte>> m_textureBytes;
		TextureType m_type = TextureType_Unknown;
		Texture_Format m_format;
		//=================================================

		// D3D11
		std::shared_ptr<RHI_Device> m_rhiDevice;
		void* m_shaderResource;
		unsigned int m_memoryUsage;
	};
}