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
#include "../../Core/Globals.h"
#include "../../Core/Settings.h"
#include "../../IO/Log.h"
//=============================

//= NAMESPACES ================
using namespace Directus::Math;
//=============================

PostProcessShader::PostProcessShader()
{
	m_shader = nullptr;
	m_constantBuffer = nullptr;
}

PostProcessShader::~PostProcessShader()
{
	DirectusSafeDelete(m_constantBuffer);
	DirectusSafeDelete(m_shader);
}

void PostProcessShader::Initialize(LPCSTR pass, GraphicsDevice* graphicsDevice)
{
	m_graphicsDevice = graphicsDevice;

	// load the vertex and the pixel shader
	m_shader = new D3D11Shader();
	m_shader->Initialize(m_graphicsDevice);
	m_shader->AddDefine(pass, true);
	m_shader->Load("Assets/Shaders/PostProcess.hlsl");
	m_shader->SetInputLayout(PositionTexture);
	m_shader->AddSampler(D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_COMPARISON_ALWAYS);
	m_shader->AddSampler(D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_COMPARISON_ALWAYS);

	// create buffer
	m_constantBuffer = new D3D11Buffer();
	m_constantBuffer->Initialize(m_graphicsDevice);
	m_constantBuffer->CreateConstantBuffer(sizeof(DefaultBuffer));
}

void PostProcessShader::Render(int indexCount, Matrix worldMatrix, Matrix viewMatrix, Matrix projectionMatrix, ID3D11ShaderResourceView* texture)
{
	//= Fill the constant buffer ===============================================
	DefaultBuffer* defaultBuffer = (DefaultBuffer*)m_constantBuffer->Map();

	defaultBuffer->worldViewProjection = Matrix::Transpose(worldMatrix * viewMatrix * Matrix::Transpose(projectionMatrix));
	defaultBuffer->viewport = RESOLUTION;
	defaultBuffer->padding = RESOLUTION;

	m_constantBuffer->Unmap();
	m_constantBuffer->SetPS(0);
	m_constantBuffer->SetVS(0);
	//==========================================================================

	//= SET TEXTURE ============================================================
	m_graphicsDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &texture);

	//= SET SHADER =============================================================
	m_shader->Set();

	//= DRAW ===================================================================
	m_graphicsDevice->GetDeviceContext()->DrawIndexed(indexCount, 0, 0);
}
