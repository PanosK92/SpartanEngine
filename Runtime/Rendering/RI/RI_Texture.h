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

//= INCLUDES ========================
#include <memory>
#include "Backend_Def.h"
#include "../../Resource/IResource.h"
//===================================

namespace Directus
{
	class ENGINE_CLASS RI_Texture : public IResource
	{
	public:
		// Used by the engine for all textures
		RI_Texture(Context* context);

		~RI_Texture();

		//= RESOURCE INTERFACE =================================
		bool SaveToFile(const std::string& filePath) override;
		bool LoadFromFile(const std::string& filePath) override;
		unsigned int GetMemory() override;
		//======================================================

		//= PROPERTIES =======================================================================================
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

		std::vector<std::vector<std::byte>>& GetRGBA() { return m_textureBytes; }
		void SetRGBA(const std::vector<std::vector<std::byte>>& textureBits) { m_textureBytes = textureBits; }

		void EnableMimaps(bool enable) { m_isUsingMipmaps = enable; }
		bool IsUsingMimmaps() { return m_isUsingMipmaps; }
		//====================================================================================================

		//= TEXTURE BITS ======================================================
		void ClearTextureBytes();
		void GetTextureBytes(std::vector<std::vector<std::byte>>* textureBytes);
		//=====================================================================
		
		//= SHADER RESOURCE ============================
		void** GetShaderResource();
		// Creates a shader resource from memory
		bool CreateShaderResource(
			unsigned int width, 
			unsigned int height, 
			unsigned int channels, 
			const std::vector<std::byte>& rgba, 
			Texture_Format format
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

		std::shared_ptr<D3D11_Texture> m_textureLowLevel;
		Texture_Format m_format;

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
		//=================================================
	};
}