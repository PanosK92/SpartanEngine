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

//= INCLUDES ========================
#include "Spartan.h"
#include "../RHI_Implementation.h"
#include "../RHI_Sampler.h"
#include "../RHI_Device.h"
#include "../../Rendering/Renderer.h"
//===================================

namespace Spartan
{
    void RHI_Sampler::CreateResource()
    {    
        D3D11_SAMPLER_DESC sampler_desc = {};
        sampler_desc.Filter             = d3d11_utility::sampler::get_filter(m_filter_min, m_filter_mag, m_filter_mipmap, m_anisotropy_enabled, m_comparison_enabled);
        sampler_desc.AddressU           = d3d11_sampler_address_mode[m_sampler_address_mode];
        sampler_desc.AddressV           = d3d11_sampler_address_mode[m_sampler_address_mode];
        sampler_desc.AddressW           = d3d11_sampler_address_mode[m_sampler_address_mode];
        sampler_desc.MipLODBias         = 0.0f;
        sampler_desc.MaxAnisotropy      = m_rhi_device->GetContext()->GetSubsystem<Renderer>()->GetOptionValue<UINT>(Option_Value_Anisotropy);
        sampler_desc.ComparisonFunc     = d3d11_comparison_function[m_comparison_function];
        sampler_desc.BorderColor[0]     = 0;
        sampler_desc.BorderColor[1]     = 0;
        sampler_desc.BorderColor[2]     = 0;
        sampler_desc.BorderColor[3]     = 0;
        sampler_desc.MinLOD             = 0;
        sampler_desc.MaxLOD             = FLT_MAX;
    
        // Create sampler state.
        if (FAILED(m_rhi_device->GetContextRhi()->device->CreateSamplerState(&sampler_desc, reinterpret_cast<ID3D11SamplerState**>(&m_resource))))
        {
            LOG_ERROR("Failed to create sampler state");
        }
    }

    RHI_Sampler::~RHI_Sampler()
    {
        d3d11_utility::release(*reinterpret_cast<ID3D11SamplerState**>(&m_resource));
    }
}
