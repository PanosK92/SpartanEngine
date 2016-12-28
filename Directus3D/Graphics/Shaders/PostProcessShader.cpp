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

//= INCLUDES ==================
#include "PostProcessShader.h"
#include "../../Core/Settings.h"
#include "../../Logging/Log.h"
//=============================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

PostProcessShader::PostProcessShader()
{
	m_shader = nullptr;
	m_constantBuffer = nullptr;
	m_graphics = nullptr;
}

PostProcessShader::~PostProcessShader()
{

}

void PostProcessShader::Initialize(LPCSTR pass, D3D11GraphicsDevice* graphicsDevice)
{
	m_graphics = graphicsDevice;

	// load the vertex and the pixel shader
	m_shader = make_shared<D3D11Shader>(m_graphics);
	m_shader->AddDefine(pass, true);
	m_shader->Load("Data/Shaders/PostProcess.hlsl");
	m_shader->SetInputLayout(PositionTexture);
	m_shader->AddSampler(D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_COMPARISON_ALWAYS);
	m_shader->AddSampler(D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_COMPARISON_ALWAYS);

	// create buffer
	m_constantBuffer = make_shared<D3D11ConstantBuffer>(m_graphics);
	m_constantBuffer->Create(sizeof(DefaultBuffer));
}

void PostProcessShader::Render(int indexCount, const Matrix& worldMatrix, const Matrix& viewMatrix, const Matrix& projectionMatrix, ID3D11ShaderResourceView* texture)
{
	// Set shader
	m_shader->Set();

	// Set texture
	m_graphics->GetDeviceContext()->PSSetShaderResources(0, 1, &texture);

	//= UPDATE BUFFER ==========================================================
	DefaultBuffer* buffer = (DefaultBuffer*)m_constantBuffer->Map();

	buffer->worldViewProjection = worldMatrix * viewMatrix * projectionMatrix;
	buffer->viewport = GET_RESOLUTION;
	buffer->padding = GET_RESOLUTION;

	m_constantBuffer->Unmap();
	//==========================================================================
	
	// Set constant buffer
	m_constantBuffer->SetPS(0);
	m_constantBuffer->SetVS(0);

	// Render
	m_graphics->GetDeviceContext()->DrawIndexed(indexCount, 0, 0);
}
