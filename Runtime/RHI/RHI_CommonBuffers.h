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

#pragma once

//= INCLUDES ==========================
#include "../Math/Matrix.h"
#include "../Math/Vector2.h"
#include "../Scene/Components/Light.h"
#include "../Scene/Components/Camera.h"
//=====================================

namespace Directus
{
	struct Struct_Matrix
	{
		Struct_Matrix(const Math::Matrix& matrix)
		{
			m_matrix = matrix;
		};

		Math::Matrix m_matrix;
	};

	struct Struct_Matrix_Vector4
	{
		Struct_Matrix_Vector4(const Math::Matrix& matrix, const Math::Vector4& vector4)
		{
			m_matrix	= matrix;
			m_vector4	= vector4;
		}

		Math::Matrix m_matrix;
		Math::Vector4 m_vector4;
	};

	struct Struct_Matrix_Vector3
	{
		Struct_Matrix_Vector3(const Math::Matrix& matrix, const Math::Vector3& vector3)
		{
			m_matrix	= matrix;
			m_vector3	= vector3;
			m_padding	= 0.0f;
		}

		Math::Matrix m_matrix;
		Math::Vector3 m_vector3;
		float m_padding;
	};

	struct Struct_Matrix_Vector2
	{
		Struct_Matrix_Vector2(const Math::Matrix& matrix, const Math::Vector2& vector2)
		{
			m_matrix	= matrix;
			m_vector2	= vector2;
			m_padding	= Math::Vector2::Zero;
		}

		Struct_Matrix_Vector2(const Math::Matrix& matrix, const Math::Vector2& vector2, const Math::Vector2& padding)
		{
			m_matrix	= matrix;
			m_vector2	= vector2;
			m_padding	= padding;
		}

		Math::Matrix m_matrix;
		Math::Vector2 m_vector2;
		Math::Vector2 m_padding;
	};

	struct Struct_Shadowing
	{
		Struct_Shadowing
		(
			const Math::Matrix& mWVPortho,
			const Math::Matrix& mWVPinv,
			const Math::Vector2& resolution,
			Light* dirLight,
			Camera* camera
		)
		{
			// Fill the buffer
			m_wvpOrtho			= mWVPortho;
			m_wvpInv			= mWVPinv;

			auto mLightView = dirLight->GetViewMatrix();
			m_mLightViewProjection[0] = mLightView * dirLight->ShadowMap_GetProjectionMatrix(0);
			m_mLightViewProjection[1] = mLightView * dirLight->ShadowMap_GetProjectionMatrix(1);
			m_mLightViewProjection[2] = mLightView * dirLight->ShadowMap_GetProjectionMatrix(2);

			m_shadowSplits			= Math::Vector4(dirLight->ShadowMap_GetSplit(0), dirLight->ShadowMap_GetSplit(1), 0, 0);
			m_lightDir				= dirLight->GetDirection();
			m_shadowMapResolution	= (float)dirLight->ShadowMap_GetResolution();
			m_resolution			= resolution;
			m_nearPlane				= camera->GetNearPlane();
			m_farPlane				= camera->GetFarPlane();
			m_doShadowMapping		= dirLight->GetCastShadows();
			m_padding				= Math::Vector3::Zero;
		}

		Math::Matrix m_wvpOrtho;
		Math::Matrix m_wvpInv;
		Math::Matrix m_mLightViewProjection[3];
		Math::Vector4 m_shadowSplits;
		Math::Vector3 m_lightDir;
		float m_shadowMapResolution;
		Math::Vector2 m_resolution;
		float m_nearPlane;
		float m_farPlane;
		float m_doShadowMapping;
		Math::Vector3 m_padding;
	};

	struct Struct_Matrix_Matrix_Matrix
	{
		Struct_Matrix_Matrix_Matrix(const Math::Matrix& matrix1, const Math::Matrix& matrix2, const Math::Matrix& matrix3)
		{
			m_matrix1 = matrix1;
			m_matrix2 = matrix2;
			m_matrix3 = matrix3;
		}

		Math::Matrix m_matrix1;
		Math::Matrix m_matrix2;
		Math::Matrix m_matrix3;
	};

	struct Struct_Matrix_Vector3_Vector3
	{
		Struct_Matrix_Vector3_Vector3(const Math::Matrix& matrix, const Math::Vector3& vector3A, const Math::Vector3& vector3B)
		{
			m_matrix	= matrix;
			m_vector3A	= vector3A;
			m_vector3B	= vector3B;
			m_padding	= 0.0f;
			m_padding2	= 0.0f;
		}

		Math::Matrix m_matrix;
		Math::Vector3 m_vector3A;
		float m_padding;
		Math::Vector3 m_vector3B;
		float m_padding2;
	};

	struct Struct_Transparency
	{
		Struct_Transparency(
			const Math::Matrix& world,
			const Math::Matrix& view,
			const Math::Matrix& projection,
			const Math::Vector4& color,
			const Math::Vector3& cameraPos,
			const Math::Vector3& lightDir,
			float roughness = 0.0f
		)
		{
			m_world			= world;
			m_view			= view;
			m_projection	= projection;
			m_wvp			= world * view * projection;
			m_color			= color;
			m_cameraPos		= cameraPos;
			m_lightDir		= lightDir;
			m_roughness		= roughness;
			m_padding		= 0.0f;
		}

		Math::Matrix m_world;
		Math::Matrix m_view;
		Math::Matrix m_projection;
		Math::Matrix m_wvp;
		Math::Vector4 m_color;
		Math::Vector3 m_cameraPos;
		float m_roughness;
		Math::Vector3 m_lightDir;
		float m_padding;
	};
}