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

//==========================
const int BUFFER_COUNT = 4;
//==========================

//= INCLUDES =============================
#include "../Graphics/D3D11/D3D11Device.h"
#include "GraphicsDevice.h"
//========================================

class GBuffer
{
public:
	GBuffer(GraphicsDevice* graphicsDevice);
	~GBuffer();

	bool Initialize(int width, int height);
	void SetRenderTargets();
	void ClearRenderTargets(float, float, float, float);

	ID3D11ShaderResourceView* GetShaderResourceView(int index);

private:
	GraphicsDevice* m_graphicsDevice;
	int m_textureWidth, m_textureHeight;
	ID3D11Texture2D* m_renderTargetTextureArray[BUFFER_COUNT];
	ID3D11RenderTargetView* m_renderTargetViewArray[BUFFER_COUNT];
	ID3D11ShaderResourceView* m_shaderResourceViewArray[BUFFER_COUNT];
	ID3D11Texture2D* m_depthStencilBuffer;
	ID3D11DepthStencilView* m_depthStencilView;
	D3D11_VIEWPORT m_viewport;
};
