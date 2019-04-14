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

//= IMPLEMENTATION ===============
#include "../RHI_Implementation.h"
#ifdef API_GRAPHICS_D3D11
//================================

//= INCLUDES ===========================
#include "../RHI_Device.h"
#include "../RHI_Shader.h"
#include "../RHI_InputLayout.h"
#include "../../Logging/Log.h"
#include "../../FileSystem/FileSystem.h"
#include <d3dcompiler.h>
#include <sstream> 
//======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
	RHI_Shader::~RHI_Shader()
	{
		safe_release(static_cast<ID3D11VertexShader*>(m_vertex_shader));
		safe_release(static_cast<ID3D11PixelShader*>(m_pixel_shader));
	}

	void* RHI_Shader::_Compile(const Shader_Type type, const string& shader, RHI_Vertex_Attribute_Type vertex_attributes /*= Vertex_Attribute_None*/)
	{
		if (!m_rhi_device)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return nullptr;
		}

		auto d3d11_device = m_rhi_device->GetContext()->device;
		if (!d3d11_device)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return nullptr;
		}

		// Compile flags
		unsigned int compile_flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
		#ifdef DEBUG
		compile_flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_PREFER_FLOW_CONTROL;
		#endif

		// Arguments
		auto entry_point	= (type == Shader_Vertex) ? _RHI_Shader::entry_point_vertex : _RHI_Shader::entry_point_pixel;
		auto target_profile	= (type == Shader_Vertex) ? "vs_" + _RHI_Shader::shader_model : "ps_" + _RHI_Shader::shader_model;

		// Defines
		vector<D3D_SHADER_MACRO> defines =
		{
			D3D_SHADER_MACRO{ "COMPILE_VS", type == Shader_Vertex ? "1" : "0" },
			D3D_SHADER_MACRO{ "COMPILE_PS", type == Shader_Pixel ? "1" : "0" }
		};
		for (const auto& define : m_defines)
		{
			defines.emplace_back(D3D_SHADER_MACRO{ define.first.c_str(), define.second.c_str() });
		}
		defines.emplace_back(D3D_SHADER_MACRO{ nullptr, nullptr });

		// Deduce weather we should compile from memory
		const auto is_file = FileSystem::IsSupportedShaderFile(shader);

		// Compile from file
		ID3DBlob* blob_error	= nullptr;
		ID3DBlob* shader_blob	= nullptr;
		HRESULT result;
		if (is_file)
		{
			wstring file_path = FileSystem::StringToWstring(shader);
			result = D3DCompileFromFile
			(
				file_path.c_str(),
				defines.data(),
				D3D_COMPILE_STANDARD_FILE_INCLUDE,
				entry_point.c_str(),
				target_profile.c_str(),
				compile_flags,
				0,
				&shader_blob,
				&blob_error
			);
		}
		else // Compile from memory
		{
			result = D3DCompile
			(
				shader.c_str(),
				shader.size(),
				nullptr,
				defines.data(),
				nullptr,
				entry_point.c_str(),
				target_profile.c_str(),
				compile_flags,
				0,
				&shader_blob,
				&blob_error
			);
		}

		// Log any compilation possible warnings and/or errors
		if (blob_error)
		{
			stringstream ss(static_cast<char*>(blob_error->GetBufferPointer()));
			string line;
			while (getline(ss, line, '\n'))
			{
				const auto is_error = line.find("error") != string::npos;
				if (is_error) LOG_ERROR(line) else LOG_WARNING(line);
			}

			safe_release(blob_error);
		}

		// Log compilation failure
		if (FAILED(result) || !shader_blob)
		{
			auto shader_name = FileSystem::GetFileNameFromFilePath(shader);
			if (result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
			{
				LOGF_ERROR("Failed to find shader \"%s\" with path \"%s\".", shader_name.c_str(), shader.c_str());
			}
			else
			{
				LOGF_ERROR("An error occurred when trying to load and compile \"%s\"", shader_name.c_str());
			}
		}

		// Create shader
		void* shader_view = nullptr;
		if (shader_blob)
		{
			if (type == Shader_Vertex)
			{
				ID3D11VertexShader* buffer_vertex = nullptr;
				if (FAILED(d3d11_device->CreateVertexShader(shader_blob->GetBufferPointer(), shader_blob->GetBufferSize(), nullptr, &buffer_vertex)))
				{
					LOG_ERROR("Failed to create vertex shader.");
				}
				shader_view = static_cast<void*>(buffer_vertex);

				// Create input layout
				if (vertex_attributes != Vertex_Attribute_None)
				{
					if (!m_input_layout->Create(shader_blob, vertex_attributes))
					{
						LOGF_ERROR("Failed to create input layout for %s", FileSystem::GetFileNameFromFilePath(m_file_path).c_str());
					}
				}
			}
			else if (type == Shader_Pixel)
			{
				ID3D11PixelShader* buffer_pixel = nullptr;
				if (FAILED(d3d11_device->CreatePixelShader(shader_blob->GetBufferPointer(), shader_blob->GetBufferSize(), nullptr, &buffer_pixel)))
				{
					LOG_ERROR("Failed to create pixel shader.");
				}
				shader_view = static_cast<void*>(buffer_pixel);
			}
		}

		safe_release(shader_blob);
		return shader_view;
	}
}
#endif