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

//= INCLUDES ============================
#include "FontShader.h"
#include "../D3D11/D3D11Shader.h"
#include "../D3D11/D3D11ConstantBuffer.h"
//=======================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	FontShader::FontShader()
	{
		m_graphics = nullptr;
	}

	FontShader::~FontShader()
	{

	}

	void FontShader::Load(const string& filePath, Graphics* graphics)
	{
		m_graphics = graphics;

		// load the vertex and the pixel shader
		m_shader = make_shared<D3D11Shader>(m_graphics);
		m_shader->Load(filePath);
		m_shader->SetInputLayout(PositionTexture);
		m_shader->AddSampler(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_COMPARISON_NEVER);

		// create buffer
		m_miscBuffer = make_shared<D3D11ConstantBuffer>(m_graphics);
		m_miscBuffer->Create(sizeof(DefaultBuffer));
	}

	void FontShader::Set()
	{
		if (!m_shader)
			return;

		m_shader->Set();
	}

	void FontShader::SetBuffer(const Matrix& mWorld, const Matrix& mView, const Matrix& mProjection)
	{
		// get a pointer of the buffer
		DefaultBuffer* buffer = static_cast<DefaultBuffer*>(m_miscBuffer->Map());

		// fill the buffer with the matrices
		buffer->worldViewProjection = mWorld * mView * mProjection;

		// unmap the buffer and set it in the vertex shader
		m_miscBuffer->Unmap();
		m_miscBuffer->SetVS(0);
	}

	void FontShader::SetTexture(ID3D11ShaderResourceView* texture)
	{
		if (!m_graphics)
			return;

		m_graphics->GetDeviceContext()->PSSetShaderResources(0, 1, &texture);
	}

	void FontShader::Render(int vertexCount)
	{
		if (!m_graphics)
			return;

		m_graphics->GetDeviceContext()->Draw(vertexCount, 0);
	}
}