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

//= INCLUDES ===========================
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_Shader.h"
#include "../RHI_InputLayout.h"
#include <d3dcompiler.h>
#include <sstream> 
#include "../../Logging/Log.h"
#include "../../FileSystem/FileSystem.h"
#include "../../Core/GUIDGenerator.h"
//======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	namespace D3D11_Shader
	{
		inline bool CompileShader(const string& shader, D3D_SHADER_MACRO* macros, const char* entryPoint, const char* shaderModel, ID3DBlob** shaderBlobOut)
		{
			ID3DBlob* errorBlob		= nullptr;
			ID3DBlob* shaderBlob	= nullptr;

			unsigned compileFlags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
			#ifdef DEBUG
			compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_PREFER_FLOW_CONTROL;
			#endif

			// Deduce weather we should compile from memory
			bool isFile = FileSystem::IsSupportedShaderFile(shader);

			// Compile from file
			HRESULT result;
			if (isFile)
			{
				result = D3DCompileFromFile
				(
					FileSystem::StringToWString(shader).c_str(),
					macros,
					D3D_COMPILE_STANDARD_FILE_INCLUDE,
					entryPoint,
					shaderModel,
					compileFlags,
					0,
					&shaderBlob,
					&errorBlob
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
					entryPoint,
					shaderModel,
					compileFlags,
					0,
					&shaderBlob,
					&errorBlob
				);
			}

			// Log any compilation possible warnings and/or errors
			if (errorBlob)
			{
				stringstream ss((char*)errorBlob->GetBufferPointer());
				string line;
				while (getline(ss, line, '\n'))
				{
					bool isError = line.find("error") != string::npos;
					if (isError) LOG_ERROR(line) else LOG_WARNING(line);
				}

				SafeRelease(errorBlob);
			}

			// Log compilation failure
			if (FAILED(result))
			{
				string shaderName = FileSystem::GetFileNameFromFilePath(shader);
				if (result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
				{
					LOGF_ERROR("Failed to find shader \"%s\" with path \"%s\".", shaderName.c_str(), shader.c_str());
				}
				else
				{
					LOGF_ERROR("An error occurred when trying to load and compile \"%s\"", shaderName.c_str());
				}
			}

			// Write to blob out
			*shaderBlobOut = shaderBlob;

			return SUCCEEDED(result);
		}

		inline bool CompileVertexShader(ID3D11Device* device, ID3D10Blob** vsBlob, ID3D11VertexShader** vertexShader, const string& path, const char* entrypoint, const char* shaderModel, D3D_SHADER_MACRO* macros)
		{
			if (!device)
			{
				LOG_ERROR("Invalid device.");
				return false;
			}
			// Compile shader
			if (!CompileShader(path, macros, entrypoint, shaderModel, vsBlob))
				return false;

			// Create the shader from the buffer.
			ID3D10Blob* vsb = *vsBlob;
			if (FAILED(device->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, vertexShader)))
			{
				LOG_ERROR("Failed to create vertex shader.");
				return false;
			}

			return true;
		}

		inline bool CompilePixelShader(ID3D11Device* device, ID3D10Blob** psBlob, ID3D11PixelShader** pixelShader, const string& path, const char* entrypoint, const char* shaderModel, D3D_SHADER_MACRO* macros)
		{
			if (!device)
			{
				LOG_ERROR("Invalid device.");
				return false;
			}

			// Compile the shader
			if (!CompileShader(path, macros, entrypoint, shaderModel, psBlob))
				return false;

			// Create the shader from the buffer.
			ID3D10Blob* psb = *psBlob;
			if (FAILED(device->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, pixelShader)))
			{
				LOG_ERROR("Failed to create pixel shader.");
				return false;
			}

			return true;
		}

		inline vector<D3D_SHADER_MACRO> GetD3DMacros(const map<string, string>& macros)
		{
			vector<D3D_SHADER_MACRO> d3dMacros;	
			for (const auto& macro : macros)
			{
				D3D_SHADER_MACRO d3dMacro;
				d3dMacro.Name		= macro.first.c_str();
				d3dMacro.Definition = macro.second.c_str();
				d3dMacros.emplace_back(d3dMacro);
			}
			return d3dMacros;
		}
	}

	RHI_Shader::RHI_Shader(shared_ptr<RHI_Device> rhiDevice)
	{
		m_rhiDevice		= rhiDevice;
		m_inputLayout	= make_shared<RHI_InputLayout>(m_rhiDevice);
	}

	RHI_Shader::~RHI_Shader()
	{
		SafeRelease((ID3D11VertexShader*)m_vertexShader);
		SafeRelease((ID3D11PixelShader*)m_pixelShader);
	}

	bool RHI_Shader::API_CompileVertex(const string& shader, unsigned long inputLayout)
	{
		if (FileSystem::IsSupportedShaderFile(shader))
		{
			m_filePath = shader;
		}

		vector<D3D_SHADER_MACRO> vsMacros = D3D11_Shader::GetD3DMacros(m_macros);
		vsMacros.push_back(D3D_SHADER_MACRO{ "COMPILE_VS", "1" });
		vsMacros.push_back(D3D_SHADER_MACRO{ "COMPILE_PS", "0" });
		vsMacros.push_back(D3D_SHADER_MACRO{ nullptr, nullptr });

		ID3D10Blob* blobVS	= nullptr;
		auto shaderPtr		= (ID3D11VertexShader**)&m_vertexShader;

		// Compile the shader
		if (D3D11_Shader::CompileVertexShader(
			m_rhiDevice->GetDevice<ID3D11Device>(),
			&blobVS,
			shaderPtr,
			shader,
			VERTEX_SHADER_ENTRYPOINT,
			VERTEX_SHADER_MODEL,
			&vsMacros.front()))
		{
			// Create input layout
			if (!m_inputLayout->Create(blobVS, inputLayout))
			{
				LOGF_ERROR("Failed to create vertex input layout for %s", FileSystem::GetFileNameFromFilePath(m_filePath).c_str());
			}

			SafeRelease(blobVS);
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
			m_filePath = shader;
		}

		vector<D3D_SHADER_MACRO> psMacros = D3D11_Shader::GetD3DMacros(m_macros);
		psMacros.push_back(D3D_SHADER_MACRO{ "COMPILE_VS", "0" });
		psMacros.push_back(D3D_SHADER_MACRO{ "COMPILE_PS", "1" });
		psMacros.push_back(D3D_SHADER_MACRO{ nullptr, nullptr });

		ID3D10Blob* blobPS	= nullptr;
		auto shaderPtr		= (ID3D11PixelShader**)&m_pixelShader;	

		if (D3D11_Shader::CompilePixelShader(
			m_rhiDevice->GetDevice<ID3D11Device>(),
			&blobPS,
			shaderPtr,
			shader,
			PIXEL_SHADER_ENTRYPOINT,
			PIXEL_SHADER_MODEL,
			&psMacros.front()
		))
		{
			SafeRelease(blobPS);
			m_hasPixelShader = true;
		}
		else
		{
			m_hasPixelShader = false;
		}

		return m_hasPixelShader;
	}
}