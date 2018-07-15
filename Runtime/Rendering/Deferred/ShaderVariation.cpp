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
#include "../RI/D3D11/D3D11_Device.h"
#include "../RI/D3D11/D3D11_ConstantBuffer.h"
#include "../RI/D3D11/D3D11_Shader.h"
#include "../RI/Backend_Imp.h"
#include "../Material.h"
#include "../../Logging/Log.h"
#include "../../Core/Settings.h"
#include "../../Scene/Components/Transform.h"
#include "../../Scene/Components/Camera.h"
//===========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	ShaderVariation::ShaderVariation(Context* context): IResource(context)
	{
		//= IResource ======================
		RegisterResource<ShaderVariation>();
		//==================================

		m_graphics		= m_context->GetSubsystem<RenderingDevice>();
		m_shaderFlags	= 0;
	}

	ShaderVariation::~ShaderVariation()
	{

	}

	void ShaderVariation::Compile(const string& filePath, unsigned long shaderFlags)
	{
		m_shaderFlags = shaderFlags;
		if (!m_graphics)
		{
			LOG_INFO("GraphicsDevice is expired. Cant't compile shader");
			return;
		}

		// Load and compile the vertex and the pixel shader
		m_D3D11Shader = make_shared<D3D11_Shader>(m_graphics);
		AddDefinesBasedOnMaterial(m_D3D11Shader);
		m_D3D11Shader->Compile(filePath);
		m_D3D11Shader->SetInputLayout(Input_PositionTextureTBN);
		m_D3D11Shader->AddSampler(Texture_Sampler_Anisotropic, Texture_Address_Wrap, Texture_Comparison_Always);

		// Matrix Buffer
		m_perObjectBuffer = make_shared<D3D11_ConstantBuffer>(m_graphics);
		m_perObjectBuffer->Create(sizeof(PerObjectBufferType));

		// Object Buffer
		m_materialBuffer = make_shared<D3D11_ConstantBuffer>(m_graphics);
		m_materialBuffer->Create(sizeof(PerMaterialBufferType));

		// Object Buffer
		m_miscBuffer = make_shared<D3D11_ConstantBuffer>(m_graphics);
		m_miscBuffer->Create(sizeof(PerFrameBufferType));
	}

	void ShaderVariation::Bind()
	{
		if (!m_D3D11Shader)
		{
			LOG_WARNING("Can't set uninitialized shader");
			return;
		}

		m_D3D11Shader->Bind();
	}

	void ShaderVariation::Bind_PerFrameBuffer(Camera* camera)
	{
		if (!m_D3D11Shader || !m_D3D11Shader->IsCompiled())
		{
			LOG_ERROR("Shader hasn't been loaded or failed to compile. Can't update per frame buffer.");
			return;
		}

		if (!camera)
			return;

		//= BUFFER UPDATE ==========================================
		auto buffer = (PerFrameBufferType*)m_miscBuffer->Map();

		buffer->cameraPos	= camera->GetTransform()->GetPosition();
		buffer->padding		= 0.0f;
		buffer->viewport	= Settings::Get().GetResolution();
		buffer->padding2	= Vector2::Zero;
		
		m_miscBuffer->Unmap();
		//==========================================================

		// Set to shader slot
		m_miscBuffer->SetPS(0);
	}

	void ShaderVariation::Bind_PerMaterialBuffer(Material* material)
	{
		if (!material)
			return;

		if (!m_D3D11Shader->IsCompiled())
		{
			LOG_ERROR("Shader hasn't been loaded or failed to compile. Can't update per material buffer.");
			return;
		}

		// Determine if the material buffer needs to update
		bool update = false;
		update = perMaterialBufferCPU.matAlbedo			!= material->GetColorAlbedo()			? true : update;
		update = perMaterialBufferCPU.matTilingUV		!= material->GetTiling()				? true : update;
		update = perMaterialBufferCPU.matOffsetUV		!= material->GetOffset()				? true : update;
		update = perMaterialBufferCPU.matRoughnessMul	!= material->GetRoughnessMultiplier()	? true : update;
		update = perMaterialBufferCPU.matMetallicMul	!= material->GetMetallicMultiplier()	? true : update;
		update = perMaterialBufferCPU.matNormalMul		!= material->GetNormalMultiplier()		? true : update;
		update = perMaterialBufferCPU.matShadingMode	!= float(material->GetShadingMode())	? true : update;

		if (update)
		{
			//= BUFFER UPDATE ===================================================================================
			auto buffer = (PerMaterialBufferType*)m_materialBuffer->Map();

			buffer->matAlbedo		= perMaterialBufferCPU.matAlbedo		= material->GetColorAlbedo();
			buffer->matTilingUV		= perMaterialBufferCPU.matTilingUV		= material->GetTiling();
			buffer->matOffsetUV		= perMaterialBufferCPU.matOffsetUV		= material->GetOffset();
			buffer->matRoughnessMul = perMaterialBufferCPU.matRoughnessMul	= material->GetRoughnessMultiplier();
			buffer->matMetallicMul	= perMaterialBufferCPU.matMetallicMul	= material->GetMetallicMultiplier();
			buffer->matNormalMul	= perMaterialBufferCPU.matNormalMul		= material->GetNormalMultiplier();
			buffer->matHeightMul	= perMaterialBufferCPU.matNormalMul		= material->GetHeightMultiplier();
			buffer->matShadingMode	= perMaterialBufferCPU.matShadingMode	= float(material->GetShadingMode());
			buffer->paddding		= Vector3::Zero;

			m_materialBuffer->Unmap();
			//===================================================================================================
		}

		// Set to shader slot
		m_materialBuffer->SetPS(1);
	}

	void ShaderVariation::Bind_PerObjectBuffer(const Matrix& mWorld, const Matrix& mView, const Matrix& mProjection)
	{
		if (!m_D3D11Shader->IsCompiled())
		{
			LOG_ERROR("Shader hasn't been loaded or failed to compile. Can't update per object buffer.");
			return;
		}

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
			//= BUFFER UPDATE ============================================================================
			auto* buffer = (PerObjectBufferType*)m_perObjectBuffer->Map();

			buffer->mWorld = perObjectBufferCPU.mWorld								= world;
			buffer->mWorldView = perObjectBufferCPU.mWorldView						= worldView;
			buffer->mWorldViewProjection = perObjectBufferCPU.mWorldViewProjection	= worldViewProjection;

			m_perObjectBuffer->Unmap();
			//============================================================================================
		}

		// Set to shader slot
		m_perObjectBuffer->SetVS(2);
	}

	void ShaderVariation::Bind_Textures(const vector<void*>& textureArray)
	{
		if (!m_graphics)
		{
			LOG_INFO("GraphicsDevice is expired. Cant't update shader textures.");
			return;
		}

		m_graphics->GetDeviceContext()->PSSetShaderResources(0, (unsigned int)textureArray.size(), (ID3D11ShaderResourceView**)&textureArray[0]);
	}

	void ShaderVariation::Render(unsigned int indexCount, unsigned int indexOffset /*= 0*/, unsigned int vertexOffset /*= 0*/)
	{
		if (!m_graphics)
		{
			LOG_INFO("ShaderVariation::Render: Invalid graphics device");
			return;
		}

		m_graphics->GetDeviceContext()->DrawIndexed(indexCount, indexOffset, vertexOffset);
	}

	void ShaderVariation::AddDefinesBasedOnMaterial(const shared_ptr<D3D11_Shader>& shader)
	{
		if (!shader)
			return;

		// Define in the shader what kind of textures it should expect
		shader->AddDefine("ALBEDO_MAP",		HasAlbedoTexture()		? "1" : "0");
		shader->AddDefine("ROUGHNESS_MAP",	HasRoughnessTexture()	? "1" : "0");
		shader->AddDefine("METALLIC_MAP",	HasMetallicTexture()	? "1" : "0");
		shader->AddDefine("NORMAL_MAP",		HasNormalTexture()		? "1" : "0");
		shader->AddDefine("HEIGHT_MAP",		HasHeightTexture()		? "1" : "0");
		shader->AddDefine("OCCLUSION_MAP",	HasOcclusionTexture()	? "1" : "0");
		shader->AddDefine("EMISSION_MAP",	HasEmissionTexture()	? "1" : "0");
		shader->AddDefine("MASK_MAP",		HasMaskTexture()		? "1" : "0");
		shader->AddDefine("CUBE_MAP",		HasCubeMapTexture()		? "1" : "0");
	}
}