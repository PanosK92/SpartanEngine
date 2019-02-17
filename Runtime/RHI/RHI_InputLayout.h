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

//= INCLUDES ==================
#include "RHI_Definition.h"
#include <memory>
#include <vector>
#include <any>
#include "../Core/EngineDefs.h"
//=============================

namespace Directus
{
	class ENGINE_CLASS RHI_InputLayout
	{
	public:
		RHI_InputLayout(std::shared_ptr<RHI_Device> rhiDevice);
		~RHI_InputLayout();

		//= GRAPHICS API =====================================
		bool Create(void* VSBlob, unsigned long input_layout);
		//====================================================

		unsigned long GetInputLayout()	{ return m_inputLayout; }
		void* GetBuffer()				{ return m_buffer; }

	private:
		std::shared_ptr<RHI_Device> m_rhiDevice;
		unsigned long m_inputLayout = 0;
		void* m_buffer				= nullptr;
	};
}