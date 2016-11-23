/*
Copyright(c) 2016 Panos Karabelas

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

//= INCLUDES ===================
#include "D3D11Shader.h"
#include "D3D11Graphics.h"
#include "../../Core/Helper.h"
#include "../../Logging/Log.h"
#include "../../FileSystem/FileSystem.h"
#include <d3dcompiler.h>
#include <sstream> 
#include <fstream>
//==============================

//= NAMESPACES =====
using namespace std;
//==================

D3D11Shader::D3D11Shader()
{
	m_vertexShader = nullptr;
	m_pixelShader = nullptr;
	m_D3D11InputLayout = nullptr;
	m_VSBlob = nullptr;
	m_compiled = false;
	m_graphics = nullptr;
}

D3D11Shader::~D3D11Shader()
{
	SafeRelease(m_vertexShader);
	SafeRelease(m_pixelShader);

	// delete samplers
	m_samplers.clear();
	m_samplers.shrink_to_fit();
}

void D3D11Shader::Initialize(Graphics*graphicsDevice)
{
	m_graphics = graphicsDevice;

	// initialize input layout
	m_D3D11InputLayout = make_shared<D3D11InputLayout>();
	m_D3D11InputLayout->Initialize(m_graphics);
}

bool D3D11Shader::Load(string path)
{
	m_path = path;

	//= Vertex shader =================================================
	vector<D3D_SHADER_MACRO> vsMacros = m_macros;
	vsMacros.push_back(D3D_SHADER_MACRO{ "COMPILE_VS", "1" });
	vsMacros.push_back(D3D_SHADER_MACRO{ "COMPILE_PS", "0" });
	vsMacros.push_back(D3D_SHADER_MACRO{ nullptr, nullptr });

	m_compiled = CompileVertexShader(
		&m_VSBlob,
		&m_vertexShader,
		m_path,
		"DirectusVertexShader",
		"vs_5_0",
		&vsMacros.front()
	);
	//=================================================================

	if (!m_compiled)
		return false;

	//= Pixel shader ===================================================	
	vector<D3D_SHADER_MACRO> psMacros = m_macros;
	psMacros.push_back(D3D_SHADER_MACRO{ "COMPILE_VS", "0" });
	psMacros.push_back(D3D_SHADER_MACRO{ "COMPILE_PS", "1" });
	psMacros.push_back(D3D_SHADER_MACRO{ nullptr, nullptr });

	ID3D10Blob* PSBlob = nullptr;
	m_compiled = CompilePixelShader(
		&PSBlob,
		&m_pixelShader,
		path,
		"DirectusPixelShader",
		"ps_5_0",
		&psMacros.front()
	);

	SafeRelease(PSBlob);
	//==================================================================

	return m_compiled;
}

bool D3D11Shader::SetInputLayout(InputLayout inputLayout)
{
	if (!m_compiled)
	{
		LOG_ERROR("Can't set input layout of a non-compiled shader.");
		return false;
	}

	// Create vertex input layout
	if (inputLayout != Auto)
		m_layoutHasBeenSet = m_D3D11InputLayout->Create(m_VSBlob, inputLayout);
	else
	{
		vector<D3D11_INPUT_ELEMENT_DESC> inputLayoutDesc = Reflect(m_VSBlob);
		m_layoutHasBeenSet = m_D3D11InputLayout->Create(m_VSBlob, &inputLayoutDesc[0], UINT(inputLayoutDesc.size()));
	}

	// If the creation was successful, release vsBlob else print a message
	if (m_layoutHasBeenSet)
		SafeRelease(m_VSBlob);
	else
		LOG_ERROR("Failed to create vertex input layout for " + FileSystem::GetFileNameFromPath(m_path) + ".");

	return m_layoutHasBeenSet;
}

bool D3D11Shader::AddSampler(D3D11_FILTER filter, D3D11_TEXTURE_ADDRESS_MODE textureAddressMode, D3D11_COMPARISON_FUNC comparisonFunction)
{
	auto sampler = make_shared<D3D11Sampler>();
	if (!sampler->Create(filter, textureAddressMode, comparisonFunction, m_graphics))
	{
		LOG_ERROR("Failed to create shader sampler");
		return false;
	}

	m_samplers.push_back(sampler);

	return true;
}

void D3D11Shader::Set()
{
	if (!m_compiled)
		return;

	// set the vertex input layout.
	m_graphics->SetInputLayout(m_D3D11InputLayout->GetInputLayout());
	m_D3D11InputLayout->Set();

	// set the vertex and pixel shaders
	m_graphics->GetDeviceContext()->VSSetShader(m_vertexShader, nullptr, 0);
	m_graphics->GetDeviceContext()->PSSetShader(m_pixelShader, nullptr, 0);

	// set the samplers
	for (int i = 0; i < m_samplers.size(); i++)
		m_samplers[i]->Set(i);
}

void D3D11Shader::SetName(string name)
{
	m_name = name;
}

void D3D11Shader::AddDefine(LPCSTR name, LPCSTR definition) // All overloads resolve to this
{
	D3D_SHADER_MACRO newMacro;

	newMacro.Name = name;
	newMacro.Definition = definition;

	m_macros.push_back(newMacro);
}

void D3D11Shader::AddDefine(LPCSTR name, int definition)
{
	AddDefine(name, m_definitionPool.insert(to_string(definition)).first->c_str());
}

void D3D11Shader::AddDefine(LPCSTR name, bool definition)
{
	AddDefine(name, static_cast<int>(definition));
}

bool D3D11Shader::IsCompiled() const
{
	return m_compiled;
}

//= COMPILATION ================================================================================================================================================================================
bool D3D11Shader::CompileVertexShader(ID3D10Blob** vsBlob, ID3D11VertexShader** vertexShader, string path, LPCSTR entrypoint, LPCSTR profile, D3D_SHADER_MACRO* macros)
{
	HRESULT result = CompileShader(path, macros, entrypoint, profile, vsBlob);
	if (FAILED(result))
		return false;

	// Create the shader from the buffer.
	ID3D10Blob* vsb = *vsBlob;
	result = m_graphics->GetDevice()->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, vertexShader);
	if (FAILED(result))
	{
		LOG_ERROR("Failed to create vertex shader.");
		return false;
	}

	return true;
}

bool D3D11Shader::CompilePixelShader(ID3D10Blob** psBlob, ID3D11PixelShader** pixelShader, string path, LPCSTR entrypoint, LPCSTR profile, D3D_SHADER_MACRO* macros)
{
	HRESULT result = CompileShader(path, macros, entrypoint, profile, psBlob);
	if (FAILED(result))
		return false;

	ID3D10Blob* psb = *psBlob;
	// Create the shader from the buffer.
	result = m_graphics->GetDevice()->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, pixelShader);
	if (FAILED(result))
	{
		LOG_ERROR("Failed to create pixel shader.");
		return false;
	}

	return true;
}

wstring s2ws(const string& s)
{
	int len;
	int slength = int(s.length()) + 1;
	len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, nullptr, 0);
	wchar_t* buf = new wchar_t[len];
	MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, buf, len);
	wstring r(buf);
	delete[] buf;
	return r;
}

HRESULT D3D11Shader::CompileShader(string filePath, D3D_SHADER_MACRO* macros, LPCSTR entryPoint, LPCSTR target, ID3DBlob** shaderBlobOut)
{
	HRESULT hr;
	ID3DBlob* errorBlob = nullptr;
	ID3DBlob* shaderBlob = nullptr;

	unsigned compileFlags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
	compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_PREFER_FLOW_CONTROL;
#endif

	// Load and compile from file
	hr = D3DCompileFromFile(
		s2ws(filePath).c_str(),
		macros,
		D3D_COMPILE_STANDARD_FILE_INCLUDE,
		entryPoint,
		target,
		compileFlags,
		0,
		&shaderBlob,
		&errorBlob
	);

	// Handle any errors
	if (FAILED(hr))
	{
		string shaderName = FileSystem::GetFileNameFromPath(filePath);
		if (errorBlob)
		{
			ExportErrorDebugLog(errorBlob);
			SafeRelease(errorBlob);
		}
		else if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
			LOG_ERROR("Failed to find shader \"" + shaderName + " \" with path \"" + filePath + "\".");
		else
			LOG_ERROR("An unknown error occured when trying to load and compile \"" + shaderName + "\"");
	}

	// Write to blob out
	*shaderBlobOut = shaderBlob;

	return hr;
}

void D3D11Shader::ExportErrorDebugLog(ID3D10Blob* errorMessage)
{
	stringstream ss((char*)errorMessage->GetBufferPointer());
	string to;

	while (getline(ss, to, '\n'))
		LOG_ERROR(to);
}

//= REFLECTION ================================================================================================================
vector<D3D11_INPUT_ELEMENT_DESC> D3D11Shader::Reflect(ID3D10Blob* vsBlob) const
{
	vector<D3D11_INPUT_ELEMENT_DESC> inputLayoutDesc;

	ID3D11ShaderReflection* reflector = nullptr;
	if (FAILED(D3DReflect(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), IID_ID3D11ShaderReflection, (void**)&reflector)))
	{
		LOG_ERROR("Failed to reflect shader.");
		return inputLayoutDesc;
	}

	// Get shader info
	D3D11_SHADER_DESC shaderDesc;
	reflector->GetDesc(&shaderDesc);

	// Read input layout description from shader info
	for (UINT i = 0; i < shaderDesc.InputParameters; i++)
	{
		D3D11_SIGNATURE_PARAMETER_DESC paramDesc;
		reflector->GetInputParameterDesc(i, &paramDesc);

		// fill out input element desc
		D3D11_INPUT_ELEMENT_DESC elementDesc;
		elementDesc.SemanticName = paramDesc.SemanticName;
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
		}
		else if (paramDesc.Mask <= 3)
		{
			if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) elementDesc.Format = DXGI_FORMAT_R32G32_UINT;
			else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) elementDesc.Format = DXGI_FORMAT_R32G32_SINT;
			else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
		}
		else if (paramDesc.Mask <= 7)
		{
			if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) elementDesc.Format = DXGI_FORMAT_R32G32B32_UINT;
			else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) elementDesc.Format = DXGI_FORMAT_R32G32B32_SINT;
			else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
		}
		else if (paramDesc.Mask <= 15)
		{
			if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) elementDesc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
			else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) elementDesc.Format = DXGI_FORMAT_R32G32B32A32_SINT;
			else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		}

		inputLayoutDesc.push_back(elementDesc);
	}
	SafeRelease(reflector);

	return inputLayoutDesc;
}
