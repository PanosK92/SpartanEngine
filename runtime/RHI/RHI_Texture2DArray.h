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

//= INCLUDES ============
#include "RHI_Texture.h"
#include "RHI_Viewport.h"
//=======================

namespace Spartan
{
    class SP_CLASS RHI_Texture2DArray : public RHI_Texture
    {
    public:
        RHI_Texture2DArray(const uint32_t flags = RHI_Texture_Srv, const char* name = nullptr)
        {
            m_resource_type = ResourceType::Texture2dArray;
            m_flags         = flags;

            if (name != nullptr)
            {
                m_object_name = name;
            }
        }

        // Creates a texture without any data (intended for usage as a render target)
        RHI_Texture2DArray(const uint32_t width, const uint32_t height, const RHI_Format format, const uint32_t array_length, const uint32_t flags, std::string name = "")
        {
            m_object_name          = name;
            m_resource_type = ResourceType::Texture2dArray;
            m_width         = width;
            m_height        = height;
            m_viewport      = RHI_Viewport(0, 0, static_cast<float>(width), static_cast<float>(height));
            m_format        = format;
            m_array_length  = array_length;
            m_flags         = flags;

            RHI_CreateResource();
            m_is_ready_for_use = true;
        }

        ~RHI_Texture2DArray() = default;
    };
}
