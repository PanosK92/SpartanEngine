/*
Copyright(c) 2016-2022 Panos Karabelas

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
    enum RHI_Texture_Flags : uint32_t
    {
        // When editing this, make sure that the bit shifts
        // in common_buffer.hlsl are also updated.

        RHI_Texture_Srv                     = 1U << 0,
        RHI_Texture_Uav                     = 1U << 1,
        RHI_Texture_Rt_Color                = 1U << 2,
        RHI_Texture_Rt_DepthStencil         = 1U << 3,
        RHI_Texture_Rt_DepthStencilReadOnly = 1U << 4,
        RHI_Texture_PerMipViews             = 1U << 5,
        RHI_Texture_Greyscale               = 1U << 6,
        RHI_Texture_Transparent             = 1U << 7,
        RHI_Texture_Srgb                    = 1U << 8,
        RHI_Texture_CanBeCleared            = 1U << 9,
        RHI_Texture_Mips                    = 1U << 10,
        RHI_Texture_Compressed              = 1U << 11,
        RHI_Texture_Visualise               = 1U << 12,
        RHI_Texture_Visualise_Pack          = 1U << 13,
        RHI_Texture_Visualise_GammaCorrect  = 1U << 14,
        RHI_Texture_Visualise_Boost         = 1U << 15,
        RHI_Texture_Visualise_Abs           = 1U << 16,
        RHI_Texture_Visualise_Channel_R     = 1U << 17,
        RHI_Texture_Visualise_Channel_G     = 1U << 18,
        RHI_Texture_Visualise_Channel_B     = 1U << 19,
        RHI_Texture_Visualise_Channel_A     = 1U << 20,
        RHI_Texture_Visualise_Sample_Point  = 1U << 21
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

    class SPARTAN_CLASS RHI_Texture : public IResource, public std::enable_shared_from_this<RHI_Texture>
    {
    public:
        RHI_Texture(Context* context);
        ~RHI_Texture();

        //= IResource ===========================================
        bool SaveToFile(const std::string& file_path) override;
        bool LoadFromFile(const std::string& file_path) override;
        //=======================================================

        uint32_t GetWidth()                                const { return m_width; }
        void SetWidth(const uint32_t width)                      { m_width = width; }

        uint32_t GetHeight()                               const { return m_height; }
        void SetHeight(const uint32_t height)                    { m_height = height; }

        uint32_t GetBitsPerChannel()                       const { return m_bits_per_channel; }
        void SetBitsPerChannel(const uint32_t bits)              { m_bits_per_channel = bits; }
        uint32_t GetBytesPerChannel()                      const { return m_bits_per_channel / 8; }
        uint32_t GetBytesPerPixel()                        const { return (m_bits_per_channel / 8) * m_channel_count; }
                                                                 
        uint32_t GetChannelCount()                         const { return m_channel_count; }
        void SetChannelCount(const uint32_t channel_count)       { m_channel_count = channel_count; }
                                                                 
        RHI_Format GetFormat()                             const { return m_format; }
        void SetFormat(const RHI_Format format)                  { m_format = format; }

        // Data
        uint32_t GetArrayLength()                          const { return m_array_length; }
        uint32_t GetMipCount()                             const { return m_mip_count; }
        bool HasData()                                     const { return !m_data.empty() && !m_data[0].mips.empty() && !m_data[0].mips[0].bytes.empty(); };
        std::vector<RHI_Texture_Slice>& GetData()                { return m_data; }
        RHI_Texture_Mip& CreateMip(const uint32_t array_index);
        RHI_Texture_Mip& GetMip(const uint32_t array_index, const uint32_t mip_index);
        RHI_Texture_Slice& GetSlice(const uint32_t array_index);

        // Flags
        void SetFlag(const uint32_t flag, bool enabled = true);
        uint32_t GetFlags()                 const { return m_flags; }
        void SetFlags(const uint32_t flags)       { m_flags = flags; }
        bool IsSrv()                        const { return m_flags & RHI_Texture_Srv; }
        bool IsUav()                        const { return m_flags & RHI_Texture_Uav; }
        bool IsRenderTargetDepthStencil()   const { return m_flags & RHI_Texture_Rt_DepthStencil; }
        bool IsRenderTargetColor()          const { return m_flags & RHI_Texture_Rt_Color; }
        bool HasPerMipViews()               const { return m_flags & RHI_Texture_PerMipViews; }
        bool HasMips()                      const { return m_flags & RHI_Texture_Mips; }
        bool CanBeCleared()                 const { return m_flags & RHI_Texture_CanBeCleared || IsRenderTargetDepthStencil() || IsRenderTargetColor(); }
        bool IsGrayscale()                  const { return m_flags & RHI_Texture_Greyscale; }
        bool IsTransparent()                const { return m_flags & RHI_Texture_Transparent; }

        // Format type
        bool IsDepthFormat()        const { return m_format == RHI_Format_D16_Unorm || m_format == RHI_Format_D32_Float || m_format == RHI_Format_D32_Float_S8X24_Uint; }
        bool IsStencilFormat()      const { return m_format == RHI_Format_D32_Float_S8X24_Uint; }
        bool IsDepthStencilFormat() const { return IsDepthFormat() || IsStencilFormat(); }
        bool IsColorFormat()        const { return !IsDepthStencilFormat(); }

        // Layout
        void SetLayout(const RHI_Image_Layout layout, RHI_CommandList* cmd_list, const int mip = -1, const bool ranged = true);
        RHI_Image_Layout GetLayout(const uint32_t mip) const { return m_layout[mip]; }
        std::array<RHI_Image_Layout, 12> GetLayouts()  const { return m_layout; }
        bool DoAllMipsHaveTheSameLayout() const;

        // Viewport
        const auto& GetViewport() const { return m_viewport; }

        // GPU resources
        void*& GetResource()                                                    { return m_resource; }
        void* GetResource_View_Srv()                                      const { return m_resource_view_srv; }
        void* GetResource_View_Uav()                                      const { return m_resource_view_uav; }
        void* GetResource_Views_Srv(const uint32_t i)                     const { return m_resource_views_srv[i]; }
        void* GetResource_Views_Uav(const uint32_t i)                     const { return m_resource_views_uav[i]; }
        void* GetResource_View_DepthStencil(const uint32_t i = 0)         const { return i < m_resource_view_depthStencil.size()         ? m_resource_view_depthStencil[i]         : nullptr; }
        void* GetResource_View_DepthStencilReadOnly(const uint32_t i = 0) const { return i < m_resource_view_depthStencilReadOnly.size() ? m_resource_view_depthStencilReadOnly[i] : nullptr; }
        void* GetResource_View_RenderTarget(const uint32_t i = 0)         const { return i < m_resource_view_renderTarget.size()         ? m_resource_view_renderTarget[i]         : nullptr; }
        void RHI_DestroyResource(const bool destroy_main, const bool destroy_per_view);

    protected:
        bool Compress(const RHI_Format format);
        bool RHI_CreateResource();
        void RHI_SetLayout(const RHI_Image_Layout new_layout, RHI_CommandList* cmd_list, const int mip_start, const int mip_range);

        uint32_t m_bits_per_channel = 0;
        uint32_t m_width            = 0;
        uint32_t m_height           = 0;
        uint32_t m_channel_count    = 0;
        uint32_t m_array_length     = 1;
        uint32_t m_mip_count        = 1;
        RHI_Format m_format         = RHI_Format_Undefined;
        uint32_t m_flags            = 0;
        std::array<RHI_Image_Layout, 12> m_layout;
        RHI_Viewport m_viewport;
        std::vector<RHI_Texture_Slice> m_data;
        std::shared_ptr<RHI_Device> m_rhi_device;

        // API
        void* m_resource               = nullptr;
        void* m_resource_view_srv      = nullptr;
        void* m_resource_view_uav      = nullptr;
        void* m_resource_views_srv[12] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
        void* m_resource_views_uav[12] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
        std::array<void*, rhi_max_render_target_count> m_resource_view_renderTarget         = { nullptr };
        std::array<void*, rhi_max_render_target_count> m_resource_view_depthStencil         = { nullptr };
        std::array<void*, rhi_max_render_target_count> m_resource_view_depthStencilReadOnly = { nullptr };

    private:
        void ComputeMemoryUsage();
    };
}
