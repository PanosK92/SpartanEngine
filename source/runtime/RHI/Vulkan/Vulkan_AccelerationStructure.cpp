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

namespace spartan
{
    RHI_AccelerationStructure::RHI_AccelerationStructure(const RHI_AccelerationStructureType type, const char* name)
    {
        m_type        = type;
        m_object_name = name ? name : "acceleration_structure";
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
    }

    void RHI_AccelerationStructure::Build(RHI_CommandList* cmd_list, const vector<RHI_AccelerationStructureGeometry>& geometries, const vector<uint32_t>& primitive_counts)
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
        VkAccelerationStructureBuildSizesInfoKHR size_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        RHI_Device::GetAccelerationStructureBuildSizes(VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, primitive_counts.data(), &size_info);

        // create result buffer
        VkBufferUsageFlags usage         = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        RHI_Device::MemoryBufferCreate(m_rhi_resource_results, size_info.accelerationStructureSize, usage, properties, nullptr, m_object_name.c_str());
    
        // create acceleration structure
        VkAccelerationStructureCreateInfoKHR create_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
        create_info.buffer                               = static_cast<VkBuffer>(m_rhi_resource_results);
        create_info.size                                 = size_info.accelerationStructureSize;
        create_info.type                                 = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        RHI_Device::CreateAccelerationStructure(&create_info, nullptr, &m_rhi_resource);
        RHI_Device::SetResourceName(m_rhi_resource, RHI_Resource_Type::AccelerationStructure, m_object_name.c_str());

        // create scratch buffer
        void* scratch_buffer     = nullptr;
        const uint64_t alignment = RHI_Device::PropertyGetMinAccelerationBufferOffsetAlignment();
        uint64_t scratch_size    = (size_info.buildScratchSize + alignment - 1) & ~(alignment - 1); // align size
        usage                    = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
        properties               = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        RHI_Device::MemoryBufferCreate(
            scratch_buffer,
            scratch_size,
            usage,
            properties,
            nullptr,
            (m_object_name + "_scratch").c_str()
        );

        // set up build
        build_info.dstAccelerationStructure  = static_cast<VkAccelerationStructureKHR>(m_rhi_resource);
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
        RHI_Device::BuildAccelerationStructures(static_cast<void*>(cmd_list->GetRhiResource()), 1, &build_info, p_range_infos.data());
    
