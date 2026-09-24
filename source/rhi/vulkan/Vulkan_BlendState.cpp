/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =================
#include "pch.h"
#include "../RHI_BlendState.h"
//============================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    RHI_BlendState::RHI_BlendState
    (
        const bool blend_enabled                  /*= false*/,
        const RHI_Blend source_blend              /*= Blend_Src_Alpha*/,
        const RHI_Blend dest_blend                /*= Blend_Inv_Src_Alpha*/,
        const RHI_Blend_Operation blend_op        /*= Blend_Operation_Add*/,
        const RHI_Blend source_blend_alpha        /*= Blend_One*/,
        const RHI_Blend dest_blend_alpha          /*= Blend_One*/,
        const RHI_Blend_Operation blend_op_alpha, /*= Blend_Operation_Add*/
        const float blend_factor                  /*= 0.0f*/
    )
    {
        // save
        m_blend_enabled      = blend_enabled;
        m_source_blend       = source_blend;
        m_dest_blend         = dest_blend;
        m_blend_op           = blend_op;
        m_source_blend_alpha = source_blend_alpha;
        m_dest_blend_alpha   = dest_blend_alpha;
        m_blend_op_alpha     = blend_op_alpha;
        m_blend_factor       = blend_factor;

        // hash
        m_hash = rhi_hash_combine(m_hash, static_cast<uint64_t>(m_blend_enabled));
        m_hash = rhi_hash_combine(m_hash, static_cast<uint64_t>(m_source_blend));
        m_hash = rhi_hash_combine(m_hash, static_cast<uint64_t>(m_dest_blend));
        m_hash = rhi_hash_combine(m_hash, static_cast<uint64_t>(m_blend_op));
        m_hash = rhi_hash_combine(m_hash, static_cast<uint64_t>(m_source_blend_alpha));
        m_hash = rhi_hash_combine(m_hash, static_cast<uint64_t>(m_dest_blend_alpha));
        m_hash = rhi_hash_combine(m_hash, static_cast<uint64_t>(m_blend_op_alpha));
        m_hash = rhi_hash_combine(m_hash, static_cast<uint64_t>(m_blend_factor));
    }

    RHI_BlendState::~RHI_BlendState()
    {

    }
}
