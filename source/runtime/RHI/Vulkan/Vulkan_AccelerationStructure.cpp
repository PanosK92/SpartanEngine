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
        if (m_rhi_resource)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::AccelerationStructure, m_rhi_resource);
            m_rhi_resource = nullptr;
        }

        if (m_result_buffer)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_result_buffer);
            m_result_buffer = nullptr;
        }
    }

    void RHI_AccelerationStructure::CreateResultBuffer(uint64_t result_size)
    {
        VkBufferUsageFlags usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        RHI_Device::MemoryBufferCreate(m_result_buffer, result_size, usage, properties, nullptr, m_object_name.c_str());
    
        VkAccelerationStructureCreateInfoKHR create_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
        create_info.buffer = static_cast<VkBuffer>(m_result_buffer);
        create_info.size   = result_size;
        create_info.type   = (m_type == RHI_AccelerationStructureType::Bottom) ? VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR : VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    
        int result = RHI_Device::CreateAccelerationStructureKHR(RHI_Context::device, &create_info, nullptr, &m_rhi_resource);
        SP_ASSERT(result == 0); // VK_SUCCESS is 0
    
        struct BufferDeviceAddressInfo
        {
            uint32_t sType = static_cast<uint32_t>(VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO);
            void* pNext = nullptr;
            void* buffer;
        } info;
        info.buffer = m_result_buffer;
        m_device_address = RHI_Device::GetBufferDeviceAddress(RHI_Context::device, &info);
    
        RHI_Device::SetResourceName(m_rhi_resource, RHI_Resource_Type::AccelerationStructure, m_object_name.c_str());
    }

    void RHI_AccelerationStructure::CreateScratchBuffer(uint64_t scratch_size)
    {
        VkBufferUsageFlags usage         = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        RHI_Device::MemoryBufferCreate(m_scratch_buffer, scratch_size, usage, properties, nullptr, (m_object_name + "_scratch").c_str());
    }

    void RHI_AccelerationStructure::Build(RHI_CommandList* cmd_list, const vector<RHI_AccelerationStructureGeometry>& geometries, const vector<uint32_t>& primitive_counts)
    {
        SP_ASSERT(m_type == RHI_AccelerationStructureType::Bottom);
        SP_ASSERT(geometries.size() == primitive_counts.size());
        SP_ASSERT(!geometries.empty());
    
        vector<VkAccelerationStructureGeometryKHR> vk_geometries;
        vk_geometries.reserve(geometries.size());
    
        for (const auto& geo : geometries)
        {
            VkAccelerationStructureGeometryKHR vk_geo             = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
            vk_geo.flags                                          = static_cast<VkGeometryFlagsKHR>(geo.flags);
            vk_geo.geometryType                                   = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            vk_geo.geometry.triangles                             = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR };
            vk_geo.geometry.triangles.vertexFormat                = static_cast<VkFormat>(geo.vertex_format);
            vk_geo.geometry.triangles.vertexData.deviceAddress    = geo.vertex_buffer_address;
            vk_geo.geometry.triangles.vertexStride                = geo.vertex_stride;
            vk_geo.geometry.triangles.maxVertex                   = geo.max_vertex;
            vk_geo.geometry.triangles.indexType                   = geo.index_format == RHI_Format::R32_Uint ? VK_INDEX_TYPE_UINT32 : (geo.index_format == RHI_Format::R16_Uint ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_NONE_KHR);
            vk_geo.geometry.triangles.indexData.deviceAddress     = geo.index_buffer_address;
            vk_geo.geometry.triangles.transformData.deviceAddress = geo.transform_buffer_address;
            vk_geometries.push_back(vk_geo);
        }
    
        VkAccelerationStructureBuildGeometryInfoKHR build_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
        build_info.type                                        = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        build_info.flags                                       = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        build_info.mode                                        = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build_info.geometryCount                               = static_cast<uint32_t>(vk_geometries.size());
        build_info.pGeometries                                 = vk_geometries.data();
    
        VkAccelerationStructureBuildSizesInfoKHR size_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        RHI_Device::GetAccelerationStructureBuildSizesKHR(RHI_Context::device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, primitive_counts.data(), &size_info);
    
        CreateResultBuffer(size_info.accelerationStructureSize);
        CreateScratchBuffer(size_info.buildScratchSize);
    
        build_info.dstAccelerationStructure = static_cast<VkAccelerationStructureKHR>(m_rhi_resource);
    
        struct BufferDeviceAddressInfo
        {
            uint32_t sType = static_cast<uint32_t>(VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO);
            void* pNext = nullptr;
            void* buffer;
        } info;
        info.buffer = m_scratch_buffer;
        build_info.scratchData.deviceAddress = RHI_Device::GetBufferDeviceAddress(RHI_Context::device, &info);
    
        vector<VkAccelerationStructureBuildRangeInfoKHR> range_infos(geometries.size());
        for (uint32_t i = 0; i < static_cast<uint32_t>(geometries.size()); ++i)
        {
            range_infos[i].primitiveCount = primitive_counts[i];
            range_infos[i].primitiveOffset = 0;
            range_infos[i].firstVertex = 0;
            range_infos[i].transformOffset = 0;
        }
    
        vector<VkAccelerationStructureBuildRangeInfoKHR*> p_range_infos;
        for (auto& range : range_infos) { p_range_infos.push_back(&range); }
    
        RHI_Device::CmdBuildAccelerationStructuresKHR(static_cast<void*>(cmd_list->GetRhiResource()), 1, &build_info, p_range_infos.data());
    
        // Barrier
        VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        barrier.srcAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()), VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    
        // Destroy scratch
        RHI_Device::MemoryBufferDestroy(m_scratch_buffer);
        m_scratch_buffer = nullptr;
    }

    void RHI_AccelerationStructure::Build(RHI_CommandList* cmd_list, const vector<RHI_AccelerationStructureInstance>& instances)
    {
        SP_ASSERT(m_type == RHI_AccelerationStructureType::Top);
        SP_ASSERT(!instances.empty());
    
        vector<VkAccelerationStructureInstanceKHR> vk_instances(instances.size());
        for (size_t i = 0; i < instances.size(); ++i)
        {
            const auto& inst                               = instances[i];
            auto& vk_inst                                  = vk_instances[i];
            memcpy(&vk_inst.transform.matrix, inst.transform.data(), sizeof(float) * 12); // row-major, no transpose needed
            vk_inst.instanceCustomIndex                    = inst.instance_custom_index;
            vk_inst.mask                                   = inst.mask;
            vk_inst.instanceShaderBindingTableRecordOffset = inst.instance_shader_binding_table_record_offset;
            vk_inst.flags                                  = static_cast<VkGeometryInstanceFlagsKHR>(inst.flags);
            vk_inst.accelerationStructureReference         = inst.acceleration_structure_reference;
        }
    
        // Upload instances to buffer
        size_t instance_size = sizeof(VkAccelerationStructureInstanceKHR) * vk_instances.size();
        VkBufferUsageFlags instance_usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VkMemoryPropertyFlags instance_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        RHI_Device::MemoryBufferCreate(m_instance_buffer, instance_size, instance_usage, instance_properties, vk_instances.data(), (m_object_name + "_instances").c_str());
    
        VkAccelerationStructureBuildGeometryInfoKHR build_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
        build_info.type                                        = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        build_info.flags                                       = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
        build_info.mode                                        = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build_info.geometryCount                               = 1;

        VkAccelerationStructureGeometryKHR geom = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
        geom.geometryType                       = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geom.geometry.instances                 = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
        geom.geometry.instances.arrayOfPointers = VK_FALSE;
    
        struct BufferDeviceAddressInfo
        {
            uint32_t sType = static_cast<uint32_t>(VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO);
            void* pNext = nullptr;
            void* buffer;
        } instance_info;
        instance_info.buffer = m_instance_buffer;
        geom.geometry.instances.data.deviceAddress = RHI_Device::GetBufferDeviceAddress(RHI_Context::device, &instance_info);
    
        build_info.pGeometries = &geom;
    
        uint32_t primitive_count = static_cast<uint32_t>(instances.size());
        VkAccelerationStructureBuildSizesInfoKHR size_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        RHI_Device::GetAccelerationStructureBuildSizesKHR(RHI_Context::device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, &primitive_count, &size_info);
    
        CreateResultBuffer(size_info.accelerationStructureSize);
        CreateScratchBuffer(size_info.buildScratchSize);
    
        build_info.dstAccelerationStructure = static_cast<VkAccelerationStructureKHR>(m_rhi_resource);
    
        struct BufferDeviceAddressInfo scratch_info;
        scratch_info.sType = static_cast<uint32_t>(VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO);
        scratch_info.pNext = nullptr;
        scratch_info.buffer = m_scratch_buffer;
        build_info.scratchData.deviceAddress = RHI_Device::GetBufferDeviceAddress(RHI_Context::device, &scratch_info);
    
        VkAccelerationStructureBuildRangeInfoKHR range_info = {};
        range_info.primitiveCount = primitive_count;
        VkAccelerationStructureBuildRangeInfoKHR* p_range_infos[] = { &range_info };
    
        RHI_Device::CmdBuildAccelerationStructuresKHR(static_cast<void*>(cmd_list->GetRhiResource()), 1, &build_info, p_range_infos);
    
        // Barrier
        VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()), VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    
        // Destroy temporaries
        RHI_Device::MemoryBufferDestroy(m_scratch_buffer);
        m_scratch_buffer = nullptr;
        RHI_Device::MemoryBufferDestroy(m_instance_buffer);
        m_instance_buffer = nullptr;
    }
}
