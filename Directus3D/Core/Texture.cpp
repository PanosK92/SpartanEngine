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

//= INCLUDES =====================
#include "Texture.h"
#include "../Misc/GUIDGenerator.h"
#include "../Misc/Globals.h"
#include "../IO/Serializer.h"
//================================

//= NAMESPACES ===========
using namespace std;

//========================

Texture::Texture()
{
	m_ID = GENERATE_GUID;
	m_width = 0;
	m_height = 0;
	m_shaderResourceView = nullptr;
	m_type = Albedo;
	m_isGrayscale = false;
	m_alphaIsTransparency = false;
}

Texture::~Texture()
{
	DirectusSafeRelease(m_shaderResourceView);
}

void Texture::Save() const
{
	Serializer::SaveSTR(m_ID);
	Serializer::SaveSTR(m_path);
	Serializer::SaveInt(m_width);
	Serializer::SaveInt(m_height);
	Serializer::SaveInt(int(m_type));
	Serializer::SaveBool(m_isGrayscale);
}

void Texture::Load()
{
	m_ID = Serializer::LoadSTR();
	m_path = Serializer::LoadSTR();
	m_width = Serializer::LoadInt();
	m_height = Serializer::LoadInt();
	m_type = TextureType(Serializer::LoadInt());
	m_isGrayscale = Serializer::LoadBool();
}

ID3D11ShaderResourceView* Texture::GetID3D11ShaderResourceView() const
{
	return m_shaderResourceView;
}

void Texture::SetShaderResourceView(ID3D11ShaderResourceView* shaderResourceView)
{
	m_shaderResourceView = shaderResourceView;
}

string Texture::GetID() const
{
	return m_ID;
}

void Texture::SetWidth(int width)
{
	m_width = width;
}

int Texture::GetWidth() const
{
	return m_width;
}

void Texture::SetHeight(int height)
{
	m_height = height;
}

int Texture::GetHeight() const
{
	return m_height;
}

string Texture::GetPath() const
{
	return m_path;
}

void Texture::SetPath(string path)
{
	m_path = path;
}

TextureType Texture::GetType() const
{
	return m_type;
}

void Texture::SetType(TextureType type)
{
	m_type = type;
}

void Texture::SetGrayscale(bool isGrayscale)
{
	m_isGrayscale = isGrayscale;
}

bool Texture::IsGrayscale() const
{
	return m_isGrayscale;
}
