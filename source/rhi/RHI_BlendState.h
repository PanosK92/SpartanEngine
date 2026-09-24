/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =====================
#include "RHI_Definitions.h"
#include "../core/SpartanObject.h"
//================================

namespace spartan
{
    class RHI_BlendState : public SpartanObject
    {
    public:
        RHI_BlendState(
            const bool blend_enabled                 = false,
            const RHI_Blend source_blend             = RHI_Blend::Src_Alpha,
            const RHI_Blend dest_blend               = RHI_Blend::Inv_Src_Alpha,
            const RHI_Blend_Operation blend_op       = RHI_Blend_Operation::Add,
            const RHI_Blend source_blend_alpha       = RHI_Blend::One,
            const RHI_Blend dest_blend_alpha         = RHI_Blend::One,
            const RHI_Blend_Operation blend_op_alpha = RHI_Blend_Operation::Add,
            const float blend_factor                 = 0.0f
        );
        ~RHI_BlendState();

        auto GetBlendEnabled()                        const { return m_blend_enabled; }
        auto GetSourceBlend()                         const { return m_source_blend; }
        auto GetDestBlend()                           const { return m_dest_blend; }
        auto GetBlendOp()                             const { return m_blend_op; }
        auto GetSourceBlendAlpha()                    const { return m_source_blend_alpha; }
        auto GetDestBlendAlpha()                      const { return m_dest_blend_alpha; }
        auto GetBlendOpAlpha()                        const { return m_blend_op_alpha; }
        void SetBlendFactor(const float blend_factor)       { m_blend_factor = blend_factor; }
        float GetBlendFactor()                        const { return m_blend_factor; }

        uint64_t GetHash() const { return m_hash; }

        bool operator==(const RHI_BlendState& rhs) const
        {
            return m_hash == rhs.m_hash;
        }

        void* GetRhiResource() const { return m_rhi_resource; }

    private:
        bool m_blend_enabled                 = false;
        RHI_Blend m_source_blend             = RHI_Blend::Src_Alpha;
        RHI_Blend m_dest_blend               = RHI_Blend::Inv_Src_Alpha;
        RHI_Blend_Operation m_blend_op       = RHI_Blend_Operation::Add;
        RHI_Blend m_source_blend_alpha       = RHI_Blend::One;
        RHI_Blend m_dest_blend_alpha         = RHI_Blend::One;
        RHI_Blend_Operation m_blend_op_alpha = RHI_Blend_Operation::Add;
        float m_blend_factor                 = 1.0f;

        uint64_t m_hash      = 0;
        void* m_rhi_resource = nullptr;
    };
}
