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

//= INCLUDES =====================
#include "Material.h"
#include "../Pools/TexturePool.h"
#include "../IO/Serializer.h"
#include "../Core/GUIDGenerator.h"
#include "../Pools/ShaderPool.h"
#include "../IO/Log.h"
//================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

Material::Material(TexturePool* texturePool, ShaderPool* shaderPool)
{
	m_texturePool = texturePool;
	m_shaderPool = shaderPool;
	m_shader = nullptr;
	m_ID = GENERATE_GUID;
	m_name = "N/A";
	m_modelID = "N/A";
	culling = CullBack;
	opacity = 1.0f;
	alphaBlending = false;
	shadingMode = Physically_Based;
	colorAlbedo = false;
	roughness = false;
	metallic = false;
	occlusion = false;
	normalStrength = 0.0f;
	height = false;
	reflectivity = 0.0f;
	tiling = Vector2(1.0f, 1.0f);

	AcquireShader();
}

Material::~Material()
{
}

/*------------------------------------------------------------------------------
[I/O]
------------------------------------------------------------------------------*/
void Material::Serialize()
{
	Serializer::SaveSTR(m_ID);
	Serializer::SaveSTR(m_name);
	Serializer::SaveSTR(m_modelID);
	Serializer::SaveInt(culling);
	Serializer::SaveFloat(opacity);
	Serializer::SaveBool(alphaBlending);
	Serializer::SaveInt(shadingMode);
	Serializer::SaveVector4(colorAlbedo);
	Serializer::SaveFloat(roughness);
	Serializer::SaveFloat(metallic);
	Serializer::SaveFloat(occlusion);
	Serializer::SaveFloat(normalStrength);
	Serializer::SaveFloat(height);
	Serializer::SaveFloat(reflectivity);
	Serializer::SaveVector2(tiling);

	Serializer::SaveInt(int(m_textures.size()));
	for (auto i = 0; i < m_textures.size(); i++)
		Serializer::SaveSTR(m_textures[i]->GetID());
}

void Material::Deserialize()
{
	m_ID = Serializer::LoadSTR();
	m_name = Serializer::LoadSTR();
	m_modelID = Serializer::LoadSTR();
	culling = Culling(Serializer::LoadInt());
	opacity = Serializer::LoadFloat();
	alphaBlending = Serializer::LoadBool();
	shadingMode = ShadingMode(Serializer::LoadInt());
	colorAlbedo = Serializer::LoadVector4();
	roughness = Serializer::LoadFloat();
	metallic = Serializer::LoadFloat();
	occlusion = Serializer::LoadFloat();
	normalStrength = Serializer::LoadFloat();
	height = Serializer::LoadFloat();
	reflectivity = Serializer::LoadFloat();
	tiling = Serializer::LoadVector2();

	int textureCount = Serializer::LoadInt();
	for (int i = 0; i < textureCount; i++)
	{
		string textureID = Serializer::LoadSTR();
		shared_ptr<Texture> texture = m_texturePool->GetTextureByID(textureID);

		if (texture)
			m_textures.push_back(texture);
	}

	AcquireShader();
}

/*------------------------------------------------------------------------------
[TEXTURES]
------------------------------------------------------------------------------*/
void Material::SetTexture(shared_ptr<Texture> texture)
{
	if (!texture)
		return;

	if (HasTextureOfType(texture->GetType())) 	// Overwrite
	{
		int textureIndex = GetTextureIndexByType(texture->GetType());
		m_textures[textureIndex] = m_texturePool->Add(texture);
	}
	else // Add
		m_textures.push_back(m_texturePool->Add(texture));

	AdjustTextureDependentProperties();
	AcquireShader();
}

void Material::AdjustTextureDependentProperties()
{
	if (HasTextureOfType(Roughness))
		roughness = 1.0f;

	if (HasTextureOfType(Metallic))
		metallic = 1.0f;

	if (HasTextureOfType(Occlusion))
		occlusion = 1.0f;

	if (HasTextureOfType(Normal))
		normalStrength = 1.0f;

	if (HasTextureOfType(Height))
		height = 1.0f;
}

shared_ptr<Texture> Material::GetTextureByType(TextureType type)
{
	for (auto i = 0; i < m_textures.size(); i++)
	{
		if (m_textures[i]->GetType() == type)
			return m_textures[i];
	}

	return nullptr;
}

