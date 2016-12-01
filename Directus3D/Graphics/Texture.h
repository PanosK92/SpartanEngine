/*
Copyright(c) 2016 Panos Karabelas

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
#include "../FileSystem/FileSystem.h"
#include "../Resource/IResource.h"
#include "D3D11/D3D11Texture.h"
//===================================

enum TextureType
{
	Albedo,
	Roughness,
	Metallic,
	Normal,
	Height,
	Occlusion,
	Emission,
	Mask,
	CubeMap,
};

class Texture : public Directus::Resource::IResource
{
public:
	Texture(Context* context);
	~Texture();

	//= IO ====================================================
	bool LoadFromFile(const std::string& filePath);
	bool SaveMetadata();
	bool LoadMetadata();
	//=========================================================

	//= PROPERTIES ===============================================================================
	std::string GetName() { return m_name; }
	void SetName(const std::string& name) { m_name = name; }

	std::string GetFilePathTexture() { return m_filePath; }
	void SetFilePathTexture(const std::string& filepath) { m_filePath = filepath; }

	std::string GetFilePathMetadata() { return m_filePath + METADATA_EXTENSION; }

	int GetWidth() { return m_width; }
	void SetWidth(int width) { m_width = width; }

	int GetHeight() { return m_height; }
	void SetHeight(int height) { m_height = height; }

	TextureType GetType() { return m_type; }
	void SetType(TextureType type)
	{
		m_type = type;

		// FIX: some models pass a normal map as a height map
		// and others pass a height map as a normal map...
		if (m_type == Height && !GetGrayscale())
			m_type = Normal;
		if (m_type == Normal && GetGrayscale())
			m_type = Height;
	}

	bool GetGrayscale() { return m_grayscale; }
	void SetGrayscale(bool grayscale) { m_grayscale = grayscale; }

	bool GetTransparency() { return m_transparency; }
	void SetTransparency(bool transparency) { m_transparency = transparency; }

	void** GetShaderResourceView() { return (void**)m_texture->GetShaderResourceView(); }
	void SetShaderResourceView(void** srv);
	//=============================================================================================
private:
	std::string m_name;
	int m_width;
	int m_height;
	TextureType m_type;
	bool m_grayscale;
	bool m_transparency;
	bool m_alphaIsTransparency;
	bool m_generateMipMaps;
	std::unique_ptr<D3D11Texture> m_texture;

	bool CreateShaderResourceView();
};
