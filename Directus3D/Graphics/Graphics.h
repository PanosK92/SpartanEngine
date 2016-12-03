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

#define D3D11

//= INCLUDES ===================
#include <Windows.h>
#include "../Math/Vector4.h"
#include "../Core/Subsystem.h"
#include "GraphicsDefinitions.h"
#include "GraphicsAPI.h"
//==============================

class GraphicsAPI;

class Graphics : public Subsystem
{
public:
	Graphics(Context* context);
	~Graphics();

	bool Initialize(HWND windowHandle);
	void Clear(const Directus::Math::Vector4& color);
	void Present();
	void SetBackBufferAsRenderTarget();

	//= DEPTH ======================
	bool CreateDepthStencil();
	bool CreateDepthStencilBuffer();	
	bool CreateDepthStencilView();
	void EnableZBuffer(bool enable);
	//==============================
	
	void EnableAlphaBlending(bool enable);
	void SetInputLayout(InputLayout inputLayout);
	void SetCullMode(CullMode cullMode);
	void SetPrimitiveTopology(PrimitiveTopology primitiveTopology);

	//= VIEWPORT ===========================
	bool SetResolution(int width, int height);
	void SetViewport(float width, float height);
	void ResetViewport();
	//======================================

	GraphicsAPI* GetAPI() { return m_api; }

private:
	GraphicsAPI* m_api;
	InputLayout m_inputLayout;
	CullMode m_cullMode;
	PrimitiveTopology m_primitiveTopology;
	bool m_zBufferEnabled;
	bool m_alphaBlendingEnabled;
};