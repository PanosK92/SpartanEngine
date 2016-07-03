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
#include "../IO/Log.h"
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

ShaderVariation* ShaderPool::CreateShaderBasedOnMaterial(
	bool albedo,
	bool roughness,
	bool metallic,
	bool occlusion,
	bool normal,
	bool height,
	bool mask,
	bool cubemap
)
{
	// If an appropriate shader already exists, return it's ID
	ShaderVariation* existingShader = FindMatchingShader(
		albedo, 
		roughness, 
		metallic, 
		occlusion, 
		normal, 
		height, 
		mask, 
		cubemap
	);

	if (existingShader)
		return existingShader;

	// If not, create a new one
	unique_ptr<ShaderVariation> shader(new ShaderVariation());
	shader->Initialize(
		albedo,
		roughness,
		metallic,
		occlusion,
		normal,
		height,
		mask,
		cubemap,
		m_D3D11Device
	);

	// Add the shader to the pool and return it
	m_shaders.push_back(move(shader));
	return m_shaders.back().get();
}

ShaderVariation* ShaderPool::GetShaderByID(string shaderID)
{
	for (int i = 0; i < m_shaders.size(); i++)
		if (m_shaders[i]->GetID() == shaderID)
			return m_shaders[i].get();

	return nullptr;
}

ShaderVariation* ShaderPool::FindMatchingShader(
	bool albedo,
	bool roughness,
	bool metallic,
	bool occlusion,
	bool normal,
	bool height,
	bool mask,
	bool cubemap
)
{
	for (int i = 0; i < m_shaders.size(); i++)
	{
		ShaderVariation* shader = m_shaders[i].get();
		
		if (shader->HasAlbedoTexture() != albedo) continue;
		if (shader->HasAlbedoTexture() != roughness) continue;
		if (shader->HasAlbedoTexture() != metallic) continue;
		if (shader->HasAlbedoTexture() != occlusion) continue;
		if (shader->HasAlbedoTexture() != normal) continue;
		if (shader->HasAlbedoTexture() != height) continue;
		if (shader->HasAlbedoTexture() != mask) continue;
		if (shader->HasAlbedoTexture() != cubemap) continue;

		return shader;
	}

	return nullptr;
}

void ShaderPool::Clear()
{
	m_shaders.clear();
	m_shaders.shrink_to_fit();
}
