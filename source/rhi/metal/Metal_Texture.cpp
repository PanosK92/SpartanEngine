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
#include "Metal_Internal.h"
//=============================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        bool is_block_compressed(const RHI_Format format)
        {
            return format == RHI_Format::BC1_Unorm || format == RHI_Format::BC3_Unorm || format == RHI_Format::BC5_Unorm || format == RHI_Format::BC7_Unorm;
        }

        MTL::TextureType texture_type(const RHI_Texture_Type type, const uint32_t array_length)
        {
            switch (type)
            {
                case RHI_Texture_Type::Type2DArray: return MTL::TextureType2DArray;
                case RHI_Texture_Type::Type3D:      return MTL::TextureType3D;
                case RHI_Texture_Type::TypeCube:    return array_length > 6 ? MTL::TextureTypeCubeArray : MTL::TextureTypeCube;
                default:                            return MTL::TextureType2D;
            }
        }

        // copies one tightly packed subresource through a staging buffer
        void upload(RHI_CommandList* cmd_list, MTL::Texture* texture, const RHI_Format format, const vector<std::byte>& bytes, uint32_t width, uint32_t height, uint32_t depth, uint32_t slice, uint32_t mip, MTL::Origin origin)
        {
            MTL::Buffer* staging = static_cast<MTL::Buffer*>(RHI_Device::StagingBufferAcquire(bytes.size()));
            SP_ASSERT_MSG(staging != nullptr, "Failed to acquire a staging buffer");
            memcpy(staging->contents(), bytes.data(), bytes.size());

            // compressed rows are rows of 4x4 blocks
            const uint32_t rows            = is_block_compressed(format) ? max(1u, (height + 3) / 4) : height;
            const uint64_t bytes_per_image = bytes.size() / depth;
            const uint64_t bytes_per_row   = bytes_per_image / rows;

            metal_encoder::compute(cmd_list->GetRhiResource(), *static_cast<metal_encoder::State*>(cmd_list->GetRhiState()))->copyFromBuffer(
                staging, 0, bytes_per_row, bytes_per_image, MTL::Size(width, height, depth),
                texture, slice, mip, origin
            );
            cmd_list->RetainStagingBuffer(staging);
        }
    }

    namespace
    {
        struct View
        {
            uint64_t key          = 0;
            MTL::Texture* texture = nullptr;
        };

        unordered_map<MTL::Texture*, vector<View>> views;
        mutex mutex_views;
    }

    MTL::Texture* metal_texture::GetView(RHI_Texture* texture, const uint32_t mip, const uint32_t mip_count, const uint32_t layer, const bool uav)
    {
        MTL::Texture* resource      = static_cast<MTL::Texture*>(texture->GetRhiResource());
        const RHI_Texture_Type type = texture->GetType();
        const bool all_layers       = layer == rhi_all_mips;
        const uint32_t layer_count  = type == RHI_Texture_Type::TypeCube ? resource->arrayLength() * 6 : resource->arrayLength();

        // the whole texture as hlsl sees it needs no view
        const bool all_mips = mip == 0 && mip_count >= resource->mipmapLevelCount();
        if (all_mips && all_layers && !(uav && type == RHI_Texture_Type::TypeCube))
            return resource;

        const uint64_t key = (static_cast<uint64_t>(mip) << 48) | (static_cast<uint64_t>(mip_count & 0xffff) << 32) | (static_cast<uint64_t>(layer & 0x7fffffff) << 1) | (uav ? 1 : 0);
        lock_guard<mutex> lock(mutex_views);
        vector<View>& texture_views = views[resource];
        for (const View& view : texture_views)
        {
            if (view.key == key)
                return view.texture;
        }

        MTL::TextureType view_type = resource->textureType();
        if (uav && type == RHI_Texture_Type::TypeCube)
        {
            view_type = MTL::TextureType2DArray;
        }

        const NS::Range levels = NS::Range(mip, min<uint32_t>(mip_count, resource->mipmapLevelCount() - mip));
        const NS::Range slices = all_layers ? NS::Range(0, layer_count) : NS::Range(layer, 1);
        MTL::Texture* view     = resource->newTextureView(resource->pixelFormat(), view_type, levels, slices);
        SP_ASSERT_MSG(view != nullptr, "Failed to create a texture view");
        texture_views.push_back({ key, view });
        return view;
    }

    bool RHI_Texture::RHI_UpdateRegion(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const void* data, bool /*defer_to_frame*/)
    {
        MTL::Texture* texture = static_cast<MTL::Texture*>(m_rhi_resource);
        if (!texture)
            return false;

        const size_t size = static_cast<size_t>(width) * height * GetBytesPerPixel();
        vector<std::byte> bytes(size);
        memcpy(bytes.data(), data, size);

        RHI_CommandList* cmd_list = RHI_CommandList::ImmediateExecutionBegin(RHI_Queue_Type::Graphics);
        if (!cmd_list)
            return false;

        upload(cmd_list, texture, m_format, bytes, width, height, 1, 0, 0, MTL::Origin(x, y, 0));
        RHI_CommandList::ImmediateExecutionEnd(cmd_list);
        return true;
    }

    bool RHI_Texture::RHI_CreateResource()
    {
        SP_ASSERT_MSG(RHI_Context::device != nullptr, "Metal device is null");

        if (m_width == 0 || m_height == 0)
        {
            SP_LOG_ERROR("Invalid texture dimensions: %dx%d", m_width, m_height);
            return false;
        }

        const MTL::PixelFormat format = metal_utility::to_pixel_format(m_format);
        if (format == MTL::PixelFormatInvalid)
        {
            SP_LOG_ERROR("%s has no metal equivalent", rhi_format_to_string(m_format));
            return false;
        }

        const uint32_t array_length = GetArrayLength();
        const bool is_depth         = IsDepthStencilFormat();

        // clears and blits are render passes, so every color texture that gets written is render target capable
        MTL::TextureUsage usage = MTL::TextureUsageShaderRead;
        if (IsUav())
        {
            usage |= MTL::TextureUsageShaderWrite;
        }
        if (IsRt() || ((IsUav() || (m_flags & RHI_Texture_ClearBlit)) && !is_block_compressed(m_format)))
        {
            usage |= MTL::TextureUsageRenderTarget;
        }
        if (HasPerMipViews())
        {
            usage |= MTL::TextureUsagePixelFormatView;
        }

        MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
        descriptor->setTextureType(texture_type(m_type, array_length));
        descriptor->setPixelFormat(format);
        descriptor->setWidth(m_width);
        descriptor->setHeight(m_height);
        descriptor->setDepth(m_type == RHI_Texture_Type::Type3D ? m_depth : 1);
        descriptor->setArrayLength(m_type == RHI_Texture_Type::TypeCube ? max(1u, array_length / 6) : (m_type == RHI_Texture_Type::Type3D ? 1 : array_length));
        descriptor->setMipmapLevelCount(m_mip_count);
        descriptor->setUsage(usage);
        descriptor->setStorageMode(MTL::StorageModePrivate);
        descriptor->setHazardTrackingMode(MTL::HazardTrackingModeUntracked);

        MTL::Texture* texture = RHI_Context::device->newTexture(descriptor);
        descriptor->release();
        if (!texture)
        {
            SP_LOG_ERROR("Failed to create texture %s", m_object_name.c_str());
            return false;
        }
        metal_utility::set_label(texture, m_object_name.c_str());

        // render targets and storage textures follow the resolution, everything else is streamed content
        const bool screen = IsRt() || IsUav();
        metal_residency::Add(screen ? metal_residency::Group::Screen : metal_residency::Group::Dynamic, texture);
        m_rhi_resource = texture;

        // metal binds the texture itself and render passes pick a slice, so most views are the texture
        if (IsSrv())
        {
            m_rhi_srv = texture;
            if (m_type == RHI_Texture_Type::Type2DArray && array_length > 1)
            {
                m_rhi_srv_layers.assign(array_length, texture);
            }
        }
        m_rhi_rtv.assign(IsRtv() ? array_length : 0, texture);
        m_rhi_dsv.assign(IsDsv() ? array_length : 0, texture);
        m_rhi_rtv_multiview = IsRtv() && array_length > 1 ? texture : nullptr;
        m_rhi_dsv_multiview = IsDsv() && array_length > 1 ? texture : nullptr;

        // per mip views are real views, a shader can only see one level of a storage texture at a time
        if (HasPerMipViews())
        {
            for (uint32_t mip = 0; mip < m_mip_count; mip++)
            {
                m_rhi_srv_mips[mip] = texture->newTextureView(format, texture->textureType(), NS::Range(mip, 1), NS::Range(0, texture->arrayLength() * (m_type == RHI_Texture_Type::TypeCube ? 6 : 1)));
            }
        }
        else if (IsUav())
        {
            m_rhi_srv_mips[0] = texture;
        }

        if (HasData())
        {
            RHI_CommandList* cmd_list = RHI_CommandList::ImmediateExecutionBegin(RHI_Queue_Type::Graphics);
            if (cmd_list)
            {
                for (uint32_t slice = 0; slice < array_length && slice < m_slices.size(); slice++)
                {
                    for (uint32_t mip = 0; mip < m_mip_count && mip < m_slices[slice].mips.size(); mip++)
                    {
                        const vector<std::byte>& bytes = m_slices[slice].mips[mip].bytes;
                        if (bytes.empty())
                            continue;

                        const uint32_t width  = max(1u, m_width >> mip);
                        const uint32_t height = max(1u, m_height >> mip);
                        const uint32_t depth  = m_type == RHI_Texture_Type::Type3D ? max(1u, m_depth >> mip) : 1;
                        upload(cmd_list, texture, m_format, bytes, width, height, depth, slice, mip, MTL::Origin(0, 0, 0));
                    }
                }
                RHI_CommandList::ImmediateExecutionEnd(cmd_list, false);
                SetLayoutDirect(0, m_mip_count, RHI_Image_Layout::General);
            }
        }

        return true;
    }

    void RHI_Texture::RHI_DestroyResource()
    {
        RHI_Device::DescriptorSetInvalidateReferencingResource(this);

        for (void*& view : m_rhi_srv_mips)
        {
            if (view && view != m_rhi_resource)
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::ImageView, view);
            }
            view = nullptr;
        }

        {
            lock_guard<mutex> lock(mutex_views);
            auto it = views.find(static_cast<MTL::Texture*>(m_rhi_resource));
            if (it != views.end())
            {
                for (const View& view : it->second)
                {
                    RHI_Device::DeletionQueueAdd(RHI_Resource_Type::ImageView, view.texture);
                }
                views.erase(it);
            }
        }

        if (m_rhi_resource)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Image, m_rhi_resource);
            m_rhi_resource = nullptr;
        }

        m_rhi_srv           = nullptr;
        m_rhi_rtv_multiview = nullptr;
        m_rhi_dsv_multiview = nullptr;
        m_rhi_srv_layers.clear();
        m_rhi_rtv.clear();
        m_rhi_dsv.clear();
    }
}
