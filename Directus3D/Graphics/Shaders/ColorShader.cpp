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

//= INCLUDES ===========
#include "ColorShader.h"
//======================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

ColorShader::ColorShader()
{
	m_graphics = nullptr;
}

ColorShader::~ColorShader()
{

}

void ColorShader::Initialize(shared_ptr<Graphics> graphicsDevice)
{
	m_graphics = graphicsDevice;

	// load the vertex and the pixel shader
	m_shader = make_shared<D3D11Shader>();
	m_shader->Initialize(m_graphics);
	m_shader->Load("Assets/Shaders/Color.hlsl");
	m_shader->SetInputLayout(PositionColor);
	m_shader->AddSampler(D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_COMPARISON_ALWAYS);

	// create buffer
	m_miscBuffer = make_shared<D3D11Buffer>();
	m_miscBuffer->Initialize(m_graphics);
	m_miscBuffer->CreateConstantBuffer(sizeof(MiscBufferType));
}

void ColorShader::Render(int vertexCount, const Matrix& worldMatrix, const Matrix& viewMatrix, const Matrix& projectionMatrix)
{
	// Set the shader parameters that it will use for rendering.
	SetShaderBuffers(worldMatrix, viewMatrix, projectionMatrix);

	// Now render the prepared buffers with the shader.
	RenderShader(vertexCount);
}

void ColorShader::SetShaderBuffers(const Matrix& worldMatrix, const Matrix& viewMatrix, const Matrix& projectionMatrix)
{
	// get a pointer of the buffer
	MiscBufferType* miscBufferType = static_cast<MiscBufferType*>(m_miscBuffer->Map());

	// fill the buffer with the matrices
	miscBufferType->world = worldMatrix.Transposed();
	miscBufferType->view = viewMatrix.Transposed();
	miscBufferType->projection = projectionMatrix.Transposed();

	// unmap the buffer and set it in the vertex shader
	m_miscBuffer->Unmap();
	m_miscBuffer->SetVS(0);
}

void ColorShader::RenderShader(unsigned int vertexCount)
{
	m_shader->Set();
	// render
	m_graphics->GetDeviceContext()->Draw(vertexCount, 0);
}
