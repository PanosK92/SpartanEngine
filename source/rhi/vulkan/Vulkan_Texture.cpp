/*
Copyright(c) 2015-2026 Panos Karabelas

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

//= INCLUDES =====================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_Texture.h"
#include "../RHI_CommandList.h"
#include "../../profiling/Breadcrumbs.h"
#include <condition_variable>
//================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        VkImageAspectFlags get_aspect_mask(const RHI_Texture* texture, const bool only_depth = false, const bool only_stencil = false)
        {
            VkImageAspectFlags aspect_mask = 0;

            if (texture->IsColorFormat())
            {
                aspect_mask |= VK_IMAGE_ASPECT_COLOR_BIT;
            }
            else
            {
                if (texture->IsDepthFormat() && !only_stencil)
                {
                    aspect_mask |= VK_IMAGE_ASPECT_DEPTH_BIT;
                }

                if (texture->IsStencilFormat() && !only_depth)
                {
                    aspect_mask |= VK_IMAGE_ASPECT_STENCIL_BIT;
                }
            }

            return aspect_mask;
        }

        void create_image_view(
        void* image,
        void*& image_view,
        const RHI_Texture* texture,
        const uint32_t array_index,
        const uint32_t array_length,
        const uint32_t mip_index,
        const uint32_t mip_count
        )
        {
            VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
        
            // Determine the correct view type based on the texture type
            if (texture->GetType() == RHI_Texture_Type::Type2D)
            {
                view_type = VkImageViewType::VK_IMAGE_VIEW_TYPE_2D;
            }
            else if (texture->GetType() == RHI_Texture_Type::Type2DArray)
            {
                view_type = VkImageViewType::VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            }
            else if (texture->GetType() == RHI_Texture_Type::Type3D)
            {
                view_type = VkImageViewType::VK_IMAGE_VIEW_TYPE_3D;
            }
            else if (texture->GetType() == RHI_Texture_Type::TypeCube)
            {
                view_type = VkImageViewType::VK_IMAGE_VIEW_TYPE_CUBE;
            }
        
            VkImageViewCreateInfo create_info           = {};
            create_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            create_info.image                           = static_cast<VkImage>(image);
            create_info.viewType                        = view_type;
            create_info.format                          = vulkan_format[rhi_format_to_index(texture->GetFormat())];
            create_info.subresourceRange.aspectMask     = get_aspect_mask(texture, texture->IsDepthFormat(), false);
            create_info.subresourceRange.baseMipLevel   = mip_index;
            create_info.subresourceRange.levelCount     = mip_count;
        
            if (texture->GetType() == RHI_Texture_Type::TypeCube)
            {
                // for cubemaps, array layers represent the faces, so we set layerCount to 6
                create_info.subresourceRange.baseArrayLayer = 0; // starting from the first face
                create_info.subresourceRange.layerCount     = 6; // 6 faces of the cubemap
            }
            else if (texture->GetType() == RHI_Texture_Type::Type3D)
            {
                // for 3D textures, baseArrayLayer must be 0, and layerCount must be 1
                create_info.subresourceRange.baseArrayLayer = 0;
                create_info.subresourceRange.layerCount     = 1;
            }
            else
            {
                // for other types (2D arrays), use array layers
                create_info.subresourceRange.baseArrayLayer = array_index;
                create_info.subresourceRange.layerCount     = array_length;
            }
        
            create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        
            SP_ASSERT_MSG(vkCreateImageView(RHI_Context::device, &create_info, nullptr, reinterpret_cast<VkImageView*>(&image_view)) == VK_SUCCESS, "Failed to create image view");
        }

        // creates a 2d view of a single layer from an array texture (for per-layer srv binding)
        void create_image_view_2d_layer(
            void* image,
            void*& image_view,
            const RHI_Texture* texture,
            const uint32_t array_index,
            const uint32_t mip_index,
            const uint32_t mip_count
        )
        {
            VkImageViewCreateInfo create_info           = {};
            create_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            create_info.image                           = static_cast<VkImage>(image);
            create_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            create_info.format                          = vulkan_format[rhi_format_to_index(texture->GetFormat())];
            create_info.subresourceRange.aspectMask     = get_aspect_mask(texture, texture->IsDepthFormat(), false);
            create_info.subresourceRange.baseMipLevel   = mip_index;
            create_info.subresourceRange.levelCount     = mip_count;
            create_info.subresourceRange.baseArrayLayer = array_index;
            create_info.subresourceRange.layerCount     = 1;
            create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

            SP_ASSERT_MSG(vkCreateImageView(RHI_Context::device, &create_info, nullptr, reinterpret_cast<VkImageView*>(&image_view)) == VK_SUCCESS, "Failed to create per-layer image view");
        }

        void set_debug_name(RHI_Texture* texture)
        {
            const char* name = texture->GetObjectName().c_str();

            RHI_Device::SetResourceName(texture->GetRhiResource(), RHI_Resource_Type::Image, name);

            if (texture->IsSrv())
            {
                RHI_Device::SetResourceName(texture->GetRhiSrv(), RHI_Resource_Type::ImageView, name);
            }

            if (texture->HasPerMipViews())
            {
                for (uint32_t i = 0; i < texture->GetMipCount(); i++)
                {
                    RHI_Device::SetResourceName(texture->GetRhiSrvMip(i), RHI_Resource_Type::ImageView, name);
                }
            }
        }

        // host visible memory (pcie bar, typically 256 mb) held by in flight uploads is capped, but
        // several uploads may be in flight at once so the copy of one texture overlaps the submit and
        // fence wait of another, a single mutex here used to serialise the whole world texture pass
        namespace staging_budget
        {
            constexpr uint64_t budget_bytes = 128ull * 1024ull * 1024ull;
            mutex mtx;
            condition_variable cv;
            uint64_t in_flight_bytes = 0;

            void acquire(const uint64_t bytes)
            {
                unique_lock<mutex> lock(mtx);

                // an upload larger than the budget waits for exclusive use instead of never fitting
                cv.wait(lock, [bytes]
                {
                    return in_flight_bytes == 0 || in_flight_bytes + bytes <= budget_bytes;
                });
                in_flight_bytes += bytes;
            }

            void release(const uint64_t bytes)
            {
                {
                    lock_guard<mutex> lock(mtx);
                    in_flight_bytes -= bytes;
                }
                cv.notify_all();
            }
        }

        // fills one copy region per mip per slice and returns the staging bytes they need
        VkDeviceSize build_copy_regions(RHI_Texture* texture, vector<VkBufferImageCopy>& regions)
        {
            const uint32_t width     = texture->GetWidth();
            const uint32_t height    = texture->GetHeight();
            const uint32_t depth     = texture->GetDepth();
            const uint32_t mip_count = texture->GetMipCount();

            regions.resize(static_cast<size_t>(depth) * mip_count);

            VkDeviceSize buffer_offset    = 0;
            VkDeviceSize buffer_alignment = RHI_Device::PropertyGetOptimalBufferCopyOffsetAlignment();

            for (uint32_t array_index = 0; array_index < depth; array_index++)
            {
                for (uint32_t mip_index = 0; mip_index < mip_count; mip_index++)
                {
                    uint32_t region_index = mip_index + array_index * mip_count;
                    uint32_t mip_width    = max(1u, width >> mip_index);
                    uint32_t mip_height   = max(1u, height >> mip_index);
                    uint32_t mip_depth    = texture->GetType() == RHI_Texture_Type::Type3D ? (depth >> mip_index) : 1;

                    SP_ASSERT(mip_width != 0 && mip_height != 0 && mip_depth != 0);

                    // align buffer offset
                    buffer_offset = (buffer_offset + buffer_alignment - 1) & ~(buffer_alignment - 1);

                    VkBufferImageCopy& region             = regions[region_index];
                    region                                = {};
                    region.bufferOffset                   = buffer_offset;
                    region.bufferRowLength                = 0;
                    region.bufferImageHeight              = 0;
                    region.imageSubresource.aspectMask    = get_aspect_mask(texture);
                    region.imageSubresource.mipLevel      = mip_index;
                    region.imageSubresource.baseArrayLayer = array_index;
                    region.imageSubresource.layerCount    = 1;
                    region.imageOffset                    = { 0, 0, 0 };
                    region.imageExtent                    = { mip_width, mip_height, mip_depth };

                    buffer_offset += RHI_Texture::CalculateMipSize(mip_width, mip_height, mip_depth, texture->GetFormat(), texture->GetBitsPerChannel(), texture->GetChannelCount());
                }
            }

            return buffer_offset;
        }

        void copy_to_staging_buffer(RHI_Texture* texture, const vector<VkBufferImageCopy>& regions, void* staging_buffer)
        {
            SP_ASSERT_MSG(texture->HasData(), "No data to stage");

            const uint32_t depth     = texture->GetDepth();
            const uint32_t mip_count = texture->GetMipCount();

            void* mapped_data = nullptr;
            RHI_Device::MemoryMap(staging_buffer, mapped_data);

            for (uint32_t array_index = 0; array_index < depth; array_index++)
            {
                for (uint32_t mip_index = 0; mip_index < mip_count; mip_index++)
                {
                    // the region already carries the aligned offset the gpu will read from
                    const VkBufferImageCopy& region = regions[mip_index + array_index * mip_count];
                    const VkExtent3D& extent        = region.imageExtent;
                    size_t size                     = RHI_Texture::CalculateMipSize(extent.width, extent.height, extent.depth, texture->GetFormat(), texture->GetBitsPerChannel(), texture->GetChannelCount());

                    RHI_Texture_Mip* mip = texture->GetMip(array_index, mip_index);
                    if (mip && !mip->bytes.empty())
                    {
                        size_t copy_size = min(size, mip->bytes.size());
                        memcpy(static_cast<std::byte*>(mapped_data) + region.bufferOffset, mip->bytes.data(), copy_size);
                    }
                }
            }

            RHI_Device::MemoryUnmap(staging_buffer);
        }

        void stage(RHI_Texture* texture)
        {
            SP_ASSERT(texture->HasData());

            {
                char marker[128];
                snprintf(marker, sizeof(marker), "texture_stage: %s", texture->GetObjectName().c_str());
                Breadcrumbs::BeginMarker(marker);
            }

            vector<VkBufferImageCopy> regions;
            const VkDeviceSize staging_size = build_copy_regions(texture, regions);

            staging_budget::acquire(staging_size);
            void* staging_buffer = RHI_Device::StagingBufferAcquire(staging_size);

            Breadcrumbs::BeginMarker("texture_stage_copy_to_buffer");
            copy_to_staging_buffer(texture, regions, staging_buffer);
            Breadcrumbs::EndMarker(); // copy_to_buffer
        
            // copy the staging buffer into the image
            Breadcrumbs::BeginMarker("texture_stage_buffer_to_image");
            if (RHI_CommandList* cmd_list = RHI_CommandList::ImmediateExecutionBegin(RHI_Queue_Type::Graphics))
            {
                RHI_Image_Layout layout = RHI_Image_Layout::General;
        
                RHI_CommandList::PrepareTextureForUpload(cmd_list, texture);
        
                vkCmdCopyBufferToImage(
                    static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()),
                    static_cast<VkBuffer>(staging_buffer),
                    static_cast<VkImage>(texture->GetRhiResource()),
                    vulkan_image_layout[static_cast<uint8_t>(layout)],
                    static_cast<uint32_t>(regions.size()),
                    regions.data()
                );
        
                RHI_CommandList::ImmediateExecutionEnd(cmd_list);

                SP_ASSERT_MSG(texture->GetLayout(0) != RHI_Image_Layout::Max, "Layout not set after staging barrier");
            }
            Breadcrumbs::EndMarker(); // buffer_to_image
        
            RHI_Device::StagingBufferRelease(staging_buffer);
            staging_budget::release(staging_size);

            Breadcrumbs::EndMarker(); // texture_stage
        }

        RHI_Image_Layout GetAppropriateLayout(RHI_Texture*)
        {
            return RHI_Image_Layout::General;
        }
    }

    bool RHI_Texture::RHI_UpdateRegion(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const void* data)
    {
        // same staging path as a fresh upload, only the copy region is a sub-rectangle of mip 0
        const uint64_t size = static_cast<uint64_t>(width) * height * GetBytesPerPixel();
        if (size == 0)
        {
            return false;
        }

        staging_budget::acquire(size);
        void* staging_buffer = RHI_Device::StagingBufferAcquire(size);
        if (!staging_buffer)
        {
            staging_budget::release(size);
            return false;
        }

        void* mapped_data = nullptr;
        RHI_Device::MemoryMap(staging_buffer, mapped_data);
        if (!mapped_data)
        {
            RHI_Device::StagingBufferRelease(staging_buffer);
            staging_budget::release(size);
            return false;
        }
        memcpy(mapped_data, data, static_cast<size_t>(size));
        RHI_Device::MemoryUnmap(staging_buffer);

        VkBufferImageCopy region{};
        region.bufferOffset                    = 0;
        region.bufferRowLength                 = 0;
        region.bufferImageHeight               = 0;
        region.imageSubresource.aspectMask     = get_aspect_mask(this);
        region.imageSubresource.mipLevel       = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount     = 1;
        region.imageOffset                     = { static_cast<int32_t>(x), static_cast<int32_t>(y), 0 };
        region.imageExtent                     = { width, height, 1 };

        bool copied = false;
        if (RHI_CommandList* cmd_list = RHI_CommandList::ImmediateExecutionBegin(RHI_Queue_Type::Graphics))
        {
            VkCommandBuffer vk_cmd = static_cast<VkCommandBuffer>(cmd_list->GetRhiResource());

            // the image already sits in general, so the layout tracker emits nothing, the memory
            // dependency against in flight shader reads still has to be spelled out by hand
            RHI_CommandList::PrepareTextureForUpload(cmd_list, this);

            VkImageMemoryBarrier2 barrier{};
            barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier.srcStageMask                    = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.srcAccessMask                   = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
            barrier.dstStageMask                    = VK_PIPELINE_STAGE_2_COPY_BIT;
            barrier.dstAccessMask                   = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.oldLayout                       = vulkan_image_layout[static_cast<uint8_t>(RHI_Image_Layout::General)];
            barrier.newLayout                       = barrier.oldLayout;
            barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            barrier.image                           = static_cast<VkImage>(m_rhi_resource);
            barrier.subresourceRange.aspectMask     = get_aspect_mask(this);
            barrier.subresourceRange.baseMipLevel   = 0;
            barrier.subresourceRange.levelCount     = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount     = 1;

            VkDependencyInfo dependency{};
            dependency.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency.imageMemoryBarrierCount = 1;
            dependency.pImageMemoryBarriers    = &barrier;
            vkCmdPipelineBarrier2(vk_cmd, &dependency);

            vkCmdCopyBufferToImage(
                vk_cmd,
                static_cast<VkBuffer>(staging_buffer),
                static_cast<VkImage>(m_rhi_resource),
                vulkan_image_layout[static_cast<uint8_t>(RHI_Image_Layout::General)],
                1,
                &region
            );

            barrier.srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            vkCmdPipelineBarrier2(vk_cmd, &dependency);

            RHI_CommandList::ImmediateExecutionEnd(cmd_list);
            copied = true;
        }

        RHI_Device::StagingBufferRelease(staging_buffer);
        staging_budget::release(size);
        return copied;
    }

    bool RHI_Texture::RHI_CreateResource()
    {
        SP_ASSERT_MSG(m_width  != 0, "Width can't be zero");
        SP_ASSERT_MSG(m_height != 0, "Height can't be zero");

        // create image
        Breadcrumbs::BeginMarker("texture_create_image");
        RHI_Device::MemoryTextureCreate(this);
        Breadcrumbs::EndMarker(); // create_image

        // if the texture has any data, stage it
        if (HasData())
        {
            stage(this);
        }

        // staging already parks the image in general, skip a second submit
        Breadcrumbs::BeginMarker("texture_layout_transition");
        RHI_Image_Layout target_layout = GetAppropriateLayout(this);
        if (GetLayout(0) != target_layout)
        {
            if (RHI_CommandList* cmd_list = RHI_CommandList::ImmediateExecutionBegin(RHI_Queue_Type::Graphics))
            {
                cmd_list->InsertBarrier(this, target_layout, 0, m_mip_count);
                RHI_CommandList::ImmediateExecutionEnd(cmd_list);
            }
        }
        SP_ASSERT_MSG(GetLayout(0) != RHI_Image_Layout::Max, "Layout not set after transition");
        Breadcrumbs::EndMarker(); // layout_transition

        // create image views
        Breadcrumbs::BeginMarker("texture_create_views");
        {
            // shader resource views
            if (IsSrv() || IsUav())
            {
                create_image_view(m_rhi_resource, m_rhi_srv, this, 0, m_depth, 0, m_mip_count);

                if (HasPerMipViews())
                {
                    for (uint32_t i = 0; i < m_mip_count; i++)
                    {
                        create_image_view(m_rhi_resource, m_rhi_srv_mips[i], this, 0, m_depth, i, 1);
                    }
                }

                // per-layer 2d views for array textures (used by compute passes to sample individual layers)
                if (m_type == RHI_Texture_Type::Type2DArray && m_depth > 1)
                {
                    for (uint32_t i = 0; i < m_depth; i++)
                    {
                        create_image_view_2d_layer(m_rhi_resource, m_rhi_srv_layers[i], this, i, 0, m_mip_count);
                    }
                }
            }

            // render target views
            if (m_type == RHI_Texture_Type::Type2D || m_type == RHI_Texture_Type::Type2DArray || m_type == RHI_Texture_Type::TypeCube)
            {
                // both cube map slices/faces and array length is encoded into m_depth
                for (uint32_t i = 0; i < m_depth; i++)
                {
                    if (IsRtv())
                    {
                        create_image_view(m_rhi_resource, m_rhi_rtv[i], this, i, 1, 0, 1);
                    }

                    if (IsDsv())
                    {
                        create_image_view(m_rhi_resource, m_rhi_dsv[i], this, i, 1, 0, 1);
                    }
                }

                // full-array views for multiview rendering (covers all layers in a single view)
                if (m_type == RHI_Texture_Type::Type2DArray && m_depth > 1)
                {
                    if (IsRtv())
                    {
                        create_image_view(m_rhi_resource, m_rhi_rtv_multiview, this, 0, m_depth, 0, 1);
                    }

                    if (IsDsv())
                    {
                        create_image_view(m_rhi_resource, m_rhi_dsv_multiview, this, 0, m_depth, 0, 1);
                    }
                }
            }
            else if (m_type == RHI_Texture_Type::Type3D)
            {
                // for 3d textures, we create a single rtv for the entire volume

                if (IsRtv())
                {
                    create_image_view(m_rhi_resource, m_rhi_rtv[0], this, 0, 1, 0, 1);
                }
            }
            else

            {
                SP_ASSERT_MSG(false, "Unknown resource type")
            }

            // name the image and image view(s)
            set_debug_name(this);
        }
        Breadcrumbs::EndMarker(); // create_views

        return true;
    }

    void RHI_Texture::RHI_DestroyResource()
    {
        // evict cached descriptor sets keyed by this texture pointer before any
        // gpu handle is queued for deletion
        RHI_Device::DescriptorSetInvalidateReferencingResource(this);

        // srv and uav
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::ImageView, m_rhi_srv);
            m_rhi_srv = nullptr;

            for (uint32_t i = 0; i < m_mip_count; i++)
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::ImageView, m_rhi_srv_mips[i]);
                m_rhi_srv_mips[i] = nullptr;
            }

            for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::ImageView, m_rhi_srv_layers[i]);
                m_rhi_srv_layers[i] = nullptr;
            }
        }

        // rtv and dsv
        for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::ImageView, m_rhi_dsv[i]);
            m_rhi_dsv[i] = nullptr;

            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::ImageView, m_rhi_rtv[i]);
            m_rhi_rtv[i] = nullptr;
        }

        // multiview full-array views
        RHI_Device::DeletionQueueAdd(RHI_Resource_Type::ImageView, m_rhi_rtv_multiview);
        m_rhi_rtv_multiview = nullptr;
        RHI_Device::DeletionQueueAdd(RHI_Resource_Type::ImageView, m_rhi_dsv_multiview);
        m_rhi_dsv_multiview = nullptr;

        // rhi resource
        ClearLayouts();
        RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Image, m_rhi_resource);
        m_rhi_resource = nullptr;
    }

    void RHI_Texture::DestroyResourceImmediate()
    {
        // synchronous destruction must also invalidate cached descriptor sets so
        // future dispatches don't bind a set that still holds these dead handles
        RHI_Device::DescriptorSetInvalidateReferencingResource(this);

        if (m_rhi_srv)
        {
            vkDestroyImageView(RHI_Context::device, static_cast<VkImageView>(m_rhi_srv), nullptr);
            m_rhi_srv = nullptr;
        }

        for (uint32_t i = 0; i < m_mip_count; i++)
        {
            if (m_rhi_srv_mips[i])
            {
                vkDestroyImageView(RHI_Context::device, static_cast<VkImageView>(m_rhi_srv_mips[i]), nullptr);
                m_rhi_srv_mips[i] = nullptr;
            }
        }

        for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
        {
            if (m_rhi_srv_layers[i])
            {
                vkDestroyImageView(RHI_Context::device, static_cast<VkImageView>(m_rhi_srv_layers[i]), nullptr);
                m_rhi_srv_layers[i] = nullptr;
            }

            if (m_rhi_dsv[i])
            {
                vkDestroyImageView(RHI_Context::device, static_cast<VkImageView>(m_rhi_dsv[i]), nullptr);
                m_rhi_dsv[i] = nullptr;
            }

            if (m_rhi_rtv[i])
            {
                vkDestroyImageView(RHI_Context::device, static_cast<VkImageView>(m_rhi_rtv[i]), nullptr);
                m_rhi_rtv[i] = nullptr;
            }
        }

        if (m_rhi_rtv_multiview)
        {
            vkDestroyImageView(RHI_Context::device, static_cast<VkImageView>(m_rhi_rtv_multiview), nullptr);
            m_rhi_rtv_multiview = nullptr;
        }

        if (m_rhi_dsv_multiview)
        {
            vkDestroyImageView(RHI_Context::device, static_cast<VkImageView>(m_rhi_dsv_multiview), nullptr);
            m_rhi_dsv_multiview = nullptr;
        }

        ClearLayouts();
        if (m_rhi_resource)
        {
            RHI_Device::MemoryTextureDestroy(m_rhi_resource);
        }
    }
}
