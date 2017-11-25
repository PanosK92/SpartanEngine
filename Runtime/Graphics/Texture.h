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
	class TextureInfo;
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

	enum LoadState
	{
		Idle,
		Loading,
		Completed,
		Failed
	};

	class DLL_API Texture : public Resource
	{
	public:
		Texture(Context* context);
		~Texture();

		//= RESOURCE INTERFACE ========================
		bool SaveToFile(const std::string& filePath) override;
		bool LoadFromFile(const std::string& filePath) override;
		//=============================================

		//= PROPERTIES =========================
		unsigned int GetWidth();
		void SetWidth(unsigned int width);

		unsigned int GetHeight();
		void SetHeight(unsigned int height);

		TextureType GetTextureType();
		void SetTextureType(TextureType type);

		bool GetGrayscale();
		void SetGrayscale(bool grayscale);

		bool GetTransparency();
		void SetTransparency(bool transparency);

		void EnableMimaps(bool enable);

		void** GetShaderResource();
		//======================================

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
		bool m_isDirty;
	};
}