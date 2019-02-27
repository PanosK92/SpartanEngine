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

//= INCLUDES ========================
#include "RHI_Shader.h"
#include "RHI_InputLayout.h"
#include "RHI_ConstantBuffer.h"
#include "../Logging/Log.h"
#include "../FileSystem/FileSystem.h"
#include "../Core/Context.h"
#include "../Threading/Threading.h"
#include <dxc/Support/WinIncludes.h>
#include <dxc/dxcapi.h>
#include <sstream> 
//===================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	RHI_Shader::RHI_Shader(const shared_ptr<RHI_Device> rhi_device)
	{
		m_rhi_device	= rhi_device;
		m_input_layout	= make_shared<RHI_InputLayout>(rhi_device);
	}

	void RHI_Shader::Compile(Shader_Type type, const string& shader, const unsigned long input_layout /*= 0*/)
	{
		// Compile
		if (type == Shader_Vertex)
		{
			m_compilation_state = Shader_Compiling;
			m_has_shader_vertex = Compile_Vertex(shader, input_layout);
			m_compilation_state = m_has_shader_vertex ? Shader_Built : Shader_Failed;
		}
		else if (type == Shader_Pixel)
		{
			m_compilation_state = Shader_Compiling;
			m_has_shader_pixel	= Compile_Pixel(shader);
			m_compilation_state = m_has_shader_pixel ? Shader_Built : Shader_Failed;
		}
		else if (type == Shader_VertexPixel)
		{
			m_compilation_state = Shader_Compiling;
			m_has_shader_vertex = Compile_Vertex(shader, input_layout);
			m_has_shader_pixel	= Compile_Pixel(shader);
			m_compilation_state = (m_has_shader_vertex && m_has_shader_pixel) ? Shader_Built : Shader_Failed;
		}

		// Log result
		if (m_compilation_state == Shader_Built)
		{
			LOGF_INFO("Successfully compiled %s", shader.c_str());
		}
		else if (m_compilation_state == Shader_Failed)
		{
			LOGF_ERROR("Failed to compile %s", shader.c_str());
		}
	}

	void RHI_Shader::Compile_Async(Context* context, Shader_Type type, const string& shader, unsigned long input_layout /*= 0*/)
	{
		context->GetSubsystem<Threading>()->AddTask([this, type, shader, input_layout]()
		{
			Compile(type, shader, input_layout);
		});
	}

	void RHI_Shader::AddDefine(const string& define, const string& value /*= "1"*/)
	{
		m_defines[define] = value;
	}

	bool RHI_Shader::UpdateBuffer(void* data) const
	{
		if (!data)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		if (!m_constant_buffer)
		{
			LOG_WARNING("Uninitialized buffer.");
			return false;
		}

		// Get a pointer of the buffer
		auto result = false;
		if (const auto buffer = m_constant_buffer->Map())	// Get buffer pointer
		{
			memcpy(buffer, data, m_buffer_size);			// Copy data
			result = m_constant_buffer->Unmap();			// Unmap buffer
		}

		if (!result)
		{
			LOG_ERROR("Failed to map buffer");
		}
		return result;
	}

	void RHI_Shader::CreateConstantBuffer(unsigned int size)
	{
		m_constant_buffer = make_shared<RHI_ConstantBuffer>(m_rhi_device, size);
	}

	void* RHI_Shader::CompileDXC(Shader_Type type, const string& shader)
	{
		// Arguments
		vector<LPCWSTR> arguments =
		{
			FileSystem::StringToWstring("-T " + (type == Shader_Vertex) ? "vs_" + _RHI_Shader::shader_model : "ps_" + _RHI_Shader::shader_model).c_str(),	// shader model
			FileSystem::StringToWstring("-E " + (type == Shader_Vertex) ? _RHI_Shader::entry_point_vertex : _RHI_Shader::entry_point_pixel).c_str(),		// entry point
			#ifdef DEBUG
			L"-Zi"
			#endif
		};

		// Defines
		vector<DxcDefine> defines;
		for (const auto& define : m_defines)
		{
			defines.emplace_back(DxcDefine{ FileSystem::StringToWstring(define.first).c_str(), FileSystem::StringToWstring(define.second).c_str() } );
		}
		if (type == Shader_Vertex)
		{
			defines.emplace_back(DxcDefine{ L"COMPILE_VS", L"1" });
			defines.emplace_back(DxcDefine{ L"COMPILE_PS", L"0" });
		}
		else if(type == Shader_Pixel)
		{
			defines.emplace_back(DxcDefine{ L"COMPILE_VS", L"0" });
			defines.emplace_back(DxcDefine{ L"COMPILE_PS", L"1" });
		}
		defines.emplace_back(DxcDefine{ nullptr, nullptr });

		IDxcLibrary* library;
		IDxcBlobEncoding* source;
		DxcCreateInstance(CLSID_DxcLibrary, __uuidof(IDxcLibrary), (void**)&library);
		library->CreateBlobWithEncodingFromPinned(shader.c_str(), (UINT32)shader.size(), CP_UTF8, &source);

		LPCWSTR ppArgs[] = { L"/Zi" }; // debug info
		IDxcCompiler* compiler;
		DxcCreateInstance(CLSID_DxcCompiler, __uuidof(IDxcCompiler), (void**)&compiler);
	
		// Compile
		IDxcOperationResult* compilation_result;
		compiler->Compile(
			source,						// program text
			nullptr,					// file name, mostly for error messages
			nullptr,					// entry point function
			nullptr,					// target profile
			arguments.data(),			// compilation arguments
			(UINT32)arguments.size(),	// number of compilation arguments
			nullptr, 0,					// name/value defines and their count
			nullptr,					// handler for #include directives
			&compilation_result
		);

		// Get compilation status
		HRESULT compilation_status;
		compilation_result->GetStatus(&compilation_status);
		void* blob_out = nullptr;

		// Check compilation status
		if (SUCCEEDED(compilation_status)) 
		{
			IDxcBlob* result_buffer;
			compilation_result->GetResult(&result_buffer);
			blob_out = static_cast<void*>(result_buffer);
		}
		else // Failure
		{
			// Get error buffer
			IDxcBlobEncoding* error_buffer = nullptr;
			compilation_result->GetErrorBuffer(&error_buffer);

			// Get error buffer in preferred encoding
			IDxcBlobEncoding* error_buffer_16 = nullptr;
			library->GetBlobAsUtf16(error_buffer, &error_buffer_16);

			// Log warnings and errors
			if (error_buffer_16)
			{
				stringstream ss(static_cast<char*>(error_buffer_16->GetBufferPointer()));
				string line;
				while (getline(ss, line, '\n'))
				{
					const auto is_error = line.find("error") != string::npos;
					if (is_error) LOG_ERROR(line) else LOG_WARNING(line);
				}
			}

			safe_release(error_buffer);
			safe_release(error_buffer_16);
		}
		safe_release(compilation_result);

		return blob_out;
	}
}