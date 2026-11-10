/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =============================
#include "pch.h"
#include "../RHI_AccelerationStructure.h"
#include "../RHI_SyncPrimitive.h"
//========================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    // webgpu has no ray tracing, acceleration structures never get a resource so nothing traces against them
    void* RHI_AccelerationStructure::s_blas_scratch_buffer                       = nullptr;
    uint64_t RHI_AccelerationStructure::s_blas_scratch_buffer_size               = 0;
    vector<RHI_AccelerationStructure*> RHI_AccelerationStructure::s_compaction_pending;
    uint64_t RHI_AccelerationStructure::s_blas_bytes                             = 0;
    uint64_t RHI_AccelerationStructure::s_compaction_generation                  = 0;

    RHI_AccelerationStructure::RHI_AccelerationStructure(const RHI_AccelerationStructureType type, const char* name)
    {
        m_type        = type;
        m_object_name = name ? name : "";
    }

    RHI_AccelerationStructure::~RHI_AccelerationStructure()
    {
        Destroy();
    }

    void RHI_AccelerationStructure::Destroy()
    {
        m_rhi_resource   = nullptr;
        m_device_address = 0;
    }

    void RHI_AccelerationStructure::CancelCompaction()
    {
    }

    void RHI_AccelerationStructure::BuildBottomLevel(const vector<RHI_AccelerationStructureGeometry>& geometries, const vector<uint32_t>& primitive_counts, bool allow_update)
    {
    }

    void RHI_AccelerationStructure::BuildBottomLevel(RHI_CommandList* cmd_list, const vector<RHI_AccelerationStructureGeometry>& geometries, const vector<uint32_t>& primitive_counts, bool allow_update)
    {
    }

    void RHI_AccelerationStructure::RefitBottomLevel(const vector<RHI_AccelerationStructureGeometry>& geometries, const vector<uint32_t>& primitive_counts)
    {
    }

    void RHI_AccelerationStructure::RefitBottomLevel(RHI_CommandList* cmd_list, const vector<RHI_AccelerationStructureGeometry>& geometries, const vector<uint32_t>& primitive_counts)
    {
    }

    void RHI_AccelerationStructure::BuildTopLevel(const vector<RHI_AccelerationStructureInstance>& instances)
    {
    }

    void RHI_AccelerationStructure::BuildTopLevel(RHI_CommandList* cmd_list, const vector<RHI_AccelerationStructureInstance>& instances)
    {
    }

    uint64_t RHI_AccelerationStructure::GetDeviceAddress()
    {
        return 0;
    }

    void RHI_AccelerationStructure::FreeSharedBlasScratch()
    {
    }

    bool RHI_AccelerationStructure::CompactBottomLevels()
    {
        return false;
    }

    void RHI_AccelerationStructure::DestroyCompactionResources()
    {
    }
}
