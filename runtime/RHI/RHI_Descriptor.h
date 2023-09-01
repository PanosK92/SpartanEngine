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

#pragma once

//= INCLUDES ===============
#include "RHI_Definitions.h"
//==========================

namespace Spartan
{
    class RHI_Descriptor
    {
    public:
        RHI_Descriptor() = default;

        RHI_Descriptor(const RHI_Descriptor& descriptor)
        {
            type         = descriptor.type;
            layout       = descriptor.layout;
            slot         = descriptor.slot;
            stage        = descriptor.stage;
            name         = descriptor.name;
            mip          = descriptor.mip;
            array_length = descriptor.array_length;
            struct_size  = descriptor.struct_size;
        }

        RHI_Descriptor(
            const std::string& name,
            const RHI_Descriptor_Type type,
            const RHI_Image_Layout layout,
            const uint32_t slot,
            const uint32_t array_length,
            const uint32_t stage,
            const uint32_t struct_size
        )
        {
            this->type         = type;
            this->layout       = layout;
            this->slot         = slot;
            this->stage        = stage;
            this->name         = name;
            this->array_length = array_length;
            this->struct_size  = struct_size;
        }

        uint64_t ComputeHash()
        {
            if (m_hash == 0)
            {
                m_hash = rhi_hash_combine(m_hash, static_cast<uint64_t>(slot));
                m_hash = rhi_hash_combine(m_hash, static_cast<uint64_t>(stage));
            }

            return m_hash;
        }

        bool IsStorage() const { return type == RHI_Descriptor_Type::TextureStorage; }
        bool IsArray()   const { return array_length > 0; };

        // Properties that affect the descriptor hash (static - reflected)
        uint32_t slot  = 0;
        uint32_t stage = 0;

        // Properties that affect the descriptor set hash (dynamic - renderer)
        uint32_t mip       = 0;
        uint32_t mip_range = 0;
        void* data         = nullptr;

        // Properties that don't affect any hash
        RHI_Descriptor_Type type = RHI_Descriptor_Type::Undefined;
        RHI_Image_Layout layout  = RHI_Image_Layout::Undefined;
        uint64_t range           = 0;
        uint32_t array_length    = 0;
        uint32_t dynamic_offset  = 0;
        uint32_t struct_size     = 0;

        // Debugging
        std::string name;

    private:
        uint64_t m_hash = 0;
    };
}
