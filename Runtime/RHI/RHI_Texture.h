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
#include <memory>
#include <array>
#include "RHI_Viewport.h"
#include "RHI_Definition.h"
#include "../Resource/IResource.h"
//================================

namespace Spartan
{
    enum RHI_Texture_Flags : uint16_t
    {
        RHI_Texture_Sampled                 = 1 << 0,
        RHI_Texture_Storage                 = 1 << 1,
        RHI_Texture_RenderTarget            = 1 << 2,
        RHI_Texture_DepthStencil            = 1 << 3,
        RHI_Texture_DepthStencilReadOnly    = 1 << 4,
        RHI_Texture_PerMipView              = 1 << 5,
        RHI_Texture_Grayscale               = 1 << 6,
        RHI_Texture_Transparent             = 1 << 7,
        RHI_Texture_GenerateMipsWhenLoading = 1 << 8
    };

    enum RHI_Shader_View_Type : uint8_t
    {
        RHI_Shader_View_ColorDepth,
        RHI_Shader_View_Stencil,
        RHI_Shader_View_Unordered_Access
    };

    struct RHI_Texture_Mip
    {
        std::vector<std::byte> bytes;
    };

    struct RHI_Texture_Slice
    {
        std::vector<RHI_Texture_Mip> mips;
        uint32_t GetMipCount() { return static_cast<uint32_t>(mips.size()); }
    };

    class SPARTAN_CLASS RHI_Texture : public IResource
    {
    public:
        RHI_Texture(Context* context);
        ~RHI_Texture();

        //= IResource ===========================================
        bool SaveToFile(const std::string& file_path) override;
        bool LoadFromFile(const std::string& file_path) override;
        //=======================================================

        uint32_t GetWidth()                                 const { return m_width; }
        void SetWidth(const uint32_t width)                       { m_width = width; }
                                                                  
        uint32_t GetHeight()                                const { return m_height; }
        void SetHeight(const uint32_t height)                     { m_height = height; }
                                                                  
        bool GetGrayscale()                                 const { return m_flags & RHI_Texture_Grayscale; }
        void SetGrayscale(const bool is_grayscale)                { is_grayscale ? m_flags |= RHI_Texture_Grayscale : m_flags &= ~RHI_Texture_Grayscale; }
                                                                  
        bool GetTransparency()                              const { return m_flags & RHI_Texture_Transparent; }
        void SetTransparency(const bool is_transparent)           { is_transparent ? m_flags |= RHI_Texture_Transparent : m_flags &= ~RHI_Texture_Transparent; }
                                                                  
        uint32_t GetBitsPerChannel()                        const { return m_bits_per_channel; }
        void SetBitsPerChannel(const uint32_t bits)               { m_bits_per_channel = bits; }
        uint32_t GetBytesPerChannel()                       const { return m_bits_per_channel / 8; }
        uint32_t GetBytesPerPixel()                         const { return (m_bits_per_channel / 8) * m_channel_count; }
                                                                  
        uint32_t GetChannelCount()                          const { return m_channel_count; }
        void SetChannelCount(const uint32_t channel_count)        { m_channel_count = channel_count; }
                                                                  
        RHI_Format GetFormat()                              const { return m_format; }
        void SetFormat(const RHI_Format format)                   { m_format = format; }

        // Data
        uint32_t GetArrayLength()                           const { return m_array_length; }
        uint32_t GetMipCount()                              const { return m_mip_count; }
        bool HasData()                                      const { return !m_data.empty() && !m_data[0].mips.empty() && !m_data[0].mips[0].bytes.empty(); };
        std::vector<RHI_Texture_Slice>& GetData()                 { return m_data; }
        RHI_Texture_Mip& CreateMip(const uint32_t array_index);
        RHI_Texture_Mip& GetMip(const uint32_t array_index, const uint32_t mip_index);
        RHI_Texture_Slice& GetSlice(const uint32_t array_index);

