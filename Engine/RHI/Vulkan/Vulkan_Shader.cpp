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
#ifdef API_GRAPHICS_VULKAN
//================================

//= INCLUDES ===========================
#include "../RHI_Device.h"
#include "../RHI_Shader.h"
#include "../../Logging/Log.h"
#include "../../FileSystem/FileSystem.h"
#include <dxc/Support/WinIncludes.h>
#include <dxc/dxcapi.h>
#include <sstream> 
//======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	RHI_Shader::~RHI_Shader()
	{
		
	}

	void* RHI_Shader::_Compile(const Shader_Type type, const string& shader)
	{
		// temp
		LOG_TO_FILE(true);

		// Arguments
		auto entry_point	= FileSystem::StringToWstring((type == Shader_Vertex) ? _RHI_Shader::entry_point_vertex : _RHI_Shader::entry_point_pixel);
		auto target_profile	= FileSystem::StringToWstring((type == Shader_Vertex) ? "vs_" + _RHI_Shader::shader_model : "ps_" + _RHI_Shader::shader_model);
		vector<LPCWSTR> arguments =
		{		
			L"-spirv",
			L"-flegacy-macro-expansion"
			#ifdef DEBUG
			,L"-Zi"
			#endif
		};

		// Defines
		vector<DxcDefine> defines =
		{
			DxcDefine{ L"COMPILE_VS", type == Shader_Vertex ? L"1" : L"0" },
			DxcDefine{ L"COMPILE_PS", type == Shader_Pixel ? L"1" : L"0" }
		};
		for (const auto& define : m_defines)
		{
			defines.emplace_back
			(	DxcDefine
				{ 
					FileSystem::StringToWstring(define.first).c_str(), 
					FileSystem::StringToWstring(define.second).c_str() 
				} 
			);
		}

		// Create library instance
		IDxcLibrary* library = nullptr;
		DxcCreateInstance(CLSID_DxcLibrary, __uuidof(IDxcLibrary), reinterpret_cast<void**>(&library));

		// Get shader source as a buffer
		IDxcBlobEncoding* source_buffer = nullptr;
		{
			HRESULT result;

			if (FileSystem::IsSupportedShaderFile(shader))
			{
				auto file_path = FileSystem::StringToWstring(shader);
				result = library->CreateBlobFromFile(file_path.c_str(), nullptr, &source_buffer);
			}
			else // Source
			{
				result = library->CreateBlobWithEncodingFromPinned(shader.c_str(), static_cast<UINT32>(shader.size()), CP_UTF8, &source_buffer);
			}

			if (FAILED(result))
			{
				LOG_ERROR("Failed to create source buffer.");
				return nullptr;
			}
		}

		// Create include handler
		IDxcIncludeHandler* include_handler = nullptr;
		{
			if (FAILED(library->CreateIncludeHandler(&include_handler)))
			{
				LOG_ERROR("Failed to create include handler.");
				return nullptr;
			}
		}

		// Create compiler instance
		IDxcCompiler* compiler = nullptr;
		DxcCreateInstance(CLSID_DxcCompiler, __uuidof(IDxcCompiler), reinterpret_cast<void**>(&compiler));
	
		// Compile
		IDxcOperationResult* operation_result = nullptr;
		compiler->Compile(
			source_buffer,												// program text
			L"",														// file name, mostly for error messages
			entry_point.c_str(),										// entry point function
			target_profile.c_str(),										// target profile
			arguments.data(), static_cast<UINT32>(arguments.size()),	// compilation arguments
			defines.data(), static_cast<UINT32>(defines.size()),		// shader defines
			include_handler,											// handler for #include directives
			&operation_result
		);
		
		if (!operation_result)
		{
			LOG_ERROR("Failed to invoke compiler. The provided source was most likely invalid.");
			return nullptr;
		}

		// Get compilation status
		HRESULT compilation_status;
		operation_result->GetStatus(&compilation_status);
		void* blob_out = nullptr;

		// Check compilation status
		if (SUCCEEDED(compilation_status)) 
		{
			IDxcBlob* result_buffer;
			operation_result->GetResult(&result_buffer);
			blob_out = static_cast<void*>(result_buffer);
		}
		else // Failure
		{
			// Get error buffer
			IDxcBlobEncoding* error_buffer = nullptr;
			operation_result->GetErrorBuffer(&error_buffer);

			// Log warnings and errors
			if (error_buffer)
			{
				stringstream ss(string(static_cast<char*>(error_buffer->GetBufferPointer()), error_buffer->GetBufferSize()));
				string line;
				while (getline(ss, line, '\n'))
				{
					const auto is_error = line.find("error") != string::npos;
					if (is_error) LOG_ERROR(line) else LOG_WARNING(line);
				}
			}

			safe_release(error_buffer);
		}

		// Create shader
		// TODO

		// temp
		LOG_TO_FILE(false);

		safe_release(operation_result);
		return blob_out;
	}
}
#endif