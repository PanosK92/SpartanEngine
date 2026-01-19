/*
Copyright(c) 2015-2026 Panos Karabelas

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

namespace spartan
{
    // layout descriptor - immutable, from shader reflection
    struct RHI_Descriptor
    {
        RHI_Descriptor() = default;

        RHI_Descriptor(
            const std::string& name,
            const RHI_Descriptor_Type type,
            const RHI_Image_Layout layout,
            const uint32_t slot,
            const uint32_t stage,
            const uint32_t struct_size,
            const bool as_array,
            const uint32_t array_length
        )
            : type(type)
            , layout(layout)
            , slot(slot)
            , stage(stage)
            , name(name)
            , struct_size(struct_size)
            , as_array(as_array)
            , array_length(array_length)
        {}

        bool IsStorage() const { return type == RHI_Descriptor_Type::TextureStorage; }

        // layout properties (from reflection)
        RHI_Descriptor_Type type = RHI_Descriptor_Type::Max;
        RHI_Image_Layout layout  = RHI_Image_Layout::Max;
        uint32_t slot            = 0;
        uint32_t stage           = 0;
        uint32_t struct_size     = 0;
        uint32_t array_length    = 0;
        bool as_array            = false;
        std::string name;
    };

    // binding state - mutable, set at runtime
    struct RHI_DescriptorBinding
    {
        void* resource           = nullptr;
        uint64_t range           = 0;
        uint32_t dynamic_offset  = 0;
        uint32_t mip             = 0;
        uint32_t mip_range       = 0;
        RHI_Image_Layout layout  = RHI_Image_Layout::Max;

        bool IsBound() const { return resource != nullptr; }

        void Reset()
        {
            resource       = nullptr;
            range          = 0;
            dynamic_offset = 0;
            mip            = 0;
            mip_range      = 0;
            layout         = RHI_Image_Layout::Max;
        }

        uint64_t GetHash() const
        {
            uint64_t hash = reinterpret_cast<uint64_t>(resource);
            hash = rhi_hash_combine(hash, static_cast<uint64_t>(mip));
            hash = rhi_hash_combine(hash, static_cast<uint64_t>(mip_range));
            return hash;
        }
    };

    // combined descriptor with binding for descriptor set creation
    struct RHI_DescriptorWithBinding
    {
        RHI_Descriptor descriptor;
        RHI_DescriptorBinding binding;

        uint32_t GetSlot() const { return descriptor.slot; }
        RHI_Descriptor_Type GetType() const { return descriptor.type; }
        bool IsBound() const { return binding.IsBound(); }
    };
}
