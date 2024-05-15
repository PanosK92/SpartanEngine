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
#include "RHI_Definitions.h"
#include "../Core/SpartanObject.h"
//================================

namespace Spartan
{
    class SP_CLASS RHI_RasterizerState : public SpartanObject
    {
    public:
        RHI_RasterizerState() = default;
        RHI_RasterizerState(
            const RHI_PolygonMode fill_mode,
            const bool depth_clip_enabled,
            const float depth_bias              = 0.0f,
            const float depth_bias_clamp        = 0.0f,
            const float depth_bias_slope_scaled = 0.0f,
            const float line_width              = 1.0f
        );
        ~RHI_RasterizerState();

        RHI_PolygonMode GetPolygonMode() const { return m_polygon_mode; }
        bool GetDepthClipEnabled()       const { return m_depth_clip_enabled; }
        void* GetRhiResource()           const { return m_rhi_resource; }
        float GetLineWidth()             const { return m_line_width; }
        float GetDepthBias()             const { return m_depth_bias; }
        float GetDepthBiasClamp()        const { return m_depth_bias_clamp; }
        float GetDepthBiasSlopeScaled()  const { return m_depth_bias_slope_scaled; }
        uint64_t GetHash()               const { return m_hash; }

        bool operator==(const RHI_RasterizerState& rhs) const
        {
            return m_hash == rhs.GetHash();
        }

    private:
        RHI_PolygonMode m_polygon_mode  = RHI_PolygonMode::Max;
        bool m_depth_clip_enabled       = false;
        float m_depth_bias              = 0.0f;
        float m_depth_bias_clamp        = 0.0f;
        float m_depth_bias_slope_scaled = 0.0f;
        float m_line_width              = 1.0f;

        uint64_t m_hash      = 0;
        void* m_rhi_resource = nullptr;
    };
}
