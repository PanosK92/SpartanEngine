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

//= INCLUDES ===========
#include "RHI_Texture.h"
//======================

namespace Spartan
{
    class SPARTAN_CLASS RHI_TextureCube : public RHI_Texture
    {
    public:
        RHI_TextureCube(Context* context) : RHI_Texture(context) { m_resource_type = ResourceType::TextureCube; }

        // Creates a cubemap with initial data, 6 textures containing (possibly) mip-levels.
        RHI_TextureCube(Context* context, const uint32_t width, const uint32_t height, const RHI_Format format, const std::vector<std::vector<std::vector<std::byte>>>& data) : RHI_Texture(context)
        {
            m_resource_type = ResourceType::TextureCube;
            m_width            = width;
            m_height        = height;
            m_viewport        = RHI_Viewport(0, 0, static_cast<float>(width), static_cast<float>(height));
            m_channel_count = GetChannelCountFromFormat(format);
            m_format        = format;
            m_data_cube        = data;
            m_array_size    = 6;
            m_flags    = RHI_Texture_Sampled;
            m_mip_count    = static_cast<uint32_t>(m_data.front().size());

            RHI_TextureCube::CreateResourceGpu();
        }

        // Creates a cubemap without any initial data, to be used as a render target
        RHI_TextureCube(Context* context, const uint32_t width, const uint32_t height, const RHI_Format format) : RHI_Texture(context)
        {
            m_resource_type = ResourceType::TextureCube;
            m_width            = width;
            m_height        = height;
            m_channel_count = GetChannelCountFromFormat(format);
            m_viewport        = RHI_Viewport(0, 0, static_cast<float>(width), static_cast<float>(height));
            m_format        = format;
            m_array_size    = 6;
            m_flags    = RHI_Texture_Sampled;
            m_flags    |= IsDepthFormat() ? RHI_Texture_DepthStencil : RHI_Texture_RenderTarget;
            m_mip_count    = 1;

            RHI_TextureCube::CreateResourceGpu();
        }

        ~RHI_TextureCube();

        // RHI_Texture
        bool CreateResourceGpu() override;

    private:
        std::vector<std::vector<std::vector<std::byte>>> m_data_cube;
    };
}
