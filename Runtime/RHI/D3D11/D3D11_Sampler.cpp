/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= IMPLEMENTATION ===============
#include "../RHI_Implementation.h"
#ifdef API_GRAPHICS_D3D11
//================================

//= INCLUDES ========================
#include "../RHI_Sampler.h"
#include "../RHI_Device.h"
#include "../../Logging/Log.h"
#include "../../Core/Context.h"
#include "../../Rendering/Renderer.h"
//===================================

namespace Spartan
{
	RHI_Sampler::RHI_Sampler(
		const std::shared_ptr<RHI_Device>& rhi_device,
		const RHI_Filter filter_min,							/*= Filter_Nearest*/
		const RHI_Filter filter_mag,							/*= Filter_Nearest*/
		const RHI_Sampler_Mipmap_Mode filter_mipmap,			/*= Sampler_Mipmap_Nearest*/
		const RHI_Sampler_Address_Mode sampler_address_mode,	/*= Sampler_Address_Wrap*/
		const RHI_Comparison_Function comparison_function,		/*= Texture_Comparison_Always*/
		const bool anisotropy_enabled,							/*= false*/
		const bool comparison_enabled							/*= false*/
		)
	{	
		if (!rhi_device || !rhi_device->GetContextRhi()->device)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		// Save properties
		m_resource				= nullptr;
		m_rhi_device			= rhi_device;
		m_filter_min			= filter_min;
		m_filter_mag			= filter_mag;
		m_filter_mipmap			= filter_mipmap;	
		m_sampler_address_mode	= sampler_address_mode;
		m_comparison_function	= comparison_function;
		m_anisotropy_enabled	= anisotropy_enabled;
		m_comparison_enabled	= comparison_enabled;

		D3D11_SAMPLER_DESC sampler_desc;
		sampler_desc.Filter			= d3d11_common::sampler::get_filter(filter_min, filter_mag, filter_mipmap, anisotropy_enabled, comparison_enabled);
		sampler_desc.AddressU		= d3d11_sampler_address_mode[sampler_address_mode];
		sampler_desc.AddressV		= d3d11_sampler_address_mode[sampler_address_mode];
		sampler_desc.AddressW		= d3d11_sampler_address_mode[sampler_address_mode];
		sampler_desc.MipLODBias		= 0.0f;
        sampler_desc.MaxAnisotropy  = rhi_device->GetContext()->GetSubsystem<Renderer>()->GetOptionValue<UINT>(Option_Value_Anisotropy);
		sampler_desc.ComparisonFunc	= d3d11_comparison_function[comparison_function];
		sampler_desc.BorderColor[0]	= 0;
		sampler_desc.BorderColor[1]	= 0;
		sampler_desc.BorderColor[2]	= 0;
		sampler_desc.BorderColor[3]	= 0;
		sampler_desc.MinLOD			= 0;
		sampler_desc.MaxLOD			= FLT_MAX;
	
		// Create sampler state.
		if (FAILED(m_rhi_device->GetContextRhi()->device->CreateSamplerState(&sampler_desc, reinterpret_cast<ID3D11SamplerState**>(&m_resource))))
		{
			LOG_ERROR("Failed to create sampler state");
		}
	}

	RHI_Sampler::~RHI_Sampler()
	{
		safe_release(*reinterpret_cast<ID3D11SamplerState**>(&m_resource));
	}
}
#endif
