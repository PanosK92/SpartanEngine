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
#include "RHI_Object.h"
#include "RHI_Definition.h"
#include "../Resource/IResource.h"
//================================

namespace Directus
{
	typedef std::vector<std::byte> MipLevel;

	class ENGINE_CLASS RHI_Texture : public RHI_Object, public IResource
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

		//= GRAPHICS API  ====================================================================================================================================================================
		// Generates a shader resource from a pre-made mip chain
		bool ShaderResource_Create2D(unsigned int width, unsigned int height, unsigned int channels, RHI_Format format, const std::vector<std::vector<std::byte>>& data);
		// Generates a shader resource and auto-creates mip-chain (if requested)
		bool ShaderResource_Create2D(unsigned int width, unsigned int height, unsigned int channels, RHI_Format format, const std::vector<std::byte>& data, bool generateMipChain = false);
		// Generates a cube-map shader resource. 6 textures containing mip-levels have to be provided (vector<textures<mip>>).
		bool ShaderResource_CreateCubemap(unsigned int width, unsigned int height, unsigned int channels, RHI_Format format, const std::vector<std::vector<MipLevel>>& data);
		
		void ShaderResource_Release();
		void* GetShaderResource() const { return m_shaderResource; }
		//====================================================================================================================================================================================
		
		//= PROPERTIES ===================================================================
		unsigned int GetWidth()						{ return m_width; }
		void SetWidth(unsigned int width)			{ m_width = width; }

		unsigned int GetHeight()					{ return m_height; }
		void SetHeight(unsigned int height)			{ m_height = height; }

		bool GetGrayscale()							{ return m_isGrayscale; }
		void SetGrayscale(bool isGrayscale)			{ m_isGrayscale = isGrayscale; }

		bool GetTransparency()						{ return m_isTransparent; }
		void SetTransparency(bool isTransparent)	{ m_isTransparent = isTransparent; }

		unsigned int GetBPP()						{ return m_bpp; }
		void SetBPP(unsigned int bpp)				{ m_bpp = bpp; }

		unsigned int GetBPC()						{ return m_bpc; }
		void SetBPC(unsigned int bpc)				{ m_bpc = bpc; }

		unsigned int GetChannels()					{ return m_channels; }
		void SetChannels(unsigned int channels)		{ m_channels = channels; }

		RHI_Format GetFormat()					{ return m_format; }
		void SetFormat(RHI_Format format)		{ m_format = format; }

		bool HasMipChain()							{ return m_mipChain.size() > 1; }

		bool GetNeedsMipChain()						{ return m_needsMipChain; }
		void SetNeedsMipChain(bool needsMipChain)	{ m_needsMipChain = needsMipChain; }

		const std::vector<MipLevel>& Data_Get()					{ return m_mipChain; }
		void Data_Set(const std::vector<MipLevel>& dataRGBA)	{ m_mipChain = dataRGBA; }
		MipLevel* Data_AddMipLevel() { return &m_mipChain.emplace_back(MipLevel()); }
		MipLevel* Data_GetMipLevel(unsigned int index);
		//================================================================================

		//= TEXTURE BITS =======================================
		void ClearTextureBytes();
		void GetTextureBytes(std::vector<MipLevel>* textureBytes);
		//======================================================

	protected:
		//= NATIVE TEXTURE HANDLING (BINARY) =========
		bool Serialize(const std::string& filePath);
		bool Deserialize(const std::string& filePath);
		//============================================

		bool LoadFromForeignFormat(const std::string& filePath);
		
		//= DATA ========================
		unsigned int m_bpp		= 0;
		unsigned int m_bpc		= 8;
		unsigned int m_width	= 0;
		unsigned int m_height	= 0;
		unsigned int m_channels = 0;
		bool m_isGrayscale		= false;
		bool m_isTransparent	= false;
		bool m_needsMipChain	= true;
		RHI_Format m_format;
		std::vector<MipLevel> m_mipChain;
		//===============================

		// D3D11
		std::shared_ptr<RHI_Device> m_rhiDevice;
		void* m_shaderResource		= nullptr;
		unsigned int m_memoryUsage	= 0;
	};
}