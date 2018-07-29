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

//= INCLUDES ==========================
#include "RHI_Shader.h"
#include "D3D11/D3D11_Shader.h"
#include "D3D11/D3D11_RenderTexture.h"
#include "RHI_Implementation.h"
#include "../Logging/Log.h"
#include "../Core/Context.h"
#include "../Scene/Components/Light.h"
#include "../Scene/Components/Camera.h"
#include "RHI_CommonBuffers.h"
//=====================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{

	RHI_Shader::RHI_Shader(RHI_Device* rhiDevice)
	{
		if (!rhiDevice)
		{
			LOG_ERROR("RI_Shader::RI_Shader: Invalid parameter");
			return;
		}

		m_rhiDevice		= rhiDevice;
		m_bufferScope	= BufferScope_Global;
	}

	void RHI_Shader::AddDefine(const char* define)
	{
		if (!m_shader)
		{
			m_shader = make_shared<D3D11_Shader>(m_rhiDevice);
		}

		m_shader->AddDefine(define, "1");
	}

	bool RHI_Shader::Compile(const string& filePath, Input_Layout inputLayout)
	{	
		if (!m_shader)
		{
			m_shader = make_shared<D3D11_Shader>(m_rhiDevice);
		}

		if (!m_shader->Compile(filePath))
		{
			LOGF_ERROR("RI_Shader::Compile: Failed to compile %s", filePath.c_str());
			return false;
		}
		m_shader->SetInputLayout(inputLayout);

		return true;
	}

	void RHI_Shader::Bind_Buffer(const Math::Matrix& matrix, unsigned int slot)
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

		m_rhiDevice->GetPipelineState()->SetConstantBuffer(m_constantBuffer, slot, m_bufferScope);
	}

	void RHI_Shader::Bind_Buffer(const Matrix& matrix, const Vector4& vector, unsigned int slot)
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

		m_rhiDevice->GetPipelineState()->SetConstantBuffer(m_constantBuffer, slot, m_bufferScope);
	}

	void RHI_Shader::Bind_Buffer(const Matrix& matrix, const Math::Vector3& vector3, unsigned int slot)
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

		m_rhiDevice->GetPipelineState()->SetConstantBuffer(m_constantBuffer, slot, m_bufferScope);
	}

	void RHI_Shader::Bind_Buffer(const Matrix& matrix, const Vector2& vector2, unsigned slot)
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

		m_rhiDevice->GetPipelineState()->SetConstantBuffer(m_constantBuffer, slot, m_bufferScope);
	}

	void RHI_Shader::Bind_Buffer(const Matrix& mWVPortho, const Matrix& mWVPinv, const Matrix& mView, const Matrix& mProjection, const Vector2& resolution, Light* dirLight, Camera* camera, unsigned slot)
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

		m_rhiDevice->GetPipelineState()->SetConstantBuffer(m_constantBuffer, slot, m_bufferScope);
	}

	void RHI_Shader::Bind_Buffer(const Matrix& m1, const Matrix& m2, const Matrix& m3, unsigned int slot)
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

		m_rhiDevice->GetPipelineState()->SetConstantBuffer(m_constantBuffer, slot, m_bufferScope);
	}

	void RHI_Shader::Bind_Buffer(const Math::Matrix& matrix, const Math::Vector3& vector3A, const Math::Vector3& vector3B, unsigned int slot)
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

		m_rhiDevice->GetPipelineState()->SetConstantBuffer(m_constantBuffer, slot, m_bufferScope);
	}

}