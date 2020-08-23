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

//= INCLUDES ======================
#include "Spartan.h"
#include "../RHI_Implementation.h"
#include "../RHI_RasterizerState.h"
#include "../RHI_Device.h"
//=================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_RasterizerState::RHI_RasterizerState
    (
        const shared_ptr<RHI_Device>& rhi_device,
        const RHI_Cull_Mode cull_mode,
        const RHI_Fill_Mode fill_mode,
        const bool depth_clip_enabled,
        const bool scissor_enabled,
        const bool multi_sample_enabled,
        const bool antialised_line_enabled,
        const float depth_bias              /*= 0.0f */,
        const float depth_bias_clamp        /*= 0.0f */,
        const float depth_bias_slope_scaled /*= 0.0f */,
        const float line_width              /*= 1.0f */)
    {
        // Save properties
        m_cull_mode                 = cull_mode;
        m_fill_mode                 = fill_mode;
        m_depth_clip_enabled        = depth_clip_enabled;
        m_scissor_enabled           = scissor_enabled;
        m_multi_sample_enabled      = multi_sample_enabled;
        m_antialised_line_enabled   = antialised_line_enabled;
        m_depth_bias                = depth_bias;
        m_depth_bias_clamp          = depth_bias_clamp;
        m_depth_bias_slope_scaled   = depth_bias_slope_scaled;
        m_line_width                = line_width;
    }
    
    RHI_RasterizerState::~RHI_RasterizerState()
    {
    
    }
}
