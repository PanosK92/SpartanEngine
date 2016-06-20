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
//=============================

//= NAMESPACES ================
using namespace Directus::Math;

//=============================

PostProcessShader::PostProcessShader()
{
	m_shader = nullptr;
	m_miscBuffer = nullptr;
}

PostProcessShader::~PostProcessShader()
{
	DirectusSafeDelete(m_miscBuffer);
	DirectusSafeDelete(m_shader);
}

void PostProcessShader::Initialize(LPCSTR pass, D3D11Device* d3d11device)
{
	m_D3D11Device = d3d11device;

	// load the vertex and the pixel shader
	m_shader = new D3D11Shader();
	m_shader->Initialize(m_D3D11Device);
	m_shader->AddDefine(pass, true);
	m_shader->Load("Assets/Shaders/PostProcess.hlsl");
	m_shader->SetInputLayout(PositionTexture);
	m_shader->AddSampler(D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_COMPARISON_ALWAYS);
	m_shader->AddSampler(D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_COMPARISON_ALWAYS);

	// create buffer
	m_miscBuffer = new D3D11Buffer();
	m_miscBuffer->Initialize(m_D3D11Device);
	m_miscBuffer->CreateConstantBuffer(sizeof(MiscBufferType));
}

void PostProcessShader::Render(int indexCount, Matrix worldMatrix, Matrix viewMatrix, Matrix projectionMatrix, ID3D11ShaderResourceView* texture)
{
	// get a pointer to the data in the constant buffer.
	MiscBufferType* miscBufferType = static_cast<MiscBufferType*>(m_miscBuffer->Map());

	// fill buffer
	miscBufferType->worldViewProjection = Matrix::Transpose(worldMatrix * viewMatrix * Matrix::Transpose(projectionMatrix));

	m_miscBuffer->Unmap(); // unlock the constant buffer
	m_miscBuffer->SetVS(0); // set the constant buffer in the vertex shader

	// set shader texture resource in the pixel shader.
	m_D3D11Device->GetDeviceContext()->PSSetShaderResources(0, 1, &texture);

	m_shader->Set();

	// render
	m_D3D11Device->GetDeviceContext()->DrawIndexed(indexCount, 0, 0);
}
