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

#pragma once

//= INCLUDES ======================
#include <memory>
#include "RHI_Definition.h"
#include "../Core/Spartan_Object.h"
//=================================

namespace Spartan
{
	class SPARTAN_CLASS RHI_DepthStencilState : public Spartan_Object
	{
	public:
		RHI_DepthStencilState(
            const std::shared_ptr<RHI_Device>& rhi_device,
            const bool depth_test                               = true,
            const bool depth_write                              = true,
            const RHI_Comparison_Function depth_function        = RHI_Comparison_LessEqual,
            const bool stencil_test                             = false,
            const bool stencil_write                            = false,
            const RHI_Comparison_Function stencil_function      = RHI_Comparison_Always,
            const RHI_Stencil_Operation stencil_fail_op         = RHI_Stencil_Keep,
            const RHI_Stencil_Operation stencil_depth_fail_op   = RHI_Stencil_Keep,
            const RHI_Stencil_Operation stencil_pass_op         = RHI_Stencil_Replace
        );
		~RHI_DepthStencilState();

        bool GetDepthTestEnabled()                              const { return m_depth_test_enabled; }
        bool GetDepthWriteEnabled()                             const { return m_depth_write_enabled; }
        bool GetStencilTestEnabled()                            const { return m_stencil_test_enabled; }
        bool GetStencilWriteEnabled()                           const { return m_stencil_write_enabled; }
        RHI_Comparison_Function GetDepthFunction()              const { return m_depth_function; }
        RHI_Comparison_Function GetStencilFunction()            const { return m_stencil_function; }
        RHI_Stencil_Operation GetStencilFailOperation()         const { return m_stencil_fail_op; }
        RHI_Stencil_Operation GetStencilDepthFailOperation()    const { return m_stencil_depth_fail_op; }
        RHI_Stencil_Operation GetStencilPassOperation()         const { return m_stencil_pass_op; }
        void* GetResource()                                     const { return m_buffer; }

	private:
		bool m_depth_test_enabled	                    = false;
        bool m_depth_write_enabled                      = false;
        RHI_Comparison_Function m_depth_function        = RHI_Comparison_Never;
        bool m_stencil_test_enabled                     = false;
        bool m_stencil_write_enabled                    = false;
        RHI_Comparison_Function m_stencil_function      = RHI_Comparison_Never;
        RHI_Stencil_Operation m_stencil_fail_op         = RHI_Stencil_Keep;
        RHI_Stencil_Operation m_stencil_depth_fail_op   = RHI_Stencil_Keep;
        RHI_Stencil_Operation m_stencil_pass_op         = RHI_Stencil_Replace;
		bool m_initialized		                        = false;
		void* m_buffer			                        = nullptr;
	};
}
