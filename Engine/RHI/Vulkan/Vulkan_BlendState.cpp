/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= IMPLEMENTATION ===============
#include "../RHI_Implementation.h"
#ifdef API_GRAPHICS_VULKAN
//================================

//= INCLUDES =================
#include "../RHI_BlendState.h"
#include "../RHI_Device.h"
//============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	RHI_BlendState::RHI_BlendState
	(
		const std::shared_ptr<RHI_Device>& device,
		const bool blend_enabled					/*= false*/,
		const RHI_Blend source_blend				/*= Blend_Src_Alpha*/,
		const RHI_Blend dest_blend					/*= Blend_Inv_Src_Alpha*/,
		const RHI_Blend_Operation blend_op			/*= Blend_Operation_Add*/,
		const RHI_Blend source_blend_alpha			/*= Blend_One*/,
		const RHI_Blend dest_blend_alpha			/*= Blend_One*/,
		const RHI_Blend_Operation blend_op_alpha	/*= Blend_Operation_Add*/
	)
	{
		VkPipelineColorBlendAttachmentState blend_state = {};
		blend_state.colorWriteMask		= VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		blend_state.blendEnable			= VK_FALSE;
		blend_state.srcColorBlendFactor	= vulkan_blend_factor[source_blend];
		blend_state.dstColorBlendFactor	= vulkan_blend_factor[dest_blend];
		blend_state.colorBlendOp		= vulkan_blend_operation[blend_op];
		blend_state.srcAlphaBlendFactor	= vulkan_blend_factor[source_blend_alpha];
		blend_state.dstAlphaBlendFactor	= vulkan_blend_factor[dest_blend_alpha];
		blend_state.alphaBlendOp		= vulkan_blend_operation[blend_op_alpha];

		VkPipelineColorBlendStateCreateInfo color_blending = {};
		color_blending.sType				= VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		color_blending.logicOpEnable		= VK_FALSE;
		color_blending.logicOp				= VK_LOGIC_OP_COPY;
		color_blending.attachmentCount		= 1;
		color_blending.pAttachments			= &blend_state;
		color_blending.blendConstants[0]	= 0.0f;
		color_blending.blendConstants[1]	= 0.0f;
		color_blending.blendConstants[2]	= 0.0f;
		color_blending.blendConstants[3]	= 0.0f;
	}

	RHI_BlendState::~RHI_BlendState()
	{
		
	}
}
#endif