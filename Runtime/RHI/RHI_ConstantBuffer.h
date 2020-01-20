/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "../Core/EngineDefs.h"
#include "../Core/Spartan_Object.h"
//=================================

namespace Spartan
{
	class SPARTAN_CLASS RHI_ConstantBuffer : public Spartan_Object
	{
	public:
		RHI_ConstantBuffer(const std::shared_ptr<RHI_Device>& rhi_device)
		{
			m_rhi_device = rhi_device;
		}
		~RHI_ConstantBuffer();

		template<typename T>
		bool Create(const uint32_t instance_count = 1)
		{
            m_stride        = static_cast<uint32_t>(sizeof(T));
            m_offset_count  = instance_count;
            m_size          = static_cast<uint64_t>(m_stride * instance_count);
			return _Create();
		}

		void* Map();
		bool Unmap() const;
        bool Flush(uint32_t offset_index = 0);

		auto GetResource()          const { return m_buffer; }
		auto GetSize()              const { return m_size; }
        auto GetStride()            const { return m_stride; }
        bool IsDynamic()            const { return m_offset_count > 1; }
        uint32_t GetOffsetCount()   const { return m_offset_count; }
        uint32_t GetOffset()        const { return m_offset_index * m_stride; }

        uint32_t GetOffsetIndex() const                     { return m_offset_index; }
        void SetOffsetIndex(const uint32_t offset_index)    { m_offset_index = offset_index; }

	private:
		bool _Create();

        uint32_t m_stride       = 0;
        uint32_t m_offset_count = 0;
        uint32_t m_offset_index = 0;

		std::shared_ptr<RHI_Device> m_rhi_device;

		// API
		void* m_buffer			= nullptr;
		void* m_buffer_memory	= nullptr;
	};
}
