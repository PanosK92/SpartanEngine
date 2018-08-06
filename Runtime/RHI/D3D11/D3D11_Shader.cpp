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

//= INCLUDES ===========================
#include "D3D11_InputLayout.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_Shader.h"
#include <d3dcompiler.h>
#include <sstream> 
#include "../../Logging/Log.h"
#include "../../FileSystem/FileSystem.h"
//======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	namespace D3D11_Shader
	{
		inline void LogD3DCompilerError(ID3D10Blob* errorMessage)
		{
			stringstream ss((char*)errorMessage->GetBufferPointer());
			string line;

			// Split into lines
			while (getline(ss, line, '\n'))
			{
				// Determine if it's a true error or just a warning
				if (line.find("error") != string::npos)
				{
					LOG_ERROR(line);
				}
				else
				{
					LOG_WARNING(line);
				}
			}
		}

		inline bool CompileShader(const string& filePath, D3D_SHADER_MACRO* macros, const string& entryPoint, const string& target, ID3DBlob** shaderBlobOut)
		{
			unsigned compileFlags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef DEBUG
			compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_PREFER_FLOW_CONTROL;
#endif

			// Load and compile from file
			ID3DBlob* errorBlob = nullptr;
			ID3DBlob* shaderBlob = nullptr;
			auto result = D3DCompileFromFile(
				FileSystem::StringToWString(filePath).c_str(),
				macros,
				D3D_COMPILE_STANDARD_FILE_INCLUDE,
				entryPoint.c_str(),
				target.c_str(),
				compileFlags,
				0,
				&shaderBlob,
				&errorBlob
			);

			// Handle any errors
			if (FAILED(result))
			{
				string shaderName = FileSystem::GetFileNameFromFilePath(filePath);
				// Log compilation warnings/errors
				if (errorBlob)
				{
					LogD3DCompilerError(errorBlob);
					SafeRelease(errorBlob);
				}
				else if (result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
				{
					LOGF_ERROR("D3D11_Shader::CompileShader: Failed to find shader \"%s\" with path \"%s\".", shaderName, filePath);
				}
				else
				{
					LOGF_ERROR("D3D11_Shader::CompileShader: An unknown error occured when trying to load and compile \"%s\"", shaderName);
				}
			}

			// Write to blob out
			*shaderBlobOut = shaderBlob;

			return SUCCEEDED(result);
		}

		inline bool CompileVertexShader(ID3D11Device* device, ID3D10Blob** vsBlob, ID3D11VertexShader** vertexShader, const string& path, const string& entrypoint, const string& profile, D3D_SHADER_MACRO* macros)
		{
			if (!CompileShader(path, macros, entrypoint, profile, vsBlob))
				return false;

			// Create the shader from the buffer.
			ID3D10Blob* vsb = *vsBlob;
			auto result = device->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, vertexShader);
			if (FAILED(result))
			{
				LOG_ERROR("D3D11_Shader::CompileVertexShader: Failed to create vertex shader.");
				return false;
			}

			return true;
		}

		inline bool CompilePixelShader(ID3D11Device* device, ID3D10Blob** psBlob, ID3D11PixelShader** pixelShader, const string& path, const string& entrypoint, const string& profile, D3D_SHADER_MACRO* macros)
		{
			auto result = CompileShader(path, macros, entrypoint, profile, psBlob);
			if (FAILED(result))
				return false;

			// Create the shader from the buffer.
			ID3D10Blob* psb = *psBlob;
			result = device->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, pixelShader);
			if (FAILED(result))
			{
				LOG_ERROR("D3D11_Shader::CompilePixelShader: Failed to create pixel shader.");
				return false;
			}

			return true;
		}

		inline std::pair<std::vector<D3D11_INPUT_ELEMENT_DESC>, std::vector<std::string>> Reflect(ID3D10Blob* vsBlob)
		{
			std::pair<std::vector<D3D11_INPUT_ELEMENT_DESC>, std::vector<std::string>> inputLayoutDesc;

			ID3D11ShaderReflection* reflector = nullptr;
			if (FAILED(D3DReflect(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), IID_ID3D11ShaderReflection, (void**)&reflector)))
			{
				LOG_ERROR("D3D11_Shader: Failed to reflect shader.");
				return inputLayoutDesc;
			}

			// Get shader info
			D3D11_SHADER_DESC shaderDesc;
			reflector->GetDesc(&shaderDesc);

			// Read input layout description from shader info
			for (unsigned int i = 0; i < shaderDesc.InputParameters; i++)
			{
				D3D11_SIGNATURE_PARAMETER_DESC paramDesc;
				reflector->GetInputParameterDesc(i, &paramDesc);

				// fill out input element desc
				D3D11_INPUT_ELEMENT_DESC elementDesc;

				inputLayoutDesc.second.emplace_back(paramDesc.SemanticName);
				elementDesc.SemanticName = nullptr;
				elementDesc.SemanticIndex = paramDesc.SemanticIndex;
				elementDesc.InputSlot = 0;
				elementDesc.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
				elementDesc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
				elementDesc.InstanceDataStepRate = 0;

				// determine DXGI format
				if (paramDesc.Mask == 1)
				{
					if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) elementDesc.Format = DXGI_FORMAT_R32_UINT;
					else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) elementDesc.Format = DXGI_FORMAT_R32_SINT;
					else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32_FLOAT;
					elementDesc.AlignedByteOffset = 4;
				}
				else if (paramDesc.Mask <= 3)
				{
					if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) elementDesc.Format = DXGI_FORMAT_R32G32_UINT;
					else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) elementDesc.Format = DXGI_FORMAT_R32G32_SINT;
					else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
					elementDesc.AlignedByteOffset = 8;
				}
				else if (paramDesc.Mask <= 7)
				{
					if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) elementDesc.Format = DXGI_FORMAT_R32G32B32_UINT;
					else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) elementDesc.Format = DXGI_FORMAT_R32G32B32_SINT;
					else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
					elementDesc.AlignedByteOffset = 12;
				}
				else if (paramDesc.Mask <= 15)
				{
					if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) elementDesc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
					else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) elementDesc.Format = DXGI_FORMAT_R32G32B32A32_SINT;
					else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
					elementDesc.AlignedByteOffset = 16;
				}

				inputLayoutDesc.first.push_back(elementDesc);
			}
			SafeRelease(reflector);

			return inputLayoutDesc;
		}

		inline bool SetInputLayout(ID3D11Device* device, ID3D10Blob* vsBlob, std::shared_ptr<D3D11_InputLayout> inputLayout, Input_Layout inputLayoutEnum)
		{
			bool result = false;

			// Create vertex input layout
			if (inputLayoutEnum != Input_Auto)
			{
				result = inputLayout->Create(vsBlob, inputLayoutEnum);
			}
			else
			{
				auto descPair = Reflect(vsBlob);
				vector<D3D11_INPUT_ELEMENT_DESC> inputLayoutDesc = descPair.first;
				for (unsigned int i = 0; i < inputLayoutDesc.size(); ++i)
				{
					inputLayoutDesc[i].SemanticName = descPair.second[i].c_str();
				}

				result = inputLayout->Create(vsBlob, &inputLayoutDesc[0], unsigned int(inputLayoutDesc.size()));
			}

			// If the creation was successful, release vsBlob else print a message
			if (result)
			{
				SafeRelease(vsBlob);
			}

			return result;
		}

		inline vector<D3D_SHADER_MACRO> GetD3DMacros(const std::map<std::string, std::string>& macros)
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
		m_rhiDevice			= rhiDevice;
		m_vertexShader		= nullptr;
		m_pixelShader		= nullptr;
		m_compiled			= false;
		m_hasVertexShader	= false;
		m_hasPixelShader	= false;
		m_D3D11InputLayout	= make_shared<D3D11_InputLayout>(m_rhiDevice);
	}

	RHI_Shader::~RHI_Shader()
	{
		SafeRelease((ID3D11VertexShader*)m_vertexShader);
		SafeRelease((ID3D11PixelShader*)m_pixelShader);
	}

	bool RHI_Shader::Compile_Vertex(const std::string& filePath, Input_Layout inputLayout)
	{
		m_filePath = filePath;

		vector<D3D_SHADER_MACRO> vsMacros = D3D11_Shader::GetD3DMacros(m_macros);
		vsMacros.push_back(D3D_SHADER_MACRO{ "COMPILE_VS", "1" });
		vsMacros.push_back(D3D_SHADER_MACRO{ "COMPILE_PS", "0" });
		vsMacros.push_back(D3D_SHADER_MACRO{ nullptr, nullptr });

		ID3D10Blob* blobVS	= nullptr;
		auto shaderPtr		= (ID3D11VertexShader**)&m_vertexShader;

		bool result = D3D11_Shader::CompileVertexShader(
			m_rhiDevice->GetDevice<ID3D11Device>(),
			&blobVS,
			shaderPtr,
			m_filePath,
			"DirectusVertexShader",
			"vs_5_0",
			&vsMacros.front()
		);

		// Set input layout
		if (result)
		{
			result = D3D11_Shader::SetInputLayout(m_rhiDevice->GetDevice<ID3D11Device>(), blobVS, m_D3D11InputLayout, inputLayout);
			if (!result)
			{
				LOGF_ERROR("D3D11_Shader::SetInputLayout: Failed to create vertex input layout for %s", FileSystem::GetFileNameFromFilePath(m_filePath).data());
			}
		}

		SafeRelease(blobVS);
		m_compiled = result;
		m_hasVertexShader = true;
		return result;
	}

	bool RHI_Shader::Compile_Pixel(const std::string& filePath)
	{
		m_filePath = filePath;

		vector<D3D_SHADER_MACRO> psMacros = D3D11_Shader::GetD3DMacros(m_macros);
		psMacros.push_back(D3D_SHADER_MACRO{ "COMPILE_VS", "0" });
		psMacros.push_back(D3D_SHADER_MACRO{ "COMPILE_PS", "1" });
		psMacros.push_back(D3D_SHADER_MACRO{ nullptr, nullptr });

		ID3D10Blob* blobPS	= nullptr;
		auto shaderPtr		= (ID3D11PixelShader**)&m_pixelShader;	

		m_compiled = D3D11_Shader::CompilePixelShader(
			m_rhiDevice->GetDevice<ID3D11Device>(),
			&blobPS,
			shaderPtr,
			m_filePath,
			"DirectusPixelShader",
			"ps_5_0",
			&psMacros.front()
		);

		SafeRelease(blobPS);
		m_hasPixelShader = true;
		return m_compiled;
	}
}