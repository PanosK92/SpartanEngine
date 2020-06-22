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

//= INCLUDES ==================
#include "Spartan.h"
#include "RHI_Sampler.h"
#include "RHI_Device.h"
#include "RHI_Implementation.h"
//=============================

namespace Spartan
{
    RHI_Sampler::RHI_Sampler(
        const std::shared_ptr<RHI_Device>& rhi_device,
        const RHI_Filter filter_min                         /* = RHI_Filter_Nearest */,
        const RHI_Filter filter_mag                         /* = RHI_Filter_Nearest */,
        const RHI_Sampler_Mipmap_Mode filter_mipmap         /* = RHI_Sampler_Mipmap_Nearest */,
        const RHI_Sampler_Address_Mode sampler_address_mode /* = RHI_Sampler_Address_Wrap */,
        const RHI_Comparison_Function comparison_function   /* = RHI_Comparison_Always */,
        const bool anisotropy_enabled                       /* = false */,
        const bool comparison_enabled                       /* = false */
    )
    {
        if (!rhi_device || !rhi_device->GetContextRhi()->device)
        {
            LOG_ERROR_INVALID_PARAMETER();
            return;
        }

        m_resource              = nullptr;
        m_rhi_device            = rhi_device;
        m_filter_min            = filter_min;
        m_filter_mag            = filter_mag;
        m_filter_mipmap         = filter_mipmap;
        m_sampler_address_mode  = sampler_address_mode;
        m_comparison_function   = comparison_function;
        m_anisotropy_enabled    = anisotropy_enabled;
        m_comparison_enabled    = comparison_enabled;

        CreateResource();
    }
}
