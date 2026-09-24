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
//================================

namespace spartan
{
    enum class RHI_Buffer_Type
    {
        Vertex,
        Index,
        Instance,
        Storage,
        Constant,
        ShaderBindingTable,
        Upload,
        Readback,
        Max
    };

    struct RHI_StridedDeviceAddressRegion
    {
        uint64_t device_address = 0;
        uint32_t stride         = 0;
        uint32_t size           = 0;
    };

    class RHI_Buffer : public SpartanObject
    {
    public:
        RHI_Buffer() = default;
        RHI_Buffer(const RHI_Buffer_Type type, const size_t stride, const uint32_t element_count, const void* data, const bool mappable, const char* name)
        {
            // check
            SP_ASSERT(type != RHI_Buffer_Type::Max);
            SP_ASSERT(stride != 0);
            SP_ASSERT(element_count != 0);
            SP_ASSERT_MSG(name != nullptr, "Name the buffer to aid the validation layer");
            if (type == RHI_Buffer_Type::Constant )
            {
                SP_ASSERT_MSG(mappable, "Constant buffers must be mappable");
            }
            if (type == RHI_Buffer_Type::ShaderBindingTable)
            {
                SP_ASSERT_MSG(mappable, "Shader binding tables must be mappable");
            }
            if (type == RHI_Buffer_Type::Upload || type == RHI_Buffer_Type::Readback)
            {
                SP_ASSERT_MSG(mappable, "Upload and readback buffers must be mappable");
            }

            // set
            m_type             = type;
            m_stride_unaligned = static_cast<uint32_t>(stride);
            m_stride           = m_stride_unaligned;
            m_element_count    = element_count;
            m_object_size      = stride * element_count;
            m_mappable         = mappable;
            m_object_name      = name;

            // allocate
            RHI_CreateResource(data);
        }
        ~RHI_Buffer() { RHI_DestroyResource(); }

        // storage and constant buffer updating
        void Update(void* data_cpu, const uint32_t size = 0);
        void Update(RHI_CommandList* cmd_list, void* data_cpu, const uint32_t size = 0);
        void ResetOffset() { m_offset = 0; first_update = true; }

        // copy data into a sub-region of this buffer (for vertex/index buffers)
        void UploadSubRegion(const void* data, uint64_t offset_bytes, uint64_t size_bytes);

        // ray tracing
        RHI_StridedDeviceAddressRegion GetRegion(const RHI_Shader_Type group_type, const uint32_t stride_extra = 0) const;
        void UpdateHandles(RHI_CommandList* cmd_list);

        // propeties
        uint32_t GetStrideUnaligned() const { return m_stride_unaligned; }
        uint32_t GetStride() const          { return m_stride; }
        uint32_t GetElementCount() const    { return m_element_count; }
        uint32_t GetOffset()   const        { return m_offset; }
        void* GetMappedData() const         { return m_data_gpu; }
        void* GetRhiResource() const        { return m_rhi_resource; }
        void* GetRhiSrv() const             { return m_rhi_srv; }
        void* GetRhiUav() const             { return m_rhi_uav; }
        RHI_Buffer_Type GetType() const     { return m_type; }
        uint64_t GetDeviceAddress() const   { return m_device_address; }

    private:
        RHI_Buffer_Type m_type         = RHI_Buffer_Type::Max;
        uint32_t m_stride_unaligned    = 0;
        uint32_t m_stride              = 0;
        uint32_t m_element_count       = 0;
        uint32_t m_offset              = 0;
        uint32_t m_aligned_handle_size = 0;
        uint64_t m_raygen_offset       = 0;
        uint64_t m_miss_offset         = 0;
        uint64_t m_hit_offset          = 0;
        uint64_t m_device_address      = 0;
        void* m_data_gpu               = nullptr;
        bool m_mappable                = false;
        bool first_update              = true;

        // rhi
        void RHI_DestroyResource();
        void RHI_CreateResource(const void* data);
        uint32_t m_rhi_srv_index = UINT32_MAX;
        uint32_t m_rhi_uav_index = UINT32_MAX;
        void* m_rhi_resource = nullptr;
        void* m_rhi_srv      = nullptr;
        void* m_rhi_uav      = nullptr;
    };
}
