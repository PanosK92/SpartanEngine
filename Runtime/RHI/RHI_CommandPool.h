/*
Copyright(c) 2016-2022 Panos Karabelas

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

//= INCLUDES =====================
#include "../Core/SpartanObject.h"
#include "RHI_Definition.h"
#include "RHI_CommandList.h"
#include <vector>
//================================

namespace Spartan
{
    class RHI_CommandPool : public SpartanObject
    {
    public:
        RHI_CommandPool(RHI_Device* rhi_device, const char* name);
        ~RHI_CommandPool();

        void AllocateCommandLists(const uint32_t command_list_count);
        bool Tick();

        RHI_CommandList* GetCommandList()    { return m_cmd_lists[m_pool_index][m_cmd_list_index].get(); }
        uint32_t GetCommandListCount() const { return static_cast<uint32_t>(m_cmd_lists[0].size()); }
        uint32_t GetCommandListIndex() const { return m_cmd_list_index; }
        void*& GetResource()                 { return m_resources[m_pool_index]; }

    private:
        void Reset();

        // Command lists
        std::array<std::vector<std::shared_ptr<RHI_CommandList>>, 2> m_cmd_lists;
        int m_cmd_list_index = -1;

        // Pools
        std::array<void*, 2> m_resources;
        int m_pool_index = -1;

        RHI_Device* m_rhi_device = nullptr;
    };
}
