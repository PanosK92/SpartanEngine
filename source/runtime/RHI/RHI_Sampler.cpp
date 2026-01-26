/*
Copyright(c) 2015-2026 Panos Karabelas

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
