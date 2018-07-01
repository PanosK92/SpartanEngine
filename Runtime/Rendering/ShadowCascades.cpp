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

//= INCLUDES ==============================
#include "ShadowCascades.h"
#include "RI\D3D11\D3D11_RenderTexture.h"
#include "RI\Backend_Imp.h"
#include "..\Math\Vector3.h"
#include "..\Math\Matrix.h"
#include "..\Logging\Log.h"
#include "..\Core\Context.h"
#include "..\Scene\Components\Light.h"
#include "..\Scene\Components\Transform.h"
#include "..\Scene\Scene.h"
#include "..\Scene\Actor.h"
//========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	ShadowCascades::ShadowCascades(Context* context, unsigned int cascadeCount, unsigned int resolution, Light* light)
	{
		m_context			= context;
		m_renderingDevice	= context->GetSubsystem<RenderingDevice>();
		m_resolution		= resolution;
		m_light				= light;
		m_cascadeCount		= cascadeCount;
		RenderTargets_Create();
	}

	ShadowCascades::~ShadowCascades()
	{
		RenderTargets_Destroy();
	}

	void ShadowCascades::SetAsRenderTarget(unsigned int cascadeIndex)
	{
		if (cascadeIndex >=	(unsigned int)m_renderTargets.size())
			return;

		m_renderTargets[cascadeIndex]->SetAsRenderTarget();
		m_renderTargets[cascadeIndex]->Clear(0.0f, 0.0f, 0.0f, 1.0f);
	}

	Matrix ShadowCascades::ComputeProjectionMatrix(unsigned int cascadeIndex)
	{
		auto mainCamera		= m_context->GetSubsystem<Scene>()->GetMainCamera().lock();
		Vector3 centerPos	= mainCamera ? mainCamera->GetTransform_PtrRaw()->GetPosition() : Vector3::Zero;
		Matrix mView		= m_light->ComputeViewMatrix();

		// Hardcoded sizes to match the splits
		float extents = 0;
		if (cascadeIndex == 0)
			extents = 10;

		if (cascadeIndex == 1)
			extents = 45;

		if (cascadeIndex == 2)
			extents = 90;

		Vector3 center = centerPos * mView;
		Vector3 min = center - Vector3(extents, extents, extents);
		Vector3 max = center + Vector3(extents, extents, extents);

		//= Shadow shimmering remedy based on ============================================
		// https://msdn.microsoft.com/en-us/library/windows/desktop/ee416324(v=vs.85).aspx
		float fWorldUnitsPerTexel = (extents * 2.0f) / m_resolution;

		min /= fWorldUnitsPerTexel;
		min.Floor();
		min *= fWorldUnitsPerTexel;
		max /= fWorldUnitsPerTexel;
		max.Floor();
		max *= fWorldUnitsPerTexel;
		//================================================================================

		return Matrix::CreateOrthoOffCenterLH(min.x, max.x, min.y, max.y, min.z, max.z);
	}

	float ShadowCascades::GetSplit(unsigned int cascadeIndex)
	{
		// These cascade splits have a logarithmic nature, have to fix

		// Second cascade
		if (cascadeIndex == 1)
			return 0.79f;

		// Third cascade
		if (cascadeIndex == 2)
			return 0.97f;

		return 0.0f;
	}

	void* ShadowCascades::GetShaderResource(unsigned int cascadeIndex)
	{
		if (cascadeIndex >= (unsigned int)m_renderTargets.size())
			return nullptr;

		return m_renderTargets[cascadeIndex]->GetShaderResourceView();
	}

	void ShadowCascades::SetEnabled(bool enabled)
	{
		enabled ? RenderTargets_Create() : RenderTargets_Destroy();
	}

	void ShadowCascades::RenderTargets_Create()
	{
		if (!m_renderTargets.empty())
			return;

		auto renderingDevice = m_context->GetSubsystem<RenderingDevice>();
		for (unsigned int i = 0; i < m_cascadeCount; i++)
		{
			m_renderTargets.emplace_back(make_unique<D3D11_RenderTexture>(renderingDevice, m_resolution, m_resolution, true, Texture_Format_R32_FLOAT));
		}
	}

	void ShadowCascades::RenderTargets_Destroy()
	{
		m_renderTargets.clear();
		m_renderTargets.shrink_to_fit();
	}
}