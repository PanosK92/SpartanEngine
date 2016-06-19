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

//= INCLUDES ==============================
#include <vector>
#include "Texture.h"
#include "../Graphics/D3D11/D3D11Shader.h"
#include "../Math/Vector2.h"
#include "../Math/Vector4.h"
//========================================

class ShaderVariation;
class ShaderPool;
class TexturePool;

enum ShadingMode
{
	Physically_Based,
	Unlit,
	Skysphere
};

enum Culling
{
	CullBack,
	CullFront,
	CullNone
};

class __declspec(dllexport) Material
{
public:
	Material(TexturePool* texturePool, ShaderPool* shaderPool);
	~Material();

	/*------------------------------------------------------------------------------
										[I/O]
	------------------------------------------------------------------------------*/
	void Serialize();
	void Deserialize();

	/*------------------------------------------------------------------------------
										[TEXTURES]
	------------------------------------------------------------------------------*/
	void AddTexture(Texture* texture);
	Texture* GetTextureByType(TextureType type);
	bool HasTextureOfType(TextureType type);
	bool HasTexture(std::string path);
	std::string GetTexturePathByType(TextureType type);
	std::vector<std::string> GetTexturePaths();

	/*------------------------------------------------------------------------------
									[SHADER]
	------------------------------------------------------------------------------*/
	void AcquireShader();
	ShaderVariation* GetShader();
	bool HasShader();
	ID3D11ShaderResourceView* GetShaderResourceViewByTextureType(TextureType type);

	/*------------------------------------------------------------------------------
									[PROPERTIES]
	------------------------------------------------------------------------------*/
	void SetID(std::string id);
	std::string GetID();

	void SetName(std::string name);
	std::string GetName();

	void SetModelID(std::string id);
	std::string GetModelID();

	void SetFaceCulling(Culling backFaceCulling);
	Culling GetFaceCulling();

	void SetOpacity(float opacity);
	float GetOpacity();

	void SetAlphaBlending(bool alphaBlending);
	bool GetAlphaBlending();

	void SetRoughness(float roughness);
	float GetRoughness();

	void SetMetallic(float metallic);
	float GetMetallic();

	void SetOcclusion(float occlusion);
	float GetOcclusion();

	void SetReflectivity(float reflectivity);
	float GetReflectivity();

	void SetNormalStrength(float strength);
	float GetNormalStrength();

	void SetShadingMode(ShadingMode shadingMode);
	ShadingMode GetShadingMode();

	void SetColorAlbedo(Directus::Math::Vector4 color);
	Directus::Math::Vector4 GetColorAlbedo();

	void SetTiling(Directus::Math::Vector2 tiling);
	Directus::Math::Vector2 GetTiling();

private:
	std::vector<Texture*> m_textures;
	std::string m_ID;
	std::string m_name;
	std::string m_modelID;
	Culling culling;
	float opacity;
	bool alphaBlending;
	Directus::Math::Vector4 colorAlbedo;
	float roughness;
	float metallic;
	float occlusion;
	float normalStrength;
	float height;
	float reflectivity;
	Directus::Math::Vector2 tiling;
	ShadingMode shadingMode;
	ShaderVariation* m_shader;

	TexturePool* m_texturePool;
	ShaderPool* m_shaderPool;

	/*------------------------------------------------------------------------------
								[HELPER FUNCTIONS]
	------------------------------------------------------------------------------*/
	int GetTextureIndexByType(TextureType type);
};
