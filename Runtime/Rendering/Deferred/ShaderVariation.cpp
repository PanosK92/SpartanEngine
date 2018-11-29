/*
Copyright(c) 2016-2018 Panos Karabelas

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
#include "../../Core/Settings.h"
//===========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	ShaderVariation::ShaderVariation(shared_ptr<RHI_Device> device, Context* context): RHI_Shader(device), IResource(context, Resource_Shader)
	{
		m_shaderFlags = 0;
	}

	void ShaderVariation::Compile(const string& filePath, unsigned long shaderFlags)
	{
		m_shaderFlags = shaderFlags;

		// Load and compile the vertex and the pixel shader
		AddDefinesBasedOnMaterial();
		Compile_VertexPixel_Async(filePath, Input_PositionTextureTBN, m_context);

		// Object Buffer (has to match GBuffer.hlsl)
		m_perObjectBuffer = make_shared<RHI_ConstantBuffer>(m_rhiDevice);
		m_perObjectBuffer->Create(sizeof(PerObjectBufferType));
	}

	void ShaderVariation::UpdatePerObjectBuffer(Transform* transform, Material* material, const Matrix& mView, const Matrix mProjection)
	{
		if (!material)
		{
			LOG_ERROR("ShaderVariation::UpdatePerObjectBuffer: Invalid parameters.");
			return;
		}

		if (GetState() != Shader_Built)
			return;

		Matrix mMVP_current = transform->GetMatrix() * mView * mProjection;

		// Determine if the material buffer needs to update
		bool update = false;
		update = perObjectBufferCPU.matAlbedo		!= material->GetColorAlbedo()				? true : update;
		update = perObjectBufferCPU.matTilingUV		!= material->GetTiling()					? true : update;
		update = perObjectBufferCPU.matOffsetUV		!= material->GetOffset()					? true : update;
		update = perObjectBufferCPU.matRoughnessMul	!= material->GetRoughnessMultiplier()		? true : update;
		update = perObjectBufferCPU.matMetallicMul	!= material->GetMetallicMultiplier()		? true : update;
		update = perObjectBufferCPU.matNormalMul	!= material->GetNormalMultiplier()			? true : update;
		update = perObjectBufferCPU.matShadingMode	!= float(material->GetShadingMode())		? true : update;
		update = perObjectBufferCPU.mModel			!= transform->GetMatrix()					? true : update;
		update = perObjectBufferCPU.mMVP_current	!= mMVP_current								? true : update;
		update = perObjectBufferCPU.mMVP_previous	!= transform->GetWVP_Previous()				? true : update;

		if (!update)
			return;

		auto buffer = (PerObjectBufferType*)m_perObjectBuffer->Map();
		
		buffer->matAlbedo		= perObjectBufferCPU.matAlbedo			= material->GetColorAlbedo();
		buffer->matTilingUV		= perObjectBufferCPU.matTilingUV		= material->GetTiling();
		buffer->matOffsetUV		= perObjectBufferCPU.matOffsetUV		= material->GetOffset();
		buffer->matRoughnessMul = perObjectBufferCPU.matRoughnessMul	= material->GetRoughnessMultiplier();
		buffer->matMetallicMul	= perObjectBufferCPU.matMetallicMul		= material->GetMetallicMultiplier();
		buffer->matNormalMul	= perObjectBufferCPU.matNormalMul		= material->GetNormalMultiplier();
		buffer->matHeightMul	= perObjectBufferCPU.matNormalMul		= material->GetHeightMultiplier();
		buffer->matShadingMode	= perObjectBufferCPU.matShadingMode		= float(material->GetShadingMode());
		buffer->padding			= perObjectBufferCPU.padding			= Vector3::Zero;
		buffer->mModel			= perObjectBufferCPU.mModel				= transform->GetMatrix();
		buffer->mMVP_current	= perObjectBufferCPU.mMVP_current		= mMVP_current;
		buffer->mMVP_previous	= perObjectBufferCPU.mMVP_previous		= transform->GetWVP_Previous();
		
		m_perObjectBuffer->Unmap();

		transform->SetWVP_Previous(mMVP_current);
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
		AddDefine("CUBE_MAP",		HasCubeMapTexture()		? "1" : "0");
	}
}