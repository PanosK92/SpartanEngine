/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ============================
#include "pch.h"
#include "../RHI_AccelerationStructure.h"
#include "../RHI_Device.h"
#include "../RHI_Implementation.h"
#include "../RHI_CommandList.h"
#include "../RHI_SyncPrimitive.h"
#include <mutex>
//=======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace
{
    PFN_vkGetAccelerationStructureBuildSizesKHR       as_get_build_sizes    = nullptr;
    PFN_vkCreateAccelerationStructureKHR              as_create             = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR           as_build              = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR    as_get_device_address = nullptr;
    PFN_vkCmdWriteAccelerationStructuresPropertiesKHR as_write_properties   = nullptr;
    PFN_vkCmdCopyAccelerationStructureKHR             as_copy               = nullptr;

    namespace compaction
    {
        // one query per blas awaiting its compacted size, a blas that finds the pool exhausted stays uncompacted
        // the initial burst queues every static blas before the first tlas exists, and plan.world alone has over 8k
        constexpr uint32_t query_count = 32768;
        // bounds the transient memory of a burst, the old storage lives until the gpu retires the copy
        constexpr uint32_t copies_per_frame = 256;
        VkQueryPool query_pool = VK_NULL_HANDLE;
        std::vector<uint32_t> free_queries;
        std::mutex mutex;
        uint64_t bytes_before = 0;
        uint64_t bytes_after  = 0;
        uint32_t compacted    = 0;
        uint32_t not_ready    = 0;
        uint32_t not_smaller  = 0;

        uint32_t acquire_query()
        {
            if (query_pool == VK_NULL_HANDLE)
            {
                VkQueryPoolCreateInfo info = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
                info.queryType             = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
                info.queryCount            = query_count;
                if (vkCreateQueryPool(spartan::RHI_Context::device, &info, nullptr, &query_pool) != VK_SUCCESS)
                {
                    query_pool = VK_NULL_HANDLE;
                    return UINT32_MAX;
                }
                free_queries.resize(query_count);
                for (uint32_t i = 0; i < query_count; i++)
                {
                    free_queries[i] = query_count - 1 - i;
                }
            }

            if (free_queries.empty())
            {
                return UINT32_MAX;
            }

            const uint32_t query = free_queries.back();
            free_queries.pop_back();
            return query;
        }
    }
}

namespace spartan
{
    void* RHI_AccelerationStructure::s_blas_scratch_buffer         = nullptr;
    uint64_t RHI_AccelerationStructure::s_blas_scratch_buffer_size = 0;
    vector<RHI_AccelerationStructure*> RHI_AccelerationStructure::s_compaction_pending;
    uint64_t RHI_AccelerationStructure::s_blas_bytes = 0;
    uint64_t RHI_AccelerationStructure::s_compaction_generation = 0;

