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

//= INCLUDES =====================
#include "RHI_Pipeline.h"
#include "RHI_Texture.h"
#include "RHI_Implementation.h"
#include "..\Rendering\Renderer.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    void* RHI_Pipeline::GetDescriptorPendingUpdate()
    {
        // Get the hash of the current descriptor blueprint
        uint32_t hash = GetDescriptorBlueprintHash(m_descriptor_blueprint);

        // If the has is already present, then we don't need to update
        if (m_descriptors_cache.find(hash) != m_descriptors_cache.end())
            return m_descriptors_cache[hash];

        // Otherwise generate a new one and return that
        return CreateDescriptorSet(hash);
    }

    uint32_t RHI_Pipeline::GetDescriptorBlueprintHash(const std::vector<RHI_Descriptor>& descriptor_blueprint)
    {
        std::hash<uint32_t> hasher;
        uint32_t hash = 0;
        for (const RHI_Descriptor& descriptor : m_descriptor_blueprint)
        {
            hash = hash * 31 + static_cast<uint32_t>(hasher(static_cast<uint32_t>(descriptor.type)));
            hash = hash * 31 + static_cast<uint32_t>(hasher(descriptor.slot));
            hash = hash * 31 + static_cast<uint32_t>(hasher(descriptor.id));
        }

        return hash;
    }
}
