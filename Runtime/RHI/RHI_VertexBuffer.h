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
#include "RHI_Vertex.h"
#include <vector>
//=========================

namespace Spartan
{
	class RHI_VertexBuffer : public RHI_Object
	{
	public:
		RHI_VertexBuffer(const std::shared_ptr<RHI_Device>& rhi_device) 
		{
			m_rhi_device = rhi_device;
		}

		template<typename T>
		bool Create(const std::vector<T>& vector)
		{
			m_is_dynamic	= false;
			m_stride		= static_cast<unsigned int>(sizeof(T));
			m_vertex_count	= static_cast<unsigned int>(vector.size());
			m_memory_usage	= m_stride * m_vertex_count;
			return Create(static_cast<const void*>(vector.data()));
		}

		template<typename T>
		bool CreateDynamic(unsigned int vertex_count)
		{
			m_is_dynamic	= true;		
			m_stride		= static_cast<unsigned int>(sizeof(T));
			m_vertex_count	= vertex_count;
			m_memory_usage	= m_stride * vertex_count;
			return Create(nullptr);
		}

		~RHI_VertexBuffer();

		void* Map() const;
		bool Unmap() const;

		void* GetBuffer() const				{ return m_buffer; }
		uint64_t GetDeviceSize() const		{ return m_device_size; }
		unsigned int GetStride() const		{ return m_stride; }
		unsigned int GetVertexCount() const	{ return m_vertex_count; }
		unsigned int GetMemoryUsage() const	{ return m_memory_usage; }

	private:
		bool Create(const void* vertices);

		unsigned int m_stride		= 0;
		unsigned int m_vertex_count = 0;
		unsigned int m_is_dynamic	= false;	
		std::shared_ptr<RHI_Device> m_rhi_device;

		void* m_buffer			= nullptr;
		void* m_buffer_memory	= nullptr;
		uint64_t m_device_size	= 256;
	};
}