bool Material::HasTextureOfType(TextureType type)
{
	shared_ptr<Texture> texture = GetTextureByType(type);
	if (texture)
		return true;

	return false;
}

bool Material::HasTexture(string path)
{
	for (auto i = 0; i < m_textures.size(); i++)
		if (m_textures[i]->GetPath() == path)
			return true;

	return false;
}

string Material::GetTexturePathByType(TextureType type)
{
	shared_ptr<Texture> texture = GetTextureByType(type);
	if (texture)
		return texture->GetPath();

	return TEXTURE_PATH_UNKNOWN;
}

vector<string> Material::GetTexturePaths()
{
	vector<string> paths;
	for (auto i = 0; i < m_textures.size(); i++)
		paths.push_back(m_textures[i]->GetPath());

	return paths;
}

/*------------------------------------------------------------------------------
[SHADER]
------------------------------------------------------------------------------*/
void Material::AcquireShader()
{
	// Add a shader to the pool based on this material, if a 
	// matching shader already exists, it will be returned instead.
	m_shader = m_shaderPool->CreateShaderBasedOnMaterial(
		HasTextureOfType(Albedo),
		HasTextureOfType(Roughness),
		HasTextureOfType(Metallic),
		HasTextureOfType(Occlusion),
		HasTextureOfType(Normal),
		HasTextureOfType(Height),
		HasTextureOfType(Mask),
		HasTextureOfType(CubeMap)
	);
}

ShaderVariation* Material::GetShader()
{
	return m_shader;
}

bool Material::HasShader()
{
	if (GetShader())
		return true;

	return false;
}

ID3D11ShaderResourceView* Material::GetShaderResourceViewByTextureType(TextureType type)
{
	shared_ptr<Texture> texture = GetTextureByType(type);

	if (texture)
		return texture->GetID3D11ShaderResourceView();

	return nullptr;
}

/*------------------------------------------------------------------------------
[PROPERTIES]
------------------------------------------------------------------------------*/
void Material::SetID(string id)
{
	m_ID = id;
}

string Material::GetID()
{
	return m_ID;
}

void Material::SetName(string name)
{
	m_name = name;
}

string Material::GetName()
{
	return m_name;
}

void Material::SetModelID(string id)
{
	m_modelID = id;
}

string Material::GetModelID()
{
	return m_modelID;
}

void Material::SetFaceCulling(Culling culling)
{
	this->culling = culling;
}

Culling Material::GetFaceCulling()
{
	return culling;
}

void Material::SetOpacity(float opacity)
{
	this->opacity = opacity;

	if (opacity != 1.0f)
		alphaBlending = true;
	else
		alphaBlending = false;
}

float Material::GetOpacity()
{
	return opacity;
}

void Material::SetAlphaBlending(bool alphaBlending)
{
	this->alphaBlending = alphaBlending;
}

bool Material::GetAlphaBlending()
{
	return alphaBlending;
}

void Material::SetRoughness(float roughness)
{
	this->roughness = roughness;
}

float Material::GetRoughness()
{
	return roughness;
}

void Material::SetReflectivity(float reflectivity)
{
	this->reflectivity = reflectivity;
}

float Material::GetReflectivity()
{
	return reflectivity;
}

void Material::SetMetallic(float metallic)
{
	this->metallic = metallic;
}

float Material::GetMetallic()
{
	return metallic;
}

void Material::SetOcclusion(float occlusion)
{
	this->occlusion = occlusion;
}

float Material::GetOcclusion()
{
	return occlusion;
}

void Material::SetNormalStrength(float intensity)
{
	normalStrength = intensity;
}

float Material::GetNormalStrength()
{
	return normalStrength;
}

void Material::SetShadingMode(ShadingMode shadingMode)
{
	this->shadingMode = shadingMode;
}

ShadingMode Material::GetShadingMode()
{
	return shadingMode;
}

void Material::SetColorAlbedo(Vector4 color)
{
	colorAlbedo = color;
}

Vector4 Material::GetColorAlbedo()
{
	return colorAlbedo;
}

void Material::SetTiling(Vector2 tiling)
{
	this->tiling = tiling;
}

Vector2 Material::GetTiling()
{
	return tiling;
}

/*------------------------------------------------------------------------------
[HELPER FUNCTIONS]
------------------------------------------------------------------------------*/
int Material::GetTextureIndexByType(TextureType type)
{
	for (auto i = 0; i < m_textures.size(); i++)
		if (m_textures[i]->GetType() == type)
			return i;

	return -1;
}
