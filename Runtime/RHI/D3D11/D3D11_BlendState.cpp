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

//= INCLUDES =====================
#include "Spartan.h"
#include "../RHI_Implementation.h"
#include "../RHI_BlendState.h"
#include "../RHI_Device.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_BlendState::RHI_BlendState
    (
        const std::shared_ptr<RHI_Device>& rhi_device,
        const bool blend_enabled                    /*= false*/,
        const RHI_Blend source_blend                /*= Blend_Src_Alpha*/,
        const RHI_Blend dest_blend                    /*= Blend_Inv_Src_Alpha*/,
        const RHI_Blend_Operation blend_op            /*= Blend_Operation_Add*/,
        const RHI_Blend source_blend_alpha            /*= Blend_One*/,
        const RHI_Blend dest_blend_alpha            /*= Blend_One*/,
        const RHI_Blend_Operation blend_op_alpha,    /*= Blend_Operation_Add*/
        const float blend_factor                    /*= 0.0f*/
    )
    {
        if (!rhi_device)
        {
            LOG_ERROR_INVALID_INTERNALS();
            return;
        }

        if (!rhi_device->GetContextRhi()->device)
        {
            LOG_ERROR_INVALID_INTERNALS();
            return;
        }

        // Save parameters
        m_blend_enabled            = blend_enabled;
        m_source_blend            = source_blend;
        m_dest_blend            = dest_blend;
        m_blend_op                = blend_op;
        m_source_blend_alpha    = source_blend_alpha;
        m_dest_blend_alpha        = dest_blend_alpha;
        m_blend_op_alpha        = blend_op_alpha;
        m_blend_factor          = blend_factor;

        // Create description
        D3D11_BLEND_DESC desc;
        desc.AlphaToCoverageEnable    = false;
        desc.IndependentBlendEnable = blend_enabled;
        for (auto& render_target : desc.RenderTarget)
        {
            render_target.BlendEnable            = blend_enabled;
            render_target.SrcBlend                = d3d11_blend_factor[source_blend];
            render_target.DestBlend                = d3d11_blend_factor[dest_blend];
            render_target.BlendOp                = d3d11_blend_operation[blend_op];
            render_target.SrcBlendAlpha            = d3d11_blend_factor[source_blend_alpha];
            render_target.DestBlendAlpha        = d3d11_blend_factor[dest_blend_alpha];
            render_target.BlendOpAlpha            = d3d11_blend_operation[blend_op_alpha];
            render_target.RenderTargetWriteMask    = D3D11_COLOR_WRITE_ENABLE_ALL;
        }
        desc.RenderTarget[0].BlendEnable = blend_enabled;

        // Create blend state
        auto blend_state    = static_cast<ID3D11BlendState*>(m_resource);
        const auto result    = rhi_device->GetContextRhi()->device->CreateBlendState(&desc, &blend_state);

        // Handle result
        if (SUCCEEDED(result))
        {
            m_resource        = static_cast<void*>(blend_state);
            m_initialized    = true;    
        }
        else
        {
            m_initialized = false;
            LOG_ERROR("Failed to create blend state %s.", d3d11_utility::dxgi_error_to_string(result));
        }
    }

    RHI_BlendState::~RHI_BlendState()
    {
        d3d11_utility::release(*reinterpret_cast<ID3D11BlendState**>(&m_resource));
    }
}
