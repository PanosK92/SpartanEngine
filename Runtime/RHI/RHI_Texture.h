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
	typedef std::vector<std::byte> mipmap;

	class ENGINE_CLASS RHI_Texture : public IResource
	{
	public:
		RHI_Texture(Context* context);
		~RHI_Texture()
		{
			ClearTextureBytes();
			ShaderResource_Release();
		}

		//= IResource ==========================================
		bool SaveToFile(const std::string& filePath) override;
		bool LoadFromFile(const std::string& filePath) override;
		unsigned int GetMemoryUsage() override;
		//======================================================

		//= GRAPHICS API  ================================================================================================================================================================
		// Generates a shader resource with mip-map support. Mip-maps can be skipped. provided or generated (generateMimaps = true)
		bool ShaderResource_Create2D(unsigned int width, unsigned int height, unsigned int channels, Texture_Format format, const std::vector<mipmap>& data, bool generateMimaps = false);
		// Generates a cube-map shader resource. 6 textures containing mip-levels have to be provided (vector<textures<mip>>).
		bool ShaderResource_CreateCubemap(unsigned int width, unsigned int height, unsigned int channels, Texture_Format format, const std::vector<std::vector<mipmap>>& data);
		
		void ShaderResource_Release();
		void* GetShaderResource() const { return m_shaderResource; }
		//================================================================================================================================================================================

		//= PROPERTIES =========================================================================
		unsigned int GetWidth()								{ return m_width; }
		void SetWidth(unsigned int width)					{ m_width = width; }

		unsigned int GetHeight()							{ return m_height; }
		void SetHeight(unsigned int height)					{ m_height = height; }

		TextureType GetTextureType()						{ return m_type; }
		void SetTextureType(TextureType type);

		bool GetGrayscale()									{ return m_isGrayscale; }
		void SetGrayscale(bool isGrayscale)					{ m_isGrayscale = isGrayscale; }

		bool GetTransparency()								{ return m_isTransparent; }
		void SetTransparency(bool isTransparent)			{ m_isTransparent = isTransparent; }

		unsigned int GetBPP()								{ return m_bpp; }
		void SetBPP(unsigned int bpp)						{ m_bpp = bpp; }

		unsigned int GetChannels()							{ return m_channels; }
		void SetChannels(unsigned int channels)				{ m_channels = channels; }

		void EnableMimaps(bool enable)						{ m_isUsingMipmaps = enable; }
		bool IsUsingMimmaps()								{ return m_isUsingMipmaps; }

		Texture_Format GetFormat()							{ return m_format; }

		std::vector<mipmap>& GetData()						{ return m_dataRGBA; }
		void SetData(const std::vector<mipmap>& dataRGBA)	{ m_dataRGBA = dataRGBA; }
		//======================================================================================

		//= TEXTURE BITS =======================================
		void ClearTextureBytes();
		void GetTextureBytes(std::vector<mipmap>* textureBytes);
		//======================================================

	protected:
		//= NATIVE TEXTURE HANDLING (BINARY) =========
		bool Serialize(const std::string& filePath);
		bool Deserialize(const std::string& filePath);
		//============================================

		bool LoadFromForeignFormat(const std::string& filePath);
		TextureType TextureTypeFromString(const std::string& type);

		//= DATA =====================================
		unsigned int m_bpp		= 0;
		unsigned int m_width	= 0;
		unsigned int m_height	= 0;
		unsigned int m_channels = 0;
		bool m_isGrayscale		= false;
		bool m_isTransparent	= false;
		bool m_isUsingMipmaps	= false;
		TextureType m_type		= TextureType_Unknown;
		Texture_Format m_format;
		std::vector<mipmap> m_dataRGBA;
		//============================================

		// D3D11
		std::shared_ptr<RHI_Device> m_rhiDevice;
		void* m_shaderResource;
		unsigned int m_memoryUsage;
	};
}