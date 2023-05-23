/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_RasterizerState.h"
#include "../RHI_Device.h"
#include "../Rendering/Renderer.h"
//=================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_RasterizerState::RHI_RasterizerState
    (
        const RHI_CullMode cull_mode,
        const RHI_PolygonMode polygon_mode,
        const bool depth_clip_enabled,
        const bool scissor_enabled,
        const float depth_bias              /*= 0.0f */,
        const float depth_bias_clamp        /*= 0.0f */,
        const float depth_bias_slope_scaled /*= 0.0f */,
        const float line_width              /*= 1.0f */)
    {
        // Save properties
        m_cull_mode               = cull_mode;
        m_polygon_mode            = polygon_mode;
        m_depth_clip_enabled      = depth_clip_enabled;
        m_scissor_enabled         = scissor_enabled;
        m_depth_bias              = depth_bias;
        m_depth_bias_clamp        = depth_bias_clamp;
        m_depth_bias_slope_scaled = depth_bias_slope_scaled;
        m_line_width              = line_width;

        // Create rasterizer description
        D3D11_RASTERIZER_DESC desc  = {};
        desc.CullMode               = d3d11_cull_mode[static_cast<uint32_t>(cull_mode)];
        desc.FillMode               = d3d11_polygon_mode[static_cast<uint32_t>(polygon_mode)];
        desc.FrontCounterClockwise  = false;
        desc.DepthBias              = static_cast<UINT>(Math::Helper::Floor(depth_bias * (float)(1 << 24)));
        desc.DepthBiasClamp         = depth_bias_clamp;
        desc.SlopeScaledDepthBias   = depth_bias_slope_scaled;
        desc.DepthClipEnable        = depth_clip_enabled;
        desc.MultisampleEnable      = false;
        desc.AntialiasedLineEnable  = m_line_width > 1.0f ? true : false;
        desc.ScissorEnable          = scissor_enabled;

        // Create rasterizer state
        SP_ASSERT(d3d11_utility::error_check(RHI_Context::device->CreateRasterizerState(&desc, reinterpret_cast<ID3D11RasterizerState**>(&m_rhi_resource))));
    }

    RHI_RasterizerState::~RHI_RasterizerState()
    {
        d3d11_utility::release<ID3D11RasterizerState>(m_rhi_resource);
    }
}
