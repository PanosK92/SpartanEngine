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
        const std::shared_ptr<RHI_Device>& device,
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
        // Save parameters
        m_blend_enabled            = blend_enabled;
        m_source_blend            = source_blend;
        m_dest_blend            = dest_blend;
        m_blend_op                = blend_op;
        m_source_blend_alpha    = source_blend_alpha;
        m_dest_blend_alpha        = dest_blend_alpha;
        m_blend_op_alpha        = blend_op_alpha;
        m_blend_factor          = blend_factor;
    }

    RHI_BlendState::~RHI_BlendState()
    {
        
    }
}
