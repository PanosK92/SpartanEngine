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

namespace Directus
{
	namespace D3D11_Shader
	{
		inline bool compile_shader(const string& shader, D3D_SHADER_MACRO* macros, const char* entry_point, const char* shader_model, ID3DBlob** shader_blob_out)
		{
			ID3DBlob* error_blob	= nullptr;
			ID3DBlob* shader_blob	= nullptr;

			unsigned int compile_flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
			#ifdef DEBUG
			compile_flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_PREFER_FLOW_CONTROL;
			#endif

			// Deduce weather we should compile from memory
			const auto is_file = FileSystem::IsSupportedShaderFile(shader);

			// Compile from file
			HRESULT result;
			if (is_file)
			{
				result = D3DCompileFromFile
				(
					FileSystem::StringToWString(shader).c_str(),
					macros,
					D3D_COMPILE_STANDARD_FILE_INCLUDE,
					entry_point,
					shader_model,
					compile_flags,
					0,
					&shader_blob,
					&error_blob
				);
			}
			else // Compile from memory
			{
				result = D3DCompile
				(
					shader.c_str(),
					shader.size(),
					nullptr,
					macros,
					nullptr,
					entry_point,
					shader_model,
					compile_flags,
					0,
					&shader_blob,
					&error_blob
				);
			}

			// Log any compilation possible warnings and/or errors
			if (error_blob)
			{
				stringstream ss(static_cast<char*>(error_blob->GetBufferPointer()));
				string line;
				while (getline(ss, line, '\n'))
				{
					const auto is_error = line.find("error") != string::npos;
					if (is_error) LOG_ERROR(line) else LOG_WARNING(line);
				}

				safe_release(error_blob);
			}

			// Log compilation failure
			if (FAILED(result))
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

			// Write to blob out
			*shader_blob_out = shader_blob;

			return SUCCEEDED(result);
		}

		inline bool compile_vertex_shader(ID3D11Device* device, ID3D10Blob** vs_blob, ID3D11VertexShader** vertex_shader, const string& path, const char* entry_point, const char* shader_model, D3D_SHADER_MACRO* macros)
		{
			if (!device)
			{
				LOG_ERROR("Invalid device.");
				return false;
			}
			// Compile shader
			if (!compile_shader(path, macros, entry_point, shader_model, vs_blob))
				return false;

			// Create the shader from the buffer.
			auto vsb = *vs_blob;
			if (FAILED(device->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, vertex_shader)))
			{
				LOG_ERROR("Failed to create vertex shader.");
				return false;
			}

			return true;
		}

		inline bool compile_pixel_shader(ID3D11Device* device, ID3D10Blob** ps_blob, ID3D11PixelShader** pixel_shader, const string& path, const char* entry_point, const char* shader_model, D3D_SHADER_MACRO* macros)
		{
			if (!device)
			{
				LOG_ERROR("Invalid device.");
				return false;
			}

			// Compile the shader
			if (!compile_shader(path, macros, entry_point, shader_model, ps_blob))
				return false;

			// Create the shader from the buffer.
			auto psb = *ps_blob;
			if (FAILED(device->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, pixel_shader)))
			{
				LOG_ERROR("Failed to create pixel shader.");
				return false;
			}

			return true;
		}

		inline vector<D3D_SHADER_MACRO> get_d3d_macros(const map<string, string>& macros)
		{
			vector<D3D_SHADER_MACRO> d3_d_macros;	
			for (const auto& macro : macros)
			{
				D3D_SHADER_MACRO d3_d_macro;
				d3_d_macro.Name			= macro.first.c_str();
				d3_d_macro.Definition	= macro.second.c_str();
				d3_d_macros.emplace_back(d3_d_macro);
			}
			return d3_d_macros;
		}
	}

	RHI_Shader::RHI_Shader(const shared_ptr<RHI_Device> rhi_device)
	{
		m_rhi_device		= rhi_device;
		m_input_layout	= make_shared<RHI_InputLayout>(m_rhi_device);
	}

	RHI_Shader::~RHI_Shader()
	{
		safe_release(static_cast<ID3D11VertexShader*>(m_vertex_shader));
		safe_release(static_cast<ID3D11PixelShader*>(m_pixel_shader));
	}

	bool RHI_Shader::API_CompileVertex(const string& shader, const unsigned long input_layout)
	{
		if (FileSystem::IsSupportedShaderFile(shader))
		{
			m_file_path = shader;
		}

		auto vs_macros = D3D11_Shader::get_d3d_macros(m_macros);
		vs_macros.push_back(D3D_SHADER_MACRO{ "COMPILE_VS", "1" });
		vs_macros.push_back(D3D_SHADER_MACRO{ "COMPILE_PS", "0" });
		vs_macros.push_back(D3D_SHADER_MACRO{ nullptr, nullptr });

		ID3D10Blob* blob_vs		= nullptr;
		const auto shader_ptr	= reinterpret_cast<ID3D11VertexShader**>(&m_vertex_shader);

		// Compile the shader
		if (D3D11_Shader::compile_vertex_shader(
			m_rhi_device->GetDevicePhysical<ID3D11Device>(),
			&blob_vs,
			shader_ptr,
			shader,
			VERTEX_SHADER_ENTRYPOINT,
			VERTEX_SHADER_MODEL,
			&vs_macros.front()))
		{
			// Create input layout
			if (!m_input_layout->Create(blob_vs, input_layout))
			{
				LOGF_ERROR("Failed to create vertex input layout for %s", FileSystem::GetFileNameFromFilePath(m_file_path).c_str());
			}

			safe_release(blob_vs);
			m_hasVertexShader	= true;
		}
		else
		{
			m_hasVertexShader	= false;
		}

		return m_hasVertexShader;
	}

	bool RHI_Shader::API_CompilePixel(const string& shader)
	{
		if (FileSystem::IsSupportedShaderFile(shader))
		{
			m_file_path = shader;
		}

		auto ps_macros = D3D11_Shader::get_d3d_macros(m_macros);
		ps_macros.push_back(D3D_SHADER_MACRO{ "COMPILE_VS", "0" });
		ps_macros.push_back(D3D_SHADER_MACRO{ "COMPILE_PS", "1" });
		ps_macros.push_back(D3D_SHADER_MACRO{ nullptr, nullptr });

		ID3D10Blob* blob_ps		= nullptr;
		const auto shader_ptr	= reinterpret_cast<ID3D11PixelShader**>(&m_pixel_shader);	

		if (D3D11_Shader::compile_pixel_shader(
			m_rhi_device->GetDevicePhysical<ID3D11Device>(),
			&blob_ps,
			shader_ptr,
			shader,
			PIXEL_SHADER_ENTRYPOINT,
			PIXEL_SHADER_MODEL,
			&ps_macros.front()
		))
		{
			safe_release(blob_ps);
			m_hasPixelShader = true;
		}
		else
		{
			m_hasPixelShader = false;
		}

		return m_hasPixelShader;
	}
}
#endif