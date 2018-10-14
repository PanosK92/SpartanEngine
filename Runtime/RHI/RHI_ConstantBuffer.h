/*
Copyright(c) 2016-2018 Panos Karabelas

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

//= INCLUDES ==================
#include "RHI_Object.h"
#include "RHI_Definition.h"
#include <memory>
#include "..\Core\EngineDefs.h"
//=============================

namespace Directus
{
	class ENGINE_CLASS RHI_ConstantBuffer : public RHI_Object
	{
	public:
		RHI_ConstantBuffer(std::shared_ptr<RHI_Device> rhiDevice);
		~RHI_ConstantBuffer();

		bool Create(unsigned int size, unsigned int slot, Buffer_Scope scope);
		void* Map();
		bool Unmap();
		void* GetBuffer()		{ return m_buffer; }
		unsigned int GetSlot()	{ return m_slot; }
		Buffer_Scope GetScope()	{ return m_scope; }

	private:
		std::shared_ptr<RHI_Device> m_rhiDevice;
		void* m_buffer;
		unsigned int m_slot;
		Buffer_Scope m_scope;
	};
}