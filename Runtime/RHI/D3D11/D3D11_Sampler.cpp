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

//= INCLUDES =====================
#include <winerror.h>
#include "../RHI_Implementation.h"
#include "../RHI_Sampler.h"
#include "../RHI_Device.h"
#include "../../Logging/Log.h"
#include "../../Core/Settings.h"
//================================

namespace Directus
{
	RHI_Sampler::RHI_Sampler(
		std::shared_ptr<RHI_Device> rhiDevice,
		RHI_Texture_Filter filter						/*= Texture_Sampler_Anisotropic*/,
		RHI_Texture_Address_Mode textureAddressMode		/*= Texture_Address_Wrap*/, 
		RHI_Comparison_Function comparisonFunction		/*= Texture_Comparison_Always*/
	)
	{	
		m_buffer				= nullptr;
		m_rhiDevice				= rhiDevice;
		m_filter				= filter;
		m_textureAddressMode	= textureAddressMode;
		m_comparisonFunction	= comparisonFunction;

		if (!rhiDevice || !rhiDevice->GetDevice<ID3D11Device>())
		{
			LOG_ERROR("Invalid device.");
			return;
		}
		
		D3D11_SAMPLER_DESC samplerDesc;
		samplerDesc.Filter			= d3d11_filter[filter];
		samplerDesc.AddressU		= d3d11_texture_address_mode[textureAddressMode];
		samplerDesc.AddressV		= d3d11_texture_address_mode[textureAddressMode];
		samplerDesc.AddressW		= d3d11_texture_address_mode[textureAddressMode];
		samplerDesc.MipLODBias		= 0.0f;
		samplerDesc.MaxAnisotropy	= Settings::Get().Anisotropy_Get();
		samplerDesc.ComparisonFunc	= d3d11_comparison_func[comparisonFunction];
		samplerDesc.BorderColor[0]	= 0;
		samplerDesc.BorderColor[1]	= 0;
		samplerDesc.BorderColor[2]	= 0;
		samplerDesc.BorderColor[3]	= 0;
		samplerDesc.MinLOD			= FLT_MIN;
		samplerDesc.MaxLOD			= FLT_MAX;
	
		// Create sampler state.
		if (FAILED(rhiDevice->GetDevice<ID3D11Device>()->CreateSamplerState(&samplerDesc, (ID3D11SamplerState**)&m_buffer)))
		{
			LOG_ERROR("Failed to create sampler state");
		}
	}

	RHI_Sampler::~RHI_Sampler()
	{
		SafeRelease((ID3D11SamplerState*)m_buffer);
	}
}