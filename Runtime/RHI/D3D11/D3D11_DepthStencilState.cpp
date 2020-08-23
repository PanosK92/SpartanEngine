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
#include "../RHI_DepthStencilState.h"
#include "../RHI_Device.h"
//===================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_DepthStencilState::RHI_DepthStencilState(
        const shared_ptr<RHI_Device>& rhi_device,
        const bool depth_test                                       /*= true*/,
        const bool depth_write                                      /*= true*/,
        const RHI_Comparison_Function depth_comparison_function     /*= Comparison_LessEqual*/,
        const bool stencil_test                                     /*= false */,
        const bool stencil_write                                    /*= false */,
        const RHI_Comparison_Function stencil_comparison_function   /*= RHI_Comparison_Equal */,
        const RHI_Stencil_Operation stencil_fail_op                 /*= RHI_Stencil_Keep */,
        const RHI_Stencil_Operation stencil_depth_fail_op           /*= RHI_Stencil_Keep */,
        const RHI_Stencil_Operation stencil_pass_op                 /*= RHI_Stencil_Replace */
    )
    {
        if (!rhi_device)
        {
            LOG_ERROR_INVALID_INTERNALS();
            return;
        }

        auto d3d11_device = rhi_device->GetContextRhi()->device;
        if (!d3d11_device)
        {
            LOG_ERROR_INVALID_INTERNALS();
            return;
        }

        // Save properties
        m_depth_test_enabled            = depth_test;
        m_depth_write_enabled           = depth_write;
        m_depth_comparison_function     = depth_comparison_function;
        m_stencil_test_enabled          = stencil_test;
        m_stencil_write_enabled         = stencil_write;
        m_stencil_comparison_function   = stencil_comparison_function;
        m_stencil_fail_op               = stencil_fail_op;
        m_stencil_depth_fail_op         = stencil_depth_fail_op;
        m_stencil_pass_op               = stencil_pass_op;

        // Create description
        D3D11_DEPTH_STENCIL_DESC desc;
        {
            // Depth test parameters
            desc.DepthEnable                    = static_cast<BOOL>(depth_test || depth_write);
            desc.DepthWriteMask                 = depth_write ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
            desc.DepthFunc                      = d3d11_comparison_function[depth_comparison_function];
            // Stencil test parameters
            desc.StencilEnable                  = static_cast<BOOL>(stencil_test || stencil_write);
            desc.StencilReadMask                = stencil_test  ? GetStencilReadMask()   : 0;
            desc.StencilWriteMask               = stencil_write ? GetStencilWriteMask()  : 0;
            // Stencil operations if pixel is front-facing
            desc.FrontFace.StencilFailOp        = d3d11_stencil_operation[m_stencil_fail_op];
            desc.FrontFace.StencilDepthFailOp   = d3d11_stencil_operation[m_stencil_depth_fail_op];
            desc.FrontFace.StencilPassOp        = d3d11_stencil_operation[m_stencil_pass_op];
            desc.FrontFace.StencilFunc          = d3d11_comparison_function[stencil_comparison_function];
            // Stencil operations if pixel is back-facing
            desc.BackFace                       = desc.FrontFace;
        }

        // Create depth-stencil state
        auto depth_stencil_state = static_cast<ID3D11DepthStencilState*>(m_buffer);
        m_initialized = d3d11_utility::error_check(d3d11_device->CreateDepthStencilState(&desc, reinterpret_cast<ID3D11DepthStencilState**>(&m_buffer)));
    }

    RHI_DepthStencilState::~RHI_DepthStencilState()
    {
        d3d11_utility::release(*reinterpret_cast<ID3D11DepthStencilState**>(&m_buffer));
    }
}
