/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==================
#include "pch.h"
#include "RHI_Sampler.h"
#include "RHI_Device.h"
#include "RHI_Implementation.h"
//=============================

namespace spartan
{
    RHI_Sampler::RHI_Sampler(
        const RHI_Filter filter_min                         /* = RHI_Filter::Nearest */,
        const RHI_Filter filter_mag                         /* = RHI_Filter::Nearest */,
        const RHI_Filter filter_mipmap                      /* = RHI_Filter::Nearest */,
        const RHI_Sampler_Address_Mode sampler_address_mode /* = RHI_Sampler_Address_Wrap */,
        const RHI_Comparison_Function comparison_function   /* = RHI_Comparison_Always */,
        const float anisotropy                              /* = 0.0f */,
        const bool comparison_enabled                       /* = false */,
        const float mip_lod_bias                            /* = 0.0f */
    )
    {
        m_rhi_resource         = nullptr;
        m_filter_min           = filter_min;
        m_filter_mag           = filter_mag;
        m_filter_mipmap        = filter_mipmap;
        m_sampler_address_mode = sampler_address_mode;
        m_comparison_function  = comparison_function;
        m_anisotropy           = anisotropy;
        m_comparison_enabled   = comparison_enabled;
        m_mip_lod_bias         = mip_lod_bias;

        CreateResource();
    }
}
