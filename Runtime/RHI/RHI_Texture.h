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

namespace Spartan
{
	class SPARTAN_CLASS RHI_Texture : public RHI_Object, public IResource
	{
	public:
		RHI_Texture(Context* context, bool mipmap_support = true);
		~RHI_Texture();

		//= IResource ===========================================
		bool SaveToFile(const std::string& file_path) override;
		bool LoadFromFile(const std::string& file_path) override;
		//=======================================================

		//= GRAPHICS API  =================================================================================================================================================================
		// Generates a shader resource from a mipmaps. If only the first mipmap is available, setting generate_mip_chain to true will auto-generate the rest.
		bool ShaderResource_Create2D(unsigned int width, unsigned int height, unsigned int channels, RHI_Format format, const std::vector<std::vector<std::byte>>& data);
		bool ShaderResource_Create2D(unsigned int width, unsigned int height, unsigned int channels, RHI_Format format, const std::vector<std::byte>& data)
		{
			m_mipmaps.clear();
			m_mipmaps.emplace_back(data);
			return ShaderResource_Create2D(width, height, channels, format, m_mipmaps);
		}
		// Generates a cube-map shader resource. 6 textures containing mip-levels have to be provided (vector<textures<mip>>).
		bool ShaderResource_CreateCubemap(unsigned int width, unsigned int height, unsigned int channels, RHI_Format format, const std::vector<std::vector<std::vector<std::byte>>>& data);
		//=================================================================================================================================================================================

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

		auto GetMipmapSupport()												{ return m_mipmap_support;}
		const auto& Data_Get() const										{ return m_mipmaps; }
		void Data_Set(const std::vector<std::vector<std::byte>>& data_rgba)	{ m_mipmaps = data_rgba; }
		auto Data_AddMipLevel()												{ return &m_mipmaps.emplace_back(std::vector<std::byte>()); }
		std::vector<std::byte>* Data_GetMipLevel(unsigned int index);

		void ClearTextureBytes();
		void GetTextureBytes(std::vector<std::vector<std::byte>>* texture_bytes);
		auto GetBufferView() const { return m_texture_view; }

	protected:
		//= NATIVE TEXTURE HANDLING (BINARY) ==========
		bool Serialize(const std::string& file_path);
		bool Deserialize(const std::string& file_path);
		//=============================================

		bool LoadFromForeignFormat(const std::string& file_path);
		
		unsigned int m_bpp		= 0;
		unsigned int m_bpc		= 8;
		unsigned int m_width	= 0;
		unsigned int m_height	= 0;
		unsigned int m_channels = 0;
		bool m_is_grayscale		= false;
		bool m_is_transparent	= false;
		bool m_mipmap_support	= true;
		RHI_Format m_format;
		std::vector<std::vector<std::byte>> m_mipmaps;	
		std::shared_ptr<RHI_Device> m_rhi_device;

		// API	
		void* m_texture_view	= nullptr;
		void* m_texture			= nullptr;
		void* m_texture_memory	= nullptr;
		static std::mutex m_mutex;
	};
}