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

//= INCLUDES ==========================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_DescriptorSet.h"
#include "../RHI_DescriptorSetLayout.h"
#include "../RHI_Device.h"
//=====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    RHI_DescriptorSetLayout::~RHI_DescriptorSetLayout()
    {
        // m_rhi_resource is a stable pointer encoding of m_layout_hash, no real com object to release
        // routing through the deletion queue keeps lifecycle parity with the vulkan path
        if (m_rhi_resource)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::DescriptorSetLayout, m_rhi_resource);
            m_rhi_resource = nullptr;
        }
    }

    void RHI_DescriptorSetLayout::CreateRhiResource()
    {
        // d3d12 uses a single bindless root signature shared across all psos, so per-layout d3d12 objects
        // are not needed; encode the layout hash into m_rhi_resource so it's a stable non-null token,
        // matching what the shared rhi code expects (it asserts on null in some paths)
        m_rhi_resource = reinterpret_cast<void*>(m_layout_hash != 0 ? m_layout_hash : static_cast<uint64_t>(0x1));
    }
}
