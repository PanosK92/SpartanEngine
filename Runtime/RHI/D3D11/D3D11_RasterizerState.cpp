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

//= INCLUDES ======================
#include "../RHI_RasterizerState.h"
#include "../RHI_Device.h"
#include "../RHI_Implementation.h"
#include "D3D11_Common.h"
#include "../../Logging/Log.h"
//=================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	RHI_RasterizerState::RHI_RasterizerState
	(
		shared_ptr<RHI_Device> device,
		RHI_Cull_Mode cullMode,
		RHI_Fill_Mode fillMode,
		bool depthClipEnabled,
		bool scissorEnabled,
		bool multiSampleEnabled,
		bool antialisedLineEnabled)
	{
		// Save properties
		m_cullMode					= cullMode;
		m_fillMode					= fillMode;
		m_depthClipEnabled			= depthClipEnabled;
		m_scissorEnabled			= scissorEnabled;
		m_multiSampleEnabled		= multiSampleEnabled;
		m_antialisedLineEnabled		= antialisedLineEnabled;

		// Create rasterizer description
		D3D11_RASTERIZER_DESC desc;
		desc.CullMode				= d3d11_cull_mode[cullMode];
		desc.FillMode				= d3d11_fill_Mode[fillMode];	
		desc.FrontCounterClockwise	= false;
		desc.DepthBias				= 0;
		desc.DepthBiasClamp			= 0.0f;
		desc.SlopeScaledDepthBias	= 0.0f;
		desc.DepthClipEnable		= depthClipEnabled;	
		desc.MultisampleEnable		= multiSampleEnabled;
		desc.AntialiasedLineEnable	= antialisedLineEnabled;
		desc.ScissorEnable			= scissorEnabled;

		// Create rasterizer state
		auto rasterizerState	= (ID3D11RasterizerState*)m_buffer;
		auto result				= device->GetDevice<ID3D11Device>()->CreateRasterizerState(&desc, &rasterizerState);
	
		// Handle result
		if (SUCCEEDED(result))
		{
			m_buffer		= (void*)rasterizerState;
			m_initialized	= true;
		}
		else
		{
			LOGF_ERROR("Failed to create the rasterizer state, %s.", D3D11_Common::DxgiErrorToString(result));
			m_initialized = false;
		}
	}

	RHI_RasterizerState::~RHI_RasterizerState()
	{
		SafeRelease((ID3D11RasterizerState*)m_buffer);
	}
}