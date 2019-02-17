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

//= INCLUDES ========================
#include "../RHI_DepthStencilState.h"
#include "../RHI_Device.h"
#include "../RHI_Implementation.h"
#include "D3D11_Common.h"
#include "../../Logging/Log.h"
#include "../../Core/Settings.h"
//===================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	RHI_DepthStencilState::RHI_DepthStencilState(shared_ptr<RHI_Device> device, bool depthEnabled)
	{
		// Save properties
		m_depthEnabled = depthEnabled;

		// Create description
		D3D11_DEPTH_STENCIL_DESC desc;

		if (!depthEnabled)
		{
			desc.DepthEnable					= false;
			desc.DepthWriteMask					= D3D11_DEPTH_WRITE_MASK_ALL;
			desc.DepthFunc						= D3D11_COMPARISON_ALWAYS;
			desc.StencilEnable					= false;
			desc.StencilReadMask				= D3D11_DEFAULT_STENCIL_READ_MASK;
			desc.StencilWriteMask				= D3D11_DEFAULT_STENCIL_WRITE_MASK;
			desc.FrontFace.StencilDepthFailOp	= D3D11_STENCIL_OP_KEEP;
			desc.FrontFace.StencilFailOp		= D3D11_STENCIL_OP_KEEP;
			desc.FrontFace.StencilPassOp		= D3D11_STENCIL_OP_KEEP;
			desc.FrontFace.StencilFunc			= D3D11_COMPARISON_ALWAYS;
			desc.BackFace						= desc.FrontFace;
		}
		else
		{
			desc.DepthEnable					= true;
			desc.DepthWriteMask					= D3D11_DEPTH_WRITE_MASK_ALL;
		#if REVERSE_Z == 1
			desc.DepthFunc						= D3D11_COMPARISON_GREATER_EQUAL;
		#else
			desc.DepthFunc						= D3D11_COMPARISON_LESS_EQUAL;
		#endif
			desc.StencilEnable					= false;
			desc.StencilReadMask				= D3D11_DEFAULT_STENCIL_READ_MASK;
			desc.StencilWriteMask				= D3D11_DEFAULT_STENCIL_WRITE_MASK;
			desc.FrontFace.StencilDepthFailOp	= D3D11_STENCIL_OP_KEEP;
			desc.FrontFace.StencilFailOp		= D3D11_STENCIL_OP_KEEP;
			desc.FrontFace.StencilPassOp		= D3D11_STENCIL_OP_REPLACE;
			desc.FrontFace.StencilFunc			= D3D11_COMPARISON_ALWAYS;
			desc.BackFace						= desc.FrontFace;
		
		}

		// Create depth-stencil state
		auto depthStencilState	= (ID3D11DepthStencilState*)m_buffer;
		auto result				= device->GetDevice<ID3D11Device>()->CreateDepthStencilState(&desc, &depthStencilState);

		// Handle result
		if (SUCCEEDED(result))
		{
			m_buffer		= (void*)depthStencilState;
			m_initialized	= true;
		}
		else
		{
			m_initialized = false;
			LOGF_ERROR("Failed to create depth-stencil state %s.", D3D11_Common::DxgiErrorToString(result));
		}
	}

	RHI_DepthStencilState::~RHI_DepthStencilState()
	{
		SafeRelease((ID3D11DepthStencilState*)m_buffer);
	}
}