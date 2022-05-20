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
#include <chrono>
#include <memory>
#include "..\RHI\RHI_Definition.h"
//================================

namespace Spartan
{
    class RHI_Device;

    enum class TimeBlockType
    {
        Cpu,
        Gpu,
        Undefined
    };

    class TimeBlock
    {
    public:
        TimeBlock() = default;
        ~TimeBlock();

        void Begin(const uint32_t id, const char* name, TimeBlockType type, const TimeBlock* parent = nullptr, RHI_CommandList* cmd_list = nullptr, const std::shared_ptr<RHI_Device>& rhi_device = nullptr);
        void End();
        void ComputeDuration(const uint32_t pass_index);
        void Reset();
        TimeBlockType GetType()      const { return m_type; }
        const char* GetName()        const { return m_name; }
        const TimeBlock* GetParent() const { return m_parent; }
        uint32_t GetTreeDepth()      const { return m_tree_depth; }
        uint32_t GetTreeDepthMax()   const { return m_max_tree_depth; }
        float GetDuration()          const { return m_duration; }
        bool IsComplete()            const { return m_is_complete; }
        uint32_t GetId()             const { return m_id; }
        void ClearGpuObjects();

    private:    
        static uint32_t FindTreeDepth(const TimeBlock* time_block, uint32_t depth = 0);
        static uint32_t m_max_tree_depth;

        const char* m_name        = nullptr;
        TimeBlockType m_type      = TimeBlockType::Undefined;
        float m_duration          = 0.0f;
        const TimeBlock* m_parent = nullptr;
        uint32_t m_tree_depth     = 0;
        bool m_is_complete        = false;
        RHI_Device* m_rhi_device  = nullptr;
        uint32_t m_id             = 0;

        // CPU timing
        std::chrono::steady_clock::time_point m_start;
        std::chrono::steady_clock::time_point m_end;
    
        // GPU timing
        void* m_query_start         = nullptr;
        void* m_query_end           = nullptr;
        RHI_CommandList* m_cmd_list = nullptr;
    };
}
