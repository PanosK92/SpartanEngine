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

//= INCLUDES ================
#include "RHI_Definition.h"
#include "RHI_Viewport.h"
#include "RHI_Object.h"
#include "../Core/Settings.h"
#include <vector>
//===========================

namespace Spartan
{
	class SPARTAN_CLASS RHI_RenderTexture : public RHI_Object
	{
	public:
		RHI_RenderTexture(
			const std::shared_ptr<RHI_Device>& rhi_device,
			unsigned int width,
			unsigned int height,
			RHI_Format texture_format	= Format_R8G8B8A8_UNORM,
			bool depth					= false,
			RHI_Format depth_format		= Format_D32_FLOAT,
			unsigned int array_size		= 1,
			bool is_cubemap				= false
		);
		~RHI_RenderTexture();


		// TODO: Remove clear functions, they should be done manually by the renderer
		bool Clear(std::shared_ptr<RHI_CommandList>& cmd_list, const Math::Vector4& clear_color);
		bool Clear(std::shared_ptr<RHI_CommandList>& cmd_list, float red, float green, float blue, float alpha);

		void* GetResource_RenderTarget(const unsigned int index = 0)	{ return index < m_buffer_render_target_views.size() ? m_buffer_render_target_views[index] : nullptr; }
		void* GetResource_Texture() const								{ return m_texture_view; }
		void* GetDepthStencilView() const								{ return m_depth_stencil_view; }
		const RHI_Viewport& GetViewport() const							{ return m_viewport; }
		bool GetDepthEnabled() const									{ return m_depth_enabled; }
		unsigned int GetWidth() const									{ return m_width; }
		unsigned int GetHeight() const									{ return m_height; }
		unsigned int GetArraySize() const								{ return m_array_size; }
		RHI_Format GetFormat() const									{ return m_format; }

	protected:
		bool m_depth_enabled		= false;
		float m_near_plane			= 0;
		float m_far_plane			= 0;
		unsigned int m_width		= 0;
		unsigned int m_height		= 0;
		unsigned int m_array_size	= 0;
		RHI_Viewport m_viewport;
		RHI_Format m_format;
		std::shared_ptr<RHI_Device> m_rhi_device;
		
		// API
		std::vector<void*> m_buffer_render_target_views;
		void* m_render_target_view		= nullptr;
		void* m_texture_view			= nullptr;
		void* m_depth_stencil_view		= nullptr;
	};
}