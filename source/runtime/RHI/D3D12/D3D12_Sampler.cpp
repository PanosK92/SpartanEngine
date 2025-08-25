/*
Copyright(c) 2016-2021 Panos Karabelas

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
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_Sampler.h"
#include "../RHI_Device.h"
#include "../../Rendering/Renderer.h"
//===================================

namespace spartan
{
    void RHI_Sampler::CreateResource()
    {
        //ID3D12Device* device = m_rhi_device->GetContextRhi()->device;

        // Get the sampler descriptor size for the current device.
        //const UINT samplerDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

        // Get a handle to the start of the descriptor heap.
        //CD3DX12_CPU_DESCRIPTOR_HANDLE samplerHandle(m_samplerHeap->GetCPUDescriptorHandleForHeapStart());


        D3D12_SAMPLER_DESC sampler_desc = {};
        sampler_desc.Filter             = d3d12_utility::sampler::get_filter(m_filter_min, m_filter_mag, m_filter_mipmap, m_anisotropy != 0, m_comparison_enabled);
        sampler_desc.AddressU           = d3d12_sampler_address_mode[static_cast<uint32_t>(m_sampler_address_mode)];
        sampler_desc.AddressV           = d3d12_sampler_address_mode[static_cast<uint32_t>(m_sampler_address_mode)];
        sampler_desc.AddressW           = d3d12_sampler_address_mode[static_cast<uint32_t>(m_sampler_address_mode)];
        sampler_desc.MipLODBias         = m_mip_lod_bias;
        sampler_desc.MaxAnisotropy      = static_cast<UINT>(m_anisotropy);
        sampler_desc.ComparisonFunc     = d3d12_comparison_function[static_cast<uint32_t>(m_comparison_function)];
        sampler_desc.BorderColor[0]     = 0;
        sampler_desc.BorderColor[1]     = 0;
        sampler_desc.BorderColor[2]     = 0;
        sampler_desc.BorderColor[3]     = 0;
        sampler_desc.MinLOD             = 0;
        sampler_desc.MaxLOD             = FLT_MAX;

        // Create sampler state.
        //d3d12_utility::error::check(device >CreateSampler(&sampler_desc, samplerHandle));
    }
    
    RHI_Sampler::~RHI_Sampler()
    {
    
    }
}
