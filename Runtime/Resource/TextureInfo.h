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
#include <memory>
#include "../IO/StreamIO.h"
#include "../Graphics/Texture.h"
//===============================

namespace Directus
{
	class DLL_API TextureInfo
	{
	public:
		TextureInfo() {}
		~TextureInfo()
		{
			Clear();
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

		void Clear()
		{
			rgba.clear();
			rgba.shrink_to_fit();
			rgba_mimaps.clear();
			rgba_mimaps.shrink_to_fit();
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

			Clear();

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
				unsigned int mipCount = file->ReadUInt();
				rgba_mimaps.reserve(mipCount);
				for (unsigned int i = 0; i < mipCount; i++)
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
		TextureType type = TextureType_Unknown;
	};
}