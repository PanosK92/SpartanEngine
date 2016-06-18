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

//= INCLUDES =============================
#include "../Graphics/D3D11/D3D11Shader.h"
//========================================

#define NULL_TEXTURE_ID "-1"

enum TextureType
{
	Albedo,
	Metallic,
	Roughness,
	Occlusion,
	Normal,
	Height,
	Mask,
	CubeMap,
};

class Texture
{
public:
	Texture();
	~Texture();

	void Save() const;
	void Load();

	ID3D11ShaderResourceView* GetID3D11ShaderResourceView() const;
	void SetShaderResourceView(ID3D11ShaderResourceView* shaderResourceView);

	std::string GetID() const;

	void SetWidth(int width);
	int GetWidth() const;

	void SetHeight(int height);
	int GetHeight() const;

	std::string GetPath() const;
	void SetPath(std::string path);

	TextureType GetType() const;
	void SetType(TextureType type);

	void SetGrayscale(bool isGrayscale);
	bool IsGrayscale() const;

private:

	// data
	std::string m_ID;
	int m_width;
	int m_height;
	TextureType m_type;
	std::string m_path;
	bool m_isGrayscale;
	bool m_alphaIsTransparency;
	ID3D11ShaderResourceView* m_shaderResourceView;
};
