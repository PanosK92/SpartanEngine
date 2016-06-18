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

//= INCLUDES ==========
#include "ShaderPool.h"
//=====================

using namespace std;

ShaderPool::ShaderPool(D3D11Device* d3d11device)
{
	m_D3D11Device = d3d11device;
}

ShaderPool::~ShaderPool()
{
	Clear();
}

ShaderVariation* ShaderPool::CreateShaderBasedOnMaterial(Material* material)
{
	if (!material)
		return nullptr;

	// If an appropriate shader already exists, return it's ID
	ShaderVariation* existingShader = FindMatchingShader(material);
	if (existingShader)
		return existingShader;

	// If not, create a new one
	ShaderVariation* shader = new ShaderVariation();
	shader->Initialize(material, m_D3D11Device);

	// Add the shader to the pool and return it
	m_shaders.push_back(shader);
	return shader;
}

ShaderVariation* ShaderPool::GetShaderByID(string shaderID)
{
	for (int i = 0; i < m_shaders.size(); i++)
		if (m_shaders[i]->GetID() == shaderID)
			return m_shaders[i];

	return nullptr;
}

ShaderVariation* ShaderPool::FindMatchingShader(Material* material)
{
	for (int i = 0; i < m_shaders.size(); i++)
		if (m_shaders[i]->MatchesMaterial(material))
			return m_shaders[i];

	return nullptr;
}

void ShaderPool::Clear()
{
	vector<ShaderVariation*>::iterator itA;
	for (itA = m_shaders.begin(); itA < m_shaders.end(); ++itA)
	{
		delete *itA;
	}
	m_shaders.clear();
	m_shaders.shrink_to_fit();
}
