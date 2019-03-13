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
#include <vector>
//=========================

namespace Directus
{
	class RHI_IndexBuffer : public RHI_Object
	{
	public:
		RHI_IndexBuffer(const std::shared_ptr<RHI_Device>& rhi_device, RHI_Format format = Format_R32_UINT);
		~RHI_IndexBuffer();
	
		bool Create(const std::vector<unsigned int>& indices);
		bool CreateDynamic(unsigned int stride, unsigned int index_count);
		void* Map() const;
		bool Unmap() const;

		void* GetBuffer() const				{ return m_buffer; }
		RHI_Format GetFormat() const		{ return m_buffer_format; }
		unsigned int GetMemoryUsage() const { return m_memory_usage; }
		unsigned int GetIndexCount() const	{ return m_index_count; }

	protected:
		unsigned int m_index_count	= 0;
		unsigned int m_memory_usage	= 0;	
		void* m_buffer				= nullptr;
		RHI_Format m_buffer_format;
		std::shared_ptr<RHI_Device> m_rhiDevice;
	};
}