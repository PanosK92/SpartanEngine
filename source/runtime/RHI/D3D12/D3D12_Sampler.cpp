/*
Copyright(c) 2016-2024 Panos Karabelas

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
#include "D3D12_Internal.h"
//===================================

namespace spartan
{
    void RHI_Sampler::CreateResource()
    {
        D3D12_SAMPLER_DESC sampler_desc = {};
        sampler_desc.Filter         = d3d12_utility::sampler::get_filter(m_filter_min, m_filter_mag, m_filter_mipmap, m_anisotropy != 0, m_comparison_enabled);
        sampler_desc.AddressU       = d3d12_sampler_address_mode[static_cast<uint32_t>(m_sampler_address_mode)];
        sampler_desc.AddressV       = d3d12_sampler_address_mode[static_cast<uint32_t>(m_sampler_address_mode)];
        sampler_desc.AddressW       = d3d12_sampler_address_mode[static_cast<uint32_t>(m_sampler_address_mode)];
        sampler_desc.MipLODBias     = m_mip_lod_bias;
        sampler_desc.MaxAnisotropy  = static_cast<UINT>(m_anisotropy);
        // d3d12 validation warns when comparisonfunc != never on non-comparison filters, so default to never unless this is an actual comparison sampler
        sampler_desc.ComparisonFunc = m_comparison_enabled ? d3d12_comparison_function[static_cast<uint32_t>(m_comparison_function)] : D3D12_COMPARISON_FUNC_NEVER;
        sampler_desc.BorderColor[0] = 0;
        sampler_desc.BorderColor[1] = 0;
        sampler_desc.BorderColor[2] = 0;
        sampler_desc.BorderColor[3] = 0;
        sampler_desc.MinLOD         = 0;
        sampler_desc.MaxLOD         = FLT_MAX;

        uint32_t index = d3d12_descriptors::AllocateSamplerCpu();
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = d3d12_descriptors::GetSamplerCpuHandle(index);

        RHI_Context::device->CreateSampler(&sampler_desc, cpu_handle);

        // store the cpu handle in the cpu-only staging heap so it can be copied later into the shader-visible heap
        m_rhi_resource = reinterpret_cast<void*>(cpu_handle.ptr);
    }

    RHI_Sampler::~RHI_Sampler()
    {
        // cpu heap doesn't reclaim slots - the descriptor just stays; ok for now
        m_rhi_resource = nullptr;
    }
}
