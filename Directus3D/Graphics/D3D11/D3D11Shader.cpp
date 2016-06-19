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
#include "D3D11Device.h"
#include <sstream>  // std::ostringstream
#include "../../Misc/Globals.h"
#include "../../Misc/Settings.h"
#include "../../IO/Log.h"
#include "../../IO/FileHelper.h"
#include <d3dcompiler.h>
#include <fstream>
//==============================

//= NAMESPACES =====
using namespace std;
//==================

D3D11Shader::D3D11Shader()
{
	m_vertexShader = nullptr;
	m_pixelShader = nullptr;
	m_layout = nullptr;
	m_VSBlob = nullptr;
	m_compiled = false;
	m_D3D11Device = nullptr;
}

D3D11Shader::~D3D11Shader()
{
	DirectusSafeRelease(m_vertexShader);
	DirectusSafeRelease(m_pixelShader);
	DirectusSafeDelete(m_layout);

	// delete sampler
	vector<D3D11Sampler*>::iterator it;
	for (it = m_samplers.begin(); it < m_samplers.end(); ++it)
	{
		delete *it;
	}
	m_samplers.clear();
	m_samplers.shrink_to_fit();
}

void D3D11Shader::Initialize(D3D11Device* d3d11Device)
{
	m_D3D11Device = d3d11Device;

	// initialize input layout
	m_layout = new D3D11InputLayout();
	m_layout->Initialize(m_D3D11Device);
}

bool D3D11Shader::Load(string path)
{
	m_path = path;

	// Add defines that always exist by default
	AddDefine("RESOLUTION_WIDTH", RESOLUTION_WIDTH);
	AddDefine("RESOLUTION_HEIGHT", RESOLUTION_HEIGHT);

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

	DirectusSafeRelease(PSBlob);
	//==================================================================

	return m_compiled;
}

bool D3D11Shader::SetInputLayout(InputLayout inputLayout)
{
	if (!m_compiled)
	{
		LOG("Can't set input layout of a non-compiled shader.", Log::Error);
		return false;
	}

	// Create vertex input layout
	if (inputLayout != Auto)
		m_layoutHasBeenSet = m_layout->Create(m_VSBlob, inputLayout);
	else
	{
		vector<D3D11_INPUT_ELEMENT_DESC> inputLayoutDesc = Reflect(m_VSBlob);
		m_layoutHasBeenSet = m_layout->Create(m_VSBlob, &inputLayoutDesc[0], UINT(inputLayoutDesc.size()));
	}		

	// If the creation was successful, release vsBlob else print a message
	if (m_layoutHasBeenSet)
		DirectusSafeRelease(m_VSBlob);
	else
		LOG("Failed to create vertex input layout for " + FileHelper::GetFileNameFromPath(m_path) + ".", Log::Error);

	return m_layoutHasBeenSet;
}

bool D3D11Shader::AddSampler(D3D11_FILTER filter, D3D11_TEXTURE_ADDRESS_MODE textureAddressMode, D3D11_COMPARISON_FUNC comparisonFunction)
{
	D3D11Sampler* sampler = new D3D11Sampler();
	if (!sampler->Create(filter, textureAddressMode, comparisonFunction, m_D3D11Device))
	{
		LOG("Failed to create shader sampler", Log::Error);
		DirectusSafeDelete(sampler);
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
	m_layout->Set();

	// set the vertex and pixel shaders
	m_D3D11Device->GetDeviceContext()->VSSetShader(m_vertexShader, nullptr, 0);
	m_D3D11Device->GetDeviceContext()->PSSetShader(m_pixelShader, nullptr, 0);

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
	result = m_D3D11Device->GetDevice()->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, vertexShader);
	if (FAILED(result))
	{
		LOG("Failed to create vertex shader.", Log::Error);
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
	result = m_D3D11Device->GetDevice()->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, pixelShader);
	if (FAILED(result))
	{
		LOG("Failed to create pixel shader.", Log::Error);
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
		string shaderName = FileHelper::GetFileNameFromPath(filePath);
		if (errorBlob)
		{
			ExportErrorBlobAsText(errorBlob);
			LOG("Failed to compile shader. File = " + shaderName +
				", EntryPoint = " + entryPoint +
				", Target = " + target +
				". Check shaderError.txt for more details.",
				Log::Error);
			DirectusSafeRelease(errorBlob);
		}
		else if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
			LOG("Failed to find shader \"" + shaderName + " \" with path \"" + filePath + "\".", Log::Error);
		else
			LOG("An unknown error occured when trying to load and compile \"" + shaderName + "\"", Log::Error);
	}

	// Write to blob out
	*shaderBlobOut = shaderBlob;

	return hr;
}

void D3D11Shader::ExportErrorBlobAsText(ID3D10Blob* errorMessage)
{
	char* compileErrors;
	unsigned long bufferSize, i;
	ofstream fout;

	// Get a pointer to the error message text buffer.
	compileErrors = static_cast<char*>(errorMessage->GetBufferPointer());

	// Get the length of the message.
	bufferSize = errorMessage->GetBufferSize();

	// Open a file to write the error message to.
	fout.open("shaderError.txt");

	// Write out the error message.
	for (i = 0; i < bufferSize; i++)
		fout << compileErrors[i];

	// Close the file.
	fout.close();

	// Release the error message.
	errorMessage->Release();
}

//= REFLECTION ================================================================================================================
vector<D3D11_INPUT_ELEMENT_DESC> D3D11Shader::Reflect(ID3D10Blob* vsBlob) const
{
	vector<D3D11_INPUT_ELEMENT_DESC> inputLayoutDesc;

	ID3D11ShaderReflection* reflector = nullptr;
	if (FAILED(D3DReflect(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), IID_ID3D11ShaderReflection, (void**)&reflector)))
	{
		LOG("Failed to reflect shader", Log::Error);
		return inputLayoutDesc;
	}

	// Get shader info
	D3D11_SHADER_DESC shaderDesc;
	reflector->GetDesc(&shaderDesc);

	// Read input layout description from shader info
	for (UINT i = 0; i< shaderDesc.InputParameters; i++)
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
	DirectusSafeRelease(reflector);

	return inputLayoutDesc;
}
