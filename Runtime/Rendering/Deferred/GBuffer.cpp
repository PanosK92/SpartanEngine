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

//= INCLUDES ===========================
#include "GBuffer.h"
#include "../../RHI/RHI_Device.h"
#include "../../RHI/RHI_RenderTexture.h"
#include "../../Logging/Log.h"
//======================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	GBuffer::GBuffer(const shared_ptr<RHI_Device>& rhiDevice, unsigned int width, unsigned int height)
	{
		if (width == 0 || height == 0)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		m_renderTargets[GBuffer_Target_Albedo]		= make_shared<RHI_RenderTexture>(rhiDevice, width, height, Format_R8G8B8A8_UNORM,		false);
		m_renderTargets[GBuffer_Target_Normal]		= make_shared<RHI_RenderTexture>(rhiDevice, width, height, Format_R16G16B16A16_FLOAT,	false); // At Texture_Format_R8G8B8A8_UNORM, normals have noticeable banding
		m_renderTargets[GBuffer_Target_Material]	= make_shared<RHI_RenderTexture>(rhiDevice, width, height, Format_R8G8B8A8_UNORM,		false);
		m_renderTargets[GBuffer_Target_Velocity]	= make_shared<RHI_RenderTexture>(rhiDevice, width, height, Format_R16G16_FLOAT,			false);
		m_renderTargets[GBuffer_Target_Depth]		= make_shared<RHI_RenderTexture>(rhiDevice, width, height, Format_R32G32_FLOAT,			true, Format_D32_FLOAT);
	}

	GBuffer::~GBuffer()
	{
		m_renderTargets.clear();
	}

	const shared_ptr<RHI_RenderTexture>& GBuffer::GetTexture(GBuffer_Texture_Type type)
	{
		return m_renderTargets[type];
	}

	void GBuffer::Clear()
	{
		float depth = 1.0f;
		#if REVERSE_Z == 1
		depth = 0.0f;
		#endif

		m_renderTargets[GBuffer_Target_Albedo]->Clear(0, 0, 0, 0);
		m_renderTargets[GBuffer_Target_Normal]->Clear(0, 0, 0, 0);
		m_renderTargets[GBuffer_Target_Material]->Clear(0, 0, 0, 0);
		m_renderTargets[GBuffer_Target_Velocity]->Clear(0, 0, 0, 0);
		m_renderTargets[GBuffer_Target_Depth]->Clear(depth, depth, 0, 0);
	}
}