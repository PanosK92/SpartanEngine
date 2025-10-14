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

#pragma once

//= INCLUDES =================================
#include "RHI_PhysicalDevice.h"
#include <memory>
#include <map>
#include <vector>
#include "RHI_Descriptor.h"
#include "../Rendering/Renderer_Definitions.h"
//============================================

namespace spartan
{
    class RHI_Device
    {
    public:
        // core
        static void Initialize();
        static void Tick(const uint64_t frame_count);
        static void Destroy();

        // queues
        static void QueueWaitAll(const bool flush = false);
        static uint32_t GetQueueIndex(const RHI_Queue_Type type);
        static RHI_Queue* GetQueue(const RHI_Queue_Type type);
        static void* GetQueueRhiResource(const RHI_Queue_Type type);

        // descriptors
        static void AllocateDescriptorSet(void*& resource, RHI_DescriptorSetLayout* descriptor_set_layout, const std::vector<RHI_Descriptor>& descriptors);
        static std::unordered_map<uint64_t, RHI_DescriptorSet>& GetDescriptorSets();
        static void* GetDescriptorSet(const RHI_Device_Bindless_Resource resource_type);
        static void* GetDescriptorSetLayout(const RHI_Device_Bindless_Resource resource_type);
        static void UpdateBindlessResources(
            std::array<RHI_Texture*, rhi_max_array_size>* material_textures,
            RHI_Buffer* material_parameters,
            RHI_Buffer* light_parameters,
            const std::array<std::shared_ptr<RHI_Sampler>, static_cast<uint32_t>(Renderer_Sampler::Max)>* samplers,
            RHI_Buffer* bindless_aabbs
        );

        // pipelines
        static void GetOrCreatePipeline(RHI_PipelineState& pso, RHI_Pipeline*& pipeline, RHI_DescriptorSetLayout*& descriptor_set_layout);
        static uint32_t GetPipelineCount();

        // deletion queue
        static void DeletionQueueAdd(const RHI_Resource_Type resource_type, void* resource);
        static void DeletionQueueParse();
        static bool DeletionQueueNeedsToParse();

        // memory
        static void* MemoryGetMappedDataFromBuffer(void* resource);
        static void MemoryBufferCreate(void*& resource, const uint64_t size, uint32_t flags_usage, uint32_t flags_memory, const void* data_initial, const char* name);
        static void MemoryBufferDestroy(void*& resource);
        static void MemoryTextureCreate(RHI_Texture* texture);
        static void MemoryTextureDestroy(void*& resource);
        static void MemoryMap(void* resource, void*& mapped_data);
        static void MemoryUnmap(void* resource);
        static uint64_t MemoryGetAllocatedMb();
        static uint64_t MemoryGetAvailableMb();
        static uint64_t MemoryGetTotalMb();

        // immediate execution command list
        static RHI_CommandList* CmdImmediateBegin(const RHI_Queue_Type queue_type);
        static void CmdImmediateSubmit(RHI_CommandList* cmd_list);

        // properties (actual silicon properties)
        static float PropertyGetTimestampPeriod()                     { return m_timestamp_period; }
        static uint64_t PropertyGetMinUniformBufferOffsetAlignment()  { return m_min_uniform_buffer_offset_alignment; }
        static uint64_t PropertyGetMinStorageBufferOffsetAlignment()  { return m_min_storage_buffer_offset_alignment; }
        static uint32_t PropertyGetMaxTexture1dDimension()            { return m_max_texture_1d_dimension; }
        static uint32_t PropertyGetMaxTexture2dDimension()            { return m_max_texture_2d_dimension; }
        static uint32_t PropertyGetMaxTexture3dDimension()            { return m_max_texture_3d_dimension; }
        static uint32_t PropertyGetMaxTextureCubeDimension()          { return m_max_texture_cube_dimension; }
        static uint32_t PropertyGetMaxTextureArrayLayers()            { return m_max_texture_array_layers; }
        static uint32_t PropertyGetMaxPushConstantSize()              { return m_max_push_constant_size; }
        static uint32_t PropertyGetMaxShadingRateTexelSizeX()         { return m_max_shading_rate_texel_size_x; }
        static uint32_t PropertyGetMaxShadingRateTexelSizeY()         { return m_max_shading_rate_texel_size_y; }
        static uint64_t PropertyGetOptimalBufferCopyOffsetAlignment() { return m_optimal_buffer_copy_offset_alignment; }
        static bool PropertyIsShadingRateSupported()                  { return m_is_shading_rate_supported; }
        static bool PropertyIsXessSupported()                         { return m_xess_supported; }
        static bool PropertyIsRayTracingSupported()                   { return m_is_ray_tracing_supported; }

        // markers
        static void MarkerBegin(RHI_CommandList* cmd_list, const char* name, const math::Vector4& color);
        static void MarkerEnd(RHI_CommandList* cmd_list);

        // ray tracing
        static int CreateAccelerationStructureKHR(const void* pCreateInfo, const void* pAllocator, void* pAccelerationStructure);
        static void DestroyAccelerationStructureKHR(void* accelerationStructure, const void* pAllocator);
        static void GetAccelerationStructureBuildSizesKHR(uint32_t buildType, const void* pBuildInfo, const uint32_t* pMaxPrimitiveCounts, void* pSizeInfo);
        static void CmdBuildAccelerationStructuresKHR(void* commandBuffer, uint32_t infoCount, const void* pInfos, const void* ppBuildRangeInfos);
        static uint64_t GetBufferDeviceAddress(void* pInfo);

        // physical device
        static void PhysicalDeviceRegister(const RHI_PhysicalDevice& physical_device);
        static void PhysicalDeviceSetPrimary(const uint32_t index);
        static std::vector<RHI_PhysicalDevice>& PhysicalDeviceGet();

        // misc
        static void SetResourceName(void* resource, const RHI_Resource_Type resource_type, const char* name);
        static bool IsValidResolution(const uint32_t width, const uint32_t height);
        static uint32_t GetDescriptorType(const RHI_Descriptor& descriptor);
        static RHI_PhysicalDevice* GetPrimaryPhysicalDevice();
        static void SetVariableRateShading(const RHI_CommandList* cmd_list, const bool enabled);

    private:
        // properties
        static float m_timestamp_period;
        static uint64_t m_min_uniform_buffer_offset_alignment;
        static uint64_t m_min_storage_buffer_offset_alignment;
        static uint32_t m_max_texture_1d_dimension;
        static uint32_t m_max_texture_2d_dimension;
        static uint32_t m_max_texture_3d_dimension;
        static uint32_t m_max_texture_cube_dimension;
        static uint32_t m_max_texture_array_layers;
        static uint32_t m_max_push_constant_size;
        static uint32_t m_max_shading_rate_texel_size_x;
        static uint32_t m_max_shading_rate_texel_size_y;
        static uint64_t m_optimal_buffer_copy_offset_alignment;
        static bool m_is_shading_rate_supported;
        static bool m_xess_supported;
        static bool m_is_ray_tracing_supported;

        // misc
        static bool m_wide_lines;
        static uint32_t m_physical_device_index;
    };
}
