/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =====================
#include <memory>
#include "RHI_Definitions.h"
#include "../core/SpartanObject.h"
//================================

namespace spartan
{
    class RHI_RasterizerState : public SpartanObject
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
