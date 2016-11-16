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

//= INCLUDES ===================================
#include "../Graphics/Shaders/ShaderVariation.h"
//==============================================

class ShaderPool : public Subsystem
{
public:
	ShaderPool(Context* context);
	~ShaderPool();

	std::weak_ptr<ShaderVariation> CreateShaderBasedOnMaterial(
		bool albedo,
		bool roughness,
		bool metallic,
		bool normal,
		bool height,
		bool occlusion,
		bool emission,
		bool mask,
		bool cubemap
	);
	std::weak_ptr<ShaderVariation> GetShaderByID(const std::string& shaderID);
	const std::vector<std::shared_ptr<ShaderVariation>>& GetAllShaders() const;
	void Clear();

private:
	std::weak_ptr<ShaderVariation> FindMatchingShader(
		bool albedo,
		bool roughness,
		bool metallic,
		bool normal,
		bool height,
		bool occlusion,
		bool emission,
		bool mask,
		bool cubemap
	);

	std::vector<std::shared_ptr<ShaderVariation>> m_shaders;
};