        // Binding type
        bool IsSampled()        const { return m_flags & RHI_Texture_Sampled; }
        bool IsStorage()        const { return m_flags & RHI_Texture_Storage; }
        bool IsDepthStencil()   const { return m_flags & RHI_Texture_DepthStencil; }
        bool IsRenderTarget()   const { return m_flags & RHI_Texture_RenderTarget; }

        // Format type
        bool IsDepthFormat()            const { return m_format == RHI_Format_D32_Float || m_format == RHI_Format_D32_Float_S8X24_Uint; }
        bool IsStencilFormat()          const { return m_format == RHI_Format_D32_Float_S8X24_Uint; }
        bool IsDepthStencilFormat()     const { return IsDepthFormat() || IsStencilFormat(); }
        bool IsColorFormat()            const { return !IsDepthStencilFormat(); }

        // Layout
        void SetLayout(const RHI_Image_Layout layout, RHI_CommandList* cmd_list, const int mip = -1, const bool ranged = true);
        RHI_Image_Layout GetLayout(const uint32_t mip) const { return m_layout[mip]; }

        // Misc
        const auto& GetViewport()   const { return m_viewport; }
        uint16_t GetFlags()         const { return m_flags; }
        bool HasPerMipView()        const { return m_flags & RHI_Texture_PerMipView; }

        // GPU resources
        void*& Get_Resource()                                                     { return m_resource; }
        void* Get_Resource_View_Srv()                                       const { return m_resource_view_srv; }
        void* Get_Resource_View_Uav()                                       const { return m_resource_view_uav; }
        void* Get_Resource_Views_Srv(const uint32_t i)                      const { return m_resource_views_srv[i]; }
        void* Get_Resource_Views_Uav(const uint32_t i)                      const { return m_resource_views_uav[i]; }
        void* Get_Resource_View_DepthStencil(const uint32_t i = 0)          const { return i < m_resource_view_depthStencil.size() ? m_resource_view_depthStencil[i] : nullptr; }
        void* Get_Resource_View_DepthStencilReadOnly(const uint32_t i = 0)  const { return i < m_resource_view_depthStencilReadOnly.size() ? m_resource_view_depthStencilReadOnly[i] : nullptr; }
        void* Get_Resource_View_RenderTarget(const uint32_t i = 0)          const { return i < m_resource_view_renderTarget.size() ? m_resource_view_renderTarget[i] : nullptr; }

    protected:
        bool LoadFromFile_NativeFormat(const std::string& file_path);
        bool LoadFromFile_ForeignFormat(const std::string& file_path);
        static uint32_t GetChannelCountFromFormat(RHI_Format format);
        bool CreateResourceGpu();
        void DestroyResourceGpu();

        uint32_t m_bits_per_channel = 8;
        uint32_t m_width            = 0;
        uint32_t m_height           = 0;
        uint32_t m_channel_count    = 4;
        uint32_t m_array_length     = 0;
        uint32_t m_mip_count        = 0;
        RHI_Format m_format         = RHI_Format_Undefined;
        uint16_t m_flags            = 0;
        std::array<RHI_Image_Layout, 12> m_layout;
        RHI_Viewport m_viewport;
        std::vector<RHI_Texture_Slice> m_data;
        std::shared_ptr<RHI_Device> m_rhi_device;

        // API
        void* m_resource                = nullptr;
        void* m_resource_view_srv       = nullptr;
        void* m_resource_view_uav       = nullptr;
        void* m_resource_views_srv[12]  = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
        void* m_resource_views_uav[12]  = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
        std::array<void*, rhi_max_render_target_count> m_resource_view_renderTarget           = { nullptr };
        std::array<void*, rhi_max_render_target_count> m_resource_view_depthStencil           = { nullptr };
        std::array<void*, rhi_max_render_target_count> m_resource_view_depthStencilReadOnly   = { nullptr };

    private:
        uint32_t GetByteCount();
    };
}
