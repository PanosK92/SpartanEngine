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

//= INCLUDES =================
#include "../../Math/Matrix.h"
#include "../GraphicsDevice.h"
//============================

class D3D11RenderTexture
{
public:
	D3D11RenderTexture();
	~D3D11RenderTexture();

	bool Initialize(GraphicsDevice* graphicsDevice, int, int);
	void SetAsRenderTarget();
	void Clear(float, float, float, float);
	ID3D11ShaderResourceView* GetShaderResourceView() const;
	void CreateOrthographicProjectionMatrix(float nearPlane, float farPlane);
	Directus::Math::Matrix GetOrthographicProjectionMatrix() const;

private:
	GraphicsDevice* m_graphicsDevice;
	ID3D11Texture2D* m_renderTargetTexture;
	ID3D11RenderTargetView* m_renderTargetView;
	ID3D11ShaderResourceView* m_shaderResourceView;
	ID3D11Texture2D* m_depthStencilBuffer;
	ID3D11DepthStencilView* m_depthStencilView;
	D3D11_VIEWPORT m_viewport;
	Directus::Math::Matrix m_orthographicProjectionMatrix;
	int m_width;
	int m_height;
};
