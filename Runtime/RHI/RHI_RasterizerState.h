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
#include <memory>
#include "RHI_Object.h"
#include "RHI_Definition.h"
//=========================

namespace Spartan
{
	class SPARTAN_CLASS RHI_RasterizerState : public RHI_Object
	{
	public:
		RHI_RasterizerState(
			const std::shared_ptr<RHI_Device>& rhi_device,
			RHI_Cull_Mode cull_mode,
			RHI_Fill_Mode fill_mode,
			bool depth_clip_enabled,
			bool scissor_enabled,
			bool multi_sample_enabled, 
			bool antialised_line_enabled
		);
		~RHI_RasterizerState();

		RHI_Cull_Mode GetCullMode() const		{ return m_cull_mode; }
		RHI_Fill_Mode GetFillMode() const		{ return m_fill_mode; }
		bool GetDepthClipEnabled() const		{ return m_depth_clip_enabled; }
		bool GetScissorEnabled() const			{ return m_scissor_enabled; }
		bool GetMultiSampleEnabled() const		{ return m_multi_sample_enabled; }
		bool GetAntialisedLineEnabled() const	{ return m_antialised_line_enabled; }
		bool IsInitialized() const				{ return m_initialized; }
		void* GetBuffer() const					{ return m_buffer; }

	private:
		// Properties
		RHI_Cull_Mode m_cull_mode;
		RHI_Fill_Mode m_fill_mode;
		bool m_depth_clip_enabled;
		bool m_scissor_enabled;
		bool m_multi_sample_enabled;
		bool m_antialised_line_enabled;

		// Initialized
		bool m_initialized = false;

		// Rasterizer state view
		void* m_buffer = nullptr;
	};
}