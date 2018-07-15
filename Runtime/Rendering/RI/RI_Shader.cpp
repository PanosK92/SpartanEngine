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
		if (!context)
		{
			LOG_ERROR("RI_Shader::RI_Shader: Invalid parameters");
			return;
		}

		m_rhi			= context->GetSubsystem<RHI>();		
		m_shader		= make_unique<D3D11_Shader>(m_rhi);
		m_bufferType	= CB_Matrix;
		m_bufferScope	= VertexShader;	
	}

	bool RI_Shader::Compile(const string& filePath, Input_Layout inputLayout)
	{	
		if (!m_shader->Compile(filePath))
		{
			LOGF_ERROR("RI_Shader::Compile: Failed to compile %s", filePath.c_str());
			return false;
		}
		m_shader->SetInputLayout(inputLayout);

		return true;
	}

	void RI_Shader::AddDefine(const char* define)
	{
		m_shader->AddDefine(define, "1");
	}

	void RI_Shader::AddBuffer(ConstantBufferType bufferType, ConstantBufferScope bufferScope)
	{
		m_bufferType		= bufferType;
		m_bufferScope		= bufferScope;
		m_constantBuffer	= make_unique<D3D11_ConstantBuffer>(m_rhi);

		switch (m_bufferType)
		{
			case CB_Matrix:
				m_constantBuffer->Create(sizeof(Struct_Matrix));
				break;
			case CB_Matrix_Vector4:
				m_constantBuffer->Create(sizeof(Struct_Matrix_Vector4));
				break;
			case CB_Matrix_Vector3:
				m_constantBuffer->Create(sizeof(Struct_Matrix_Vector3));
				break;
			case CB_Matrix_Vector2:
				m_constantBuffer->Create(sizeof(Struct_Matrix_Vector2));
				break;
			case CB_Matrix_Matrix_Matrix:
				m_constantBuffer->Create(sizeof(Struct_Matrix_Matrix_Matrix));
				break;
			case CB_Matrix_Vector3_Vector3:
				m_constantBuffer->Create(sizeof(Struct_Matrix_Vector3_Vector3));
				break;
			case CB_Shadowing:
				m_constantBuffer->Create(sizeof(Struct_Shadowing));
				break;
		}
	}

	bool RI_Shader::Bind()
	{
		if (!m_shader)
		{
			LOG_WARNING("RI_Shader::Bind_Buffer: Uninitialized shader.");
			return false;
		}

		return m_shader->Bind();
	}

	void RI_Shader::Bind_Buffer(const Math::Matrix& matrix, unsigned int slot)
	{
		if (!m_constantBuffer)
		{
			LOG_WARNING("RI_Shader::Bind_Buffer: Uninitialized buffer");
			return;
		}

		auto buffer = static_cast<Struct_Matrix*>(m_constantBuffer->Map());
		buffer->matrix = matrix;

		// Unmap buffer
		m_constantBuffer->Unmap();

		SetBufferScope(m_constantBuffer.get(), slot);
	}

	void RI_Shader::Bind_Buffer(const Matrix& matrix, const Vector4& vector, unsigned int slot)
	{
		if (!m_constantBuffer)
		{
			LOG_WARNING("RI_Shader::Bind_Buffer: Uninitialized buffer.");
			return;
		}

		// Get a pointer of the buffer
		auto buffer	= static_cast<Struct_Matrix_Vector4*>(m_constantBuffer->Map());

		// Fill the buffer
		buffer->matrix	= matrix;
		buffer->vector4	= vector;

		// Unmap buffer
		m_constantBuffer->Unmap();

		SetBufferScope(m_constantBuffer.get(), slot);
	}

	void RI_Shader::Bind_Buffer(const Matrix& matrix, const Math::Vector3& vector3, unsigned int slot)
	{
		if (!m_constantBuffer)
		{
			LOG_WARNING("RI_Shader::Bind_Buffer: Uninitialized buffer.");
			return;
		}

		// Get a pointer of the buffer
		auto buffer = static_cast<Struct_Matrix_Vector3*>(m_constantBuffer->Map());

		// Fill the buffer
		buffer->matrix	= matrix;
		buffer->vector3 = vector3;
		buffer->padding = 0.0f;

		// Unmap buffer
		m_constantBuffer->Unmap();

		SetBufferScope(m_constantBuffer.get(), slot);
	}

	void RI_Shader::Bind_Buffer(const Matrix& matrix, const Vector2& vector2, unsigned slot)
	{
		if (!m_constantBuffer)
		{
			LOG_WARNING("RI_Shader::Bind_Buffer: Uninitialized buffer.");
			return;
		}

		// Get a pointer of the buffer
		auto buffer = static_cast<Struct_Matrix_Vector2*>(m_constantBuffer->Map());

		// Fill the buffer
		buffer->matrix		= matrix;
		buffer->vector2		= vector2;
		buffer->padding		= Vector2::Zero;

		// Unmap buffer
		m_constantBuffer->Unmap();

		SetBufferScope(m_constantBuffer.get(), slot);
	}

	void RI_Shader::Bind_Buffer(const Matrix& mWVPortho, const Matrix& mWVPinv, const Matrix& mView, const Matrix& mProjection, const Vector2& resolution, Light* dirLight, Camera* camera, unsigned slot)
	{
		if (!m_constantBuffer)
		{
			LOG_WARNING("RI_Shader::Bind_Buffer: Uninitialized buffer.");
			return;
		}

		// Get a pointer of the buffer
		auto buffer = (Struct_Shadowing*)m_constantBuffer->Map();

		// Fill the buffer
		buffer->wvpOrtho				= mWVPortho;
		buffer->wvpInv					= mWVPinv;
		buffer->view					= mView;
		buffer->projection				= mProjection;
		buffer->projectionInverse		= mProjection.Inverted();

		auto mLightView = dirLight->ComputeViewMatrix();
		buffer->mLightViewProjection[0] = mLightView * dirLight->ShadowMap_ComputeProjectionMatrix(0);
		buffer->mLightViewProjection[1] = mLightView * dirLight->ShadowMap_ComputeProjectionMatrix(1);
		buffer->mLightViewProjection[2] = mLightView * dirLight->ShadowMap_ComputeProjectionMatrix(2);

		buffer->shadowSplits			= Vector4(dirLight->ShadowMap_GetSplit(0), dirLight->ShadowMap_GetSplit(1), 0, 0);
		buffer->lightDir				= dirLight->GetDirection();
		buffer->shadowMapResolution		= (float)dirLight->ShadowMap_GetResolution();
		buffer->resolution				= resolution;
		buffer->nearPlane				= camera->GetNearPlane();
		buffer->farPlane				= camera->GetFarPlane();
		buffer->doShadowMapping			= dirLight->GetCastShadows();
		buffer->padding					= Vector3::Zero;

		// Unmap buffer
		m_constantBuffer->Unmap();

		SetBufferScope(m_constantBuffer.get(), slot);
	}

	void RI_Shader::Bind_Buffer(const Matrix& m1, const Matrix& m2, const Matrix& m3, unsigned int slot)
	{
		if (!m_constantBuffer)
		{
			LOG_WARNING("RI_Shader::Bind_Buffer: Uninitialized buffer.");
			return;
		}

		auto buffer = static_cast<Struct_Matrix_Matrix_Matrix*>(m_constantBuffer->Map());
		buffer->m1 = m1;
		buffer->m2 = m2;
		buffer->m3 = m3;

		// Unmap buffer
		m_constantBuffer->Unmap();

		SetBufferScope(m_constantBuffer.get(), slot);
	}

	void RI_Shader::Bind_Buffer(const Math::Matrix& matrix, const Math::Vector3& vector3A, const Math::Vector3& vector3B, unsigned int slot)
	{
		if (!m_constantBuffer)
		{
			LOG_WARNING("RI_Shader::Bind_Buffer: Uninitialized buffer.");
			return;
		}

		// Get a pointer of the buffer
		auto buffer = static_cast<Struct_Matrix_Vector3_Vector3*>(m_constantBuffer->Map());

		// Fill the buffer
		buffer->matrix		= matrix;
		buffer->vector3A	= vector3A;
		buffer->padding		= 0.0f;
		buffer->vector3B	= vector3B;
		buffer->padding2	= 0.0f;

		// Unmap buffer
		m_constantBuffer->Unmap();

		SetBufferScope(m_constantBuffer.get(), slot);
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