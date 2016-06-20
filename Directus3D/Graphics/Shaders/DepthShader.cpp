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
#include "DepthShader.h"
#include "../../Core/Globals.h"
//=============================

//= NAMESPACES ================
using namespace Directus::Math;

//=============================

DepthShader::DepthShader()
{
	m_D3D11Device = nullptr;
	m_shader = nullptr;
	m_defaultBuffer = nullptr;
}

DepthShader::~DepthShader()
{
	DirectusSafeDelete(m_defaultBuffer);
	DirectusSafeDelete(m_shader);
}

void DepthShader::Initialize(D3D11Device* d3d11device)
{
	m_D3D11Device = d3d11device;

	// load the vertex and the pixel shader
	m_shader = new D3D11Shader();
	m_shader->Initialize(m_D3D11Device);
	m_shader->Load("Assets/Shaders/Depth.hlsl");
	m_shader->SetInputLayout(Position);

	// create a buffer
	m_defaultBuffer = new D3D11Buffer();
	m_defaultBuffer->Initialize(sizeof(DefaultBuffer), m_D3D11Device);
}

void DepthShader::Render(int indexCount, Matrix worldMatrix, Matrix viewMatrix, Matrix projectionMatrix)
{
	// Set the shader parameters that it will use for rendering.
	SetShaderBuffers(worldMatrix, viewMatrix, projectionMatrix);

	// Now render the prepared buffers with the shader.
	RenderShader(indexCount);
}

void DepthShader::SetShaderBuffers(Matrix worldMatrix, Matrix viewMatrix, Matrix projectionMatrix)
{
	// get a pointer to the data in the constant buffer.
	DefaultBuffer* miscBufferType = static_cast<DefaultBuffer*>(m_defaultBuffer->Map());

	// fill buffer
	miscBufferType->worldViewProjection = Matrix::Transpose(worldMatrix * viewMatrix * projectionMatrix);

	m_defaultBuffer->Unmap(); // unlock the buffer
	m_defaultBuffer->SetVS(0); // set the buffer in the vertex shader
}

void DepthShader::RenderShader(int indexCount)
{
	m_shader->Set();

	// render
	m_D3D11Device->GetDeviceContext()->DrawIndexed(indexCount, 0, 0);
}
