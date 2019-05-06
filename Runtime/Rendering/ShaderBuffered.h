/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES =============================
#include "../Core/EngineDefs.h"
#include "../World/Components/Light.h"
#include "../World/Components/Transform.h"
#include "../RHI/RHI_Shader.h"
#include "../RHI/RHI_Texture.h"
#include "../Math/Matrix.h"
#include "../Math/Vector2.h"
//========================================

namespace Spartan
{
	struct Struct_Blur
	{
		Struct_Blur(const Math::Vector2& direction, float sigma)
		{
			m_direction = direction;
			m_sigma = sigma;
		};

		Math::Vector2 m_direction;
		float m_sigma;
		float m_padding;
	};

	struct Struct_Matrix
	{
		Struct_Matrix(const Math::Matrix& matrix)
		{
			m_matrix = matrix;
		};

		Math::Matrix m_matrix;
	};

	struct Struct_Matrix_Matrix
	{
		Struct_Matrix_Matrix(const Math::Matrix& matrix1, const Math::Matrix& matrix2)
		{
			m_matrix1 = matrix1;
			m_matrix2 = matrix2;
		};

		Math::Matrix m_matrix1;
		Math::Matrix m_matrix2;
	};

	struct Struct_Matrix_Matrix_Float
	{
		Struct_Matrix_Matrix_Float(const Math::Matrix& matrix1, const Math::Matrix& matrix2, float value)
		{
			m_matrix1 = matrix1;
			m_matrix2 = matrix2;
			m_value = value;
			padding = Math::Vector3::Zero;
		};

		Math::Matrix m_matrix1;
		Math::Matrix m_matrix2;
		float m_value;
		Math::Vector3 padding;
	};

	struct Struct_Matrix_Vector4
	{
		Struct_Matrix_Vector4(const Math::Matrix& matrix, const Math::Vector4& vector4)
		{
			m_matrix = matrix;
			m_vector4 = vector4;
		}

		Math::Matrix m_matrix;
		Math::Vector4 m_vector4;
	};

	struct Struct_Matrix_Vector3
	{
		Struct_Matrix_Vector3(const Math::Matrix& matrix, const Math::Vector3& vector)
		{
			m_matrix	= matrix;
			m_vector	= vector;
			m_padding	= 0.0f;
		}

		Math::Matrix m_matrix;
		Math::Vector3 m_vector;
		float m_padding;
	};

	struct Struct_Matrix_Vector2
	{
		Struct_Matrix_Vector2(const Math::Matrix& matrix, const Math::Vector2& vector2, const Math::Vector2& padding = Math::Vector2::Zero)
		{
			m_matrix	= matrix;
			m_vector2	= vector2;
			m_padding	= padding;
		}

		Math::Matrix m_matrix;
		Math::Vector2 m_vector2;
		Math::Vector2 m_padding;
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
			m_matrix = matrix;
			m_vector3A = vector3A;
			m_vector3B = vector3B;
			m_padding = 0.0f;
			m_padding2 = 0.0f;
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
			m_world = world;
			m_wvp = world * view * projection;
			m_color = color;
			m_cameraPos = cameraPos;
			m_lightDir = lightDir;
			m_roughness = roughness;
			m_padding = 0.0f;
		}

		Math::Matrix m_world;
		Math::Matrix m_wvp;
		Math::Vector4 m_color;
		Math::Vector3 m_cameraPos;
		float m_roughness;
		Math::Vector3 m_lightDir;
		float m_padding;
	};

	struct Struct_ShadowMapping
	{
		Struct_ShadowMapping(const Math::Matrix& view_projection_inv, Light* light)
		{
			// Fill the buffer
			m_view_projection_inv = view_projection_inv;

			if (light)
			{
				auto light_view			= light->GetViewMatrix();
				m_view					= light_view;
				m_view_projection[0]	= light_view * light->GetProjectionMatrix(0);
				m_view_projection[1]	= light_view * light->GetProjectionMatrix(1);
				m_view_projection[2]	= light_view * light->GetProjectionMatrix(2);
				m_biases				= Math::Vector2(light->GetBias(), light->GetNormalBias());
				m_direction				= light->GetDirection();
				m_range					= light->GetRange();
				m_position				= light->GetTransform()->GetPosition();
				m_resolution			= static_cast<float>(light->GetShadowMap()->GetWidth());
			}
		}

		Math::Matrix m_view;
		Math::Matrix m_view_projection_inv;
		Math::Matrix m_view_projection[3];
		Math::Vector3 m_position;
		float m_resolution;
		Math::Vector3 m_direction;
		float m_range;
		Math::Vector2 m_biases;
		Math::Vector2 m_padding = Math::Vector2::Zero;
	};

	struct Struct_Matrix_Matrix_Vector2
	{
		Struct_Matrix_Matrix_Vector2
		(
			const Math::Matrix& matrix1,
			const Math::Matrix& matrix2,
			const Math::Vector2& vector,
			float value
		)
		{
			m_matrix1	= matrix1;
			m_matrix2	= matrix2;
			m_vector	= vector;
			m_value		= value;
			m_padding	= 0.0f;
		}

		Math::Matrix m_matrix1;
		Math::Matrix m_matrix2;
		Math::Vector2 m_vector;
		float m_value;
		float m_padding;
	};

	struct Struct_Matrix_Matrix_Vector3
	{
		Struct_Matrix_Matrix_Vector3
		(
			const Math::Matrix& matrix1,
			const Math::Matrix& matrix2,
			const Math::Vector3& vector
		)
		{
			m_matrix1	= matrix1;
			m_matrix2	= matrix2;
			m_vector	= vector;
			m_padding	= 0.0f;
		}

		Math::Matrix m_matrix1;
		Math::Matrix m_matrix2;
		Math::Vector3 m_vector;
		float m_padding;
	};

	class SPARTAN_CLASS ShaderBuffered : public RHI_Shader
	{
	public:
		ShaderBuffered(const std::shared_ptr<RHI_Device>& rhi_device);
		ShaderBuffered() = default;

		template <typename T>
		void AddBuffer()
		{
			m_buffers.emplace_back(std::make_shared<RHI_ConstantBuffer>(m_rhi_device))->Create<T>();

		}
		
		bool UpdateBuffer(void* data, unsigned int index = 0) const;
		const auto& GetConstantBuffer(unsigned int index = 0) { return m_buffers[index]; }

	private:
		std::vector<std::shared_ptr<RHI_ConstantBuffer>> m_buffers;
	};
}