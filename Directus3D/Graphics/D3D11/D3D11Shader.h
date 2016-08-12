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

#pragma once

//= LINKING ===========================
#pragma comment(lib, "d3dcompiler.lib")
//=====================================

//= INCLUDES ================
#include <vector>
#include "D3D11InputLayout.h"
#include "D3D11Sampler.h"
#include <set>
#include "../Graphics.h"
//===========================

class D3D11Shader
{
public:
	D3D11Shader();
	~D3D11Shader();

	void Initialize(Graphics* d3d11Device);
	bool Load(std::string path);
	bool SetInputLayout(InputLayout inputLayout);
	bool AddSampler(D3D11_FILTER filter, D3D11_TEXTURE_ADDRESS_MODE textureAddressMode, D3D11_COMPARISON_FUNC comparisonFunction);
	void Set();

	void SetName(std::string name);
	void AddDefine(LPCSTR name, LPCSTR definition);
	void AddDefine(LPCSTR name, int definition);
	void AddDefine(LPCSTR name, bool definition);
	bool IsCompiled() const;

private:
	//= COMPILATION ================================================================================================================================================================================
	bool CompileVertexShader(ID3D10Blob** vsBlob, ID3D11VertexShader** vertexShader, std::string path, LPCSTR entrypoint, LPCSTR profile, D3D_SHADER_MACRO* macros);
	bool CompilePixelShader(ID3D10Blob** psBlob, ID3D11PixelShader** pixelShader, std::string path, LPCSTR entrypoint, LPCSTR profile, D3D_SHADER_MACRO* macros);
	HRESULT CompileShader(std::string filePath, D3D_SHADER_MACRO* macros, LPCSTR entryPoint, LPCSTR target, ID3DBlob** shaderBlobOut);
	void ExportErrorDebugLog(ID3D10Blob* errorMessage);

	//= REFLECTION ============================
	std::vector<D3D11_INPUT_ELEMENT_DESC> Reflect(ID3D10Blob* shaderBlob) const;

	//= MISC ===========
	std::string m_name;
	std::string m_path;
	LPCSTR m_entrypoint;
	LPCSTR m_profile;
	bool m_compiled;
	std::vector<D3D11Sampler*> m_samplers;
	ID3D11VertexShader* m_vertexShader;
	ID3D11PixelShader* m_pixelShader;
	ID3D10Blob* m_VSBlob = nullptr;
	
	//= MACROS ============================
	std::vector<D3D_SHADER_MACRO> m_macros;
	std::set<std::string> m_definitionPool;

	//= INPUT LAYOUT ======================
	D3D11InputLayout* m_D3D11InputLayout;
	bool m_layoutHasBeenSet;

	//= DEPENDENCIES============
	Graphics* m_graphics;
};
