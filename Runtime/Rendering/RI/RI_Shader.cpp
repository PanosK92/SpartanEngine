/*
Copyright(c) 2016-2018 Panos Karabelas

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

//= INCLUDES =============================
#include "RI_Shader.h"
#include "D3D11/D3D11_Shader.h"
#include "D3D11/D3D11_ConstantBuffer.h"
#include "D3D11/D3D11_RenderTexture.h"
#include "Backend_Imp.h"
#include "../../Logging/Log.h"
#include "../../Core/Context.h"
#include "../../Scene/Components/Light.h"
#include "../../Scene/Components/Camera.h"
//========================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	RI_Shader::RI_Shader(Context* context)
	{
		m_graphics		= context->GetSubsystem<RenderingDevice>();
		m_bufferType	= CB_WVP;
		m_bufferScope	= VertexShader;
	}

	RI_Shader::~RI_Shader()
	{

	}

	void RI_Shader::Compile(const string& filePath)
	{
		if (!m_graphics)
		{
			LOG_WARNING("Shader: Uninitialized graphics, can't load shader.");
			return;
		}

		if (!m_shader)
		{
			m_shader = make_unique<D3D11_Shader>(m_graphics);
		}

		m_shader->Compile(filePath);
	}

	void RI_Shader::AddDefine(const char* define)
	{
		if (!m_shader)
		{
			m_shader = make_unique<D3D11_Shader>(m_graphics);
		}

		m_shader->AddDefine(define, "1");
	}

	void RI_Shader::AddBuffer(ConstantBufferType bufferType, ConstantBufferScope bufferScope)
	{
		m_bufferType = bufferType;
		m_bufferScope = bufferScope;

		m_constantBuffer = make_unique<D3D11_ConstantBuffer>(m_graphics);

		switch (m_bufferType)
		{
			case CB_WVP:
				m_constantBuffer->Create(sizeof(Struct_WVP));
				break;
			case CB_W_V_P:
				m_constantBuffer->Create(sizeof(Struct_W_V_P));
				break;
			case CB_WVP_Color:
				m_constantBuffer->Create(sizeof(Struct_WVP_Color));
				break;
			case CB_WVP_Resolution:
				m_constantBuffer->Create(sizeof(Struct_WVP_Resolution));
			case CB_Shadowing:
				m_constantBuffer->Create(sizeof(Struct_Shadowing));
				break;
		}
	}

	bool RI_Shader::AddSampler(Texture_Sampler_Filter filter, Texture_Address_Mode addressMode, Texture_Comparison_Function comparisonFunc)
	{
		if (!m_shader)
		{
			LOG_WARNING("Shader: Can't add sampler to uninitialized shader.");
			return false;
		}

		return m_shader->AddSampler(filter, addressMode, comparisonFunc);
	}

	void RI_Shader::Set()
	{
		if (!m_shader)
			return;

		m_shader->Set();
	}

	void RI_Shader::SetInputLaytout(InputLayout inputLayout)
	{
		if (!m_shader)
		{
			LOG_WARNING("Shader: Can't set input layout for uninitialized shader.");
			return;
		}

		m_shader->SetInputLayout(inputLayout);
	}

	void RI_Shader::SetTexture(void* texture, unsigned int slot)
	{
		if (!m_graphics)
			return;

		auto id3d11Srv = (ID3D11ShaderResourceView*)texture;
		m_graphics->GetDeviceContext()->PSSetShaderResources(slot, 1, &id3d11Srv);
	}

	void RI_Shader::SetTextures(vector<void*> textures)
	{
		if (!m_graphics)
			return;

		auto ptr = (ID3D11ShaderResourceView**)textures.data();
		int length = (int)textures.size();
		auto tex = vector<ID3D11ShaderResourceView*>(ptr, ptr + length);

		m_graphics->GetDeviceContext()->PSSetShaderResources(0, unsigned int(textures.size()), &tex.front());
	}

	void RI_Shader::SetBuffer(const Matrix& mWorld, const Matrix& mView, const Matrix& mProjection, unsigned int slot)
	{
		if (!m_constantBuffer)
		{
			LOG_WARNING("Shader: Can't map uninitialized buffer.");
			return;
		}

		if (m_bufferType == CB_WVP)
		{
			auto buffer = static_cast<Struct_WVP*>(m_constantBuffer->Map());
			buffer->wvp = mWorld * mView * mProjection;
		}
		else
		{
			auto buffer			= static_cast<Struct_W_V_P*>(m_constantBuffer->Map());
			buffer->world		= mWorld;
			buffer->view		= mView;
			buffer->projection	= mProjection;
		}

		// Unmap buffer
		m_constantBuffer->Unmap();

		SetBufferScope(m_constantBuffer.get(), slot);
	}

	void RI_Shader::SetBuffer(const Matrix& mWorld, const Matrix& mView, const Matrix& mProjection, const Vector4& color, unsigned int slot)
	{
		if (!m_constantBuffer)
		{
			LOG_WARNING("Shader: Can't map uninitialized buffer.");
			return;
		}

		// Get a pointer of the buffer
		auto buffer	= static_cast<Struct_WVP_Color*>(m_constantBuffer->Map());

		// Fill the buffer
		buffer->wvp		= mWorld * mView * mProjection;
		buffer->color	= color;

		// Unmap buffer
		m_constantBuffer->Unmap();

		SetBufferScope(m_constantBuffer.get(), slot);
	}

	void RI_Shader::SetBuffer(const Matrix& mWorld, const Matrix& mView, const Matrix& mProjection, const Vector2& resolution, unsigned slot)
	{
		if (!m_constantBuffer)
		{
			LOG_WARNING("Shader: Can't map uninitialized buffer.");
			return;
		}

		// Get a pointer of the buffer
		auto buffer = static_cast<Struct_WVP_Resolution*>(m_constantBuffer->Map());

		// Fill the buffer
		buffer->wvp			= mWorld * mView * mProjection;
		buffer->resolution	= resolution;
		buffer->padding		= Vector2::Zero;

		// Unmap buffer
		m_constantBuffer->Unmap();

		SetBufferScope(m_constantBuffer.get(), slot);
	}

	void RI_Shader::SetBuffer(const Matrix& mWVPortho, const Matrix& mWVPinv, const Matrix& mView, const Matrix& mProjection, const Vector2& resolution, Light* dirLight, Camera* camera, unsigned slot)
	{
		if (!m_constantBuffer)
		{
			LOG_WARNING("Shader: Can't map uninitialized buffer.");
			return;
		}

		// Get a pointer of the buffer
		auto buffer = static_cast<Struct_Shadowing*>(m_constantBuffer->Map());

		// Fill the buffer
		buffer->wvpOrtho				= mWVPortho;
		buffer->wvpInv					= mWVPinv;
		buffer->view					= mView;
		buffer->projection				= mProjection;
		buffer->projectionInverse		= mProjection.Inverted();
		buffer->mLightViewProjection[0] = dirLight->GetViewMatrix() * dirLight->GetOrthographicProjectionMatrix(0);
		buffer->mLightViewProjection[1] = dirLight->GetViewMatrix() * dirLight->GetOrthographicProjectionMatrix(1);
		buffer->mLightViewProjection[2] = dirLight->GetViewMatrix() * dirLight->GetOrthographicProjectionMatrix(2);
		buffer->shadowSplits			= Vector4(dirLight->GetShadowCascadeSplit(1), dirLight->GetShadowCascadeSplit(2), 0, 0);
		buffer->lightDir				= dirLight->GetDirection();
		buffer->shadowMapResolution		= (float)dirLight->GetShadowCascadeResolution();
		buffer->resolution				= resolution;
		buffer->nearPlane				= camera->GetNearPlane();
		buffer->farPlane				= camera->GetFarPlane();
		buffer->doShadowMapping			= dirLight->GetCastShadows();
		buffer->padding					= Vector3::Zero;

		// Unmap buffer
		m_constantBuffer->Unmap();

		SetBufferScope(m_constantBuffer.get(), slot);
	}

	void RI_Shader::Draw(unsigned int vertexCount)
	{
		if (!m_graphics)
			return;

		m_graphics->GetDeviceContext()->Draw(vertexCount, 0);
	}

	void RI_Shader::DrawIndexed(unsigned int indexCount)
	{
		if (!m_graphics)
			return;

		m_graphics->GetDeviceContext()->DrawIndexed(indexCount, 0, 0);
	}

	void RI_Shader::SetBufferScope(D3D11_ConstantBuffer* buffer, unsigned int slot)
	{
		if (!buffer)
			return;

		if (m_bufferScope == VertexShader)
		{
			buffer->SetVS(slot);
		}
		else if (m_bufferScope == PixelShader)
		{
			buffer->SetPS(slot);
		}
		else if (m_bufferScope == Global)
		{
			buffer->SetVS(slot);
			buffer->SetPS(slot);
		}
	}
}