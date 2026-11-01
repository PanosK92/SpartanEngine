/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =========================
#include "pch.h"
#include "../RHI_AccelerationStructure.h"
#include "../RHI_CommandList.h"
#include "../RHI_Device.h"
#include "Metal_Internal.h"
//====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        // the converter reaches a tlas through this header, see IRRaytracingAccelerationStructureGPUHeader
        struct AccelerationStructureHeader
        {
            uint64_t acceleration_structure_id      = 0;
            uint64_t instance_contributions_address = 0;
            uint64_t padding[4]                     = {};
            uint32_t padding_dispatch[3]            = {};
        };
        const uint64_t header_size = (sizeof(AccelerationStructureHeader) + 15) & ~15ull;

        MTL::Buffer* create_buffer(const uint64_t size, const bool shared, const string& name)
        {
            const MTL::ResourceOptions storage = shared ? MTL::ResourceStorageModeShared : MTL::ResourceStorageModePrivate;
            MTL::Buffer* buffer                = RHI_Context::device->newBuffer(max<uint64_t>(size, 16), storage | MTL::ResourceHazardTrackingModeUntracked);
            SP_ASSERT_MSG(buffer != nullptr, "Failed to create an acceleration structure buffer");
            metal_utility::set_label(buffer, name.c_str());
            metal_residency::Add(metal_residency::Group::Static, buffer);
            return buffer;
        }

        // drops the old buffer once the gpu is done with it and makes a bigger one
        void ensure_buffer(void*& buffer, uint64_t& size, const uint64_t size_required, const bool shared, const string& name)
        {
            if (buffer && size >= size_required)
                return;

            if (buffer)
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, buffer);
            }

            buffer = create_buffer(size_required, shared, name);
            size   = size_required;
        }

        uint64_t get_address(void* buffer)
        {
            return static_cast<MTL::Buffer*>(buffer)->gpuAddress();
        }

        MTL4::ComputeCommandEncoder* get_encoder(RHI_CommandList* cmd_list)
        {
            return metal_encoder::compute(cmd_list->GetRhiResource(), *static_cast<metal_encoder::State*>(cmd_list->GetRhiState()));
        }

        MTL4::PrimitiveAccelerationStructureDescriptor* create_primitive_descriptor(const vector<RHI_AccelerationStructureGeometry>& geometries, const vector<uint32_t>& primitive_counts, const bool allow_update)
        {
            NS::Object* triangles[64] = {};
            SP_ASSERT(geometries.size() <= 64);
            for (size_t i = 0; i < geometries.size(); i++)
            {
                const RHI_AccelerationStructureGeometry& geometry = geometries[i];
                SP_ASSERT(geometry.vertex_format == RHI_Format::R32G32B32_Float && geometry.index_format == RHI_Format::R32_Uint);

                MTL4::AccelerationStructureTriangleGeometryDescriptor* triangle = MTL4::AccelerationStructureTriangleGeometryDescriptor::alloc()->init();
                triangle->setVertexBuffer(MTL4::BufferRange::Make(geometry.vertex_buffer_address, static_cast<uint64_t>(geometry.max_vertex + 1) * geometry.vertex_stride));
                triangle->setVertexStride(geometry.vertex_stride);
                triangle->setVertexFormat(MTL::AttributeFormatFloat3);
                triangle->setIndexBuffer(MTL4::BufferRange::Make(geometry.index_buffer_address, static_cast<uint64_t>(primitive_counts[i]) * 3 * sizeof(uint32_t)));
                triangle->setIndexType(MTL::IndexTypeUInt32);
                triangle->setTriangleCount(primitive_counts[i]);
                triangle->setOpaque(!geometry.transparent);
                triangles[i] = triangle;
            }

            NS::Array* geometry_array                                  = NS::Array::alloc()->init(triangles, geometries.size());
            MTL4::PrimitiveAccelerationStructureDescriptor* descriptor = MTL4::PrimitiveAccelerationStructureDescriptor::alloc()->init();
            descriptor->setGeometryDescriptors(geometry_array);
            descriptor->setUsage(allow_update ? MTL::AccelerationStructureUsageRefit : MTL::AccelerationStructureUsageNone);
            geometry_array->release();

            for (size_t i = 0; i < geometries.size(); i++)
            {
                triangles[i]->release();
            }

            return descriptor;
        }

        // row major 3x4 to metal's column major 4x3
        MTL::PackedFloat4x3 to_packed_transform(const array<float, 12>& transform)
        {
            MTL::PackedFloat4x3 packed;
            for (uint32_t column = 0; column < 4; column++)
            {
                packed.columns[column] = MTL::PackedFloat3(transform[column], transform[4 + column], transform[8 + column]);
            }

            return packed;
        }
    }

    void* RHI_AccelerationStructure::s_blas_scratch_buffer         = nullptr;
    uint64_t RHI_AccelerationStructure::s_blas_scratch_buffer_size = 0;
    vector<RHI_AccelerationStructure*> RHI_AccelerationStructure::s_compaction_pending;
    uint64_t RHI_AccelerationStructure::s_blas_bytes               = 0;
    uint64_t RHI_AccelerationStructure::s_compaction_generation    = 0;

    // blas keep their worst case storage for now
    bool RHI_AccelerationStructure::CompactBottomLevels()
    {
        return false;
    }

    void RHI_AccelerationStructure::DestroyCompactionResources()
    {
    }

    void RHI_AccelerationStructure::CancelCompaction()
    {
    }

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
        m_object_name = name;
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
            s_blas_bytes  -= m_type == RHI_AccelerationStructureType::Bottom ? m_size : 0;
        }

        if (m_scratch_buffer)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_scratch_buffer);
            m_scratch_buffer      = nullptr;
            m_scratch_buffer_size = 0;
        }

        // on metal the instance buffers hold the instances and the staging buffers hold the headers shaders read
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

        m_size           = 0;
        m_device_address = 0;
    }

    void RHI_AccelerationStructure::BuildBottomLevel(const vector<RHI_AccelerationStructureGeometry>& geometries, const vector<uint32_t>& primitive_counts, bool allow_update)
    {
        BuildBottomLevel(RHI_Device::Cmd(), geometries, primitive_counts, allow_update);
    }

    void RHI_AccelerationStructure::BuildBottomLevel(RHI_CommandList* cmd_list, const vector<RHI_AccelerationStructureGeometry>& geometries, const vector<uint32_t>& primitive_counts, bool allow_update)
    {
        SP_ASSERT(m_type == RHI_AccelerationStructureType::Bottom);
        SP_ASSERT(geometries.size() == primitive_counts.size());
        SP_ASSERT(!geometries.empty());

        Destroy();
        m_allow_update = allow_update;

        MTL4::PrimitiveAccelerationStructureDescriptor* descriptor = create_primitive_descriptor(geometries, primitive_counts, allow_update);
        const MTL::AccelerationStructureSizes sizes                = RHI_Context::device->accelerationStructureSizes(descriptor);

        MTL::AccelerationStructure* acceleration_structure = RHI_Context::device->newAccelerationStructure(sizes.accelerationStructureSize);
        SP_ASSERT_MSG(acceleration_structure != nullptr, "Failed to create a bottom level acceleration structure");
        metal_utility::set_label(acceleration_structure, m_object_name.c_str());
        metal_residency::Add(metal_residency::Group::Static, acceleration_structure);
        m_rhi_resource   = acceleration_structure;
        m_size           = sizes.accelerationStructureSize;
        m_device_address = acceleration_structure->gpuResourceID()._impl;
        s_blas_bytes    += m_size;

        // static blas share one scratch, a refittable one keeps its own for the refits
        const uint64_t scratch_size = max(sizes.buildScratchBufferSize, sizes.refitScratchBufferSize);
        void*& scratch              = allow_update ? m_scratch_buffer : s_blas_scratch_buffer;
        uint64_t& scratch_capacity  = allow_update ? m_scratch_buffer_size : s_blas_scratch_buffer_size;
        ensure_buffer(scratch, scratch_capacity, scratch_size, false, m_object_name + "_scratch");

        get_encoder(cmd_list)->buildAccelerationStructure(acceleration_structure, descriptor, MTL4::BufferRange::Make(get_address(scratch), scratch_size));
        descriptor->release();
    }

    void RHI_AccelerationStructure::RefitBottomLevel(const vector<RHI_AccelerationStructureGeometry>& geometries, const vector<uint32_t>& primitive_counts)
    {
        RefitBottomLevel(RHI_Device::Cmd(), geometries, primitive_counts);
    }

    void RHI_AccelerationStructure::RefitBottomLevel(RHI_CommandList* cmd_list, const vector<RHI_AccelerationStructureGeometry>& geometries, const vector<uint32_t>& primitive_counts)
    {
        SP_ASSERT(m_type == RHI_AccelerationStructureType::Bottom);
        SP_ASSERT(m_allow_update && m_rhi_resource && m_scratch_buffer);
        SP_ASSERT(geometries.size() == primitive_counts.size());

        // refitting in place, the source and destination are the same structure
        MTL::AccelerationStructure* acceleration_structure         = static_cast<MTL::AccelerationStructure*>(m_rhi_resource);
        MTL4::PrimitiveAccelerationStructureDescriptor* descriptor = create_primitive_descriptor(geometries, primitive_counts, true);
        get_encoder(cmd_list)->refitAccelerationStructure(acceleration_structure, descriptor, acceleration_structure, MTL4::BufferRange::Make(get_address(m_scratch_buffer), m_scratch_buffer_size));
        descriptor->release();
    }

    void RHI_AccelerationStructure::BuildTopLevel(const vector<RHI_AccelerationStructureInstance>& instances)
    {
        BuildTopLevel(RHI_Device::Cmd(), instances);
    }

    void RHI_AccelerationStructure::BuildTopLevel(RHI_CommandList* cmd_list, const vector<RHI_AccelerationStructureInstance>& instances)
    {
        SP_ASSERT(m_type == RHI_AccelerationStructureType::Top);
        SP_ASSERT(!instances.empty());

        // one set of instances and header per frame in flight, so the cpu never writes what the gpu still reads
        const uint32_t index = m_buffer_index;
        m_buffer_index       = (m_buffer_index + 1) % buffer_count;

        // instances reference their blas by resource id, which is what GetDeviceAddress returns for a blas
        // the engine's instance flags share their bit values with metal's instance options
        const uint64_t instance_count = instances.size();
        const uint64_t instances_size = instance_count * sizeof(MTL::IndirectAccelerationStructureInstanceDescriptor);
        ensure_buffer(m_instance_buffer[index], m_instance_buffer_size[index], instances_size, true, m_object_name + "_instances");
        MTL::IndirectAccelerationStructureInstanceDescriptor* descriptors = static_cast<MTL::IndirectAccelerationStructureInstanceDescriptor*>(static_cast<MTL::Buffer*>(m_instance_buffer[index])->contents());

        // the header is followed by each instance's hit group offset, InstanceContributionToHitGroupIndex reads it
        ensure_buffer(m_staging_buffer[index], m_staging_buffer_size[index], header_size + instance_count * sizeof(uint32_t), true, m_object_name + "_header");
        uint8_t* header_data    = static_cast<uint8_t*>(static_cast<MTL::Buffer*>(m_staging_buffer[index])->contents());
        uint32_t* contributions = reinterpret_cast<uint32_t*>(header_data + header_size);

        for (uint64_t i = 0; i < instance_count; i++)
        {
            const RHI_AccelerationStructureInstance& instance = instances[i];
            MTL::IndirectAccelerationStructureInstanceDescriptor& descriptor = descriptors[i];
            descriptor.transformationMatrix            = to_packed_transform(instance.transform);
            descriptor.options                         = static_cast<MTL::AccelerationStructureInstanceOptions>(instance.flags & 0xF);
            descriptor.mask                            = instance.mask & 0xFF;
            descriptor.intersectionFunctionTableOffset = 0;
            descriptor.userID                          = instance.instance_custom_index & 0xFFFFFF;
            descriptor.accelerationStructureID._impl   = instance.device_address;
            contributions[i]                           = instance.instance_shader_binding_table_record_offset & 0xFFFFFF;
        }

        MTL4::InstanceAccelerationStructureDescriptor* descriptor = MTL4::InstanceAccelerationStructureDescriptor::alloc()->init();
        descriptor->setInstanceDescriptorBuffer(MTL4::BufferRange::Make(get_address(m_instance_buffer[index]), instances_size));
        descriptor->setInstanceDescriptorStride(sizeof(MTL::IndirectAccelerationStructureInstanceDescriptor));
        descriptor->setInstanceDescriptorType(MTL::AccelerationStructureInstanceDescriptorTypeIndirect);
        descriptor->setInstanceCount(instance_count);
        const MTL::AccelerationStructureSizes sizes = RHI_Context::device->accelerationStructureSizes(descriptor);

        // grows with the instance count, the old structure goes once frames using it are done
        if (!m_rhi_resource || sizes.accelerationStructureSize > m_size)
        {
            if (m_rhi_resource)
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::AccelerationStructure, m_rhi_resource);
            }

            MTL::AccelerationStructure* acceleration_structure = RHI_Context::device->newAccelerationStructure(sizes.accelerationStructureSize);
            SP_ASSERT_MSG(acceleration_structure != nullptr, "Failed to create a top level acceleration structure");
            metal_utility::set_label(acceleration_structure, m_object_name.c_str());
            metal_residency::Add(metal_residency::Group::Static, acceleration_structure);
            m_rhi_resource = acceleration_structure;
            m_size         = sizes.accelerationStructureSize;
        }
        ensure_buffer(m_scratch_buffer, m_scratch_buffer_size, sizes.buildScratchBufferSize, false, m_object_name + "_scratch");

        AccelerationStructureHeader header;
        header.acceleration_structure_id      = static_cast<MTL::AccelerationStructure*>(m_rhi_resource)->gpuResourceID()._impl;
        header.instance_contributions_address = get_address(m_staging_buffer[index]) + header_size;
        memcpy(header_data, &header, sizeof(header));
        m_device_address = get_address(m_staging_buffer[index]);

        get_encoder(cmd_list)->buildAccelerationStructure(static_cast<MTL::AccelerationStructure*>(m_rhi_resource), descriptor, MTL4::BufferRange::Make(get_address(m_scratch_buffer), sizes.buildScratchBufferSize));
        descriptor->release();
    }

    // a blas is referenced by its resource id, a tlas by the header converted shaders read it through
    uint64_t RHI_AccelerationStructure::GetDeviceAddress()
    {
        return m_device_address;
    }
}
