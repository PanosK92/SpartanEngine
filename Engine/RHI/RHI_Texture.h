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
	typedef std::vector<std::byte> mip_level;

	class ENGINE_CLASS RHI_Texture : public RHI_Object, public IResource
	{
	public:
		RHI_Texture(Context* context);
		~RHI_Texture()
		{
			ClearTextureBytes();
			ShaderResource_Release();
		}

		//= IResource ===========================================
		bool SaveToFile(const std::string& file_path) override;
		bool LoadFromFile(const std::string& file_path) override;
		//=======================================================

		//= GRAPHICS API  ===================================================================================================================================================================
		// Generates a shader resource from a pre-made mip chain
		bool ShaderResource_Create2D(unsigned int width, unsigned int height, unsigned int channels, RHI_Format format, const std::vector<std::vector<std::byte>>& data);
		// Generates a shader resource and auto-creates mip-chain (if requested)
		bool ShaderResource_Create2D(unsigned int width, unsigned int height, unsigned int channels, RHI_Format format, const std::vector<std::byte>& data, bool generate_mip_chain = false);
		// Generates a cube-map shader resource. 6 textures containing mip-levels have to be provided (vector<textures<mip>>).
		bool ShaderResource_CreateCubemap(unsigned int width, unsigned int height, unsigned int channels, RHI_Format format, const std::vector<std::vector<mip_level>>& data);	
		void ShaderResource_Release() const;
		void* GetShaderResource() const { return m_shader_resource; }
		//===================================================================================================================================================================================
		
		//= PROPERTIES ==========================================================================================
		unsigned int GetWidth() const						{ return m_width; }
		void SetWidth(const unsigned int width)				{ m_width = width; }

		unsigned int GetHeight() const						{ return m_height; }
		void SetHeight(const unsigned int height)			{ m_height = height; }

		bool GetGrayscale() const							{ return m_is_grayscale; }
		void SetGrayscale(const bool is_grayscale)			{ m_is_grayscale = is_grayscale; }

		bool GetTransparency() const						{ return m_is_transparent; }
		void SetTransparency(const bool is_transparent)		{ m_is_transparent = is_transparent; }

		unsigned int GetBpp() const							{ return m_bpp; }
		void SetBpp(const unsigned int bpp)					{ m_bpp = bpp; }

		unsigned int GetBpc() const							{ return m_bpc; }
		void SetBpc(const unsigned int bpc)					{ m_bpc = bpc; }

		unsigned int GetChannels() const					{ return m_channels; }
		void SetChannels(const unsigned int channels)		{ m_channels = channels; }

		RHI_Format GetFormat() const						{ return m_format; }
		void SetFormat(const RHI_Format format)				{ m_format = format; }

		bool HasMipChain() const							{ return m_mip_chain.size() > 1; }

		bool GetNeedsMipChain() const						{ return m_needs_mip_chain; }
		void SetNeedsMipChain(const bool needs_mip_chain)	{ m_needs_mip_chain = needs_mip_chain; }

		const std::vector<mip_level>& Data_Get() const			{ return m_mip_chain; }
		void Data_Set(const std::vector<mip_level>& data_rgba)	{ m_mip_chain = data_rgba; }
		mip_level* Data_AddMipLevel()							{ return &m_mip_chain.emplace_back(mip_level()); }
		mip_level* Data_GetMipLevel(unsigned int index);
		//=======================================================================================================

		//= TEXTURE BITS ===========================================
		void ClearTextureBytes();
		void GetTextureBytes(std::vector<mip_level>* texture_bytes);
		//==========================================================

	protected:
		//= NATIVE TEXTURE HANDLING (BINARY) ==========
		bool Serialize(const std::string& file_path);
		bool Deserialize(const std::string& file_path);
		//=============================================

		bool LoadFromForeignFormat(const std::string& file_path);
		
		//= DATA ==========================
		unsigned int m_bpp		= 0;
		unsigned int m_bpc		= 8;
		unsigned int m_width	= 0;
		unsigned int m_height	= 0;
		unsigned int m_channels = 0;
		bool m_is_grayscale		= false;
		bool m_is_transparent	= false;
		bool m_needs_mip_chain	= true;
		RHI_Format m_format;
		std::vector<mip_level> m_mip_chain;
		//=================================

		// D3D11
		std::shared_ptr<RHI_Device> m_rhi_device;
		void* m_shader_resource		= nullptr;
		unsigned int m_memory_usage	= 0;
	};
}