/*
Copyright(c) 2015-2025 Panos Karabelas

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
#include <array>
#include "RHI_Viewport.h"
#include "RHI_Definitions.h"
#include "../Resource/IResource.h"
//================================

namespace spartan
{
    enum class RHI_Texture_Type
    {
        Type2D,
        Type2DArray,
        Type3D,
        TypeCube,
        Max
    };

    enum RHI_Texture_Flags : uint32_t
    {
        RHI_Texture_Srv               = 1U << 0,
        RHI_Texture_Uav               = 1U << 1,
        RHI_Texture_Rtv               = 1U << 2,
        RHI_Texture_Vrs               = 1U << 3,
        RHI_Texture_ClearBlit         = 1U << 4,
        RHI_Texture_PerMipViews       = 1U << 5,
        RHI_Texture_Greyscale         = 1U << 6,
        RHI_Texture_Transparent       = 1U << 7,
        RHI_Texture_Srgb              = 1U << 8,
        RHI_Texture_Mappable          = 1U << 9,
        RHI_Texture_Compress          = 1U << 10,
        RHI_Texture_DontPrepareForGpu = 1U << 11,
        RHI_Texture_Thumbnail         = 1U << 12
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

    class RHI_Texture : public IResource
    {
    public:
        RHI_Texture(
            const RHI_Texture_Type type,
            const uint32_t width,
            const uint32_t height,
            const uint32_t depth,
            const uint32_t mip_count,
            const RHI_Format format,
            const uint32_t flags,
            const char* name                    = nullptr,
            std::vector<RHI_Texture_Slice> data = {}
        );
        RHI_Texture(const std::string& file_path);
        RHI_Texture();
        ~RHI_Texture();

        // iresource
        void SaveToFile(const std::string& file_path) override;
        void LoadFromFile(const std::string& file_path) override;

        uint32_t GetWidth() const           { return m_width; }
        void SetWidth(const uint32_t width) { m_width = width; }

        uint32_t GetHeight() const            { return m_height; }
        void SetHeight(const uint32_t height) { m_height = height; }

        uint32_t GetBitsPerChannel() const          { return m_bits_per_channel; }
        void SetBitsPerChannel(const uint32_t bits) { m_bits_per_channel = bits; }
        uint32_t GetBytesPerChannel() const         { return m_bits_per_channel / 8; }
        uint32_t GetBytesPerPixel() const           { return (m_bits_per_channel / 8) * m_channel_count; }

        uint32_t GetChannelCount()  const                  { return m_channel_count; }
        void SetChannelCount(const uint32_t channel_count) { m_channel_count = channel_count; }

        RHI_Format GetFormat() const            { return m_format; }
        void SetFormat(const RHI_Format format) { m_format = format; }
        static bool IsCompressedFormat(const RHI_Format format);
        bool IsCompressedFormat()               { return IsCompressedFormat(m_format); }

        // external memory
        void* GetExternalMemoryHandle() const      { return m_rhi_external_memory; }
        void SetExternalMemoryHandle(void* handle) { m_rhi_external_memory = handle; }

        // misc
        void ClearData();
        void PrepareForGpu();
        static size_t CalculateMipSize(uint32_t width, uint32_t height, uint32_t depth, RHI_Format format, uint32_t bits_per_channel, uint32_t channel_count);

        // data
        uint32_t GetMipCount() const    { return m_mip_count; }
        uint32_t GetDepth() const       { return m_depth; }
        uint32_t GetArrayLength() const { return (m_type == RHI_Texture_Type::Type3D) ? 1 : m_depth; }
        bool HasData() const            { return !m_slices.empty() && !m_slices[0].mips.empty() && !m_slices[0].mips[0].bytes.empty(); };
        RHI_Texture_Mip& GetMip(const uint32_t array_index, const uint32_t mip_index);
        RHI_Texture_Slice& GetSlice(const uint32_t array_index);
        void AllocateMip();

        // flags
        bool IsSrv() const             { return m_flags & RHI_Texture_Srv; }
        bool IsUav() const             { return m_flags & RHI_Texture_Uav; }
        bool IsVrs() const             { return m_flags & RHI_Texture_Vrs; }
        bool IsRt() const              { return m_flags & RHI_Texture_Rtv; }
        bool IsDsv() const             { return IsRt() && IsDepthStencilFormat(); }
        bool IsRtv() const             { return IsRt() && IsColorFormat(); }
        bool HasPerMipViews() const    { return m_flags & RHI_Texture_PerMipViews; }
        bool IsGrayscale() const       { return m_flags & RHI_Texture_Greyscale; }
        bool IsSemiTransparent() const { return m_flags & RHI_Texture_Transparent; }

        // format type
        bool IsDepthFormat() const        { return m_format == RHI_Format::D16_Unorm || m_format == RHI_Format::D32_Float || m_format == RHI_Format::D32_Float_S8X24_Uint; }
        bool IsStencilFormat() const      { return m_format == RHI_Format::D32_Float_S8X24_Uint; }
        bool IsDepthStencilFormat() const { return IsDepthFormat() || IsStencilFormat(); }
        bool IsColorFormat() const        { return !IsDepthStencilFormat(); }

        // usage
        bool IsMaterialTexture() const
        {
            return m_format           == RHI_Format::R8G8B8A8_Unorm &&
                   m_channel_count    == 4                          &&
                   m_bits_per_channel == 8                          &&
                   !(IsRtv() || IsDsv());
        }

        // layout
        void SetLayout(const RHI_Image_Layout layout, RHI_CommandList* cmd_list, uint32_t mip_index = rhi_all_mips,  uint32_t mip_range = 0);
        RHI_Image_Layout GetLayout(const uint32_t mip) const;
        std::array<RHI_Image_Layout, rhi_max_mip_count> GetLayouts();

        // viewport
        const auto& GetViewport() const { return m_viewport; }

        // rhi
        RHI_Texture_Type GetType() const            { return m_type; }
        void*& GetRhiResource()                     { return m_rhi_resource; }
        void* GetRhiSrv() const                     { return m_rhi_srv; }
        void* GetRhiSrvMip(const uint32_t i) const  { return m_rhi_srv_mips[i]; }
        void* GetRhiDsv(const uint32_t i = 0) const { return m_rhi_dsv[i]; }
        void* GetRhiRtv(const uint32_t i = 0) const { return m_rhi_rtv[i]; }
        void RHI_DestroyResource();
        void*& GetMappedData() { return m_mapped_data; }

    protected:
        bool RHI_CreateResource();

        uint32_t m_width            = 0;
        uint32_t m_height           = 0;
        uint32_t m_depth            = 0; // array length and depth are both m_depth (for simplicity), in case of 3D textures we only have one layer though
        uint32_t m_mip_count        = 0;
        uint32_t m_bits_per_channel = 0;
        uint32_t m_channel_count    = 0;
        RHI_Format m_format         = RHI_Format::Max;
        RHI_Texture_Type m_type     = RHI_Texture_Type::Max;
        RHI_Viewport m_viewport;
        std::vector<RHI_Texture_Slice> m_slices;

        // api resources
        void* m_rhi_srv                                          = nullptr;     // an srv with all mips
        std::array<void*, rhi_max_mip_count> m_rhi_srv_mips      = { nullptr }; // an srv for each mip
        std::array<void*, rhi_max_render_target_count> m_rhi_rtv = { nullptr };
        std::array<void*, rhi_max_render_target_count> m_rhi_dsv = { nullptr };
        void* m_rhi_resource                                     = nullptr;
        void* m_rhi_external_memory                              = nullptr;
        void* m_mapped_data                                      = nullptr;

    private:
        void ComputeMemoryUsage();
    };
}
