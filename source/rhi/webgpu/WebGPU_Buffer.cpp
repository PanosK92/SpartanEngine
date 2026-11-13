/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==================
#include "pch.h"
#include "../RHI_Buffer.h"
#include "../RHI_Device.h"
#include "../RHI_CommandList.h"
#include "../../core/ThreadPool.h"
#include "WebGPU_Internal.h"
#include <thread>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif
//=============================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        // webgpu can't map a buffer the gpu uses, so mappable buffers are cpu memory that the queue copies over
        struct Shadow
        {
            uint64_t size          = 0;
            uint64_t dirty_begin   = UINT64_MAX;
            uint64_t dirty_end     = 0;
            bool tracked           = false;      // written through Update, the dirty range is exact
            uint64_t flushed_frame = UINT64_MAX; // written by whoever holds the pointer, big buffers are diffed once per frame and rely on MarkDirty in between
            vector<uint8_t> uploaded;            // what the gpu has, only kept for untracked buffers
        };

        unordered_map<RHI_Buffer*, Shadow> shadows;
        mutex mutex_shadows;

        const uint64_t page_size = 4096;

        void write(RHI_Buffer* buffer, uint64_t begin, uint64_t end)
        {
            begin = begin & ~3ull;
            end   = (end + 3) & ~3ull;
            if (end <= begin)
                return;

            wgpuQueueWriteBuffer(webgpu::queue(), static_cast<WGPUBuffer>(buffer->GetRhiResource()), begin, static_cast<uint8_t*>(buffer->GetMappedData()) + begin, end - begin);
        }

        uint64_t aligned_size(const uint64_t size)
        {
            return (max<uint64_t>(size, 4) + 3) & ~3ull;
        }
    }

    void webgpu::buffer_register(RHI_Buffer* buffer)
    {
        lock_guard<mutex> lock(mutex_shadows);
        shadows[buffer].size = aligned_size(buffer->GetObjectSize());
    }

    void webgpu::buffer_unregister(RHI_Buffer* buffer)
    {
        lock_guard<mutex> lock(mutex_shadows);
        shadows.erase(buffer);
    }

    bool webgpu::buffer_is_alive(RHI_Buffer* buffer)
    {
        lock_guard<mutex> lock(mutex_shadows);
        return shadows.find(buffer) != shadows.end();
    }

    void webgpu::buffer_mark_dirty(RHI_Buffer* buffer, const uint64_t offset, const uint64_t size, const bool exact)
    {
        lock_guard<mutex> lock(mutex_shadows);
        auto it = shadows.find(buffer);
        if (it == shadows.end())
            return;

        Shadow& shadow     = it->second;
        shadow.dirty_begin = min(shadow.dirty_begin, offset);
        shadow.dirty_end   = min(max(shadow.dirty_end, offset + size), shadow.size);

        // a hint names one write into a buffer that is also written through its pointer, so the buffer keeps being compared
        if (exact)
        {
            shadow.tracked = true;
            shadow.uploaded.clear();
        }
    }

    void webgpu::buffer_flush(RHI_Buffer* buffer, const bool touched)
    {
        lock_guard<mutex> lock(mutex_shadows);
        auto it = shadows.find(buffer);
        if (it == shadows.end() || !buffer->GetRhiResource() || !buffer->GetMappedData())
            return;

        Shadow& shadow = it->second;
        if (shadow.dirty_end > shadow.dirty_begin)
        {
            if (shadow.uploaded.size() == shadow.size)
            {
                // a hinted range of a compared buffer, the gpu gets what is remembered as uploaded, so a write racing this one shows up in the next comparison
                const uint64_t begin = shadow.dirty_begin & ~3ull;
                const uint64_t end   = min((shadow.dirty_end + 3) & ~3ull, shadow.size);
                memcpy(shadow.uploaded.data() + begin, static_cast<const uint8_t*>(buffer->GetMappedData()) + begin, end - begin);
                wgpuQueueWriteBuffer(webgpu::queue(), static_cast<WGPUBuffer>(buffer->GetRhiResource()), begin, shadow.uploaded.data() + begin, end - begin);
            }
            else
            {
                write(buffer, shadow.dirty_begin, shadow.dirty_end);
            }
            shadow.dirty_begin = UINT64_MAX;
            shadow.dirty_end   = 0;
        }

        // nobody said what changed, so the pages that differ from the last upload go over, on every submit since
        // a frame records several lists and what the cpu writes between them has to reach the list that follows,
        // the geometry pools are too big to diff that often and only change between frames
        if (!touched || shadow.tracked)
            return;

        const uint64_t per_submit_limit = 16ull * 1024 * 1024;
        if (shadow.size > per_submit_limit && shadow.flushed_frame == frame_index())
            return;
        shadow.flushed_frame = frame_index();

        const uint8_t* data = static_cast<const uint8_t*>(buffer->GetMappedData());
        if (shadow.uploaded.size() != shadow.size)
        {
            shadow.uploaded.assign(data, data + shadow.size);
            write(buffer, 0, shadow.size);
            return;
        }

        uint64_t run_begin = UINT64_MAX;
        for (uint64_t page = 0; page < shadow.size; page += page_size)
        {
            const uint64_t page_end = min(page + page_size, shadow.size);
            const bool changed      = memcmp(data + page, shadow.uploaded.data() + page, page_end - page) != 0;
            if (changed && run_begin == UINT64_MAX)
            {
                run_begin = page;
            }

            if ((!changed || page_end == shadow.size) && run_begin != UINT64_MAX)
            {
                const uint64_t run_end = changed ? page_end : page;
                write(buffer, run_begin, run_end);
                memcpy(shadow.uploaded.data() + run_begin, data + run_begin, run_end - run_begin);
                run_begin = UINT64_MAX;
            }
        }
    }

    void webgpu::buffer_read_back(RHI_Buffer* buffer)
    {
        WGPUBuffer source = static_cast<WGPUBuffer>(buffer->GetRhiResource());
        uint8_t* data     = static_cast<uint8_t*>(buffer->GetMappedData());
        if (!source || !data)
            return;

        // map read only combines with copy dst, so the gpu buffer goes through a staging copy
        const uint64_t size             = aligned_size(buffer->GetObjectSize());
        WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
        descriptor.label                = webgpu::label("readback_staging");
        descriptor.size                 = size;
        descriptor.usage                = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        WGPUBuffer staging              = wgpuDeviceCreateBuffer(RHI_Context::device, &descriptor);
        if (!staging)
            return;

        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(RHI_Context::device, nullptr);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, source, 0, staging, 0, size);
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(webgpu::queue(), 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);

        atomic<int> status = -1;
        WGPUBufferMapCallbackInfo callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        callback.mode                      = WGPUCallbackMode_AllowProcessEvents;
        callback.userdata1                 = &status;
        callback.callback                  = [](WGPUMapAsyncStatus map_status, WGPUStringView message, void* userdata1, void* userdata2)
        {
            static_cast<atomic<int>*>(userdata1)->store(static_cast<int>(map_status));
        };
        wgpuBufferMapAsync(staging, WGPUMapMode_Read, 0, size, callback);
        while (status.load() < 0)
        {
            webgpu::process_events();
            #if defined(__EMSCRIPTEN__)
            emscripten_sleep(1);
            #else
            this_thread::yield();
            #endif
        }

        if (status.load() == static_cast<int>(WGPUMapAsyncStatus_Success))
        {
            const void* mapped = wgpuBufferGetConstMappedRange(staging, 0, size);
            if (mapped)
            {
                lock_guard<mutex> lock(mutex_shadows);
                memcpy(data, mapped, buffer->GetObjectSize());
                auto it = shadows.find(buffer);
                if (it != shadows.end() && it->second.uploaded.size() == it->second.size)
                {
                    memcpy(it->second.uploaded.data(), mapped, it->second.size);
                }
            }
            wgpuBufferUnmap(staging);
        }
        wgpuBufferRelease(staging);
    }

    void RHI_Buffer::RHI_DestroyResource()
    {
        if (m_data_gpu)
        {
            webgpu::buffer_unregister(this);
            free(m_data_gpu);
        }

        if (m_rhi_resource)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_rhi_resource);
            m_rhi_resource = nullptr;
        }

        m_data_gpu       = nullptr;
        m_device_address = 0;
    }

    void RHI_Buffer::RHI_CreateResource(const void* data)
    {
        SP_ASSERT(RHI_Context::device != nullptr);

        // constant buffers are bound at offsets, which webgpu wants 256 byte aligned
        uint64_t size = m_object_size;
        if (m_type == RHI_Buffer_Type::Constant)
        {
            m_stride      = (m_stride_unaligned + 255) & ~255;
            size          = static_cast<uint64_t>(m_stride) * m_element_count;
            m_object_size = size;
        }

        // the engine reuses buffers across roles (a storage buffer as indirect arguments, an upload buffer as a shader input)
        WGPUBufferUsage usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage | WGPUBufferUsage_Vertex | WGPUBufferUsage_Index | WGPUBufferUsage_Indirect;
        if (m_type == RHI_Buffer_Type::Constant)
        {
            usage |= WGPUBufferUsage_Uniform;
        }

        WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
        descriptor.label                = webgpu::label(m_object_name.c_str());
        descriptor.size                 = aligned_size(size);
        descriptor.usage                = usage;
        WGPUBuffer buffer               = wgpuDeviceCreateBuffer(RHI_Context::device, &descriptor);
        SP_ASSERT_MSG(buffer != nullptr, "Failed to allocate buffer");
        m_rhi_resource   = buffer;
        m_device_address = 0;
        webgpu::bindless_invalidate();

        const bool shadowed = m_mappable || m_type == RHI_Buffer_Type::Upload || m_type == RHI_Buffer_Type::Readback;
        if (shadowed)
        {
            m_data_gpu = calloc(1, descriptor.size);
            SP_ASSERT_MSG(m_data_gpu != nullptr, "Failed to allocate a buffer shadow");
            webgpu::buffer_register(this);
        }

        if (data)
        {
            if (m_data_gpu)
            {
                memcpy(m_data_gpu, data, m_object_size);
                webgpu::buffer_mark_dirty(this, 0, m_object_size);
                webgpu::buffer_flush(this, false);
            }
            else
            {
                UploadSubRegion(data, 0, m_object_size);
            }
        }
    }

    void RHI_Buffer::UploadSubRegion(const void* data, uint64_t offset_bytes, uint64_t size_bytes)
    {
        SP_ASSERT(data && offset_bytes + size_bytes <= m_object_size);
        if (!m_rhi_resource || size_bytes == 0)
            return;

        if (m_data_gpu)
        {
            memcpy(static_cast<uint8_t*>(m_data_gpu) + offset_bytes, data, size_bytes);
            webgpu::buffer_mark_dirty(this, offset_bytes, size_bytes);
            return;
        }

        // the queue orders the write before anything submitted later
        if ((offset_bytes & 3) == 0 && (size_bytes & 3) == 0)
        {
            wgpuQueueWriteBuffer(webgpu::queue(), static_cast<WGPUBuffer>(m_rhi_resource), offset_bytes, data, size_bytes);
            return;
        }

        const uint64_t begin = offset_bytes & ~3ull;
        const uint64_t end   = min<uint64_t>((offset_bytes + size_bytes + 3) & ~3ull, aligned_size(m_object_size));
        vector<uint8_t> padded(end - begin, 0);
        memcpy(padded.data() + (offset_bytes - begin), data, size_bytes);
        SP_LOG_WARNING("Unaligned upload into \"%s\", the bytes around it are cleared", m_object_name.c_str());
        wgpuQueueWriteBuffer(webgpu::queue(), static_cast<WGPUBuffer>(m_rhi_resource), begin, padded.data(), padded.size());
    }

    void RHI_Buffer::Update(void* data_cpu, const uint32_t size)
    {
        Update(RHI_Device::Cmd(), data_cpu, size);
    }

    void RHI_Buffer::Update(RHI_CommandList* cmd_list, void* data_cpu, const uint32_t size)
    {
        if (!data_cpu)
            return;

        // mappable buffers ring advance by stride, so consumers that read through GetOffset see the right slot
        if (m_data_gpu)
        {
            if (first_update)
            {
                first_update = false;
            }
            else
            {
                m_offset += m_stride;
            }

            const uint64_t upload_size = size != 0 ? static_cast<uint64_t>(size) : static_cast<uint64_t>(m_stride_unaligned);
            SP_ASSERT_MSG(
                static_cast<uint64_t>(m_offset) + upload_size <= m_object_size,
                ("buffer \"" + GetObjectName() + "\" was handed more data than it can hold").c_str()
            );
            ThreadPool::ParallelCopy(static_cast<uint8_t*>(m_data_gpu) + m_offset, data_cpu, upload_size);
            webgpu::buffer_mark_dirty(this, m_offset, upload_size);
            return;
        }

        SP_ASSERT(cmd_list);
        cmd_list->update_buffer(this, 0, size != 0 ? size : m_stride_unaligned, data_cpu);
    }

    void RHI_Buffer::MarkDirty(const uint64_t offset_bytes, const uint64_t size_bytes)
    {
        if (m_data_gpu && size_bytes != 0)
        {
            webgpu::buffer_mark_dirty(this, offset_bytes, size_bytes, false);
        }
    }

    void RHI_Buffer::UpdateHandles(RHI_CommandList* cmd_list)
    {
        // no ray tracing on webgpu
    }

    RHI_StridedDeviceAddressRegion RHI_Buffer::GetRegion(const RHI_Shader_Type group_type, const uint32_t stride_extra) const
    {
        return {};
    }
}
