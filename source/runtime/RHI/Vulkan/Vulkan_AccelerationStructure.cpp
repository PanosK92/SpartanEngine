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

//= INCLUDES ============================
#include "pch.h"
#include "../RHI_AccelerationStructure.h"
#include "../RHI_Device.h"
#include "../RHI_Implementation.h"
#include "../RHI_CommandList.h"
//=======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace
{
    PFN_vkGetAccelerationStructureBuildSizesKHR    as_get_build_sizes    = nullptr;
    PFN_vkCreateAccelerationStructureKHR           as_create             = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR        as_build              = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR as_get_device_address = nullptr;
}

namespace spartan
{
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
        }
    }

    RHI_AccelerationStructure::~RHI_AccelerationStructure()
    {
        Destroy();
    }

    void RHI_AccelerationStructure::Destroy()
    {
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

        if (m_instance_buffer)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_instance_buffer);
            m_instance_buffer      = nullptr;
            m_instance_buffer_size = 0;
        }

        if (m_staging_buffer)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_staging_buffer);
            m_staging_buffer      = nullptr;
            m_staging_buffer_size = 0;
        }

        m_size = 0;
    }

    void RHI_AccelerationStructure::BuildBottomLevel(RHI_CommandList* cmd_list, const vector<RHI_AccelerationStructureGeometry>& geometries, const vector<uint32_t>& primitive_counts)
    {
        SP_ASSERT(m_type == RHI_AccelerationStructureType::Bottom);
        SP_ASSERT(geometries.size() == primitive_counts.size());
        SP_ASSERT(!geometries.empty());

        Destroy();

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
        build_info.mode                                        = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build_info.geometryCount                               = static_cast<uint32_t>(vk_geometries.size());
        build_info.pGeometries                                 = vk_geometries.data();

        // get build sizes
        VkDevice device = static_cast<VkDevice>(RHI_Context::device);

        VkAccelerationStructureBuildSizesInfoKHR size_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        as_get_build_sizes(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, primitive_counts.data(), &size_info);

        // create result buffer
        VkBufferUsageFlags usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        RHI_Device::MemoryBufferCreate(m_rhi_resource_results, size_info.accelerationStructureSize, usage, properties, nullptr, m_object_name.c_str());

        // create acceleration structure
        VkAccelerationStructureCreateInfoKHR create_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
        create_info.buffer = static_cast<VkBuffer>(m_rhi_resource_results);
        create_info.size = size_info.accelerationStructureSize;
        create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        as_create(device, &create_info, nullptr, reinterpret_cast<VkAccelerationStructureKHR*>(&m_rhi_resource));
        RHI_Device::SetResourceName(m_rhi_resource, RHI_Resource_Type::AccelerationStructure, m_object_name.c_str());

        // create scratch buffer
        void* scratch_buffer = nullptr;
        const uint64_t alignment = RHI_Device::PropertyGetMinAccelerationBufferOffsetAlignment();
        uint64_t scratch_size = (size_info.buildScratchSize + alignment - 1) & ~(alignment - 1); // align size
        usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        RHI_Device::MemoryBufferCreate(
            scratch_buffer,
            scratch_size,
            usage,
            properties,
            nullptr,
            (m_object_name + "_scratch").c_str()
        );

        // set up build
        build_info.dstAccelerationStructure = static_cast<VkAccelerationStructureKHR>(m_rhi_resource);
        build_info.scratchData.deviceAddress = RHI_Device::GetBufferDeviceAddress(scratch_buffer);

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

        // barrier: ensure build completes before use
        VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        barrier.srcAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(
            static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()),
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            0, 1, &barrier, 0, nullptr, 0, nullptr
        );

        // destroy temp buffer
        RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, scratch_buffer);
    }

    void RHI_AccelerationStructure::BuildTopLevel(RHI_CommandList* cmd_list, const vector<RHI_AccelerationStructureInstance>& instances)
    {
        SP_ASSERT(m_type == RHI_AccelerationStructureType::Top);
        SP_ASSERT(!instances.empty());
    
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
    
        // reuse or create staging buffer
        const size_t data_size = sizeof(VkAccelerationStructureInstanceKHR) * vk_instances.size();
        if (!m_staging_buffer || data_size > m_staging_buffer_size)
        {
            if (m_staging_buffer)
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_staging_buffer);
            }
            VkBufferUsageFlags staging_usage         = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            VkMemoryPropertyFlags staging_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            RHI_Device::MemoryBufferCreate(m_staging_buffer, data_size, staging_usage, staging_properties, nullptr, (m_object_name + "_staging").c_str());
            m_staging_buffer_size = data_size;
        }

        // copy data to staging buffer
        void* mapped_data = RHI_Device::MemoryGetMappedDataFromBuffer(m_staging_buffer);
        memcpy(mapped_data, vk_instances.data(), data_size);
    
        // reuse or create instance buffer
        const uint64_t alignment = max(static_cast<uint64_t>(16), RHI_Device::PropertyGetMinStorageBufferOffsetAlignment());
        const size_t required_instance_size = data_size + alignment - 1; // pad for alignment
        if (!m_instance_buffer || required_instance_size > m_instance_buffer_size)
        {
            if (m_instance_buffer)
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_instance_buffer);
            }
            VkBufferUsageFlags instance_usage         = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
            VkMemoryPropertyFlags instance_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            RHI_Device::MemoryBufferCreate(m_instance_buffer, required_instance_size, instance_usage, instance_properties, nullptr, (m_object_name + "_instances").c_str());
            m_instance_buffer_size = required_instance_size;
        }
    
        // compute aligned offset
        VkDeviceAddress base_address    = RHI_Device::GetBufferDeviceAddress(m_instance_buffer);
        VkDeviceAddress aligned_address = (base_address + alignment - 1) & ~(alignment - 1);
        uint64_t dst_offset             = aligned_address - base_address;
    
        // copy from staging to instance buffer at aligned offset
        VkBufferCopy region = {};
        region.size         = data_size;
        region.dstOffset    = dst_offset;
        vkCmdCopyBuffer(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()), static_cast<VkBuffer>(m_staging_buffer), static_cast<VkBuffer>(m_instance_buffer), 1, &region);
    
        // barrier: make copy available for build
        VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        barrier.srcAccessMask   = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(
            static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()),
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            0, 1, &barrier, 0, nullptr, 0, nullptr
        );
    
        // build info
        VkAccelerationStructureBuildGeometryInfoKHR build_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
        build_info.type                                        = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        build_info.flags                                       = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
        build_info.geometryCount                               = 1;
        VkAccelerationStructureGeometryKHR geom                = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
        geom.geometryType                                      = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geom.geometry.instances                                = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
        geom.geometry.instances.arrayOfPointers                = VK_FALSE;
        geom.geometry.instances.data.deviceAddress             = aligned_address;
        build_info.pGeometries                                 = &geom;
    
        // always use full build mode - tlas updates can produce degenerate bvh when transforms change significantly
        uint32_t primitive_count            = static_cast<uint32_t>(instances.size());
        build_info.mode                     = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build_info.srcAccelerationStructure = VK_NULL_HANDLE;
        build_info.dstAccelerationStructure = VK_NULL_HANDLE;
    
        // get build sizes
        VkAccelerationStructureBuildSizesInfoKHR size_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        as_get_build_sizes(RHI_Context::device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, &primitive_count, &size_info);
    
        // create or resize acceleration structure if needed
        if (!m_rhi_resource || size_info.accelerationStructureSize > m_size)
        {
            // destroy old resources if they exist
            if (m_rhi_resource)
                Destroy();

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
            RHI_Device::SetResourceName(m_rhi_resource, RHI_Resource_Type::AccelerationStructure, m_object_name.c_str());
    
            m_size = size_info.accelerationStructureSize;
        }
    
        // update dst
        build_info.dstAccelerationStructure = static_cast<VkAccelerationStructureKHR>(m_rhi_resource);
    
        // reuse or create scratch buffer
        const uint64_t scratch_alignment = RHI_Device::PropertyGetMinAccelerationBufferOffsetAlignment();
        uint64_t required_scratch_size   = size_info.buildScratchSize;
        required_scratch_size            = (required_scratch_size + scratch_alignment - 1) & ~(scratch_alignment - 1);
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
    
        // set up build
        build_info.scratchData.deviceAddress = RHI_Device::GetBufferDeviceAddress(m_scratch_buffer);
    
        // build
        VkAccelerationStructureBuildRangeInfoKHR range_info       = {};
        range_info.primitiveCount                                 = primitive_count;
        VkAccelerationStructureBuildRangeInfoKHR* p_range_infos[] = { &range_info };
        as_build(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()), 1, &build_info, p_range_infos);
    
        // barrier: ensure build complete before use and before next frame's copy/build
        barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        vkCmdPipelineBarrier(
            static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()),
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 1, &barrier, 0, nullptr, 0, nullptr
        );
    }

    uint64_t RHI_AccelerationStructure::GetDeviceAddress()
    {
        VkAccelerationStructureDeviceAddressInfoKHR address_info = {};
        address_info.sType                                       = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        address_info.pNext                                       = nullptr;
        address_info.accelerationStructure                       = static_cast<VkAccelerationStructureKHR>(m_rhi_resource);

        return as_get_device_address(static_cast<VkDevice>(RHI_Context::device), &address_info);
    }
}
