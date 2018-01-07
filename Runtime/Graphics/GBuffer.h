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

//= INCLUDES =====================================
#include <vector>
#include <memory>
#include "../Graphics/D3D11/D3D11GraphicsDevice.h"
#include "../Core/Settings.h"
//================================================

namespace Directus
{
	class D3D11RenderTexture;

	class GBuffer
	{
	public:
		GBuffer(Graphics* graphics, int width = RESOLUTION_WIDTH, int height = RESOLUTION_HEIGHT);
		~GBuffer();

		bool SetAsRenderTarget();
		bool Clear();

		ID3D11ShaderResourceView* GetShaderResource(int index);

	private:
		std::vector<std::unique_ptr<D3D11RenderTexture>> m_renderTargets;

		// Dependencies
		Graphics* m_graphics;

	};
}