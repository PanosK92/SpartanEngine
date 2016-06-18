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
	m_shaderLoader = new ShaderLoader();
	m_compiled = false;
	m_D3D11Device = nullptr;
}

D3D11Shader::~D3D11Shader()
{
	DirectusSafeRelease(m_vertexShader);
	DirectusSafeRelease(m_pixelShader);
	DirectusSafeDelete(m_layout);
	DirectusSafeDelete(m_shaderLoader);

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
	// Add defines that always exist by default
	AddDefine("RESOLUTION_WIDTH", RESOLUTION_WIDTH);
	AddDefine("RESOLUTION_HEIGHT", RESOLUTION_HEIGHT);

	//= Vertex shader =================================================
	vector<D3D_SHADER_MACRO> vsMacros = m_macros;
	vsMacros.push_back(D3D_SHADER_MACRO{"COMPILE_VS", "1"});
	vsMacros.push_back(D3D_SHADER_MACRO{"COMPILE_PS", "0"});
	vsMacros.push_back(D3D_SHADER_MACRO{nullptr, nullptr});

	m_compiled = m_shaderLoader->CompileVertexShader(
		&m_VSBlob,
		&m_vertexShader,
		path,
		"DirectusVertexShader",
		"vs_5_0",
		&vsMacros.front(),
		m_D3D11Device
	);
	//=================================================================

	if (!m_compiled)
		return false;

	//= Pixel shader ===================================================	
	vector<D3D_SHADER_MACRO> psMacros = m_macros;
	psMacros.push_back(D3D_SHADER_MACRO{"COMPILE_VS", "0"});
	psMacros.push_back(D3D_SHADER_MACRO{"COMPILE_PS", "1"});
	psMacros.push_back(D3D_SHADER_MACRO{nullptr, nullptr});

	ID3D10Blob* PSBlob = nullptr;
	m_compiled = m_shaderLoader->CompilePixelShader(
		&PSBlob,
		&m_pixelShader,
		path,
		"DirectusPixelShader",
		"ps_5_0",
		&psMacros.front(),
		m_D3D11Device
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

	bool success = m_layout->Create(m_VSBlob, inputLayout);
	DirectusSafeRelease(m_VSBlob);

	if (!success)
	{
		LOG("Failed to create the G-Buffer shader vertex input layout", Log::Error);
		return false;
	}

	return true;
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
	AddDefine(name, (int)definition);
}

bool D3D11Shader::IsCompiled()
{
	return m_compiled;
}
