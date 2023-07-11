/*
Copyright(c) 2016-2023 Panos Karabelas

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
#include "pch.h"
#include "RHI_CommandPool.h"
//==========================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    void RHI_CommandPool::AllocateCommandLists(const RHI_Queue_Type queue_type, const uint32_t cmd_list_count /*= 2*/, const uint32_t cmd_pool_count /*= 2*/)
    {
        m_cmd_list_count = cmd_list_count;
        m_cmd_pool_count = cmd_pool_count;

        for (uint32_t index_cmd_pool = 0; index_cmd_pool < cmd_pool_count; index_cmd_pool++)
        {
            for (uint32_t index_cmd_list = 0; index_cmd_list < cmd_list_count; index_cmd_list++)
            {
                CreateCommandPool(queue_type);
                string cmd_list_name = m_object_name + "_cmd_pool_" + to_string(index_cmd_pool) + "_cmd_list_" + to_string(index_cmd_list);
                m_cmd_lists.push_back(make_shared<RHI_CommandList>(queue_type, index_cmd_list, m_swap_chain_id, m_rhi_resources[index_cmd_pool], cmd_list_name.c_str()));
            }
        }
    }

    bool RHI_CommandPool::Step()
    {
        if (!m_first_step)
        {
            m_cmd_list_index = (m_cmd_list_index + 1) % static_cast<uint32_t>(m_cmd_lists.size());

            // Ensure that the new m_cmd_list_index refers to a command list that's ready to use
            if (m_cmd_lists[m_cmd_list_index]->GetState() == RHI_CommandListState::Submitted)
            {
                m_cmd_lists[m_cmd_list_index]->Wait();
            }
        }

        m_first_step = false;

        // Every time the command pool (that the command list comes from) changes, we reset it.
        if ((m_cmd_list_index % m_cmd_pool_count) == 0)
        {
            Reset(GetPoolIndex());
            return true;
        }

        return false;
    }
}
