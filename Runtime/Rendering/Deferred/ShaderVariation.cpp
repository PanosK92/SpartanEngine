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
	ShaderVariation::ShaderVariation(shared_ptr<RHI_Device> device, Context* context): RHI_Shader(device), IResource(context)
	{
		//= IResource ======================
		RegisterResource<ShaderVariation>();
		//==================================
		m_shaderFlags = 0;
	}

	void ShaderVariation::Compile(const string& filePath, unsigned long shaderFlags)
	{
		m_shaderFlags = shaderFlags;

		// Load and compile the vertex and the pixel shader
		AddDefinesBasedOnMaterial();
		Compile_VertexPixel_Async(filePath, Input_PositionTextureTBN, m_context);

		// The buffers below have to match GBuffer.hlsl

		// Material Buffer
		m_materialBuffer = make_shared<RHI_ConstantBuffer>(m_rhiDevice);
		m_materialBuffer->Create(sizeof(PerMaterialBufferType), 0, Buffer_PixelShader);

		// Object Buffer
		m_perObjectBuffer = make_shared<RHI_ConstantBuffer>(m_rhiDevice);
		m_perObjectBuffer->Create(sizeof(PerObjectBufferType), 1, Buffer_VertexShader);
	}

	void ShaderVariation::UpdatePerMaterialBuffer(Camera* camera, Material* material)
	{
		if (!camera || !material)
		{
			LOG_ERROR("ShaderVariation::UpdatePerMaterialBuffer: Invalid parameters.");
			return;
		}

		if (GetState() != Shader_Built)
			return;

		Vector2 planes = Vector2(camera->GetNearPlane(), camera->GetFarPlane());

		// Determine if the material buffer needs to update
		bool update = false;
		update = perMaterialBufferCPU.matAlbedo			!= material->GetColorAlbedo()				? true : update;
		update = perMaterialBufferCPU.matTilingUV		!= material->GetTiling()					? true : update;
		update = perMaterialBufferCPU.matOffsetUV		!= material->GetOffset()					? true : update;
		update = perMaterialBufferCPU.matRoughnessMul	!= material->GetRoughnessMultiplier()		? true : update;
		update = perMaterialBufferCPU.matMetallicMul	!= material->GetMetallicMultiplier()		? true : update;
		update = perMaterialBufferCPU.matNormalMul		!= material->GetNormalMultiplier()			? true : update;
		update = perMaterialBufferCPU.matShadingMode	!= float(material->GetShadingMode())		? true : update;
		update = perMaterialBufferCPU.cameraPos			!= camera->GetTransform()->GetPosition()	? true : update;
		update = perMaterialBufferCPU.resolution		!= Settings::Get().GetResolution()			? true : update;
		update = perMaterialBufferCPU.planes			!= planes									? true : update;

		if (update)
		{
			//= BUFFER UPDATE ======================================================================================
			auto buffer = (PerMaterialBufferType*)m_materialBuffer->Map();

			buffer->matAlbedo		= perMaterialBufferCPU.matAlbedo		= material->GetColorAlbedo();
			buffer->matTilingUV		= perMaterialBufferCPU.matTilingUV		= material->GetTiling();
			buffer->matOffsetUV		= perMaterialBufferCPU.matOffsetUV		= material->GetOffset();
			buffer->matRoughnessMul = perMaterialBufferCPU.matRoughnessMul	= material->GetRoughnessMultiplier();
			buffer->matMetallicMul	= perMaterialBufferCPU.matMetallicMul	= material->GetMetallicMultiplier();
			buffer->matNormalMul	= perMaterialBufferCPU.matNormalMul		= material->GetNormalMultiplier();
			buffer->matHeightMul	= perMaterialBufferCPU.matNormalMul		= material->GetHeightMultiplier();
			buffer->matShadingMode	= perMaterialBufferCPU.matShadingMode	= float(material->GetShadingMode());
			buffer->cameraPos		= perMaterialBufferCPU.cameraPos		= camera->GetTransform()->GetPosition();
			buffer->resolution		= perMaterialBufferCPU.resolution		= Settings::Get().GetResolution();
			buffer->planes			= perMaterialBufferCPU.planes			= planes;
			buffer->padding			= perMaterialBufferCPU.padding			= 0.0f;
			buffer->padding2		= perMaterialBufferCPU.padding2			= Vector3::Zero;

			m_materialBuffer->Unmap();
			//======================================================================================================
		}
	}

	void ShaderVariation::UpdatePerObjectBuffer(const Matrix& mWorld, const Matrix& mView, const Matrix& mProjection)
	{
		if (GetState() != Shader_Built)
			return;

		Matrix world				= mWorld;
		Matrix worldView			= mWorld * mView;
		Matrix worldViewProjection	= worldView * mProjection;

		// Determine if the buffer actually needs to update
		bool update = false;
		update = perObjectBufferCPU.mWorld					!= world ? true : update;
		update = perObjectBufferCPU.mWorldView				!= worldView ? true : update;
		update = perObjectBufferCPU.mWorldViewProjection	!= worldViewProjection ? true : update;

		if (update)
		{
			//= BUFFER UPDATE ================================================================================
			auto* buffer = (PerObjectBufferType*)m_perObjectBuffer->Map();

			buffer->mWorld					= perObjectBufferCPU.mWorld					= world;
			buffer->mWorldView				= perObjectBufferCPU.mWorldView				= worldView;
			buffer->mWorldViewProjection	= perObjectBufferCPU.mWorldViewProjection	= worldViewProjection;

			m_perObjectBuffer->Unmap();
			//================================================================================================
		}
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