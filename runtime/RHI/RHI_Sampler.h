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

#pragma once

//= INCLUDES =====================
#include <memory>
#include "../Core/SpartanObject.h"
#include "RHI_Definitions.h"
//================================

namespace Spartan
{
    class RHI_Sampler : public SpartanObject
    {
    public:
        RHI_Sampler(
            const RHI_Filter filter_min                         = RHI_Filter::Nearest,
            const RHI_Filter filter_mag                         = RHI_Filter::Nearest,
            const RHI_Filter filter_mipmap                      = RHI_Filter::Nearest,
            const RHI_Sampler_Address_Mode sampler_address_mode = RHI_Sampler_Address_Mode::Wrap,
            const RHI_Comparison_Function comparison_function   = RHI_Comparison_Function::Never,
            const float anisotropy                              = 0.0f,
            const bool comparison_enabled                       = false,
            const float mip_bias                                = 0.0f
            );
        ~RHI_Sampler();

        RHI_Filter GetFilterMin()                       const { return m_filter_min; }
        RHI_Filter GetFilterMag()                       const { return m_filter_mag; }
        RHI_Filter GetFilterMipmap()                    const { return m_filter_mipmap; }
        RHI_Sampler_Address_Mode GetAddressMode()       const { return m_sampler_address_mode; }
        RHI_Comparison_Function GetComparisonFunction() const { return m_comparison_function; }
        bool GetAnisotropyEnabled()                     const { return m_anisotropy != 0; }
        bool GetComparisonEnabled()                     const { return m_comparison_enabled; }
        void* GetRhiResource()                          const { return m_rhi_resource; }

    private:
        void CreateResource();

        RHI_Filter m_filter_min                         = RHI_Filter::Nearest;
        RHI_Filter m_filter_mag                         = RHI_Filter::Nearest;
        RHI_Filter m_filter_mipmap                      = RHI_Filter::Nearest;
        RHI_Sampler_Address_Mode m_sampler_address_mode = RHI_Sampler_Address_Mode::Wrap;
        RHI_Comparison_Function m_comparison_function   = RHI_Comparison_Function::Always;
        float m_anisotropy                              = 0;
        bool m_comparison_enabled                       = false;
        float m_mip_lod_bias                            = 0.0f;

        // rhi
        void* m_rhi_resource = nullptr;
    };
}