        // barrier
        VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        barrier.srcAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(
            static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()),
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            0, 1, &barrier, 0, nullptr, 0, nullptr
        );

        // destroy temp buffer
        RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, scratch_buffer);
    }

    void RHI_AccelerationStructure::Build(RHI_CommandList* cmd_list, const vector<RHI_AccelerationStructureInstance>& instances)
    {
        SP_ASSERT(m_type == RHI_AccelerationStructureType::Top);
        SP_ASSERT(!instances.empty());

        // define instances
        vector<VkAccelerationStructureInstanceKHR> vk_instances(instances.size());
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

        // create staging buffer
        void* staging_buffer                     = nullptr;
        const size_t data_size                   = sizeof(VkAccelerationStructureInstanceKHR) * vk_instances.size();
        VkBufferUsageFlags staging_usage         = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VkMemoryPropertyFlags staging_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        RHI_Device::MemoryBufferCreate(staging_buffer, data_size, staging_usage, staging_properties, vk_instances.data(), (m_object_name + "_staging").c_str());

        // create instance buffer (device local, no host visible)
        void* instance_buffer                     = nullptr;
        const uint64_t alignment                  = max(static_cast<uint64_t>(16), RHI_Device::PropertyGetMinStorageBufferOffsetAlignment());
        const size_t buffer_size                  = data_size + alignment - 1; // pad for alignment
        VkBufferUsageFlags instance_usage         = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
        VkMemoryPropertyFlags instance_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        RHI_Device::MemoryBufferCreate(instance_buffer, buffer_size, instance_usage, instance_properties, nullptr, (m_object_name + "_instances").c_str());

        // compute aligned offset
        VkDeviceAddress base_address    = RHI_Device::GetBufferDeviceAddress(instance_buffer);
        VkDeviceAddress aligned_address = (base_address + alignment - 1) & ~(alignment - 1);
        uint64_t dst_offset             = aligned_address - base_address;

        // copy from staging to instance buffer at aligned offset
        VkBufferCopy region = {};
        region.size         = data_size;
        region.dstOffset    = dst_offset;
        vkCmdCopyBuffer(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()), static_cast<VkBuffer>(staging_buffer), static_cast<VkBuffer>(instance_buffer), 1, &region);

        // barrier: make copy available for build
        VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        barrier.srcAccessMask   = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask   = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(
            static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()),
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            0, 1, &barrier, 0, nullptr, 0, nullptr
        );

        Destroy(); // moved here to avoid recreating unnecessarily, but still rebuild for now

        // build info
        VkAccelerationStructureBuildGeometryInfoKHR build_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
        build_info.type                                        = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        build_info.flags                                       = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
        build_info.mode                                        = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build_info.geometryCount                               = 1;
        VkAccelerationStructureGeometryKHR geom                = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
        geom.geometryType                                      = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geom.geometry.instances                                = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
        geom.geometry.instances.arrayOfPointers                = VK_FALSE;
        geom.geometry.instances.data.deviceAddress             = aligned_address;
        build_info.pGeometries                                 = &geom;

        // get build sizes
        uint32_t primitive_count                           = static_cast<uint32_t>(instances.size());
        VkAccelerationStructureBuildSizesInfoKHR size_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        RHI_Device::GetAccelerationStructureBuildSizes(VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, &primitive_count, &size_info);

        // create result buffer
        VkBufferUsageFlags usage         = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        RHI_Device::MemoryBufferCreate(m_rhi_resource_results, size_info.accelerationStructureSize, usage, properties, nullptr, m_object_name.c_str());

        // create acceleration structure
        VkAccelerationStructureCreateInfoKHR create_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
        create_info.buffer                               = static_cast<VkBuffer>(m_rhi_resource_results);
        create_info.size                                 = size_info.accelerationStructureSize;
        create_info.type                                 = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        RHI_Device::CreateAccelerationStructure(&create_info, nullptr, &m_rhi_resource);
        RHI_Device::SetResourceName(m_rhi_resource, RHI_Resource_Type::AccelerationStructure, m_object_name.c_str());

        // create scratch buffer
        void* scratch_buffer             = nullptr;
        const uint64_t scratch_alignment = RHI_Device::PropertyGetMinAccelerationBufferOffsetAlignment();
        uint64_t scratch_size            = (size_info.buildScratchSize + scratch_alignment - 1) & ~(scratch_alignment - 1);
        usage                            = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
        RHI_Device::MemoryBufferCreate(scratch_buffer, scratch_size, usage, properties, nullptr, (m_object_name + "_scratch").c_str());

        // set up build
        build_info.dstAccelerationStructure  = static_cast<VkAccelerationStructureKHR>(m_rhi_resource);
        build_info.scratchData.deviceAddress = RHI_Device::GetBufferDeviceAddress(scratch_buffer);

        // build
        VkAccelerationStructureBuildRangeInfoKHR range_info       = {};
        range_info.primitiveCount                                 = primitive_count;
        VkAccelerationStructureBuildRangeInfoKHR* p_range_infos[] = { &range_info };
        RHI_Device::BuildAccelerationStructures(static_cast<void*>(cmd_list->GetRhiResource()), 1, &build_info, p_range_infos);

        // barrier: ensure build complete
        barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(
            static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()),
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            0, 1, &barrier, 0, nullptr, 0, nullptr
        );

        // destroy temp buffers
        RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, staging_buffer);
        RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, instance_buffer);
        RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, scratch_buffer);
    }
}
