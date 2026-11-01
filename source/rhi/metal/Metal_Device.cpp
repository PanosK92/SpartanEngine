/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ========================
#include "pch.h"
#include "../RHI_Device.h"
#include "../RHI_Queue.h"
#include "../RHI_CommandList.h"
#include "../RHI_Pipeline.h"
#include "../RHI_DescriptorSet.h"
#include "../RHI_Compiler.h"
#include "../../profiling/Profiler.h"
#include "Metal_Internal.h"
//===================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace queues
    {
        MTL4::CommandQueue* queue = nullptr;
        array<shared_ptr<RHI_Queue>, static_cast<uint32_t>(RHI_Queue_Type::Max)> regular;

        void destroy()
        {
            regular.fill(nullptr);
        }
    }

    namespace residency
    {
        array<MTL::ResidencySet*, static_cast<uint32_t>(metal_residency::Group::Count)> sets = {};
        array<bool, static_cast<uint32_t>(metal_residency::Group::Count)> dirty              = {};
        MTL4::CommandQueue* queue                                                            = nullptr;
        mutex mutex_sets;
    }

    namespace deletion
    {
        struct Entry
        {
            RHI_Resource_Type type;
            void* resource;
            shared_ptr<const RHI_PendingWork> completion;
        };

        vector<Entry> queue;
        mutex mutex_queue;

        void destroy_resource(RHI_Resource_Type type, void* resource)
        {
            if (!resource)
                return;

            // metal objects are all ns objects, residency only tracks allocations
            if (type == RHI_Resource_Type::Buffer || type == RHI_Resource_Type::Image || type == RHI_Resource_Type::AccelerationStructure)
            {
                metal_residency::Remove(static_cast<MTL::Allocation*>(resource));
            }

            // descriptor set layouts are hash tokens, not objects
            if (type == RHI_Resource_Type::DescriptorSetLayout)
                return;

            // shaders and pipelines are backend structs that own their metal objects
            if (type == RHI_Resource_Type::Shader)
            {
                metal_shader::Destroy(resource);
                return;
            }
            if (type == RHI_Resource_Type::Pipeline)
            {
                metal_pipeline::Destroy(resource);
                return;
            }

            static_cast<NS::Object*>(resource)->release();
        }
    }

    namespace staging
    {
        struct Entry
        {
            MTL::Buffer* buffer = nullptr;
            uint64_t size       = 0;
            bool in_use         = false;
        };
        vector<Entry> pool;
        mutex mutex_pool;
    }

    namespace pipelines
    {
        unordered_map<uint64_t, shared_ptr<RHI_Pipeline>> cache;
        mutex mutex_cache;
    }

    MTL4::CommandQueue* metal_queue::Get()
    {
        return queues::queue;
    }

    mutex& metal_queue::GetMutex()
    {
        static mutex queue_mutex;
        return queue_mutex;
    }

    void metal_residency::Initialize(MTL4::CommandQueue* queue)
    {
        static const char* names[] = { "residency_static", "residency_dynamic", "residency_screen" };

        residency::queue = queue;
        for (uint32_t i = 0; i < static_cast<uint32_t>(Group::Count); i++)
        {
            MTL::ResidencySetDescriptor* descriptor = MTL::ResidencySetDescriptor::alloc()->init();
            metal_utility::set_label(descriptor, names[i]);

            NS::Error* error   = nullptr;
            residency::sets[i] = RHI_Context::device->newResidencySet(descriptor, &error);
            descriptor->release();
            SP_ASSERT_MSG(residency::sets[i] != nullptr, "Failed to create residency set");

            // attached once, every command buffer committed to the queue sees all three sets
            queue->addResidencySet(residency::sets[i]);
        }
    }

    void metal_residency::Shutdown()
    {
        for (MTL::ResidencySet*& set : residency::sets)
        {
            if (!set)
                continue;

            residency::queue->removeResidencySet(set);
            set->removeAllAllocations();
            set->release();
            set = nullptr;
        }
        residency::queue = nullptr;
    }

    void metal_residency::Add(Group group, MTL::Allocation* allocation)
    {
        SP_ASSERT(allocation != nullptr);

        lock_guard<mutex> lock(residency::mutex_sets);
        const uint32_t index = static_cast<uint32_t>(group);
        residency::sets[index]->addAllocation(allocation);
        residency::dirty[index] = true;
    }

    void metal_residency::Remove(MTL::Allocation* allocation)
    {
        lock_guard<mutex> lock(residency::mutex_sets);
        for (uint32_t i = 0; i < static_cast<uint32_t>(Group::Count); i++)
        {
            if (residency::sets[i] && residency::sets[i]->containsAllocation(allocation))
            {
                residency::sets[i]->removeAllocation(allocation);
                residency::dirty[i] = true;
            }
        }
    }

    void metal_residency::Commit()
    {
        lock_guard<mutex> lock(residency::mutex_sets);
        for (uint32_t i = 0; i < static_cast<uint32_t>(Group::Count); i++)
        {
            if (residency::dirty[i])
            {
                residency::sets[i]->commit();
                residency::dirty[i] = false;
            }
        }
    }

    void RHI_Device::Initialize()
    {
        RHI_Context::device = MTL::CreateSystemDefaultDevice();
        SP_ASSERT_MSG(RHI_Context::device != nullptr, "Failed to create metal device");
        SP_ASSERT_MSG(RHI_Context::device->supportsFamily(MTL::GPUFamilyMetal4), "Metal 4 is not supported by this gpu");

        // physical device, apple gpus share memory with the cpu so the working set is the useful size
        {
            RHI_PhysicalDevice_Type type = RHI_Context::device->hasUnifiedMemory() ? RHI_PhysicalDevice_Type::Integrated : RHI_PhysicalDevice_Type::Discrete;
            PhysicalDeviceRegister(RHI_PhysicalDevice(
                (4 << 22),
                0,
                nullptr,
                0x106B, // apple
                type,
                RHI_Context::device->name()->utf8String(),
                RHI_Context::device->recommendedMaxWorkingSetSize(),
                RHI_Context::device
            ));
            PhysicalDeviceSetPrimary(0);
        }

        // limits
        m_max_texture_2d_dimension                 = 16384;
        m_max_push_constant_size                   = 256;
        m_min_uniform_buffer_offset_alignment      = 256;
        m_min_storage_buffer_offset_alignment      = 16;
        m_min_acceleration_buffer_offset_alignment = 256;
        m_optimal_buffer_copy_offset_alignment     = 256;
        m_shader_group_handle_size                 = 32;
        m_shader_group_handle_alignment            = 32;
        m_shader_group_base_alignment              = 64;
        m_timestamp_period                         = static_cast<float>(1e9 / static_cast<double>(RHI_Context::device->queryTimestampFrequency()));

        // features come online as the backend implements them
        m_is_ray_tracing_supported          = RHI_Context::device->supportsRaytracing();
        m_is_mesh_shaders_supported         = true; // every metal 4 gpu has mesh shaders
        m_is_shading_rate_supported         = false;
        m_is_shader_base_instance_supported = false;
        m_xess_supported                    = false;
        m_dlss_supported                    = false;

        MTL4::CommandQueueDescriptor* descriptor = MTL4::CommandQueueDescriptor::alloc()->init();
        metal_utility::set_label(descriptor, "queue");
        NS::Error* error = nullptr;
        queues::queue    = RHI_Context::device->newMTL4CommandQueue(descriptor, &error);
        descriptor->release();
        SP_ASSERT_MSG(queues::queue != nullptr, "Failed to create metal 4 command queue");
        metal_residency::Initialize(queues::queue);
        metal_bindings::Initialize();

        queues::regular[static_cast<uint32_t>(RHI_Queue_Type::Graphics)] = make_shared<RHI_Queue>(RHI_Queue_Type::Graphics, "graphics");
        queues::regular[static_cast<uint32_t>(RHI_Queue_Type::Compute)]  = make_shared<RHI_Queue>(RHI_Queue_Type::Compute,  "compute");
        queues::regular[static_cast<uint32_t>(RHI_Queue_Type::Copy)]     = make_shared<RHI_Queue>(RHI_Queue_Type::Copy,     "copy");
        queues::regular[static_cast<uint32_t>(RHI_Queue_Type::Present)]  = make_shared<RHI_Queue>(RHI_Queue_Type::Present,  "present");

        RHI_Context::api_version_cstr = "4";
        SP_LOG_INFO("Metal 4 initialized successfully (%s)", RHI_Context::device->name()->utf8String());
    }

    RHI_TimestampCalibration RHI_Device::GetTimestampCalibration(RHI_Queue_Type type)
    {
        // every queue shares the device's gpu clock, the cpu side of the pair is on another clock than the
        // engine's, so the sample is bracketed by two engine reads and placed at their midpoint
        // the sampled pair is in nanoseconds while timestamp heaps count ticks of the timestamp frequency
        const double frequency = static_cast<double>(RHI_Context::device->queryTimestampFrequency());
        RHI_TimestampCalibration result;
        result.period_ns = 1e9 / frequency;

        MTL::Timestamp cpu_ns = 0;
        MTL::Timestamp gpu_ns = 0;
        const double before   = GetCpuTimestampMs();
        RHI_Context::device->sampleTimestamps(&cpu_ns, &gpu_ns);
        const double after    = GetCpuTimestampMs();

        result.gpu_tick     = static_cast<uint64_t>(static_cast<double>(gpu_ns) * frequency * 1e-9);
        result.cpu_ms       = (before + after) * 0.5;
        result.deviation_ms = (after - before) * 0.5;
        result.calibrated   = true;
        return result;
    }

    void RHI_Device::Tick(const uint64_t frame_count)
    {
        DeletionQueueParse();
    }

    void RHI_Device::Destroy()
    {
        RHI_Compiler::Shaders().Flush();
        RHI_Compiler::Pipelines().Flush();
        DestroySwapChain();
        QueueWaitAll();

        DeletionQueueParse();
        {
            lock_guard<mutex> lock(pipelines::mutex_cache);
            pipelines::cache.clear();
        }
        queues::destroy();
        StagingBufferPoolDestroy();
        DeletionQueueFlush();

        metal_bindings::Shutdown();
        metal_residency::Shutdown();
        queues::queue->release();
        queues::queue = nullptr;

        RHI_Context::device->release();
        RHI_Context::device = nullptr;
    }

    void RHI_Device::QueueWaitAll(const bool flush)
    {
        static mutex wait_mutex;
        lock_guard<mutex> lock(wait_mutex);

        for (const shared_ptr<RHI_Queue>& queue : queues::regular)
        {
            if (queue)
            {
                queue->Wait(flush);
            }
        }
    }

    uint32_t RHI_Device::GetQueueIndex(const RHI_Queue_Type type)
    {
        return 0;
    }

    bool RHI_Device::IsAsyncComputeConcurrent()
    {
        return false;
    }

    RHI_Queue* RHI_Device::GetQueue(const RHI_Queue_Type type)
    {
        return queues::regular[static_cast<uint32_t>(type)].get();
    }

    void* RHI_Device::GetQueueRhiResource(const RHI_Queue_Type type)
    {
        return queues::queue;
    }

    void RHI_Device::DeletionQueueAdd(const RHI_Resource_Type resource_type, void* resource)
    {
        if (!resource)
            return;

        shared_ptr<const RHI_PendingWork> completion = RHI_CommandList::CapturePendingWork();
        lock_guard<mutex> guard(deletion::mutex_queue);
        deletion::queue.push_back({ resource_type, resource, move(completion) });
    }

    void RHI_Device::DeletionQueueParse()
    {
        vector<deletion::Entry> retired;
        {
            lock_guard<mutex> guard(deletion::mutex_queue);
            size_t retained = 0;
            for (size_t i = 0; i < deletion::queue.size(); i++)
            {
                deletion::Entry& entry = deletion::queue[i];
                if (entry.completion->IsComplete())
                {
                    retired.push_back(move(entry));
                }
                else
                {
                    if (retained != i)
                    {
                        deletion::queue[retained] = move(entry);
                    }
                    retained++;
                }
            }
            deletion::queue.resize(retained);
        }

        // destroying can enqueue more deletions, so never under the lock
        for (const deletion::Entry& entry : retired)
        {
            deletion::destroy_resource(entry.type, entry.resource);
        }
    }

    void RHI_Device::DeletionQueueFlush()
    {
        do
        {
            DeletionQueueParse();
        } while (DeletionQueueNeedsToParse());
    }

    bool RHI_Device::DeletionQueueNeedsToParse()
    {
        lock_guard<mutex> guard(deletion::mutex_queue);
        for (const deletion::Entry& entry : deletion::queue)
        {
            if (entry.completion->IsComplete())
                return true;
        }

        return false;
    }

    // buffers and textures create their own allocations, like d3d12
    void* RHI_Device::MemoryGetMappedDataFromBuffer(void* resource)
    {
        return resource ? static_cast<MTL::Buffer*>(resource)->contents() : nullptr;
    }

    void RHI_Device::MemoryBufferCreate(void*& resource, const uint64_t size, uint32_t flags_usage, uint32_t flags_memory, const void* data, const char* name, const bool transient, const bool allow_failure)
    {
        SP_ASSERT_MSG(false, "metal buffers allocate in RHI_Buffer::RHI_CreateResource");
    }

    void RHI_Device::MemoryBufferDestroy(void*& resource)
    {
        if (resource)
        {
            DeletionQueueAdd(RHI_Resource_Type::Buffer, resource);
            resource = nullptr;
        }
    }

    void RHI_Device::MemoryTextureCreate(RHI_Texture* texture)
    {
        SP_ASSERT_MSG(false, "metal textures allocate in RHI_Texture::RHI_CreateResource");
    }

    void RHI_Device::MemoryTextureDestroy(void*& resource)
    {
        if (resource)
        {
            DeletionQueueAdd(RHI_Resource_Type::Image, resource);
            resource = nullptr;
        }
    }

    void RHI_Device::MemoryMap(void* resource, void*& mapped_data)
    {
        mapped_data = MemoryGetMappedDataFromBuffer(resource);
    }

    void RHI_Device::MemoryUnmap(void* resource)
    {
        // shared buffers stay mapped for their lifetime
    }

    uint64_t RHI_Device::MemoryGetAllocatedMb()
    {
        return RHI_Context::device ? RHI_Context::device->currentAllocatedSize() / (1024ull * 1024ull) : 0;
    }

    uint64_t RHI_Device::MemoryGetAvailableMb()
    {
        const uint64_t total     = MemoryGetTotalMb();
        const uint64_t allocated = MemoryGetAllocatedMb();
        return total > allocated ? total - allocated : 0;
    }

    uint64_t RHI_Device::MemoryGetTotalMb()
    {
        return RHI_Context::device ? RHI_Context::device->recommendedMaxWorkingSetSize() / (1024ull * 1024ull) : 0;
    }

    void* RHI_Device::StagingBufferAcquire(uint64_t size)
    {
        lock_guard<mutex> lock(staging::mutex_pool);

        for (staging::Entry& entry : staging::pool)
        {
            if (!entry.in_use && entry.size >= size)
            {
                entry.in_use = true;
                return entry.buffer;
            }
        }

        MTL::Buffer* buffer = RHI_Context::device->newBuffer(size, MTL::ResourceStorageModeShared | MTL::ResourceCPUCacheModeWriteCombined);
        if (!buffer)
            return nullptr;

        metal_utility::set_label(buffer, "staging");
        metal_residency::Add(metal_residency::Group::Static, buffer);
        staging::pool.push_back({ buffer, size, true });
        return buffer;
    }

    void RHI_Device::StagingBufferRelease(void* buffer)
    {
        lock_guard<mutex> lock(staging::mutex_pool);
        for (staging::Entry& entry : staging::pool)
        {
            if (entry.buffer == buffer)
            {
                entry.in_use = false;
                return;
            }
        }
    }

    void RHI_Device::StagingBufferPoolDestroy()
    {
        lock_guard<mutex> lock(staging::mutex_pool);
        for (staging::Entry& entry : staging::pool)
        {
            metal_residency::Remove(entry.buffer);
            entry.buffer->release();
        }
        staging::pool.clear();
    }

    void RHI_Device::GetOrCreatePipeline(RHI_PipelineState& pso, RHI_Pipeline*& pipeline, RHI_DescriptorSetLayout*& descriptor_set_layout)
    {
        SP_ASSERT(pso.GetHash() != 0);
        lock_guard<mutex> lock(pipelines::mutex_cache);

        descriptor_set_layout = nullptr;
        uint64_t hash         = pso.GetHash();
        auto it               = pipelines::cache.find(hash);
        if (it == pipelines::cache.end())
        {
            it = pipelines::cache.emplace(make_pair(hash, make_shared<RHI_Pipeline>(pso, descriptor_set_layout))).first;
        }

        pipeline = it->second->IsReady() && it->second->GetRhiResource() ? it->second.get() : nullptr;
    }

    uint32_t RHI_Device::GetPipelineCount()
    {
        lock_guard<mutex> lock(pipelines::mutex_cache);
        return static_cast<uint32_t>(pipelines::cache.size());
    }

    void* RHI_Device::GetPipelineCache()
    {
        return nullptr;
    }

    void RHI_Device::SetResourceName(void* resource, const RHI_Resource_Type resource_type, const char* name)
    {
        if (!resource || !name)
            return;

        if (resource_type == RHI_Resource_Type::Buffer)
        {
            metal_utility::set_label(static_cast<MTL::Buffer*>(resource), name);
        }
        else if (resource_type == RHI_Resource_Type::Image)
        {
            metal_utility::set_label(static_cast<MTL::Texture*>(resource), name);
        }
    }

    void RHI_Device::MarkerBegin(RHI_CommandList* cmd_list, const char* name, const math::Vector4& color)
    {
        if (!cvar_debug_gpu_marking.GetValue() || !cmd_list || !name)
            return;

        NS::String* label = metal_utility::ns_string(name);
        static_cast<MTL4::CommandBuffer*>(cmd_list->GetRhiResource())->pushDebugGroup(label);
        label->release();
    }

    void RHI_Device::MarkerEnd(RHI_CommandList* cmd_list)
    {
        if (!cvar_debug_gpu_marking.GetValue() || !cmd_list)
            return;

        static_cast<MTL4::CommandBuffer*>(cmd_list->GetRhiResource())->popDebugGroup();
    }

    void RHI_Device::SetVariableRateShading(const RHI_CommandList* cmd_list, const bool enabled)
    {
        // metal has rasterization rate maps instead of shading rate images, unsupported for now
    }

    void* RHI_Device::GetDescriptorSet(const RHI_Device_Bindless_Resource resource_type)
    {
        return nullptr;
    }

    void* RHI_Device::GetDescriptorSetLayout(const RHI_Device_Bindless_Resource resource_type)
    {
        return nullptr;
    }

    uint32_t RHI_Device::GetDescriptorType(const RHI_Descriptor& descriptor)
    {
        return 0;
    }

    void RHI_Device::AllocateDescriptorSet(void*& resource, RHI_DescriptorSetLayout* descriptor_set_layout, const vector<RHI_DescriptorWithBinding>& descriptors)
    {
        // metal binds through argument tables, there are no descriptor sets
    }

    unordered_map<uint64_t, RHI_DescriptorSet>& RHI_Device::GetDescriptorSets()
    {
        static unordered_map<uint64_t, RHI_DescriptorSet> descriptors;
        return descriptors;
    }

    mutex& RHI_Device::GetDescriptorSetMutex()
    {
        static mutex descriptor_mutex;
        return descriptor_mutex;
    }

    uint64_t RHI_Device::GetDescriptorSetFrame()
    {
        return 0;
    }

    void RHI_Device::DescriptorSetInvalidateReferencingResource(void* resource)
    {
        if (!resource)
            return;

        lock_guard<mutex> lock(GetDescriptorSetMutex());
        unordered_map<uint64_t, RHI_DescriptorSet>& sets = GetDescriptorSets();
        for (auto it = sets.begin(); it != sets.end();)
        {
            if (it->second.IsReferingToResource(resource))
            {
                it = sets.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    uint64_t RHI_Device::GetBufferDeviceAddress(void* buffer)
    {
        return buffer ? static_cast<MTL::Buffer*>(buffer)->gpuAddress() : 0;
    }
}
