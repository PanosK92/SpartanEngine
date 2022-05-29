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

//= INCLUDES ===============
#include "Spartan.h"
#include "RHI_CommandPool.h"
//==========================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    void RHI_CommandPool::AllocateCommandLists(const uint32_t command_list_count)
    {
        for (uint32_t i = 0; i < command_list_count; i++)
        {
            string cmd_list_name = m_object_name + "_command_list_" + to_string(i);
            shared_ptr<RHI_CommandList> cmd_list = make_shared<RHI_CommandList>(m_context, this, cmd_list_name.c_str());
            m_cmd_lists.emplace_back(cmd_list);
        }
    }

    bool RHI_CommandPool::Tick()
    {
        if (m_cmd_list_index == -1)
        {
            m_cmd_list_index = 0;
            return false;
        }

        // Calculate command list index
        m_cmd_list_index = (m_cmd_list_index + 1) % GetCommandListCount();

        // Reset the command pool
        if (m_cmd_list_index == 0)
        {
            Reset();
            return true;
        }

        return false;
    }
}
