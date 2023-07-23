/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES ======================
#include "../Display/DisplayMode.h"
#include "RHI_PhysicalDevice.h"
#include <memory>
//=================================

namespace Spartan
{
    enum class RHI_Device_Resource
    {
        sampler_comparison,
        sampler_regular
    };

    class SP_CLASS RHI_Device
    {
    public:
        static void Initialize();
        static void Tick(const uint64_t frame_count);
        static void Destroy();

        // Physical device
        static PhysicalDevice* GetPrimaryPhysicalDevice();

        // Queues
        static void QueuePresent(void* swapchain_view, uint32_t* image_index, std::vector<RHI_Semaphore*>& wait_semaphores);
        static void QueueSubmit(const RHI_Queue_Type type, const uint32_t wait_flags, void* cmd_buffer, RHI_Semaphore* wait_semaphore = nullptr, RHI_Semaphore* signal_semaphore = nullptr, RHI_Fence* signal_fence = nullptr);
        static void QueueWait(const RHI_Queue_Type type);
        static void QueueWaitAll();
        static void* GetQueue(const RHI_Queue_Type type);
        static uint32_t GetQueueIndex(const RHI_Queue_Type type);
        static void SetQueueIndex(const RHI_Queue_Type type, const uint32_t index);

        // Descriptors sets, descriptor set layouts and pipelines
        static void* GetDescriptorPool();
        static std::unordered_map<uint64_t, RHI_DescriptorSet>& GetDescriptorSets();
        static bool HasDescriptorSetCapacity();
        static void SetDescriptorSetCapacity(uint32_t descriptor_set_capacity);
        static void SetBindlessSamplers(const std::array<std::shared_ptr<RHI_Sampler>, 7>& samplers);
        static void* GetDescriptorSet(const RHI_Device_Resource resource_type);
        static void* GetDescriptorSetLayout(const RHI_Device_Resource resource_type);
        static void GetOrCreatePipeline(RHI_PipelineState& pso, RHI_Pipeline*& pipeline, RHI_DescriptorSetLayout*& descriptor_set_layout);

        // Command pools
        static RHI_CommandPool* AllocateCommandPool(const char* name, const uint64_t swap_chain_id, const RHI_Queue_Type queue_type);
        static void DestroyCommandPool(RHI_CommandPool* cmd_pool);
        static const std::vector<std::shared_ptr<RHI_CommandPool>>& GetCommandPools();

        // Deletion queue
        static void DeletionQueue_Add(const RHI_Resource_Type resource_type, void* resource);
        static void DeletionQueue_Parse();
        static bool DeletionQueue_NeedsToParse();

        // Vulkan memory allocator
        static void* GetMappedDataFromBuffer(void* resource);
        static void CreateBuffer(void*& resource, const uint64_t size, uint32_t usage, uint32_t memory_property_flags, const void* data_initial, const char* name);
        static void DestroyBuffer(void*& resource);
        static void CreateTexture(void* vk_image_creat_info, void*& resource, const char* name);
        static void DestroyTexture(void*& resource);
        static void MapMemory(void* resource, void*& mapped_data);
        static void UnmapMemory(void* resource, void*& mapped_data);
        static void FlushAllocation(void* resource, uint64_t offset, uint64_t size);

        // Immediate execution
        static RHI_CommandList* ImmediateBegin(const RHI_Queue_Type queue_type);
        static void ImmediateSubmit(RHI_CommandList* cmd_list);

        // Debug
        static void MarkerBegin(RHI_CommandList* cmd_list, const char* name, const Math::Vector4& color);
        static void MarkerEnd(RHI_CommandList* cmd_list);
        static void SetResourceName(void* resource, const RHI_Resource_Type resource_type, const std::string name);

        // Misc
        static bool IsValidResolution(const uint32_t width, const uint32_t height);
        static uint32_t GetEnabledGraphicsStages() { return m_enabled_graphics_shader_stages; }

        // Memory
        static uint32_t GetMemoryUsageMb();
        static uint32_t GetMemoryBudgetMb();

        // Properties
        static uint32_t GetMaxTexture1dDimension()            { return m_max_texture_1d_dimension; }
        static uint32_t GetMaxTexture2dDimension()            { return m_max_texture_2d_dimension; }
        static uint32_t GetMaxTexture3dDimension()            { return m_max_texture_3d_dimension; }
        static uint32_t GetMaxTextureCubeDimension()          { return m_max_texture_cube_dimension; }
        static uint32_t GetMaxTextureArrayLayers()            { return m_max_texture_array_layers; }
        static uint64_t GetMinUniformBufferOffsetAllignment() { return m_min_uniform_buffer_offset_alignment; }
        static uint64_t GetMinStorageBufferOffsetAllignment() { return m_min_storage_buffer_offset_alignment; }
        static float GetTimestampPeriod()                     { return m_timestamp_period; }

    private:
        // Physical device
        static bool DetectPhysicalDevices();
        static void RegisterPhysicalDevice(const PhysicalDevice& physical_device);
        static void SelectPrimaryPhysicalDevice();
        static void SetPrimaryPhysicalDevice(const uint32_t index);
        static std::vector<PhysicalDevice>& GetPhysicalDevices();

        // Properties
        static uint32_t m_max_texture_1d_dimension;
        static uint32_t m_max_texture_2d_dimension;
        static uint32_t m_max_texture_3d_dimension;
        static uint32_t m_max_texture_cube_dimension;
        static uint32_t m_max_texture_array_layers;
        static uint64_t m_min_uniform_buffer_offset_alignment;
        static uint64_t m_min_storage_buffer_offset_alignment;
        static float m_timestamp_period;

        // Misc
        static bool m_wide_lines;
        static uint32_t m_physical_device_index;
        static uint32_t m_enabled_graphics_shader_stages;
    };
}
