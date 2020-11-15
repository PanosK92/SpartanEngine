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
        RHI_Texture_Sampled                    = 1 << 0,
        RHI_Texture_Storage                 = 1 << 1,
        RHI_Texture_RenderTarget            = 1 << 2,
        RHI_Texture_DepthStencil            = 1 << 3,
        RHI_Texture_DepthStencilReadOnly    = 1 << 4,
        RHI_Texture_Grayscale               = 1 << 5,
        RHI_Texture_Transparent             = 1 << 6,
        RHI_Texture_GenerateMipsWhenLoading = 1 << 7
    };

    enum RHI_Shader_View_Type : uint8_t
    {
        RHI_Shader_View_ColorDepth,
        RHI_Shader_View_Stencil,
        RHI_Shader_View_Unordered_Access
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

        auto GetWidth() const                                           { return m_width; }
        void SetWidth(const uint32_t width)                             { m_width = width; }

        auto GetHeight() const                                          { return m_height; }
        void SetHeight(const uint32_t height)                           { m_height = height; }

        auto GetGrayscale() const                                       { return m_flags & RHI_Texture_Grayscale; }
        void SetGrayscale(const bool is_grayscale)                      { is_grayscale ? m_flags |= RHI_Texture_Grayscale : m_flags &= ~RHI_Texture_Grayscale; }

        auto GetTransparency() const                                    { return m_flags & RHI_Texture_Transparent; }
        void SetTransparency(const bool is_transparent)                 { is_transparent ? m_flags |= RHI_Texture_Transparent : m_flags &= ~RHI_Texture_Transparent; }

        uint32_t GetBitsPerChannel() const                              { return m_bits_per_channel; }
        void SetBitsPerChannel(const uint32_t bits)                     { m_bits_per_channel = bits; }
        uint32_t GetBytesPerChannel() const                             { return m_bits_per_channel / 8; }
        uint32_t GetBytesPerPixel() const                               { return (m_bits_per_channel / 8) * m_channel_count; }

        uint32_t GetChannelCount() const                                { return m_channel_count; }
        void SetChannelCount(const uint32_t channel_count)              { m_channel_count = channel_count; }

        auto GetFormat() const                                          { return m_format; }
        void SetFormat(const RHI_Format format)                         { m_format = format; }

        // Data
        bool HasData() const                                            { return !m_data.empty(); }
        void SetData(const std::vector<std::vector<std::byte>>& data)   { m_data = data; }
        bool HasMipmaps() const                                         { return m_mip_count > 1;  }
        uint8_t GetMipCount() const                                     { return m_mip_count; }
        std::vector<std::byte>& AddMip()                             { return m_data.emplace_back(std::vector<std::byte>()); }
        std::vector<std::vector<std::byte>>& GetMips()                  { return m_data; }
        std::vector<std::byte>& GetMip(const uint8_t mip_index);
        std::vector<std::byte> GetOrLoadMip(const uint8_t mip_index);

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
        void SetLayout(const RHI_Image_Layout layout, RHI_CommandList* command_list = nullptr);
        RHI_Image_Layout GetLayout() const { return m_layout; }

        // Misc
        auto GetArraySize()         const { return m_array_size; }
        const auto& GetViewport()   const { return m_viewport; }
        uint16_t GetFlags()         const { return m_flags; }

        // GPU resources
        void* Get_Resource()                                                const { return m_resource; }
        void  Set_Resource(void* resource)                                        { m_resource = resource; }
        void* Get_Resource_View(const uint32_t i = 0)                       const { return m_resource_view[i]; }
        void* Get_Resource_View_UnorderedAccess()                           const { return m_resource_view_unorderedAccess; }
        void* Get_Resource_View_DepthStencil(const uint32_t i = 0)          const { return i < m_resource_view_depthStencil.size() ? m_resource_view_depthStencil[i] : nullptr; }
        void* Get_Resource_View_DepthStencilReadOnly(const uint32_t i = 0)  const { return i < m_resource_view_depthStencilReadOnly.size() ? m_resource_view_depthStencilReadOnly[i] : nullptr; }
        void* Get_Resource_View_RenderTarget(const uint32_t i = 0)          const { return i < m_resource_view_renderTarget.size() ? m_resource_view_renderTarget[i] : nullptr; }

    protected:
        bool LoadFromFile_NativeFormat(const std::string& file_path);
        bool LoadFromFile_ForeignFormat(const std::string& file_path, bool generate_mipmaps);
        static uint32_t GetChannelCountFromFormat(RHI_Format format);
        virtual bool CreateResourceGpu() { LOG_ERROR("Function not implemented by API"); return false; }

        uint32_t m_bits_per_channel = 8;
        uint32_t m_width            = 0;
        uint32_t m_height           = 0;
        uint32_t m_channel_count    = 4;
        uint32_t m_array_size       = 1;
        uint8_t m_mip_count         = 1;
        RHI_Format m_format         = RHI_Format_Undefined;
        RHI_Image_Layout m_layout   = RHI_Image_Layout::Undefined;
        uint16_t m_flags            = 0;
        RHI_Viewport m_viewport;
        std::vector<std::vector<std::byte>> m_data;
        std::shared_ptr<RHI_Device> m_rhi_device;

        // API
        void* m_resource_view[2]                = { nullptr, nullptr }; // color/depth, stencil
        void* m_resource_view_unorderedAccess   = nullptr;
        void* m_resource                        = nullptr;
        std::array<void*, rhi_max_render_target_count> m_resource_view_renderTarget           = { nullptr };
        std::array<void*, rhi_max_render_target_count> m_resource_view_depthStencil           = { nullptr };
        std::array<void*, rhi_max_render_target_count> m_resource_view_depthStencilReadOnly   = { nullptr };
    private:
        uint32_t GetByteCount();
    };
}
