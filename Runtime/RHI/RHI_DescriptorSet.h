#pragma once

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

#pragma once

//= INCLUDES =====================
#include "../Core/SpartanObject.h"
#include "RHI_Descriptor.h"
//================================

namespace Spartan
{
    class SPARTAN_CLASS RHI_DescriptorSet : public SpartanObject
    {
    public:
        RHI_DescriptorSet() = default;
        RHI_DescriptorSet(const RHI_Device* rhi_device, const RHI_DescriptorSetLayoutCache* descriptor_set_layout_cache, const std::vector<RHI_Descriptor>& descriptors);
        ~RHI_DescriptorSet();

        void* GetResource() { return m_resource; }

    private:
        bool Create();
        void Update(const std::vector<RHI_Descriptor>& descriptors);

        void* m_resource = nullptr;
        const RHI_DescriptorSetLayoutCache* m_descriptor_set_layout_cache = nullptr;
        const RHI_Device* m_rhi_device = nullptr;
    };
}
