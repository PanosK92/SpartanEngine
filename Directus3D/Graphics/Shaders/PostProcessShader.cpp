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
#include "../../IO/Log.h"
//=============================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

PostProcessShader::PostProcessShader()
{
	m_shader = nullptr;
	m_constantBuffer = nullptr;
}

PostProcessShader::~PostProcessShader()
{

}

void PostProcessShader::Initialize(LPCSTR pass, Graphics* graphicsDevice)
{
	m_graphics = graphicsDevice;

	// load the vertex and the pixel shader
	m_shader = make_shared<D3D11Shader>();
	m_shader->Initialize(m_graphics);
	m_shader->AddDefine(pass, true);
	m_shader->Load("Assets/Shaders/PostProcess.hlsl");
	m_shader->SetInputLayout(PositionTexture);
	m_shader->AddSampler(D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_COMPARISON_ALWAYS);
	m_shader->AddSampler(D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_COMPARISON_ALWAYS);

	// create buffer
	m_constantBuffer = make_shared<D3D11Buffer>();
	m_constantBuffer->Initialize(m_graphics);
	m_constantBuffer->CreateConstantBuffer(sizeof(DefaultBuffer));
}

void PostProcessShader::Render(int indexCount, const Matrix& worldMatrix, const Matrix& viewMatrix, const Matrix& projectionMatrix, ID3D11ShaderResourceView* texture)
{
	//= Fill the constant buffer ===============================================
	DefaultBuffer* defaultBuffer = (DefaultBuffer*)m_constantBuffer->Map();

	defaultBuffer->worldViewProjection = Matrix::Transposed(worldMatrix * viewMatrix * Matrix::Transposed(projectionMatrix));
	defaultBuffer->viewport = GET_RESOLUTION;
	defaultBuffer->padding = GET_RESOLUTION;

	m_constantBuffer->Unmap();
	m_constantBuffer->SetPS(0);
	m_constantBuffer->SetVS(0);
	//==========================================================================

	//= SET TEXTURE ============================================================
	m_graphics->GetDeviceContext()->PSSetShaderResources(0, 1, &texture);

	//= SET SHADER =============================================================
	m_shader->Set();

	//= DRAW ===================================================================
	m_graphics->GetDeviceContext()->DrawIndexed(indexCount, 0, 0);
}
