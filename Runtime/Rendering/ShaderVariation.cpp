/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ===============
#include "ShaderVariation.h"
#include "Material.h"
//==========================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
	vector<shared_ptr<ShaderVariation>> ShaderVariation::m_variations;

	const shared_ptr<ShaderVariation>& ShaderVariation::GetMatchingShader(const uint16_t flags)
	{
		for (const auto& shader : m_variations)
		{
			if (shader->GetFlags() == flags)
				return shader;
		}

		static shared_ptr<ShaderVariation> empty;
		return empty;
	}

	ShaderVariation::ShaderVariation(const shared_ptr<RHI_Device>& rhi_device, Context* context) : RHI_Shader(rhi_device)
	{
		m_context   = context;
		m_flags		= 0;
	}

	void ShaderVariation::Compile(const string& file_path, const uint16_t shader_flags)
	{
		m_flags = shader_flags;

		// Load and compile the pixel shader
		AddDefinesBasedOnMaterial();
		CompileAsync(m_context, RHI_Shader_Pixel, file_path);

		m_variations.emplace_back(shared_from_this());
	}

	void ShaderVariation::AddDefinesBasedOnMaterial()
	{
		// Define in the shader what kind of textures it should expect
		AddDefine("ALBEDO_MAP",		(m_flags & RHI_Material_Color)      ? "1" : "0");
		AddDefine("ROUGHNESS_MAP",  (m_flags & RHI_Material_Roughness)  ? "1" : "0");
		AddDefine("METALLIC_MAP",   (m_flags & RHI_Material_Metallic)   ? "1" : "0");
		AddDefine("NORMAL_MAP",     (m_flags & RHI_Material_Normal)     ? "1" : "0");
		AddDefine("HEIGHT_MAP",     (m_flags & RHI_Material_Height)     ? "1" : "0");
		AddDefine("OCCLUSION_MAP",  (m_flags & RHI_Material_Occlusion)  ? "1" : "0");
		AddDefine("EMISSION_MAP",   (m_flags & RHI_Material_Emission)   ? "1" : "0");
		AddDefine("MASK_MAP",       (m_flags & RHI_Material_Mask)       ? "1" : "0");
	}
}
