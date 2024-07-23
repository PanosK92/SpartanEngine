/*
Copyright(c) 2016-2024 Panos Karabelas

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
    class SP_CLASS RHI_Texture3D : public RHI_Texture
    {
    public:
        RHI_Texture3D(const uint32_t width, const uint32_t height, const uint32_t depth, const RHI_Format format, const uint32_t flags, const char* name = nullptr)
        {
            m_resource_type    = ResourceType::Texture3d;
            m_width            = width;
            m_height           = height;
            m_depth            = depth;
            m_mip_count        = 1;
            m_array_length     = 1;
            m_viewport         = RHI_Viewport(0, 0, static_cast<float>(width), static_cast<float>(height));
            m_format           = format;
            m_flags            = flags;
            m_channel_count    = rhi_to_format_channel_count(format);
            m_bits_per_channel = rhi_format_to_bits_per_channel(m_format);
            m_object_name      = name ? name : m_object_name;

            RHI_Texture::RHI_CreateResource();
            m_is_ready_for_use = true;
        }

        ~RHI_Texture3D() = default;
    };
}
