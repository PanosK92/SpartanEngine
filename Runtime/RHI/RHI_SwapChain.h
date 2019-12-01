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

//= INCLUDES ======================
#include <memory>
#include <vector>
#include "RHI_Definition.h"
#include "../Core/Spartan_Object.h"
//=================================

namespace Spartan
{
	namespace Math { class Vector4; }

	class RHI_SwapChain : public Spartan_Object
	{
	public:
		RHI_SwapChain(
			void* window_handle,
			std::shared_ptr<RHI_Device> rhi_device,
			uint32_t width,
			uint32_t height,
			RHI_Format format		= RHI_Format_R8G8B8A8_Unorm,       
			uint32_t buffer_count	= 1,
            uint32_t flags          = RHI_Present_Immediate
		);
		~RHI_SwapChain();

		bool Resize(uint32_t width, uint32_t height);
		bool AcquireNextImage();
		bool Present();

        uint32_t GetWidth()				        const   { return m_width; }
        uint32_t GetHeight()			        const   { return m_height; }
        uint32_t GetBufferCount()		        const   { return m_buffer_count; }
        uint32_t GetImageIndex()                const   { return m_image_index; }
		bool IsInitialized()		            const   { return m_initialized; }
        void* GetRenderTargetView()	            const   { return m_render_target_view; }
        void* GetRenderPass()		            const   { return m_render_pass; }
        void* GetFrameBuffer()                  const   { return m_frame_buffers[m_image_index]; }
		void* GetSemaphoreImageAcquired()       const   { return m_image_acquired_semaphores[m_image_index]; }
        void*& GetCmdPool()                             { return m_cmd_pool; }
        RHI_CommandList* GetCmdList()                   { return m_cmd_lists[m_image_index].get(); }

	private:
        // Properties
		bool m_initialized			= false;
		bool m_windowed				= false;
		uint32_t m_buffer_count		= 0;		
		uint32_t m_max_resolution	= 16384;
		uint32_t m_width			= 0;
		uint32_t m_height			= 0;
		uint32_t m_flags			= 0;
		RHI_Format m_format			= RHI_Format_R8G8B8A8_Unorm;
		
		// API  
		void* m_swap_chain_view		= nullptr;
		void* m_render_target_view	= nullptr;
		void* m_surface				= nullptr;	
		void* m_window_handle		= nullptr;
        void* m_cmd_pool            = nullptr;
        void* m_render_pass         = nullptr;
        bool m_image_acquired       = false;
        uint32_t m_image_index      = 0;
        RHI_Device* m_rhi_device    = nullptr;
        std::vector<std::shared_ptr<RHI_CommandList>> m_cmd_lists;
		std::vector<void*> m_image_acquired_semaphores;
		std::vector<void*> m_image_views;
		std::vector<void*> m_frame_buffers;
	};
}
