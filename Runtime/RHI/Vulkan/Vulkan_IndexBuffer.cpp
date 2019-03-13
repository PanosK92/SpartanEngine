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

//= INCLUDES ==================
#include "../RHI_Device.h"
#include "../RHI_IndexBuffer.h"
#include "../../Logging/Log.h"
//=============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	RHI_IndexBuffer::RHI_IndexBuffer(const std::shared_ptr<RHI_Device>& rhi_device, const RHI_Format format)
	{
		m_rhiDevice		= rhi_device;
		m_buffer		= nullptr;
		m_buffer_format	= format;
		m_memory_usage	= 0;
		m_index_count	= 0;
	}

	RHI_IndexBuffer::~RHI_IndexBuffer()
	{
		
	}

	bool RHI_IndexBuffer::Create(const vector<unsigned int>& indices)
	{
		return false;
	}

	bool RHI_IndexBuffer::CreateDynamic(const unsigned int stride, const unsigned int index_count)
	{
		return false;
	}

	void* RHI_IndexBuffer::Map() const
	{
		return nullptr;
	}

	bool RHI_IndexBuffer::Unmap() const
	{
		return false;
	}
}
#endif