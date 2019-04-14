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
#include "RHI_Definition.h"
#include "RHI_Object.h"
#include <memory>
#include <vector>
//=========================

namespace Spartan
{
	namespace Math { class Vector4; }

	class RHI_SwapChain : public RHI_Object
	{
	public:
		RHI_SwapChain(
			void* window_handle,
			const std::shared_ptr<RHI_Device>& rhi_device,
			unsigned int width,
			unsigned int height,
			RHI_Format format			= Format_R8G8B8A8_UNORM,
			RHI_Swap_Effect swap_effect	= Swap_Discard,
			unsigned long flags			= 0,
			unsigned int buffer_count	= 1,
			void* render_pass			= nullptr
		);
		~RHI_SwapChain();

		bool Resize(unsigned int width, unsigned int height);
		bool AcquireNextImage();
		bool Present(RHI_Present_Mode mode, void* semaphore_wait);

		auto IsInitialized()			{ return m_initialized; }
		auto GetSwapChainView()			{ return m_swap_chain_view; }
		auto GetRenderTargetView()		{ return m_render_target_view; }
		auto GetWidth()					{ return m_width; }
		auto GetHeight()				{ return m_height; }
		auto GetBufferCount()			{ return m_buffer_count; }
		const auto& GetFrameBuffer()	{ return m_frame_buffers[m_image_index]; }
		auto& GetWaitSemaphore()		{ return m_semaphore_image_acquired;  }

	private:
		bool m_initialized				= false;
		bool m_windowed					= false;
		unsigned long m_flags			= 0;
		unsigned int m_buffer_count		= 0;		
		unsigned int m_max_resolution	= 16384;
		unsigned int m_width			= 0;
		unsigned int m_height			= 0;
		RHI_Format m_format;
		std::shared_ptr<RHI_Device> m_rhi_device;

		// API
		void* m_swap_chain_view				= nullptr;
		void* m_render_target_view			= nullptr;
		void* m_surface						= nullptr;	
		uint32_t m_image_index				= 0;
		void* m_semaphore_image_acquired	= nullptr;
		void* m_render_pass					= nullptr;
		void* m_window_handle				= nullptr;
		std::vector<void*> m_image_views;
		std::vector<void*> m_frame_buffers;
	};
}