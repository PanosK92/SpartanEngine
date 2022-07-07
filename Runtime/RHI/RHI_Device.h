/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "../Core/SpartanObject.h"
#include "../Display/DisplayMode.h"
#include <mutex>
#include <memory>
#include "RHI_PhysicalDevice.h"
#include "RHI_DescriptorSet.h"
//=================================

namespace Spartan
{
    class SPARTAN_CLASS RHI_Device : public SpartanObject
    {
    public:
        RHI_Device(Context* context);
        ~RHI_Device();

        // Physical device
        const PhysicalDevice* GetPrimaryPhysicalDevice();

        // Queue
        void QueuePresent(void* swapchain_view, uint32_t* image_index, std::vector<RHI_Semaphore*>& wait_semaphores) const;
        void QueueSubmit(const RHI_Queue_Type type, const uint32_t wait_flags, void* cmd_buffer, RHI_Semaphore* wait_semaphore = nullptr, RHI_Semaphore* signal_semaphore = nullptr, RHI_Fence* signal_fence = nullptr) const;
        bool QueueWait(const RHI_Queue_Type type) const;
        bool QueueWaitAll() const;
        void* GetQueue(const RHI_Queue_Type type) const;
        uint32_t GetQueueIndex(const RHI_Queue_Type type) const;
        void SetQueueIndex(const RHI_Queue_Type type, const uint32_t index);

        // Queries
        void QueryCreate(void** query = nullptr, RHI_Query_Type type = RHI_Query_Type::Timestamp);
        void QueryRelease(void*& query);
        void QueryBegin(void* query);
        void QueryEnd(void* query);
        void QueryGetData(void* query);

        // Device properties
        uint32_t GetMaxTexture1dDimension()            const { return m_max_texture_1d_dimension; }
        uint32_t GetMaxTexture2dDimension()            const { return m_max_texture_2d_dimension; }
        uint32_t GetMaxTexture3dDimension()            const { return m_max_texture_3d_dimension; }
        uint32_t GetMaxTextureCubeDimension()          const { return m_max_texture_cube_dimension; }
        uint32_t GetMaxTextureArrayLayers()            const { return m_max_texture_array_layers; }
        uint64_t GetMinUniformBufferOffsetAllignment() const { return m_min_uniform_buffer_offset_alignment; }
        float GetTimestampPeriod()                     const { return m_timestamp_period; }

        // Descriptors
        void* GetDescriptorPool()                                            { return m_descriptor_pool; }
        std::unordered_map<uint32_t, RHI_DescriptorSet>& GetDescriptorSets() { return m_descriptor_sets; }
        bool HasDescriptorSetCapacity();
        void SetDescriptorSetCapacity(uint32_t descriptor_set_capacity);

        // Command pools
        RHI_CommandPool* AllocateCommandPool(const char* name, const uint64_t swap_chain_id);
        void DestroyCommandPool(RHI_CommandPool* cmd_pool);
        const std::vector<std::shared_ptr<RHI_CommandPool>>& GetCommandPools() { return m_cmd_pools; }

        // Static
        static RHI_Api_Type GetApiType();

        // Misc
        bool IsValidResolution(const uint32_t width, const uint32_t height);
        RHI_Context* GetContextRhi()        const { return m_rhi_context.get(); }
        Context* GetContext()               const { return m_context; }
        uint32_t GetEnabledGraphicsStages() const { return m_enabled_graphics_shader_stages; }

    private:
        // Physical device
        bool DetectPhysicalDevices();
        void RegisterPhysicalDevice(const PhysicalDevice& physical_device);
        bool SelectPrimaryPhysicalDevice();
        void SetPrimaryPhysicalDevice(const uint32_t index);

        // Display modes
        bool DetectDisplayModes(const PhysicalDevice* physical_device, const RHI_Format format);

        // Queues
        void* m_queue_graphics          = nullptr;
        void* m_queue_compute           = nullptr;
        void* m_queue_copy              = nullptr;
        uint32_t m_queue_graphics_index = 0;
        uint32_t m_queue_compute_index  = 0;
        uint32_t m_queue_copy_index     = 0;

        // Descriptors
        std::unordered_map<uint32_t, RHI_DescriptorSet> m_descriptor_sets;
        void* m_descriptor_pool            = nullptr;
        uint32_t m_descriptor_set_capacity = 0;

        // Command pools
        std::vector<std::shared_ptr<RHI_CommandPool>> m_cmd_pools;

        // Device properties
        uint32_t m_max_texture_1d_dimension            = 0;
        uint32_t m_max_texture_2d_dimension            = 0;
        uint32_t m_max_texture_3d_dimension            = 0;
        uint32_t m_max_texture_cube_dimension          = 0;
        uint32_t m_max_texture_array_layers            = 0;
        uint64_t m_min_uniform_buffer_offset_alignment = 0;
        float m_timestamp_period                       = 0;
        bool m_wide_lines                              = false;
        uint32_t m_max_bound_descriptor_sets           = 4; // worst case scenario

        // Misc
        uint32_t m_physical_device_index          = 0;
        uint32_t m_enabled_graphics_shader_stages = 0;
        mutable std::mutex m_queue_mutex;
        std::vector<PhysicalDevice> m_physical_devices;
        std::shared_ptr<RHI_Context> m_rhi_context;
    };
}
