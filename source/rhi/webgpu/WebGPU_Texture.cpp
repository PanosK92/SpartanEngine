/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==================
#include "pch.h"
#include "../RHI_Texture.h"
#include "../RHI_Device.h"
#include "../RHI_CommandList.h"
#include "WebGPU_Internal.h"
//=============================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        struct View
        {
            uint64_t key         = 0;
            WGPUTextureView view = nullptr;
        };

        unordered_map<void*, vector<View>> views;
        mutex mutex_views;

        bool is_block_compressed(const RHI_Format format)
        {
            return format == RHI_Format::BC1_Unorm || format == RHI_Format::BC3_Unorm || format == RHI_Format::BC5_Unorm || format == RHI_Format::BC7_Unorm || format == RHI_Format::ASTC;
        }

        bool is_renderable(const WGPUTextureFormat format)
        {
            switch (format)
            {
                case WGPUTextureFormat_RG11B10Ufloat:
                    return webgpu::has_feature(WGPUFeatureName_RG11B10UfloatRenderable);
                case WGPUTextureFormat_R16Unorm:
                case WGPUTextureFormat_RGBA16Unorm:
                    return webgpu::has_feature(WGPUFeatureName_Unorm16TextureFormats);
                case WGPUTextureFormat_RGBA16Snorm:
                case WGPUTextureFormat_BC1RGBAUnorm:
                case WGPUTextureFormat_BC3RGBAUnorm:
                case WGPUTextureFormat_BC5RGUnorm:
                case WGPUTextureFormat_BC7RGBAUnorm:
                case WGPUTextureFormat_ASTC4x4Unorm:
                case WGPUTextureFormat_Undefined:
                    return false;
                default:
                    return true;
            }
        }

        WGPUTextureViewDimension natural_dimension(RHI_Texture* texture)
        {
            switch (texture->GetType())
            {
                case RHI_Texture_Type::Type3D:      return WGPUTextureViewDimension_3D;
                case RHI_Texture_Type::Type2DArray: return WGPUTextureViewDimension_2DArray;
                case RHI_Texture_Type::TypeCube:    return texture->GetArrayLength() > 6 ? WGPUTextureViewDimension_CubeArray : WGPUTextureViewDimension_Cube;
                default:                            return WGPUTextureViewDimension_2D;
            }
        }

        // texels of a format webgpu stores wider (see webgpu::texture_format) are expanded to four channels, missing ones read 0 and alpha 1
        bool widen(const WGPUTextureFormat from, const WGPUTextureFormat to, const void* bytes, const uint64_t size, vector<uint8_t>& out)
        {
            if (from == to)
                return false;

            if (from == WGPUTextureFormat_RG16Float && to == WGPUTextureFormat_RGBA16Float)
            {
                const uint16_t* source = static_cast<const uint16_t*>(bytes);
                const uint64_t count   = size / 4;
                out.resize(count * 8);
                uint16_t* destination = reinterpret_cast<uint16_t*>(out.data());
                for (uint64_t i = 0; i < count; i++)
                {
                    destination[i * 4 + 0] = source[i * 2 + 0];
                    destination[i * 4 + 1] = source[i * 2 + 1];
                    destination[i * 4 + 2] = 0;
                    destination[i * 4 + 3] = 0x3c00; // 1.0
                }
                return true;
            }

            if (from == WGPUTextureFormat_RG32Float && to == WGPUTextureFormat_RGBA32Float)
            {
                const float* source  = static_cast<const float*>(bytes);
                const uint64_t count = size / 8;
                out.resize(count * 16);
                float* destination = reinterpret_cast<float*>(out.data());
                for (uint64_t i = 0; i < count; i++)
                {
                    destination[i * 4 + 0] = source[i * 2 + 0];
                    destination[i * 4 + 1] = source[i * 2 + 1];
                    destination[i * 4 + 2] = 0.0f;
                    destination[i * 4 + 3] = 1.0f;
                }
                return true;
            }

            if (from == WGPUTextureFormat_RG11B10Ufloat && to == WGPUTextureFormat_RGBA16Float)
            {
                // the packed floats share half's exponent bias, only the mantissa shifts
                const uint32_t* source = static_cast<const uint32_t*>(bytes);
                const uint64_t count   = size / 4;
                out.resize(count * 8);
                uint16_t* destination = reinterpret_cast<uint16_t*>(out.data());
                for (uint64_t i = 0; i < count; i++)
                {
                    const uint32_t packed  = source[i];
                    const uint32_t r       = packed & 0x7ff;
                    const uint32_t g       = (packed >> 11) & 0x7ff;
                    const uint32_t b       = (packed >> 22) & 0x3ff;
                    destination[i * 4 + 0] = static_cast<uint16_t>(((r >> 6) << 10) | ((r & 0x3f) << 4));
                    destination[i * 4 + 1] = static_cast<uint16_t>(((g >> 6) << 10) | ((g & 0x3f) << 4));
                    destination[i * 4 + 2] = static_cast<uint16_t>(((b >> 5) << 10) | ((b & 0x1f) << 5));
                    destination[i * 4 + 3] = 0x3c00;
                }
                return true;
            }

            return false;
        }

        // copies one tightly packed subresource, the queue orders it before anything submitted later
        void upload(WGPUTexture texture, const RHI_Format format, const void* bytes, uint64_t size, uint32_t width, uint32_t height, uint32_t depth, uint32_t slice, uint32_t mip, uint32_t x, uint32_t y)
        {
            vector<uint8_t> widened;
            if (widen(webgpu::to_format(format), wgpuTextureGetFormat(texture), bytes, size, widened))
            {
                bytes = widened.data();
                size  = widened.size();
            }

            const bool compressed = is_block_compressed(format);
            const uint32_t rows   = compressed ? max(1u, (height + 3) / 4) : height;
            const uint64_t bytes_per_image = size / depth;
            const uint64_t bytes_per_row   = bytes_per_image / rows;

            WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            destination.texture                  = texture;
            destination.mipLevel                 = mip;
            destination.origin                   = { x, y, slice };
            WGPUTexelCopyBufferLayout layout     = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
            layout.bytesPerRow                   = static_cast<uint32_t>(bytes_per_row);
            layout.rowsPerImage                  = rows;

            // compressed copies cover whole blocks, the physical size of a small mip
            const WGPUExtent3D extent =
            {
                compressed ? ((width + 3) & ~3u) : width,
                compressed ? ((height + 3) & ~3u) : height,
                depth
            };
            wgpuQueueWriteTexture(webgpu::queue(), &destination, bytes, static_cast<size_t>(size), &layout, &extent);
        }
    }

    WGPUTextureView webgpu::texture_view(RHI_Texture* texture, WGPUTextureViewDimension dimension, const uint32_t mip, const uint32_t mip_count, const uint32_t layer, const uint32_t layer_count, const bool depth_only)
    {
        WGPUTexture resource = static_cast<WGPUTexture>(texture->GetRhiResource());
        if (!resource)
            return nullptr;

        if (dimension == WGPUTextureViewDimension_Undefined)
        {
            dimension = natural_dimension(texture);
        }

        const uint64_t key =
            (static_cast<uint64_t>(dimension)          << 56) |
            (static_cast<uint64_t>(mip & 0xff)         << 48) |
            (static_cast<uint64_t>(mip_count & 0xff)   << 40) |
            (static_cast<uint64_t>(layer & 0xffff)     << 24) |
            (static_cast<uint64_t>(layer_count & 0xffff) << 8) |
            (depth_only ? 1 : 0);

        lock_guard<mutex> lock(mutex_views);
        vector<View>& texture_views = views[resource];
        for (const View& view : texture_views)
        {
            if (view.key == key)
                return view.view;
        }

        const WGPUTextureFormat format       = texture_format(texture);
        WGPUTextureViewDescriptor descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        descriptor.format                    = format;
        descriptor.dimension                 = dimension;
        descriptor.baseMipLevel              = mip;
        descriptor.mipLevelCount             = mip_count;
        descriptor.baseArrayLayer            = layer;
        descriptor.arrayLayerCount           = layer_count;
        descriptor.aspect                    = depth_only && is_depth_format(format) ? WGPUTextureAspect_DepthOnly : WGPUTextureAspect_All;
        WGPUTextureView view                 = wgpuTextureCreateView(resource, &descriptor);
        texture_views.push_back({ key, view });
        return view;
    }

    WGPUTextureFormat webgpu::texture_format(const RHI_Texture* texture)
    {
        const WGPUTextureFormat format = to_format(texture->GetFormat());
        if (!texture->IsUav())
            return format;

        // only four channel formats can be read and written in one dispatch, the two channel and packed ones widen
        switch (format)
        {
            case WGPUTextureFormat_RG11B10Ufloat:
            case WGPUTextureFormat_RG16Float:
                return WGPUTextureFormat_RGBA16Float;
            case WGPUTextureFormat_RG32Float:
                return WGPUTextureFormat_RGBA32Float;
            default:
                return format;
        }
    }

    void webgpu::texture_views_release(void* texture)
    {
        lock_guard<mutex> lock(mutex_views);
        auto it = views.find(texture);
        if (it == views.end())
            return;

        for (const View& view : it->second)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::ImageView, view.view);
        }
        views.erase(it);
    }

    bool RHI_Texture::RHI_UpdateRegion(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const void* data, bool /*defer_to_frame*/)
    {
        WGPUTexture texture = static_cast<WGPUTexture>(m_rhi_resource);
        if (!texture || !data)
            return false;

        const uint64_t size = static_cast<uint64_t>(width) * height * GetBytesPerPixel();
        upload(texture, m_format, data, size, width, height, 1, 0, 0, x, y);
        return true;
    }

    bool RHI_Texture::RHI_CreateResource()
    {
        SP_ASSERT_MSG(RHI_Context::device != nullptr, "WebGPU device is null");

        if (m_width == 0 || m_height == 0)
        {
            SP_LOG_ERROR("Invalid texture dimensions: %dx%d", m_width, m_height);
            return false;
        }

        const WGPUTextureFormat format = webgpu::texture_format(this);
        if (format == WGPUTextureFormat_Undefined)
        {
            SP_LOG_ERROR("%s has no webgpu equivalent", rhi_format_to_string(m_format));
            return false;
        }

        const uint32_t array_length = GetArrayLength();
        const bool compressed       = is_block_compressed(m_format);

        // clears and blits are render passes, so every color texture that gets written is render target capable
        WGPUTextureUsage usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        if (IsUav() && webgpu::is_storage_format(format))
        {
            usage |= WGPUTextureUsage_StorageBinding;
        }
        if ((IsRt() || ((IsUav() || (m_flags & RHI_Texture_ClearBlit)) && !compressed)) && is_renderable(format))
        {
            usage |= WGPUTextureUsage_RenderAttachment;
        }

        WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
        descriptor.label                 = webgpu::label(m_object_name.c_str());
        descriptor.usage                 = usage;
        descriptor.dimension             = m_type == RHI_Texture_Type::Type3D ? WGPUTextureDimension_3D : WGPUTextureDimension_2D;
        // block compressed textures must span whole blocks, a 2048x1 strip is stored as 2048x4
        descriptor.size                  = { compressed ? ((m_width + 3) & ~3u) : m_width, compressed ? ((m_height + 3) & ~3u) : m_height, m_type == RHI_Texture_Type::Type3D ? m_depth : max(1u, array_length) };
        descriptor.format                = format;
        descriptor.mipLevelCount         = max(1u, m_mip_count);
        descriptor.sampleCount           = 1;
        WGPUTexture texture              = wgpuDeviceCreateTexture(RHI_Context::device, &descriptor);
        if (!texture)
        {
            SP_LOG_ERROR("Failed to create texture %s", m_object_name.c_str());
            return false;
        }
        m_rhi_resource = texture;

        // views, the bind code asks for whatever else it needs through webgpu::texture_view
        const bool depth          = IsDepthStencilFormat();
        const uint32_t layers     = m_type == RHI_Texture_Type::Type3D ? 1 : max(1u, array_length);
        const bool renderable     = (usage & WGPUTextureUsage_RenderAttachment) != 0;
        if (IsSrv())
        {
            m_rhi_srv = webgpu::texture_view(this, WGPUTextureViewDimension_Undefined, 0, descriptor.mipLevelCount, 0, layers, depth);
            if (m_type == RHI_Texture_Type::Type2DArray && array_length > 1)
            {
                m_rhi_srv_layers.resize(array_length);
                for (uint32_t layer = 0; layer < array_length; layer++)
                {
                    m_rhi_srv_layers[layer] = webgpu::texture_view(this, WGPUTextureViewDimension_2D, 0, descriptor.mipLevelCount, layer, 1, depth);
                }
            }
        }

        m_rhi_rtv.assign(IsRtv() && renderable ? layers : 0, nullptr);
        m_rhi_dsv.assign(IsDsv() && renderable ? layers : 0, nullptr);
        for (uint32_t layer = 0; layer < m_rhi_rtv.size(); layer++)
        {
            m_rhi_rtv[layer] = webgpu::texture_view(this, m_type == RHI_Texture_Type::Type3D ? WGPUTextureViewDimension_3D : WGPUTextureViewDimension_2D, 0, 1, layer, 1, false);
        }
        for (uint32_t layer = 0; layer < m_rhi_dsv.size(); layer++)
        {
            m_rhi_dsv[layer] = webgpu::texture_view(this, WGPUTextureViewDimension_2D, 0, 1, layer, 1, false);
        }
        m_rhi_rtv_multiview = nullptr;
        m_rhi_dsv_multiview = nullptr;

        // per mip views, a storage view sees one level, cubes are written as arrays
        const WGPUTextureViewDimension mip_dimension = m_type == RHI_Texture_Type::TypeCube ? WGPUTextureViewDimension_2DArray : WGPUTextureViewDimension_Undefined;
        if (HasPerMipViews())
        {
            for (uint32_t mip = 0; mip < m_mip_count && mip < rhi_max_mip_count; mip++)
            {
                m_rhi_srv_mips[mip] = webgpu::texture_view(this, mip_dimension, mip, 1, 0, layers, depth);
            }
        }
        else if (IsUav())
        {
            m_rhi_srv_mips[0] = webgpu::texture_view(this, mip_dimension, 0, 1, 0, layers, depth);
        }

        if (HasData())
        {
            for (uint32_t slice = 0; slice < layers && slice < m_slices.size(); slice++)
            {
                for (uint32_t mip = 0; mip < m_mip_count && mip < m_slices[slice].mips.size(); mip++)
                {
                    const vector<std::byte>& bytes = m_slices[slice].mips[mip].bytes;
                    if (bytes.empty())
                        continue;

                    const uint32_t width  = max(1u, m_width >> mip);
                    const uint32_t height = max(1u, m_height >> mip);
                    const uint32_t depth_ = m_type == RHI_Texture_Type::Type3D ? max(1u, m_depth >> mip) : 1;
                    upload(texture, m_format, bytes.data(), bytes.size(), width, height, depth_, slice, mip, 0, 0);
                }
            }
        }

        SetLayoutDirect(0, m_mip_count, RHI_Image_Layout::General);
        return true;
    }

    void RHI_Texture::RHI_DestroyResource()
    {
        if (m_rhi_resource)
        {
            webgpu::texture_views_release(m_rhi_resource);
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Image, m_rhi_resource);
            m_rhi_resource = nullptr;
        }

        m_rhi_srv_mips.fill(nullptr);
        m_rhi_srv           = nullptr;
        m_rhi_rtv_multiview = nullptr;
        m_rhi_dsv_multiview = nullptr;
        m_rhi_srv_layers.clear();
        m_rhi_rtv.clear();
        m_rhi_dsv.clear();
    }
}