    void RHI_AccelerationStructure::FreeSharedBlasScratch()
    {
        if (s_blas_scratch_buffer)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, s_blas_scratch_buffer);
            s_blas_scratch_buffer      = nullptr;
            s_blas_scratch_buffer_size = 0;
        }
    }

    RHI_AccelerationStructure::RHI_AccelerationStructure(const RHI_AccelerationStructureType type, const char* name)
    {
        m_type        = type;
        m_object_name = name ? name : "acceleration_structure";

        // load extension functions if not already loaded
        if (!as_get_build_sizes)
        {
            as_get_build_sizes    = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(vkGetDeviceProcAddr(RHI_Context::device, "vkGetAccelerationStructureBuildSizesKHR"));
            as_create             = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(vkGetDeviceProcAddr(RHI_Context::device, "vkCreateAccelerationStructureKHR"));
            as_build              = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(RHI_Context::device, "vkCmdBuildAccelerationStructuresKHR"));
            as_get_device_address = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(vkGetDeviceProcAddr(RHI_Context::device, "vkGetAccelerationStructureDeviceAddressKHR"));
            as_write_properties   = reinterpret_cast<PFN_vkCmdWriteAccelerationStructuresPropertiesKHR>(vkGetDeviceProcAddr(RHI_Context::device, "vkCmdWriteAccelerationStructuresPropertiesKHR"));
            as_copy               = reinterpret_cast<PFN_vkCmdCopyAccelerationStructureKHR>(vkGetDeviceProcAddr(RHI_Context::device, "vkCmdCopyAccelerationStructureKHR"));
        }
    }

    RHI_AccelerationStructure::~RHI_AccelerationStructure()
    {
        Destroy();
    }

    void RHI_AccelerationStructure::Destroy()
    {
        if (m_type == RHI_AccelerationStructureType::Bottom)
        {
            lock_guard lock(compaction::mutex);
            CancelCompaction();
            s_blas_bytes -= min(s_blas_bytes, m_size);
        }

        m_device_address = 0;
        m_tlas_instance_count = m_tlas_refit_count = 0;
        if (m_type == RHI_AccelerationStructureType::Top && m_rhi_resource)
        {
            RHI_Device::DescriptorSetInvalidateReferencingResource(this);
        }

        if (m_rhi_resource)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::AccelerationStructure, m_rhi_resource);
            m_rhi_resource = nullptr;
        }

        if (m_rhi_resource_results)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_rhi_resource_results);
            m_rhi_resource_results = nullptr;
        }

        if (m_scratch_buffer)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_scratch_buffer);
            m_scratch_buffer      = nullptr;
            m_scratch_buffer_size = 0;
        }

        // destroy double-buffered instance and staging buffers
        for (uint32_t i = 0; i < buffer_count; i++)
        {
            if (m_instance_buffer[i])
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_instance_buffer[i]);
                m_instance_buffer[i]      = nullptr;
                m_instance_buffer_size[i] = 0;
            }

            if (m_staging_buffer[i])
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_staging_buffer[i]);
                m_staging_buffer[i]      = nullptr;
                m_staging_buffer_size[i] = 0;
            }
        }

        m_size = 0;
    }

    void RHI_AccelerationStructure::BuildBottomLevel(const vector<RHI_AccelerationStructureGeometry>& geometries, const vector<uint32_t>& primitive_counts, bool allow_update)
    {
        BuildBottomLevel(RHI_Device::Cmd(), geometries, primitive_counts, allow_update);
    }

    void RHI_AccelerationStructure::BuildBottomLevel(RHI_CommandList* cmd_list, const vector<RHI_AccelerationStructureGeometry>& geometries, const vector<uint32_t>& primitive_counts, bool allow_update)
    {
        // Dynamic vertex uploads recorded earlier this frame must precede AS reads.
        cmd_list->SynchronizeResources(false);
        SP_ASSERT(m_type == RHI_AccelerationStructureType::Bottom);
        SP_ASSERT(geometries.size() == primitive_counts.size());
        SP_ASSERT(!geometries.empty());

        Destroy();
        m_allow_update = allow_update;

        // define geometry
        vector<VkAccelerationStructureGeometryKHR> vk_geometries;
        vk_geometries.reserve(geometries.size());

        for (const RHI_AccelerationStructureGeometry& geo : geometries)
        {
            VkAccelerationStructureGeometryTrianglesDataKHR triangles_data = {};
            triangles_data.sType                                           = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            triangles_data.vertexFormat                                    = vulkan_format[static_cast<uint32_t>(geo.vertex_format)];
            triangles_data.vertexData.deviceAddress                        = geo.vertex_buffer_address;
            triangles_data.vertexStride                                    = geo.vertex_stride;
            triangles_data.maxVertex                                       = geo.max_vertex;
            triangles_data.indexType                                       = geo.index_format == RHI_Format::R32_Uint ? VK_INDEX_TYPE_UINT32 : (geo.index_format == RHI_Format::R16_Uint ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_NONE_KHR);
            triangles_data.indexData.deviceAddress                         = geo.index_buffer_address;
            triangles_data.transformData.deviceAddress                     = 0;

            VkAccelerationStructureGeometryKHR geometry = {};
            geometry.sType                              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            geometry.flags                              = geo.transparent ? 0 : VK_GEOMETRY_OPAQUE_BIT_KHR;
            geometry.geometryType                       = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geometry.geometry.triangles                 = triangles_data;

            vk_geometries.emplace_back(geometry);
        }

        // build info
        VkAccelerationStructureBuildGeometryInfoKHR build_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
        build_info.type                                        = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        build_info.flags                                       = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        if (allow_update)
        {
            build_info.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
        }
        else
        {
            build_info.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
        }
        build_info.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build_info.geometryCount = static_cast<uint32_t>(vk_geometries.size());
        build_info.pGeometries   = vk_geometries.data();

        // get build sizes
        VkDevice device = static_cast<VkDevice>(RHI_Context::device);

        VkAccelerationStructureBuildSizesInfoKHR size_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        as_get_build_sizes(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, primitive_counts.data(), &size_info);

        // create result buffer
        VkBufferUsageFlags usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        RHI_Device::MemoryBufferCreate(m_rhi_resource_results, size_info.accelerationStructureSize, usage, properties, nullptr, m_object_name.c_str(), !allow_update);

        // bail if alloc failed, calling as_create with a null buffer would crash the driver
        if (!m_rhi_resource_results)
        {
            SP_LOG_WARNING("BLAS result buffer alloc failed (%llu bytes) for %s, skipping build", size_info.accelerationStructureSize, m_object_name.c_str());
            return;
        }

        // create acceleration structure
        VkAccelerationStructureCreateInfoKHR create_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
        create_info.buffer = static_cast<VkBuffer>(m_rhi_resource_results);
        create_info.size = size_info.accelerationStructureSize;
        create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        as_create(device, &create_info, nullptr, reinterpret_cast<VkAccelerationStructureKHR*>(&m_rhi_resource));
        m_device_address = 0;
        RHI_Device::SetResourceName(m_rhi_resource, RHI_Resource_Type::AccelerationStructure, m_object_name.c_str());
        {
            lock_guard lock(compaction::mutex);
            m_size        = size_info.accelerationStructureSize;
            s_blas_bytes += m_size;
        }

        // static blas share one growing scratch buffer, per-instance scratch oom'd the gpu, overallocated so the address can be aligned at use
        const uint64_t alignment = RHI_Device::PropertyGetMinAccelerationBufferOffsetAlignment();
        uint64_t scratch_size    = max(size_info.buildScratchSize, size_info.updateScratchSize);
        scratch_size             = ((scratch_size + alignment - 1) & ~(alignment - 1)) + alignment;
        usage                    = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        properties               = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        void** scratch_target = allow_update ? &m_scratch_buffer : &s_blas_scratch_buffer;
        uint64_t* scratch_size_target = allow_update ? &m_scratch_buffer_size : &s_blas_scratch_buffer_size;
        if (!*scratch_target || scratch_size > *scratch_size_target)
        {
            if (*scratch_target)
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, *scratch_target);
                *scratch_target = nullptr;
            }
            RHI_Device::MemoryBufferCreate(*scratch_target, scratch_size, usage, properties, nullptr, (m_object_name + "_scratch").c_str(), true);
            if (!*scratch_target)
            {
                SP_LOG_WARNING("BLAS scratch buffer alloc failed (%llu bytes) for %s, skipping build", scratch_size, m_object_name.c_str());
                *scratch_size_target = 0;
                return;
            }
            *scratch_size_target = scratch_size;
        }

        // set up build with aligned scratch address
        VkDeviceAddress scratch_base    = RHI_Device::GetBufferDeviceAddress(*scratch_target);
        VkDeviceAddress scratch_aligned = (scratch_base + alignment - 1) & ~(alignment - 1);
        build_info.dstAccelerationStructure  = static_cast<VkAccelerationStructureKHR>(m_rhi_resource);
        build_info.scratchData.deviceAddress = scratch_aligned;

        // build
        vector<VkAccelerationStructureBuildRangeInfoKHR> range_infos(geometries.size());

        for (uint32_t i = 0; i < static_cast<uint32_t>(geometries.size()); ++i)
        {
            range_infos[i].primitiveCount  = primitive_counts[i];
            range_infos[i].primitiveOffset = 0;
            range_infos[i].firstVertex     = 0;
            range_infos[i].transformOffset = 0;
        }

        vector<VkAccelerationStructureBuildRangeInfoKHR*> p_range_infos;

        for (auto& range : range_infos) { p_range_infos.push_back(&range); }

        as_build(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()), 1, &build_info, p_range_infos.data());

        // Initialize the immutable address before parallel TLAS readers use it.
        GetDeviceAddress();

        // barrier: ensure build completes before use, and allow next blas to reuse the shared scratch buffer
        // dst must include ACCELERATION_STRUCTURE_WRITE so consecutive builds writing the shared scratch are ordered
        {
            VkMemoryBarrier2 memory_barrier = {};
            memory_barrier.sType            = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            memory_barrier.srcStageMask     = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
            memory_barrier.srcAccessMask    = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
            memory_barrier.dstStageMask     = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
            memory_barrier.dstAccessMask    = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT;

            VkDependencyInfo dependency_info   = {};
            dependency_info.sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency_info.memoryBarrierCount = 1;
            dependency_info.pMemoryBarriers    = &memory_barrier;

            vkCmdPipelineBarrier2(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()), &dependency_info);
        }

        // for static blas the global shared scratch is reused, no per-instance teardown
        // for refit-capable blas the per-instance scratch is kept alive
        if (!allow_update)
        {
            m_scratch_buffer      = nullptr;
            m_scratch_buffer_size = 0;

            // the compacted size is only known once the gpu has run the build, CompactBottomLevels picks it up later
            if (as_write_properties && as_copy)
            {
                lock_guard lock(compaction::mutex);
                CancelCompaction();
                const uint32_t query = compaction::acquire_query();
                if (query != UINT32_MAX)
                {
                    VkCommandBuffer cmd                  = static_cast<VkCommandBuffer>(cmd_list->GetRhiResource());
                    VkAccelerationStructureKHR as_handle = static_cast<VkAccelerationStructureKHR>(m_rhi_resource);
                    vkCmdResetQueryPool(cmd, compaction::query_pool, query, 1);
                    as_write_properties(cmd, 1, &as_handle, VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, compaction::query_pool, query);
                    m_compaction_query = query;
                    m_compaction_ready = RHI_CommandList::CapturePendingWork();
                    s_compaction_pending.push_back(this);
                }
            }
        }
    }

    void RHI_AccelerationStructure::CancelCompaction()
    {
        // caller holds compaction::mutex
        if (m_compaction_query == UINT32_MAX)
        {
            return;
        }

        compaction::free_queries.push_back(m_compaction_query);
        m_compaction_query = UINT32_MAX;
        m_compaction_ready = nullptr;
        erase(s_compaction_pending, this);
    }

    bool RHI_AccelerationStructure::CompactBottomLevels()
    {
        lock_guard lock(compaction::mutex);
        RHI_CommandList* cmd_list = RHI_Device::Cmd();
        if (s_compaction_pending.empty() || !cmd_list || !as_copy)
        {
            return false;
        }

        VkCommandBuffer cmd = static_cast<VkCommandBuffer>(cmd_list->GetRhiResource());
        VkDevice device     = static_cast<VkDevice>(RHI_Context::device);
        uint32_t copies     = 0;
        size_t retained     = 0;
        for (size_t i = 0; i < s_compaction_pending.size(); i++)
        {
            RHI_AccelerationStructure* blas = s_compaction_pending[i];
            if (copies >= compaction::copies_per_frame || !blas->m_compaction_ready || !blas->m_compaction_ready->IsComplete())
            {
                s_compaction_pending[retained++] = blas;
                continue;
            }

            // the build has retired, so the query holds this build's result and not a previous owner's
            uint64_t compacted_size = 0;
            const VkResult result   = vkGetQueryPoolResults(device, compaction::query_pool, blas->m_compaction_query, 1, sizeof(uint64_t), &compacted_size, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
            compaction::free_queries.push_back(blas->m_compaction_query);
            blas->m_compaction_query = UINT32_MAX;
            blas->m_compaction_ready = nullptr;

            if (result != VK_SUCCESS || compacted_size == 0)
            {
                compaction::not_ready++;
                continue;
            }

            // not worth a copy when the driver cannot shrink it meaningfully
            if (compacted_size + compacted_size / 10 >= blas->m_size)
            {
                compaction::not_smaller++;
                continue;
            }

            void* compacted_buffer = nullptr;
            VkBufferUsageFlags usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            RHI_Device::MemoryBufferCreate(compacted_buffer, compacted_size, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, nullptr, blas->m_object_name.c_str());
            if (!compacted_buffer)
            {
                continue;
            }

            VkAccelerationStructureCreateInfoKHR create_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
            create_info.buffer                               = static_cast<VkBuffer>(compacted_buffer);
            create_info.size                                 = compacted_size;
            create_info.type                                 = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            VkAccelerationStructureKHR compacted             = VK_NULL_HANDLE;
            if (as_create(device, &create_info, nullptr, &compacted) != VK_SUCCESS)
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, compacted_buffer);
                continue;
            }
            RHI_Device::SetResourceName(compacted, RHI_Resource_Type::AccelerationStructure, blas->m_object_name.c_str());

            VkCopyAccelerationStructureInfoKHR copy_info = { VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR };
            copy_info.src                                = static_cast<VkAccelerationStructureKHR>(blas->m_rhi_resource);
            copy_info.dst                                = compacted;
            copy_info.mode                               = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
            as_copy(cmd, &copy_info);

            // the previous tlas and this frame's copy still read the old storage, the deletion queue waits for both
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::AccelerationStructure, blas->m_rhi_resource);
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, blas->m_rhi_resource_results);

            compaction::bytes_before += blas->m_size;
            compaction::bytes_after  += compacted_size;
            compaction::compacted++;
            s_blas_bytes -= min(s_blas_bytes, blas->m_size);
            s_blas_bytes += compacted_size;

            blas->m_rhi_resource         = compacted;
            blas->m_rhi_resource_results = compacted_buffer;
            blas->m_size                 = compacted_size;
            blas->m_device_address       = 0;
            blas->GetDeviceAddress();
            copies++;
        }
        s_compaction_pending.resize(retained);

        if (s_compaction_pending.empty() && (compaction::compacted > 0 || compaction::not_ready > 0 || compaction::not_smaller > 0))
        {
            SP_LOG_INFO("Ray tracing: compacted %u BLAS from %.1f MB to %.1f MB (%u unreadable, %u not worth it), resident BLAS %.1f MB",
                compaction::compacted,
                static_cast<double>(compaction::bytes_before) / (1024.0 * 1024.0),
                static_cast<double>(compaction::bytes_after) / (1024.0 * 1024.0),
                compaction::not_ready,
                compaction::not_smaller,
                static_cast<double>(s_blas_bytes) / (1024.0 * 1024.0));
            compaction::bytes_before = 0;
            compaction::bytes_after  = 0;
            compaction::compacted    = 0;
            compaction::not_ready    = 0;
            compaction::not_smaller  = 0;
        }

        if (copies == 0)
        {
            return false;
        }
        s_compaction_generation++;

        // the tlas build and every ray query read the compacted copies
        {
            VkMemoryBarrier2 memory_barrier = {};
            memory_barrier.sType            = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            memory_barrier.srcStageMask     = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
            memory_barrier.srcAccessMask    = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
            memory_barrier.dstStageMask     = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            memory_barrier.dstAccessMask    = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT;

            VkDependencyInfo dependency_info   = {};
            dependency_info.sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency_info.memoryBarrierCount = 1;
            dependency_info.pMemoryBarriers    = &memory_barrier;

            vkCmdPipelineBarrier2(cmd, &dependency_info);
        }

        return true;
    }

    void RHI_AccelerationStructure::DestroyCompactionResources()
    {
        lock_guard lock(compaction::mutex);
        for (RHI_AccelerationStructure* blas : s_compaction_pending)
        {
            blas->m_compaction_query = UINT32_MAX;
            blas->m_compaction_ready = nullptr;
        }
        s_compaction_pending.clear();
        compaction::free_queries.clear();

        if (compaction::query_pool != VK_NULL_HANDLE)
        {
            vkDestroyQueryPool(static_cast<VkDevice>(RHI_Context::device), compaction::query_pool, nullptr);
            compaction::query_pool = VK_NULL_HANDLE;
        }
    }

    void RHI_AccelerationStructure::RefitBottomLevel(const vector<RHI_AccelerationStructureGeometry>& geometries, const vector<uint32_t>& primitive_counts)
    {
        RefitBottomLevel(RHI_Device::Cmd(), geometries, primitive_counts);
    }

    void RHI_AccelerationStructure::RefitBottomLevel(RHI_CommandList* cmd_list, const vector<RHI_AccelerationStructureGeometry>& geometries, const vector<uint32_t>& primitive_counts)
    {
        // Dynamic vertex uploads recorded earlier this frame must precede AS reads.
        cmd_list->SynchronizeResources(false);
        SP_ASSERT(m_type == RHI_AccelerationStructureType::Bottom);
        SP_ASSERT(m_allow_update && m_rhi_resource && m_scratch_buffer);
        SP_ASSERT(geometries.size() == primitive_counts.size());
        SP_ASSERT(!geometries.empty());

        // define geometry (same topology, updated vertex positions)
        vector<VkAccelerationStructureGeometryKHR> vk_geometries;
        vk_geometries.reserve(geometries.size());

        for (const RHI_AccelerationStructureGeometry& geo : geometries)
        {
            VkAccelerationStructureGeometryTrianglesDataKHR triangles_data = {};
            triangles_data.sType                                           = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            triangles_data.vertexFormat                                    = vulkan_format[static_cast<uint32_t>(geo.vertex_format)];
            triangles_data.vertexData.deviceAddress                        = geo.vertex_buffer_address;
            triangles_data.vertexStride                                    = geo.vertex_stride;
            triangles_data.maxVertex                                       = geo.max_vertex;
            triangles_data.indexType                                       = geo.index_format == RHI_Format::R32_Uint ? VK_INDEX_TYPE_UINT32 : (geo.index_format == RHI_Format::R16_Uint ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_NONE_KHR);
            triangles_data.indexData.deviceAddress                         = geo.index_buffer_address;
            triangles_data.transformData.deviceAddress                     = 0;

            VkAccelerationStructureGeometryKHR geometry = {};
            geometry.sType                              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            geometry.flags                              = geo.transparent ? 0 : VK_GEOMETRY_OPAQUE_BIT_KHR;
            geometry.geometryType                       = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geometry.geometry.triangles                 = triangles_data;

            vk_geometries.emplace_back(geometry);
        }

        VkAccelerationStructureKHR as_handle = static_cast<VkAccelerationStructureKHR>(m_rhi_resource);

        // in-place update: src and dst point to the same acceleration structure
        VkAccelerationStructureBuildGeometryInfoKHR build_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
        build_info.type                      = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        build_info.flags                     = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
        build_info.mode                      = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
        build_info.srcAccelerationStructure  = as_handle;
        build_info.dstAccelerationStructure  = as_handle;
        build_info.geometryCount             = static_cast<uint32_t>(vk_geometries.size());
        build_info.pGeometries               = vk_geometries.data();
        // align scratch device address, the buffer was overallocated to allow this
        const uint64_t scratch_alignment_refit = RHI_Device::PropertyGetMinAccelerationBufferOffsetAlignment();
        VkDeviceAddress scratch_base_refit     = RHI_Device::GetBufferDeviceAddress(m_scratch_buffer);
        build_info.scratchData.deviceAddress   = (scratch_base_refit + scratch_alignment_refit - 1) & ~(scratch_alignment_refit - 1);

        // build ranges
        vector<VkAccelerationStructureBuildRangeInfoKHR> range_infos(geometries.size());
        for (uint32_t i = 0; i < static_cast<uint32_t>(geometries.size()); ++i)
        {
            range_infos[i].primitiveCount  = primitive_counts[i];
            range_infos[i].primitiveOffset = 0;
            range_infos[i].firstVertex     = 0;
            range_infos[i].transformOffset = 0;
        }

        vector<VkAccelerationStructureBuildRangeInfoKHR*> p_range_infos;
        for (auto& range : range_infos) { p_range_infos.push_back(&range); }

        as_build(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()), 1, &build_info, p_range_infos.data());

        // barrier: ensure refit completes before use
        {
            VkMemoryBarrier2 memory_barrier = {};
            memory_barrier.sType            = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            memory_barrier.srcStageMask     = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
            memory_barrier.srcAccessMask    = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
            memory_barrier.dstStageMask     = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
            memory_barrier.dstAccessMask    = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT;

            VkDependencyInfo dependency_info   = {};
            dependency_info.sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency_info.memoryBarrierCount = 1;
            dependency_info.pMemoryBarriers    = &memory_barrier;

            vkCmdPipelineBarrier2(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()), &dependency_info);
        }
    }

    void RHI_AccelerationStructure::BuildTopLevel(const vector<RHI_AccelerationStructureInstance>& instances)
    {
        BuildTopLevel(RHI_Device::Cmd(), instances);
    }

    void RHI_AccelerationStructure::BuildTopLevel(RHI_CommandList* cmd_list, const vector<RHI_AccelerationStructureInstance>& instances)
    {
        SP_ASSERT(m_type == RHI_AccelerationStructureType::Top);
        SP_ASSERT(!instances.empty());

        // use double buffering to avoid frame-to-frame synchronization issues
        // while frame N's GPU is reading from buffer set 0, frame N+1's CPU writes to buffer set 1
        uint32_t buf_idx = m_buffer_index;
        m_buffer_index   = (m_buffer_index + 1) % buffer_count;

        uint32_t primitive_count = static_cast<uint32_t>(instances.size());
        uint32_t sized_count     = 256;
        while (sized_count < primitive_count)
        {
            uint32_t next = sized_count << 1;
            if (next < sized_count)
            {
                sized_count = primitive_count;
                break;
            }
            sized_count = next;
        }
    
        // define instances (static to avoid per-frame heap allocation - resize keeps capacity)
        static vector<VkAccelerationStructureInstanceKHR> vk_instances;
        vk_instances.resize(instances.size());
        for (size_t i = 0; i < instances.size(); ++i)
        {
            const RHI_AccelerationStructureInstance& instance = instances[i];
            auto& vk_inst                                     = vk_instances[i];
            vk_inst.instanceCustomIndex                       = instance.instance_custom_index;
            vk_inst.mask                                      = instance.mask;
            vk_inst.instanceShaderBindingTableRecordOffset    = instance.instance_shader_binding_table_record_offset;
            vk_inst.flags                                     = static_cast<VkGeometryInstanceFlagsKHR>(instance.flags);
            vk_inst.accelerationStructureReference            = instance.device_address;
            memcpy(&vk_inst.transform.matrix, instance.transform.data(), sizeof(float) * 12);
        }
    
        // reuse or create staging buffer for current frame
        const size_t data_size   = sizeof(VkAccelerationStructureInstanceKHR) * vk_instances.size();
        const size_t padded_size = sizeof(VkAccelerationStructureInstanceKHR) * sized_count;
        if (!m_staging_buffer[buf_idx] || data_size > m_staging_buffer_size[buf_idx])
        {
            if (m_staging_buffer[buf_idx])
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_staging_buffer[buf_idx]);
            }
            VkBufferUsageFlags staging_usage         = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            VkMemoryPropertyFlags staging_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            RHI_Device::MemoryBufferCreate(m_staging_buffer[buf_idx], padded_size, staging_usage, staging_properties, nullptr, (m_object_name + "_staging_" + to_string(buf_idx)).c_str());
            m_staging_buffer_size[buf_idx] = padded_size;
        }

        // copy data to staging buffer
        void* mapped_data = RHI_Device::MemoryGetMappedDataFromBuffer(m_staging_buffer[buf_idx]);
        memcpy(mapped_data, vk_instances.data(), data_size);
    
        // reuse or create instance buffer for current frame
        const uint64_t alignment = max(static_cast<uint64_t>(16), RHI_Device::PropertyGetMinStorageBufferOffsetAlignment());
        const size_t required_instance_size = padded_size + alignment - 1;
        if (!m_instance_buffer[buf_idx] || required_instance_size > m_instance_buffer_size[buf_idx])
        {
            if (m_instance_buffer[buf_idx])
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_instance_buffer[buf_idx]);
            }
            VkBufferUsageFlags instance_usage         = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
            VkMemoryPropertyFlags instance_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            RHI_Device::MemoryBufferCreate(m_instance_buffer[buf_idx], required_instance_size, instance_usage, instance_properties, nullptr, (m_object_name + "_instances_" + to_string(buf_idx)).c_str());
            m_instance_buffer_size[buf_idx] = required_instance_size;
        }
    
        // compute aligned offset
        VkDeviceAddress base_address    = RHI_Device::GetBufferDeviceAddress(m_instance_buffer[buf_idx]);
        VkDeviceAddress aligned_address = (base_address + alignment - 1) & ~(alignment - 1);
        uint64_t dst_offset             = aligned_address - base_address;
    
        // copy from staging to instance buffer at aligned offset
        VkBufferCopy region = {};
        region.size         = data_size;
        region.dstOffset    = dst_offset;
        vkCmdCopyBuffer(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()), static_cast<VkBuffer>(m_staging_buffer[buf_idx]), static_cast<VkBuffer>(m_instance_buffer[buf_idx]), 1, &region);
    
        // Make instance uploads and prior TLAS writes available to a refit, and
        // finish prior ray queries before overwriting this shared TLAS.
        // the as build stage reads instance data via shader read, not acceleration structure read
        {
            VkMemoryBarrier2 memory_barrier = {};
            memory_barrier.sType            = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            memory_barrier.srcStageMask     = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            memory_barrier.srcAccessMask    = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
            memory_barrier.dstStageMask     = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
            memory_barrier.dstAccessMask    = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;

            VkDependencyInfo dependency_info   = {};
            dependency_info.sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency_info.memoryBarrierCount = 1;
            dependency_info.pMemoryBarriers    = &memory_barrier;

            vkCmdPipelineBarrier2(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()), &dependency_info);
        }
    
        // build info
        VkAccelerationStructureBuildGeometryInfoKHR build_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
        build_info.type                                        = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        // Rigid motion can refit the existing hierarchy. Rebuild on count
        // changes and periodically to recover tree quality after sustained driving.
        build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
        build_info.geometryCount                               = 1;
        VkAccelerationStructureGeometryKHR geom                = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
        geom.geometryType                                      = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geom.geometry.instances                                = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
        geom.geometry.instances.arrayOfPointers                = VK_FALSE;
        geom.geometry.instances.data.deviceAddress             = aligned_address;
        build_info.pGeometries                                 = &geom;
    
        // query storage for a full build; select refit below after checking the existing allocation
        build_info.mode                     = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build_info.srcAccelerationStructure = VK_NULL_HANDLE;
        build_info.dstAccelerationStructure = VK_NULL_HANDLE;
    
        // size for the next power of two instance count so growth does not recreate the handle
        VkAccelerationStructureBuildSizesInfoKHR size_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        as_get_build_sizes(RHI_Context::device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, &sized_count, &size_info);
    
        // create or resize acceleration structure if needed
        if (!m_rhi_resource || size_info.accelerationStructureSize > m_size)
        {
            m_tlas_instance_count = 0;
            m_tlas_refit_count = 0;
            // drop only the as storage, instance buffers are already sized for this capacity
            if (m_rhi_resource)
            {
                RHI_Device::DescriptorSetInvalidateReferencingResource(this);
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::AccelerationStructure, m_rhi_resource);
                m_rhi_resource = nullptr;
            }
            if (m_rhi_resource_results)
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_rhi_resource_results);
                m_rhi_resource_results = nullptr;
            }

            // create result buffer
            VkBufferUsageFlags usage         = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            RHI_Device::MemoryBufferCreate(m_rhi_resource_results, size_info.accelerationStructureSize, usage, properties, nullptr, m_object_name.c_str());
    
            // create acceleration structure
            VkAccelerationStructureCreateInfoKHR create_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
            create_info.buffer                               = static_cast<VkBuffer>(m_rhi_resource_results);
            create_info.size                                 = size_info.accelerationStructureSize;
            create_info.type                                 = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
            as_create(RHI_Context::device, &create_info, nullptr, reinterpret_cast<VkAccelerationStructureKHR*>(&m_rhi_resource));
            m_device_address = 0;
            RHI_Device::SetResourceName(m_rhi_resource, RHI_Resource_Type::AccelerationStructure, m_object_name.c_str());
    
            m_size = size_info.accelerationStructureSize;
        }
    
        // a refit keeps the old hierarchy, it must not outlive the blas storage that compaction moved
        const bool refit = m_tlas_instance_count == primitive_count && m_tlas_refit_count < 60 && m_compaction_generation == s_compaction_generation;
        m_compaction_generation = s_compaction_generation;
        build_info.mode = refit ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build_info.srcAccelerationStructure = refit ? static_cast<VkAccelerationStructureKHR>(m_rhi_resource) : VK_NULL_HANDLE;
        build_info.dstAccelerationStructure = static_cast<VkAccelerationStructureKHR>(m_rhi_resource);
        m_tlas_instance_count = primitive_count;
        m_tlas_refit_count = refit ? m_tlas_refit_count + 1 : 0;
    
        // overallocated by alignment, vma does not guarantee the base satisfies minAccelerationStructureScratchOffsetAlignment
        const uint64_t scratch_alignment = RHI_Device::PropertyGetMinAccelerationBufferOffsetAlignment();
        uint64_t required_scratch_size   = max(size_info.buildScratchSize, size_info.updateScratchSize);
        required_scratch_size            = ((required_scratch_size + scratch_alignment - 1) & ~(scratch_alignment - 1)) + scratch_alignment;
        if (!m_scratch_buffer || required_scratch_size > m_scratch_buffer_size)
        {
            if (m_scratch_buffer)
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_scratch_buffer);
            }
            VkBufferUsageFlags usage         = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            RHI_Device::MemoryBufferCreate(m_scratch_buffer, required_scratch_size, usage, properties, nullptr, (m_object_name + "_scratch").c_str());
            m_scratch_buffer_size            = required_scratch_size;
        }
    
        // set up build with aligned scratch address
        VkDeviceAddress tlas_scratch_base    = RHI_Device::GetBufferDeviceAddress(m_scratch_buffer);
        build_info.scratchData.deviceAddress = (tlas_scratch_base + scratch_alignment - 1) & ~(scratch_alignment - 1);
    
        // build
        VkAccelerationStructureBuildRangeInfoKHR range_info       = {};
        range_info.primitiveCount                                 = primitive_count;
        VkAccelerationStructureBuildRangeInfoKHR* p_range_infos[] = { &range_info };
        as_build(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()), 1, &build_info, p_range_infos);
    
        // barrier: ensure build complete before use and before next frame's copy/build
        {
            VkMemoryBarrier2 memory_barrier = {};
            memory_barrier.sType            = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            memory_barrier.srcStageMask     = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
            memory_barrier.srcAccessMask    = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
            memory_barrier.dstStageMask     = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            memory_barrier.dstAccessMask    = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;

            VkDependencyInfo dependency_info   = {};
            dependency_info.sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency_info.memoryBarrierCount = 1;
            dependency_info.pMemoryBarriers    = &memory_barrier;

            vkCmdPipelineBarrier2(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()), &dependency_info);
        }
    }

    uint64_t RHI_AccelerationStructure::GetDeviceAddress()
    {
        if (!m_rhi_resource) return 0;
        if (m_device_address != 0) return m_device_address;
        VkAccelerationStructureDeviceAddressInfoKHR address_info = {};
        address_info.sType                                       = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        address_info.pNext                                       = nullptr;
        address_info.accelerationStructure                       = static_cast<VkAccelerationStructureKHR>(m_rhi_resource);

        m_device_address = as_get_device_address(static_cast<VkDevice>(RHI_Context::device), &address_info);
        return m_device_address;
    }
}
