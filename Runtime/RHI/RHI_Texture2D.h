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

//= INCLUDES ============
#include "RHI_Texture.h"
#include "RHI_Viewport.h"
//=======================

namespace Spartan
{
    class SPARTAN_CLASS RHI_Texture2D : public RHI_Texture
    {
    public:
        // Creates a texture from data
        RHI_Texture2D(Context* context, const uint32_t width, const uint32_t height, const RHI_Format format, const std::vector<std::vector<std::byte>>& data) : RHI_Texture(context)
        {
            m_resource_type = ResourceType::Texture2d;
            m_width         = width;
            m_height        = height;
            m_viewport      = RHI_Viewport(0, 0, static_cast<float>(width), static_cast<float>(height));
            m_channel_count = GetChannelCountFromFormat(format);
            m_format        = format;
            m_data          = data;
            m_flags         = RHI_Texture_Sampled;
            m_mip_count     = static_cast<uint32_t>(data.size());

            RHI_Texture2D::CreateResourceGpu();
        }

        // Creates a texture from data
        RHI_Texture2D(Context* context, const uint32_t width, const uint32_t height, const RHI_Format format, const std::vector<std::byte>& data) : RHI_Texture(context)
        {
            m_data.emplace_back(data);

            m_resource_type = ResourceType::Texture2d;
            m_width         = width;
            m_height        = height;
            m_viewport      = RHI_Viewport(0, 0, static_cast<float>(width), static_cast<float>(height));
            m_channel_count = GetChannelCountFromFormat(format);
            m_format        = format;
            m_flags         = RHI_Texture_Sampled;
            m_mip_count     = 1;

            RHI_Texture2D::CreateResourceGpu();
        }

        // Creates an empty texture (intended for deferred loading)
        RHI_Texture2D(Context* context, const bool generate_mipmaps = true) : RHI_Texture(context)
        {
            m_resource_type = ResourceType::Texture2d;
            m_flags         = RHI_Texture_Sampled;
            m_flags         |= generate_mipmaps ? RHI_Texture_GenerateMipsWhenLoading : 0;
        }

        // Creates a texture without any data (intended for usage as a render target)
        RHI_Texture2D(Context* context, const uint32_t width, const uint32_t height, const RHI_Format format, const uint32_t array_size = 1, const uint16_t flags = 0, std::string name = "") : RHI_Texture(context)
        {
            m_name          = name;
            m_resource_type = ResourceType::Texture2d;
            m_width         = width;
            m_height        = height;
            m_channel_count = GetChannelCountFromFormat(format);
            m_viewport      = RHI_Viewport(0, 0, static_cast<float>(width), static_cast<float>(height));
            m_format        = format;
            m_array_size    = array_size;
            m_flags         = flags;
            m_flags         |= RHI_Texture_Sampled;
            m_flags         |= IsDepthFormat() ? RHI_Texture_DepthStencil : (RHI_Texture_RenderTarget | RHI_Texture_Storage); // Need to optimize that, not every rt is used in a compute shader
            m_mip_count     = 1;

            RHI_Texture2D::CreateResourceGpu();
        }

        ~RHI_Texture2D();

        // RHI_Texture
        bool CreateResourceGpu() override;
    };
}
