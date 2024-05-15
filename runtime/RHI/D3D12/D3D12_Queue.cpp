/*
Copyright(c) 2016-2021 Panos Karabelas

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

//= INCLUDES =============
#include "pch.h"
#include "../RHI_Queue.h"
#include "../RHI_Device.h"
//========================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_Queue::RHI_Queue(const RHI_Queue_Type queue_type, const char* name)
    {
        m_object_name = name;
        m_type        = queue_type;

        for (uint32_t i = 0; i < static_cast<uint32_t>(m_rhi_resources.size()); i++)
        {
            SP_ASSERT_MSG(false, "Function not implmented");
        }
    }

    RHI_Queue::~RHI_Queue()
    {

    }

    void RHI_Queue::NextCommandList()
    {

    }
}
