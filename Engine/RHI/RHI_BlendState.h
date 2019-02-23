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

#pragma once

//= INCLUDES ==============
#include "RHI_Object.h"
#include "RHI_Definition.h"
#include <memory>
//=========================

namespace Directus
{
	class ENGINE_CLASS RHI_BlendState : public RHI_Object
	{
	public:
		RHI_BlendState(std::shared_ptr<RHI_Device> device,
			bool blendEnabled					= false,
			RHI_Blend sourceBlend				= Blend_Src_Alpha,
			RHI_Blend destBlend					= Blend_Inv_Src_Alpha,
			RHI_Blend_Operation blendOp			= Blend_Operation_Add,
			RHI_Blend sourceBlendAlpha			= Blend_One,
			RHI_Blend destBlendAlpha			= Blend_One,
			RHI_Blend_Operation blendOpAlpha	= Blend_Operation_Add
		);
		~RHI_BlendState();

		bool BlendEnabled() { return m_blendEnabled; }
		void* GetBuffer()	{ return m_buffer; }

	private:
		bool m_blendEnabled = false;
		bool m_initialized	= false;
		void* m_buffer		= nullptr;
	};
}