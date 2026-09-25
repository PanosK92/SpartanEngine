/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =====================
#include "../core/SpartanObject.h"
#include "RHI_Definitions.h"
#include <vector>
#include <array>
#include <memory>
//================================

namespace spartan
{
    struct RHI_PendingWork;

   enum class RHI_AccelerationStructureType
    {
        Bottom,
        Top,
        Max
    };

    struct RHI_AccelerationStructureGeometry
    {
        bool transparent                  = false;
        RHI_Format vertex_format          = RHI_Format::Max;
        uint64_t vertex_buffer_address    = 0;
        uint32_t vertex_stride            = 0;
        uint32_t max_vertex               = 0;
        RHI_Format index_format           = RHI_Format::Max;
        uint64_t index_buffer_address     = 0;
    };

    struct RHI_AccelerationStructureInstance
    {
        std::array<float, 12> transform                      = {}; // row-major 3x4 matrix
        uint32_t instance_custom_index                       = 0;
        uint32_t mask                                        = 0xFF;
        uint32_t instance_shader_binding_table_record_offset = 0;
        uint32_t flags                                       = 0;
        uint64_t device_address                              = 0;
    };

    class RHI_AccelerationStructure : public SpartanObject
    {
    public:
        RHI_AccelerationStructure(const RHI_AccelerationStructureType type, const char* name);
        ~RHI_AccelerationStructure();

        void BuildBottomLevel(const std::vector<RHI_AccelerationStructureGeometry>& geometries, const std::vector<uint32_t>& primitive_counts, bool allow_update = false);
        void BuildBottomLevel(RHI_CommandList* cmd_list, const std::vector<RHI_AccelerationStructureGeometry>& geometries, const std::vector<uint32_t>& primitive_counts, bool allow_update = false);
        void RefitBottomLevel(const std::vector<RHI_AccelerationStructureGeometry>& geometries, const std::vector<uint32_t>& primitive_counts);
        void RefitBottomLevel(RHI_CommandList* cmd_list, const std::vector<RHI_AccelerationStructureGeometry>& geometries, const std::vector<uint32_t>& primitive_counts);
        void BuildTopLevel(const std::vector<RHI_AccelerationStructureInstance>& instances);
        void BuildTopLevel(RHI_CommandList* cmd_list, const std::vector<RHI_AccelerationStructureInstance>& instances);

        // misc
        uint64_t GetDeviceAddress();
        void* GetRhiResource() const                  { return m_rhi_resource; }
        RHI_AccelerationStructureType GetType() const { return m_type; }
        bool CanRefit() const                         { return m_allow_update && m_rhi_resource; }

        // releases the global shared scratch buffer used by static blas builds
        // call after a build burst completes to reclaim that memory
        static void FreeSharedBlasScratch();

        // static blas are built with worst case storage, once the gpu has finished a build this copies it
        // into storage sized to the compacted result (roughly half), returns true when any blas device address
        // changed so the tlas has to be rebuilt against the new addresses
        static bool CompactBottomLevels();
        static void DestroyCompactionResources();
        static uint64_t GetBottomLevelBytes() { return s_blas_bytes; }

    private:
        void Destroy();
        void CancelCompaction();

        // misc
        RHI_AccelerationStructureType m_type = RHI_AccelerationStructureType::Max;
        uint64_t m_size                      = 0;
        uint64_t m_device_address            = 0;
        bool m_allow_update                  = false;
        uint32_t m_tlas_instance_count = 0;
        uint32_t m_tlas_refit_count = 0;

        // rhi
        void* m_rhi_resource         = nullptr;
        void* m_rhi_resource_results = nullptr;

        // reusable buffers - double buffered to avoid frame-to-frame synchronization issues
        // when frame N is being processed by the GPU while frame N+1 updates the buffers
        static const uint32_t buffer_count = 2;
        uint32_t m_buffer_index            = 0;
        void* m_scratch_buffer                                    = nullptr;
        uint64_t m_scratch_buffer_size                            = 0;
        std::array<void*, buffer_count> m_instance_buffer         = {};
        std::array<uint64_t, buffer_count> m_instance_buffer_size = {};
        std::array<void*, buffer_count> m_staging_buffer          = {};
        std::array<uint64_t, buffer_count> m_staging_buffer_size  = {};

        // shared scratch across all blas builds, grows monotonically
        // building 2148 blas with per-instance scratch oom'd the gpu, sharing one keeps it bounded
        static void* s_blas_scratch_buffer;
        static uint64_t s_blas_scratch_buffer_size;

        // compaction
        uint32_t m_compaction_query = UINT32_MAX;
        uint64_t m_compaction_generation = 0;
        std::shared_ptr<const RHI_PendingWork> m_compaction_ready;
        static std::vector<RHI_AccelerationStructure*> s_compaction_pending;
        static uint64_t s_blas_bytes;
        static uint64_t s_compaction_generation;
    };
}
