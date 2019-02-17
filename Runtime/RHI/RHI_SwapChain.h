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
//=========================

namespace Directus
{
	namespace Math { class Vector4; }

	class RHI_SwapChain : public RHI_Object
	{
	public:
		RHI_SwapChain(
			void* windowHandle,
			std::shared_ptr<RHI_Device> device,
			unsigned int width,
			unsigned int height,
			RHI_Format format			= Format_R8G8B8A8_UNORM,
			RHI_Swap_Effect swapEffect	= Swap_Discard,
			unsigned long flags			= 0,
			unsigned int bufferCount	= 1
		);
		~RHI_SwapChain();

		bool Resize(unsigned int width, unsigned int height);
		bool SetAsRenderTarget();
		bool Present(RHI_Present_Mode mode);
		bool Clear(const Math::Vector4& color);

	private:
		void* m_swapChain			= nullptr;
		void* m_renderTargetView	= nullptr;
		unsigned long m_flags		= 0;
		unsigned int m_bufferCount	= 0;
		RHI_Format m_format;
		std::shared_ptr<RHI_Device> m_device;
	};
}