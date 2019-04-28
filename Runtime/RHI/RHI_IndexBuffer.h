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

namespace Spartan
{
	class RHI_IndexBuffer : public RHI_Object
	{
	public:
		RHI_IndexBuffer(const std::shared_ptr<RHI_Device>& rhi_device)
		{
			m_rhi_device = rhi_device;
		}

		~RHI_IndexBuffer();
	
		template<typename T>
		bool Create(const std::vector<T>& indices)
		{
			m_is_dynamic	= false;
			m_stride		= sizeof(T);
			m_index_count	= static_cast<unsigned int>(indices.size());
			m_size			= m_stride * m_index_count;
			return Create(indices.data());
		}

		template<typename T>
		bool CreateDynamic(unsigned int index_count)
		{
			m_is_dynamic	= true;
			m_stride		= sizeof(T);
			m_index_count	= index_count;
			m_size			= m_stride * m_index_count;
			return Create(nullptr);
		}

		void* Map() const;
		bool Unmap() const;

		auto GetResource()		const { return m_buffer; }
		auto GetSize()			const { return m_size; }
		auto GetIndexCount()	const { return m_index_count; }
		auto Is16Bit()			const { return sizeof(uint16_t) == m_stride; }
		auto Is32Bit()			const { return sizeof(uint32_t) == m_stride; }

	protected:
		bool Create(const void* indices);

		bool m_is_dynamic			= false;
		unsigned int m_stride		= 0;
		unsigned int m_index_count	= 0;
		std::shared_ptr<RHI_Device> m_rhi_device;

		void* m_buffer			= nullptr;
		void* m_buffer_memory	= nullptr;		
	};
}