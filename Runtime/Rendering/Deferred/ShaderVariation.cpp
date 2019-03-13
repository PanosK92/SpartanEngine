/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES ================================
#include "ShaderVariation.h"
#include "../Renderer.h"
#include "../Material.h"
#include "../../RHI/RHI_Implementation.h"
#include "../../RHI/RHI_Shader.h"
#include "../../RHI/RHI_ConstantBuffer.h"
#include "../../World/Components/Transform.h"
#include "../../World/Components/Camera.h"
//===========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	vector<shared_ptr<ShaderVariation>> ShaderVariation::m_variations;

	shared_ptr<ShaderVariation> ShaderVariation::GetMatchingShader(const unsigned long flags)
	{
		for (const auto& shader : m_variations)
		{
			if (shader->GetShaderFlags() == flags)
				return shader;
		}

		return nullptr;
	}

	ShaderVariation::ShaderVariation(const shared_ptr<RHI_Device>& rhi_device, Context* context) : RHI_Shader(rhi_device)
	{
		m_context	= context;
		m_flags		= 0;
	}

	void ShaderVariation::Compile(const string& file_path, const unsigned long shader_flags)
	{
		m_flags = shader_flags;

		// Load and compile the pixel shader
		AddDefinesBasedOnMaterial();
		CompileAsync(m_context, Shader_Pixel, file_path);

		// Object Buffer (has to match GBuffer.hlsl)
		m_constant_buffer = make_shared<RHI_ConstantBuffer>(m_rhi_device, static_cast<unsigned int>(sizeof(PerObjectBufferType)));

		m_variations.emplace_back(shared_from_this());
	}

	void ShaderVariation::UpdatePerObjectBuffer(Transform* transform, Material* material, const Matrix& m_view, const Matrix& mProjection)
	{
		if (!material)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		if (GetCompilationState() != Shader_Compiled)
			return;

		auto m_mvp_current = transform->GetMatrix() * m_view * mProjection;

		// Determine if the material buffer needs to update
		auto update = false;
		update = per_object_buffer_cpu.mat_albedo			!= material->GetColorAlbedo()			? true : update;
		update = per_object_buffer_cpu.mat_tiling_uv		!= material->GetTiling()				? true : update;
		update = per_object_buffer_cpu.mat_offset_uv		!= material->GetOffset()				? true : update;
		update = per_object_buffer_cpu.mat_roughness_mul	!= material->GetRoughnessMultiplier()	? true : update;
		update = per_object_buffer_cpu.mat_metallic_mul		!= material->GetMetallicMultiplier()	? true : update;
		update = per_object_buffer_cpu.mat_normal_mul		!= material->GetNormalMultiplier()		? true : update;
		update = per_object_buffer_cpu.mat_shading_mode		!= float(material->GetShadingMode())	? true : update;
		update = per_object_buffer_cpu.model				!= transform->GetMatrix()				? true : update;

		bool new_input		= per_object_buffer_cpu.mvp_current != m_mvp_current;
		bool non_zero_delta = per_object_buffer_cpu.mvp_current != per_object_buffer_cpu.mvp_previous;
		update = new_input || non_zero_delta ? true : update;

		if (!update)
			return;

		auto buffer = static_cast<PerObjectBufferType*>(m_constant_buffer->Map());
		
		buffer->mat_albedo			= per_object_buffer_cpu.mat_albedo			= material->GetColorAlbedo();
		buffer->mat_tiling_uv		= per_object_buffer_cpu.mat_tiling_uv		= material->GetTiling();
		buffer->mat_offset_uv		= per_object_buffer_cpu.mat_offset_uv		= material->GetOffset();
		buffer->mat_roughness_mul	= per_object_buffer_cpu.mat_roughness_mul	= material->GetRoughnessMultiplier();
		buffer->mat_metallic_mul	= per_object_buffer_cpu.mat_metallic_mul	= material->GetMetallicMultiplier();
		buffer->mat_normal_mul		= per_object_buffer_cpu.mat_normal_mul		= material->GetNormalMultiplier();
		buffer->mat_height_mul		= per_object_buffer_cpu.mat_normal_mul		= material->GetHeightMultiplier();
		buffer->mat_shading_mode	= per_object_buffer_cpu.mat_shading_mode	= float(material->GetShadingMode());
		buffer->padding				= per_object_buffer_cpu.padding				= Vector3::Zero;
		buffer->model				= per_object_buffer_cpu.model				= transform->GetMatrix();
		buffer->mvp_current			= per_object_buffer_cpu.mvp_current			= m_mvp_current;
		buffer->mvp_previous		= per_object_buffer_cpu.mvp_previous		= transform->GetWVP_Previous();
		
		m_constant_buffer->Unmap();

		transform->SetWVP_Previous(m_mvp_current);
	}

	void ShaderVariation::AddDefinesBasedOnMaterial()
	{
		// Define in the shader what kind of textures it should expect
		AddDefine("ALBEDO_MAP",		HasAlbedoTexture()		? "1" : "0");
		AddDefine("ROUGHNESS_MAP",	HasRoughnessTexture()	? "1" : "0");
		AddDefine("METALLIC_MAP",	HasMetallicTexture()	? "1" : "0");
		AddDefine("NORMAL_MAP",		HasNormalTexture()		? "1" : "0");
		AddDefine("HEIGHT_MAP",		HasHeightTexture()		? "1" : "0");
		AddDefine("OCCLUSION_MAP",	HasOcclusionTexture()	? "1" : "0");
		AddDefine("EMISSION_MAP",	HasEmissionTexture()	? "1" : "0");
		AddDefine("MASK_MAP",		HasMaskTexture()		? "1" : "0");
	}
}