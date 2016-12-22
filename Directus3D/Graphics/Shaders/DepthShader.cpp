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
#include "DepthShader.h"
//======================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

DepthShader::DepthShader()
{
	m_graphics = nullptr;
	m_shader = nullptr;
	m_defaultBuffer = nullptr;
}

DepthShader::~DepthShader()
{

}

void DepthShader::Initialize(D3D11GraphicsDevice* graphicsDevice)
{
	m_graphics = graphicsDevice;

	// load the vertex and the pixel shader
	m_shader = make_shared<D3D11Shader>(m_graphics);
	m_shader->Load("Assets/Shaders/Depth.hlsl");
	m_shader->SetInputLayout(Position);

	// create a buffer
	m_defaultBuffer = make_shared<D3D11ConstantBuffer>(m_graphics);
	m_defaultBuffer->Create(sizeof(DefaultBuffer));
}

void DepthShader::UpdateMatrixBuffer(const Matrix& worldMatrix, const Matrix& viewMatrix, const Matrix& projectionMatrix)
{
	if (!m_defaultBuffer)
		return;

	// Get buffer pointer
	DefaultBuffer* miscBufferType = static_cast<DefaultBuffer*>(m_defaultBuffer->Map());

	// Fill buffer
	miscBufferType->worldViewProjection = worldMatrix * viewMatrix * projectionMatrix;

	// Unlock the buffer
	m_defaultBuffer->Unmap();

	// Set the buffer to the vertex shader
	m_defaultBuffer->SetVS(0);
}

void DepthShader::Set()
{
	if (m_shader)
		m_shader->Set();
}

void DepthShader::Render(unsigned int indexCount)
{
	if (m_graphics)
		m_graphics->GetDeviceContext()->DrawIndexed(indexCount, 0, 0);
}