/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES =================
#include "RHI_Definition.h"
#include "../Utilities/Hash.h"
//============================

namespace Spartan
{
    struct RHI_Descriptor
    {
        RHI_Descriptor() = default;

        RHI_Descriptor(const RHI_Descriptor& descriptor)
        {
            type                        = descriptor.type;
            slot                        = descriptor.slot;
            stage                       = descriptor.stage;
            is_storage                  = descriptor.is_storage;
            is_dynamic_constant_buffer  = descriptor.is_dynamic_constant_buffer;
        }

        RHI_Descriptor(const RHI_Descriptor_Type type, const uint32_t slot, const uint32_t stage, const bool is_storage, const bool is_dynamic_constant_buffer)
        {
            this->type                          = type;
            this->slot                          = slot;
            this->stage                         = stage;
            this->is_storage                    = is_storage;
            this->is_dynamic_constant_buffer    = is_dynamic_constant_buffer;
        }

        std::size_t GetHash() const
        {
            std::size_t hash = 0;

            Utility::Hash::hash_combine(hash, slot);
            Utility::Hash::hash_combine(hash, stage);
            Utility::Hash::hash_combine(hash, offset);
            Utility::Hash::hash_combine(hash, range);
            Utility::Hash::hash_combine(hash, is_storage);
            Utility::Hash::hash_combine(hash, is_dynamic_constant_buffer);
            Utility::Hash::hash_combine(hash, static_cast<uint32_t>(type));
            Utility::Hash::hash_combine(hash, static_cast<uint32_t>(layout));
           
            return hash;
        }

        uint32_t slot                   = 0;
        uint32_t stage                  = 0;
        uint64_t offset                 = 0;
        uint64_t range                  = 0;
        RHI_Descriptor_Type type        = RHI_Descriptor_Undefined;
        RHI_Image_Layout layout         = RHI_Image_Layout::Undefined;
        bool is_storage                 = false;
        bool is_dynamic_constant_buffer = false;
        void* resource                  = nullptr;
    };
}
