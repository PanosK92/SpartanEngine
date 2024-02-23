/*
Copyright(c) 2016-2024 Panos Karabelas

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
#include "../Display/DisplayMode.h"
#include "RHI_PhysicalDevice.h"
#include <memory>
#include "RHI_Descriptor.h"
#include "../Rendering/Renderer_Definitions.h"
//============================================

namespace Spartan
{
    class SP_CLASS RHI_Device
    {
    public:
        static void Initialize();
        static void Tick(const uint64_t frame_count);
        static void Destroy();

        // queues
        static void QueuePresent(void* swapchain_view, uint32_t* image_index, std::vector<RHI_Semaphore*>& wait_semaphores);
        static void QueueSubmit(const RHI_Queue_Type type, const uint32_t wait_flags, void* cmd_buffer, RHI_Semaphore* wait_semaphore = nullptr, RHI_Semaphore* signal_semaphore = nullptr, RHI_Fence* signal_fence = nullptr);
        static void QueueWait(const RHI_Queue_Type type);
        static void QueueWaitAll();
        static void* QueueGet(const RHI_Queue_Type type);
        static uint32_t QueueGetIndex(const RHI_Queue_Type type);
        static void QueueSetIndex(const RHI_Queue_Type type, const uint32_t index);
        
        // descriptors
        static void CreateDescriptorPool();
        static void AllocateDescriptorSet(void*& resource, RHI_DescriptorSetLayout* descriptor_set_layout, const std::vector<RHI_Descriptor>& descriptors);
        static std::unordered_map<uint64_t, RHI_DescriptorSet>& GetDescriptorSets();
        static void* GetDescriptorSet(const RHI_Device_Resource resource_type);
        static void* GetDescriptorSetLayout(const RHI_Device_Resource resource_type);
        static void UpdateBindlessResources(const std::array<std::shared_ptr<RHI_Sampler>, static_cast<uint32_t>(Renderer_Sampler::Max)>* samplers, std::array<RHI_Texture*, rhi_max_array_size>* textures);

        // pipelines
        static void GetOrCreatePipeline(RHI_PipelineState& pso, RHI_Pipeline*& pipeline, RHI_DescriptorSetLayout*& descriptor_set_layout);
        static uint32_t GetPipelineCount();

        // command pools
        static RHI_CommandPool* CommandPoolAllocate(const char* name, const uint64_t swap_chain_id, const RHI_Queue_Type queue_type);
        static void CommandPoolDestroy(RHI_CommandPool* cmd_pool);
        static const std::vector<std::shared_ptr<RHI_CommandPool>>& GetCommandPools();

        // deletion queue
        static void DeletionQueueAdd(const RHI_Resource_Type resource_type, void* resource);
        static void DeletionQueueParse();
        static bool DeletionQueueNeedsToParse();

        // memory
        static void* MemoryGetMappedDataFromBuffer(void* resource);
        static void MemoryBufferCreate(void*& resource, const uint64_t size, uint32_t usage, uint32_t memory_property_flags, const void* data_initial, const char* name);
        static void MemoryBufferDestroy(void*& resource);
        static void MemoryTextureCreate(RHI_Texture* texture);
        static void MemoryTextureDestroy(void*& resource);
        static void MemoryMap(void* resource, void*& mapped_data);
        static void MemoryUnmap(void* resource);
        static uint32_t MemoryGetUsageMb();
        static uint32_t MemoryGetBudgetMb();

        // immediate execution command list
        static RHI_CommandList* CmdImmediateBegin(const RHI_Queue_Type queue_type);
        static void CmdImmediateSubmit(RHI_CommandList* cmd_list);

        // properties (actual silicon properties)
        static float PropertyGetTimestampPeriod()                     { return m_timestamp_period; }
        static uint64_t PropertyGetMinUniformBufferOffsetAllignment() { return m_min_uniform_buffer_offset_alignment; }
        static uint64_t PropertyGetMinStorageBufferOffsetAllignment() { return m_min_storage_buffer_offset_alignment; }
        static uint32_t PropertyGetMaxTexture1dDimension()            { return m_max_texture_1d_dimension; }
        static uint32_t PropertyGetMaxTexture2dDimension()            { return m_max_texture_2d_dimension; }
        static uint32_t PropertyGetMaxTexture3dDimension()            { return m_max_texture_3d_dimension; }
        static uint32_t PropertyGetMaxTextureCubeDimension()          { return m_max_texture_cube_dimension; }
        static uint32_t PropertyGetMaxTextureArrayLayers()            { return m_max_texture_array_layers; }
        static uint32_t PropertyGetMaxPushConstantSize()              { return m_max_push_constant_size; }
        static uint32_t PropertyGetMinShadingRateTexelSizeX()         { return m_min_shading_rate_texel_size_x; }
        static uint32_t PropertyGetMinShadingRateTexelSizeY()         { return m_min_shading_rate_texel_size_y; }

        // markers
        static void MarkerBegin(RHI_CommandList* cmd_list, const char* name, const Math::Vector4& color);
        static void MarkerEnd(RHI_CommandList* cmd_list);

        // misc
        static void SetResourceName(void* resource, const RHI_Resource_Type resource_type, const std::string name);
        static bool IsValidResolution(const uint32_t width, const uint32_t height);
        static uint32_t GetEnabledGraphicsStages();
        static uint32_t GetDescriptorType(const RHI_Descriptor& descriptor);
        static PhysicalDevice* GetPrimaryPhysicalDevice();
 
    private:
        // physical device
        static bool PhysicalDeviceDetect();
        static void PhysicalDeviceRegister(const PhysicalDevice& physical_device);
        static void PhysicalDeviceSelectPrimary();
        static void PhysicalDeviceSetPrimary(const uint32_t index);
        static std::vector<PhysicalDevice>& PhysicalDeviceGet();

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
        static uint32_t m_min_shading_rate_texel_size_x;
        static uint32_t m_min_shading_rate_texel_size_y;

        // misc
        static bool m_wide_lines;
        static uint32_t m_physical_device_index;
    };
}
