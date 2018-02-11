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

//= INCLUDES ===================
#include "D3D11GraphicsDevice.h"
#include <vector>
//==============================

namespace Directus
{
	class D3D11Texture
	{
	public:
		D3D11Texture(D3D11GraphicsDevice* context);
		~D3D11Texture();

		// Create from data
		bool Create(int width, int height, int channels, const std::vector<unsigned char>& data, DXGI_FORMAT format);

		// Creates from data with mimaps
		bool Create(int width, int height, int channels, const std::vector<std::vector<unsigned char>>& mimaps, DXGI_FORMAT format);

		// Creates a texture and generates mimaps (easy way to get mimaps 
		// but not as high quality as the mimaps you can generate manually)
		bool CreateAndGenerateMipmaps(int width, int height, int channels, const std::vector<unsigned char>& data, DXGI_FORMAT format);
	

		// Shader resource
		ID3D11ShaderResourceView* GetShaderResourceView() { return m_shaderResourceView; }
		void SetShaderResourceView(ID3D11ShaderResourceView* srv) { m_shaderResourceView = srv; }

	private:
		ID3D11ShaderResourceView* m_shaderResourceView;
		D3D11GraphicsDevice* m_graphics;
	};
}
