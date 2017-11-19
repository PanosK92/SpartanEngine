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
#include "../IO/StreamIO.h"

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

	enum LoadState
	{
		Idle,
		Loading,
		Completed,
		Failed
	};

	class DLL_API TextureInfo
	{
	public:
		TextureInfo() {}
		~TextureInfo()
		{
			rgba.clear();
			rgba.shrink_to_fit();
			rgba_mimaps.clear();
			rgba_mimaps.shrink_to_fit();
		}

		TextureInfo(unsigned int width, unsigned int height)
		{
			this->width = width;
			this->height = height;
		}

		TextureInfo(bool generateMipmaps)
		{
			this->isUsingMipmaps = generateMipmaps;
		}

		bool Serialize(const std::string& filePath)
		{
			auto file = std::make_unique<StreamIO>(filePath, Mode_Write);
			if (!file->IsCreated())
				return false;

			file->Write((int)type);
			file->Write(bpp);
			file->Write(width);
			file->Write(height);
			file->Write(channels);
			file->Write(isGrayscale);
			file->Write(isTransparent);
			file->Write(isUsingMipmaps);
			if (!isUsingMipmaps)
			{
				file->Write(rgba);
			}
			else
			{
				file->Write((unsigned int)rgba_mimaps.size());
				for (auto& mip : rgba_mimaps)
				{
					file->Write(mip);
				}
			}

			return true;
		}

		bool Deserialize(const std::string& filePath)
		{
			auto file = std::make_unique<StreamIO>(filePath, Mode_Read);
			if (!file->IsCreated())
				return false;

			type = (TextureType)file->ReadInt();
			file->Read(bpp);
			file->Read(width);
			file->Read(height);
			file->Read(channels);
			file->Read(isGrayscale);
			file->Read(isTransparent);
			file->Read(isUsingMipmaps);
			if (!isUsingMipmaps)
			{
				file->Read(rgba);
			}
			else
			{
				int mipCount = file->ReadUInt();
				for (int i = 0; i < mipCount; i++)
				{
					rgba_mimaps.emplace_back(std::vector<unsigned char>());
					file->Read(rgba_mimaps[i]);
				}
			}

			return true;
		}

		unsigned int bpp = 0;
		unsigned int width = 0;
		unsigned int height = 0;
		unsigned int channels = 0;
		bool isGrayscale = false;
		bool isTransparent = false;
		bool isUsingMipmaps = false;
		std::vector<unsigned char> rgba;
		std::vector<std::vector<unsigned char>> rgba_mimaps;
		LoadState loadState = Idle;
		TextureType type = Unknown_Texture;
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

		//= PROPERTIES ========================================================================
		int GetWidth() { return m_textureInfo->width; }
		void SetWidth(int width) { m_textureInfo->width = width; }

		int GetHeight() { return m_textureInfo->height; }
		void SetHeight(int height) { m_textureInfo->height = height; }

		TextureType GetTextureType() { return m_textureInfo->type; }
		void SetTextureType(TextureType type);

		bool GetGrayscale() { return m_textureInfo->isGrayscale; }
		void SetGrayscale(bool grayscale) { m_textureInfo->isGrayscale = grayscale; }

		bool GetTransparency() { return m_textureInfo->isTransparent; }
		void SetTransparency(bool transparency) { m_textureInfo->isTransparent = transparency; }

		void EnableMimaps(bool enable) { m_textureInfo->isUsingMipmaps = enable; }

		void** GetShaderResource();
		//======================================================================================

		// Creates a shader resource from memory
		bool CreateShaderResource(unsigned int width, unsigned int height, unsigned int channels, std::vector<unsigned char> rgba, TextureFormat format);
		// Creates a shader resource from memory
		bool CreateShaderResource(TextureInfo* texInfo);

	private:		
		bool LoadFromForeignFormat(const std::string& filePath);
		TextureType TextureTypeFromString(const std::string& type);
		int ToAPIFormat(TextureFormat format);

		std::unique_ptr<D3D11Texture> m_textureAPI;
		std::unique_ptr<TextureInfo> m_textureInfo;
		TextureFormat m_format = RGBA_8_UNORM;
	};
}