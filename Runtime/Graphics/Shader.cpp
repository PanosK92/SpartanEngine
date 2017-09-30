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
#include "Shader.h"
#include "D3D11/D3D11Shader.h"
#include "D3D11/D3D11ConstantBuffer.h"
#include "../../Core/Context.h"
#include "../../Logging/Log.h"
//=======================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	Shader::Shader(Context* context)
	{
		m_graphics = context->GetSubsystem<Graphics>();
		m_bufferType = mWVP;
		m_bufferScope = VertexShader;
	}

	Shader::~Shader()
	{

	}

	void Shader::Load(const string& filePath)
	{
		if (!m_graphics)
		{
			LOG_WARNING("Shader: Uninitialized graphics, can't load shader.");
			return;
		}

		if (!m_shader)
		{
			m_shader = make_shared<D3D11Shader>(m_graphics);
		}

		m_shader->Load(filePath);
	}

	void Shader::AddDefine(const string& define)
	{
		if (!m_shader)
		{
			m_shader = make_shared<D3D11Shader>(m_graphics);
		}

		m_shader->AddDefine(define.c_str(), true);
	}

	void Shader::AddBuffer(ConstantBufferType bufferType, ConstantBufferScope bufferScope)
	{
		m_bufferType = bufferType;
		m_bufferScope = bufferScope;

		m_constantBuffer = make_shared<D3D11ConstantBuffer>(m_graphics);
		if (m_bufferType == mWVP)
		{
			m_constantBuffer->Create(sizeof(Struct_mWVP));
		}
		else if (m_bufferType == mWmVmP)
		{
			m_constantBuffer->Create(sizeof(Struct_mWmVmP));
		}
		else if (m_bufferType == mWVPvColor)
		{
			m_constantBuffer->Create(sizeof(Struct_mWVPvColor));
		}
		else if (m_bufferType == mWVPvResolution)
		{
			m_constantBuffer->Create(sizeof(Struct_mWVP_vResolution));
		}
	}

	void Shader::AddSampler(TextureSampler samplerType)
	{
		if (!m_shader)
		{
			LOG_WARNING("Shader: Can't add sampler to uninitialized shader.");
			return;
		}

		if (samplerType == Anisotropic_Sampler)
		{
			m_shader->AddSampler(D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_COMPARISON_ALWAYS);
		}
		else if (samplerType == Linear_Sampler)
		{
			m_shader->AddSampler(D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_COMPARISON_ALWAYS);
		}
		else if (samplerType == Point_Sampler)
		{
			m_shader->AddSampler(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_COMPARISON_ALWAYS);
		}
	}

	void Shader::Set()
	{
		if (!m_shader)
			return;

		m_shader->Set();
	}

	void Shader::SetInputLaytout(InputLayout inputLayout)
	{
		if (!m_shader)
		{
			LOG_WARNING("Shader: Can't set input layout for uninitialized shader.");
			return;
		}

		m_shader->SetInputLayout(inputLayout);
	}

	void Shader::SetTexture(ID3D11ShaderResourceView* texture, unsigned int slot)
	{
		if (!m_graphics)
			return;

		m_graphics->GetDeviceContext()->PSSetShaderResources(slot, 1, &texture);
	}

	void Shader::SetBuffer(const Matrix& mWorld, const Matrix& mView, const Matrix& mProjection, unsigned int slot)
	{
		if (!m_constantBuffer)
		{
			LOG_WARNING("Shader: Can't map uninitialized buffer.");
			return;
		}

		if (m_bufferType == mWVP)
		{
			Struct_mWVP* buffer = static_cast<Struct_mWVP*>(m_constantBuffer->Map());
			buffer->mMVP = mWorld * mView * mProjection;
		}
		else
		{
			Struct_mWmVmP* buffer = static_cast<Struct_mWmVmP*>(m_constantBuffer->Map());
			buffer->mWorld = mWorld;
			buffer->mView = mView;
			buffer->mProjection = mProjection;
		}

		// Unmap buffer
		m_constantBuffer->Unmap();

		SetBufferScope(m_constantBuffer, slot);
	}

	void Shader::SetBuffer(const Matrix& mWorld, const Matrix& mView, const Matrix& mProjection, const Vector4& color, unsigned int slot)
	{
		if (!m_constantBuffer)
		{
			LOG_WARNING("Shader: Can't map uninitialized buffer.");
			return;
		}

		// Get a pointer of the buffer
		Struct_mWVPvColor* buffer = static_cast<Struct_mWVPvColor*>(m_constantBuffer->Map());

		// Fill the buffer
		buffer->mMVP = mWorld * mView * mProjection;
		buffer->vColor = color;

		// Unmap buffer
		m_constantBuffer->Unmap();

		SetBufferScope(m_constantBuffer, slot);
	}

	void Shader::SetBuffer(const Matrix& mWorld, const Matrix& mView, const Matrix& mProjection, const Vector2& resolution, unsigned slot)
	{
		if (!m_constantBuffer)
		{
			LOG_WARNING("Shader: Can't map uninitialized buffer.");
			return;
		}

		// Get a pointer of the buffer
		Struct_mWVP_vResolution* buffer = static_cast<Struct_mWVP_vResolution*>(m_constantBuffer->Map());

		// Fill the buffer
		buffer->mMVP = mWorld * mView * mProjection;
		buffer->vResolution = resolution;
		buffer->vPadding = Vector2::Zero;

		// Unmap buffer
		m_constantBuffer->Unmap();

		SetBufferScope(m_constantBuffer, slot);
	}

	void Shader::Draw(unsigned int vertexCount)
	{
		if (!m_graphics)
			return;

		m_graphics->GetDeviceContext()->Draw(vertexCount, 0);
	}

	void Shader::DrawIndexed(unsigned int indexCount)
	{
		if (!m_graphics)
			return;

		m_graphics->GetDeviceContext()->DrawIndexed(indexCount, 0, 0);
	}

	void Shader::SetBufferScope(shared_ptr<D3D11ConstantBuffer> buffer, unsigned int slot)
	{
		if (m_bufferScope == VertexShader)
		{
			buffer->SetVS(slot);
		}
		else if (m_bufferScope == PixelShader)
		{
			buffer->SetPS(slot);
		}
		else if (m_bufferScope == Both)
		{
			buffer->SetVS(slot);
			buffer->SetPS(slot);
		}
	}
}