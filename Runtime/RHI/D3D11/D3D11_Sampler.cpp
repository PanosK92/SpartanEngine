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

//= INCLUDES =====================
#include "D3D11_Device.h"
#include "D3D11_Sampler.h"
#include "../RHI_Implementation.h"
#include "../../Core/EngineDefs.h"
#include "../../Core/Settings.h"
#include "../../Logging/Log.h"
//================================

namespace Directus
{
	D3D11_Sampler::D3D11_Sampler(D3D11_Device* device,
		Texture_Sampler_Filter filter					/*= Texture_Sampler_Anisotropic*/,
		Texture_Address_Mode textureAddressMode			/*= Texture_Address_Wrap*/, 
		Texture_Comparison_Function comparisonFunction	/*= Texture_Comparison_Always*/)
	{
		if (!device)
		{
			LOG_ERROR("D3D11_Sampler::D3D11_Sampler: Invalid device");
			return;
		}

		D3D11_SAMPLER_DESC samplerDesc;
		samplerDesc.Filter			= d3d11_filter[filter];
		samplerDesc.AddressU		= d3d11_texture_address_mode[textureAddressMode];
		samplerDesc.AddressV		= d3d11_texture_address_mode[textureAddressMode];
		samplerDesc.AddressW		= d3d11_texture_address_mode[textureAddressMode];
		samplerDesc.MipLODBias		= 0.0f;
		samplerDesc.MaxAnisotropy	= Settings::Get().GetAnisotropy();
		samplerDesc.ComparisonFunc	= d3d11_comparison_func[comparisonFunction];
		samplerDesc.BorderColor[0]	= 0;
		samplerDesc.BorderColor[1]	= 0;
		samplerDesc.BorderColor[2]	= 0;
		samplerDesc.BorderColor[3]	= 0;
		samplerDesc.MinLOD			= FLT_MIN;
		samplerDesc.MaxLOD			= FLT_MAX;

		// Create sampler state.
		if (FAILED(device->GetDevice()->CreateSamplerState(&samplerDesc, &m_samplerState)))
		{
			LOG_ERROR("Failed to create sampler.");
		}
	}

	D3D11_Sampler::~D3D11_Sampler()
	{
		SafeRelease(m_samplerState);
	}
}