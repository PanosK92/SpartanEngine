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
		RHI_IndexBuffer(std::shared_ptr<RHI_Device> rhiDevice, RHI_Format format = Format_R32_UINT);
		~RHI_IndexBuffer();
	
		bool Create(const std::vector<unsigned int>& indices);
		bool CreateDynamic(unsigned int stride, unsigned int indexCount);
		void* Map();
		bool Unmap();

		void* GetBuffer()				{ return m_buffer; }
		RHI_Format GetFormat()			{ return m_bufferFormat; }
		unsigned int GetMemoryUsage()	{ return m_memoryUsage; }
		unsigned int GetIndexCount()	{ return m_indexCount; }

	protected:
		unsigned int m_indexCount	= 0;
		unsigned int m_memoryUsage	= 0;
		RHI_Format m_bufferFormat;
		void* m_buffer				= nullptr;
		std::shared_ptr<RHI_Device> m_rhiDevice;
	};
}