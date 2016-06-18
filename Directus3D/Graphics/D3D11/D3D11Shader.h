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

//= INCLUDES =========================
#include "D3D11Device.h"
#include <vector>
#include "D3D11InputLayout.h"
#include "D3D11Sampler.h"
#include <set>
#include "../../Loading/ShaderLoader.h"

//====================================

class D3D11Shader
{
public:
	D3D11Shader();
	~D3D11Shader();

	void Initialize(D3D11Device* d3d11Device);
	bool Load(std::string path);
	bool SetInputLayout(InputLayout inputLayout);
	bool AddSampler(D3D11_FILTER filter, D3D11_TEXTURE_ADDRESS_MODE textureAddressMode, D3D11_COMPARISON_FUNC comparisonFunction);
	void Set();

	void SetName(std::string name);
	void AddDefine(LPCSTR name, LPCSTR definition);
	void AddDefine(LPCSTR name, int definition);
	void AddDefine(LPCSTR name, bool definition);
	bool IsCompiled();

private:
	D3D11Device* m_D3D11Device;
	ID3D11VertexShader* m_vertexShader;
	ID3D11PixelShader* m_pixelShader;
	D3D11InputLayout* m_layout;
	std::vector<D3D11Sampler*> m_samplers;
	ShaderLoader* m_shaderLoader;

	ID3D10Blob* m_VSBlob = nullptr;
	bool m_compiled;
	std::string m_name;

	std::vector<D3D_SHADER_MACRO> m_macros;
	std::set<std::string> m_definitionPool;
};
