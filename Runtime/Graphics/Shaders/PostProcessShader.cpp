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
#include "PostProcessShader.h"
#include "../D3D11/D3D11Shader.h"
#include "../D3D11/D3D11ConstantBuffer.h"
#include "../../Core/Settings.h"
#include "../../Logging/Log.h"
//=======================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	PostProcessShader::PostProcessShader()
	{
		m_graphics = nullptr;
	}

	PostProcessShader::~PostProcessShader()
	{

	}

	void PostProcessShader::Load(const string& filePath, const string& pass, Graphics* graphics)
	{
		m_graphics = graphics;

		// load the vertex and the pixel shader
		m_shader = make_shared<D3D11Shader>(m_graphics);
		m_shader->AddDefine(pass.c_str(), true);
		m_shader->Load(filePath);
		m_shader->SetInputLayout(PositionTexture);
		m_shader->AddSampler(D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_COMPARISON_ALWAYS);
		m_shader->AddSampler(D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_COMPARISON_ALWAYS);

		// create buffer
		m_constantBuffer = make_shared<D3D11ConstantBuffer>(m_graphics);
		m_constantBuffer->Create(sizeof(DefaultBuffer));
	}

	void PostProcessShader::Set()
	{
		if (!m_shader)
			return;

		m_shader->Set();
	}

	void PostProcessShader::SetBuffer(const Matrix& worldMatrix, const Matrix& viewMatrix, const Matrix& projectionMatrix)
	{
		DefaultBuffer* buffer = (DefaultBuffer*)m_constantBuffer->Map();

		buffer->worldViewProjection = worldMatrix * viewMatrix * projectionMatrix;
		buffer->viewport = GET_RESOLUTION;
		buffer->padding = GET_RESOLUTION;

		m_constantBuffer->Unmap();

		// Set in pixel and vertex shader
		m_constantBuffer->SetPS(0);
		m_constantBuffer->SetVS(0);
	}

	void PostProcessShader::SetTexture(ID3D11ShaderResourceView* texture)
	{
		if (!m_graphics)
			return;

		m_graphics->GetDeviceContext()->PSSetShaderResources(0, 1, &texture);
	}

	bool PostProcessShader::Render(int indexCount)
	{
		if (!m_graphics->GetDeviceContext()) 
		{
			LOG_ERROR("Can't render, graphics is null.");
			return false;
		}

		m_graphics->GetDeviceContext()->DrawIndexed(indexCount, 0, 0);
		return true;
	}
}