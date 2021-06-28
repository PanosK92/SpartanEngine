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

//= INCLUDES ============
#include "RHI_Texture.h"
#include "RHI_Viewport.h"
//=======================

namespace Spartan
{
    class SPARTAN_CLASS RHI_Texture2DArray : public RHI_Texture
    {
    public:
        RHI_Texture2DArray(Context* context, const bool generate_mipmaps = true) : RHI_Texture(context)
        {
            m_resource_type = ResourceType::Texture2dArray;
            m_flags         = RHI_Texture_Sampled;
            m_flags         |= generate_mipmaps ? RHI_Texture_GenerateMipsWhenLoading : 0;
        }

        // Creates a texture from data
        RHI_Texture2DArray(Context* context, const uint32_t width, const uint32_t height, const RHI_Format format, const std::vector<RHI_Texture_Slice>& data) : RHI_Texture(context)
        {
            m_resource_type = ResourceType::Texture2dArray;
            m_width         = width;
            m_height        = height;
            m_viewport      = RHI_Viewport(0, 0, static_cast<float>(width), static_cast<float>(height));
            m_channel_count = GetChannelCountFromFormat(format);
            m_format        = format;
            m_data          = data;
            m_mip_count     = GetSlice(0).GetMipCount();
            m_flags         = RHI_Texture_Sampled;

            RHI_Texture2DArray::CreateResourceGpu();
        }

        ~RHI_Texture2DArray() {}

        // RHI_Texture
        bool CreateResourceGpu() override { return false; }
    };
}
