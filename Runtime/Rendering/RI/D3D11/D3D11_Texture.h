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

//= INCLUDES ============
#include <vector>
#include "../RI_Device.h"
//=======================

namespace Directus
{
	class D3D11_Texture
	{
	public:
		D3D11_Texture(D3D11_Device* context);
		~D3D11_Texture();

		// Create from data
		bool Create(unsigned int width, unsigned int height, unsigned int channels, const std::vector<std::byte>& data, Texture_Format format);

		// Creates from data with mipmaps
		bool CreateFromMipmaps(unsigned int width, unsigned int height, unsigned int channels, const std::vector<std::vector<std::byte>>& mipmaps, Texture_Format format);

		// Creates a texture and generates mipmaps (easy way to get mipmaps but not as high quality as the mipmaps you can generate manually)
		bool CreateAndGenerateMipmaps(unsigned int width, int height, unsigned int channels, const std::vector<std::byte>& data, Texture_Format format);

		// Shader resource
		ID3D11ShaderResourceView* GetShaderResourceView()			{ return m_shaderResourceView; }
		void SetShaderResourceView(ID3D11ShaderResourceView* srv)	{ m_shaderResourceView = srv; }

		unsigned int GetMemoryUsage() { return m_memoryUsage; }

	private:
		ID3D11ShaderResourceView* m_shaderResourceView;
		D3D11_Device* m_graphics;
		unsigned int m_memoryUsage;
	};
}
