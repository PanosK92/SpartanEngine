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

//= INCLUDES =====================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_DescriptorSet.h"
#include "../RHI_Pipeline.h"
#include <wrl/client.h>
#include "../RHI_Queue.h"
#include "../Core/Debugging.h"
#include <unordered_map>
#include <mutex>
//================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace queues
    {
        // raw d3d12 command queues
        ID3D12CommandQueue* graphics = nullptr;
        ID3D12CommandQueue* compute  = nullptr;
        ID3D12CommandQueue* copy     = nullptr;

        // command allocators (one per queue type)
        ID3D12CommandAllocator* allocator_graphics = nullptr;
        ID3D12CommandAllocator* allocator_compute  = nullptr;
        ID3D12CommandAllocator* allocator_copy     = nullptr;

        // rhi queue wrappers
        array<shared_ptr<RHI_Queue>, 3> regular;

        // fences for synchronization
        ID3D12Fence* fence_graphics = nullptr;
        uint64_t fence_value_graphics = 0;
        HANDLE fence_event = nullptr;

        void destroy()
        {
            for (auto& queue : regular)
            {
                queue = nullptr;
            }

            if (fence_event)
            {
                CloseHandle(fence_event);
                fence_event = nullptr;
            }

            if (fence_graphics)
            {
                fence_graphics->Release();
                fence_graphics = nullptr;
            }

            if (allocator_graphics)
            {
                allocator_graphics->Release();
                allocator_graphics = nullptr;
            }

            if (allocator_compute)
            {
                allocator_compute->Release();
                allocator_compute = nullptr;
            }

            if (allocator_copy)
            {
                allocator_copy->Release();
                allocator_copy = nullptr;
            }
        }
    }

    namespace descriptors
    {
        // descriptor heaps
        ID3D12DescriptorHeap* heap_rtv       = nullptr;
        ID3D12DescriptorHeap* heap_dsv       = nullptr;
        ID3D12DescriptorHeap* heap_cbv_srv_uav = nullptr;

        uint32_t rtv_descriptor_size       = 0;
        uint32_t dsv_descriptor_size       = 0;
        uint32_t cbv_srv_uav_descriptor_size = 0;

        // current allocation offsets
        uint32_t rtv_offset       = 0;
        uint32_t dsv_offset       = 0;
        uint32_t cbv_srv_uav_offset = 0;

        // pipeline cache
        unordered_map<uint64_t, shared_ptr<RHI_Pipeline>> pipelines;
        mutex pipeline_mutex;

        void destroy()
        {
            pipelines.clear();
            if (heap_rtv) { heap_rtv->Release(); heap_rtv = nullptr; }
            if (heap_dsv) { heap_dsv->Release(); heap_dsv = nullptr; }
            if (heap_cbv_srv_uav) { heap_cbv_srv_uav->Release(); heap_cbv_srv_uav = nullptr; }
        }
    }

    namespace device_physical
    { 
        void detect_all()
        {
            // create directx graphics interface factory
            IDXGIFactory1* factory;
            const auto result = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
            if (FAILED(result))
            {
                SP_LOG_ERROR("Failed to create a DirectX graphics interface factory, %s.", d3d12_utility::error::dxgi_error_to_string(result));
                SP_ASSERT(false);
            }

            const auto get_available_adapters = [](IDXGIFactory1* factory)
            {
                uint32_t i = 0;
                IDXGIAdapter* adapter;
                vector<IDXGIAdapter*> adapters;
                while (factory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND)
                {
                    adapters.emplace_back(adapter);
                    ++i;
                }

                return adapters;
            };

            // get all available adapters
            vector<IDXGIAdapter*> adapters = get_available_adapters(factory);
            factory->Release();
            factory = nullptr;
            SP_ASSERT(adapters.size() > 0);

            // save all available adapters
            for (IDXGIAdapter* display_adapter : adapters)
            {
                // try to get extended adapter info
                IDXGIAdapter1* adapter1 = nullptr;
                DXGI_ADAPTER_DESC1 adapter_desc1 = {};
                
                if (SUCCEEDED(display_adapter->QueryInterface(IID_PPV_ARGS(&adapter1))))
                {
                    adapter1->GetDesc1(&adapter_desc1);
                    adapter1->Release();
                }
                else
                {
                    // fallback to basic desc
                    DXGI_ADAPTER_DESC adapter_desc;
                    if (FAILED(display_adapter->GetDesc(&adapter_desc)))
                    {
                        SP_LOG_ERROR("Failed to get adapter description");
                        continue;
                    }
                    // copy to desc1 format
                    wcscpy_s(adapter_desc1.Description, adapter_desc.Description);
                    adapter_desc1.VendorId = adapter_desc.VendorId;
                    adapter_desc1.DedicatedVideoMemory = adapter_desc.DedicatedVideoMemory;
                    adapter_desc1.Flags = 0;
                }

                // convert the device name
                char name[128];
                auto def_char = ' ';
                WideCharToMultiByte(CP_ACP, 0, adapter_desc1.Description, -1, name, 128, &def_char, nullptr);

                // determine device type
                RHI_PhysicalDevice_Type device_type = RHI_PhysicalDevice_Type::Discrete;
                if (adapter_desc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                {
                    device_type = RHI_PhysicalDevice_Type::Cpu;
                }
                else if (adapter_desc1.DedicatedVideoMemory == 0)
                {
                    // integrated gpus typically share system memory
                    device_type = RHI_PhysicalDevice_Type::Integrated;
                }

                RHI_Device::PhysicalDeviceRegister(RHI_PhysicalDevice
                (
                    12 << 22,                                                  // api version (d3d12)
                    0,                                                         // driver version
                    nullptr,                                                   // driver info
                    adapter_desc1.VendorId,                                    // vendor id
                    device_type,                                               // type
                    &name[0],                                                  // name
                    static_cast<uint64_t>(adapter_desc1.DedicatedVideoMemory), // memory
                    static_cast<void*>(display_adapter))                       // data
                );
            }
        }
        
        void select_primary()
        {
            // get the first available device
            RHI_Device::PhysicalDeviceSetPrimary(0);
        }
    }

    void RHI_Device::Initialize()
    {
        // detect device limits
        m_max_texture_1d_dimension   = D3D12_REQ_TEXTURE1D_U_DIMENSION;
        m_max_texture_2d_dimension   = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        m_max_texture_3d_dimension   = D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
        m_max_texture_cube_dimension = D3D12_REQ_TEXTURECUBE_DIMENSION;
        m_max_texture_array_layers   = D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;
        m_max_push_constant_size     = 256; // d3d12 root constants

        // find a physical device
        device_physical::detect_all();
        device_physical::select_primary();

        // debug layer
        UINT dxgi_factory_flags = 0;
        if (Debugging::IsValidationLayerEnabled())
        {
            Microsoft::WRL::ComPtr<ID3D12Debug1> debug_interface;
            if (d3d12_utility::error::check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_interface))))
            {
                debug_interface->EnableDebugLayer();
                debug_interface->SetEnableGPUBasedValidation(true);
                dxgi_factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
                SP_LOG_INFO("D3D12 debug layer enabled");
            }
        }

        // factory
        Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
        SP_ASSERT_MSG(d3d12_utility::error::check(CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&factory))), "Failed to create dxgi factory");

        // adapter
        D3D_FEATURE_LEVEL minimum_feature_level = D3D_FEATURE_LEVEL_12_0;
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        {
            for (UINT adapter_index = 0; DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(adapter_index, &adapter); ++adapter_index)
            {
                DXGI_ADAPTER_DESC1 desc;
                adapter->GetDesc1(&desc);

                // skip basic render driver adapter
                if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                    continue;

                // check to see if the adapter supports direct3d 12
                if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), minimum_feature_level, _uuidof(ID3D12Device), nullptr)))
                    break;
            }
        }

        // device
        SP_ASSERT_MSG(d3d12_utility::error::check(D3D12CreateDevice(
            adapter.Get(),
            minimum_feature_level,
            IID_PPV_ARGS(&RHI_Context::device)
        )), "Failed to create device");

        // create command queues
        {
            D3D12_COMMAND_QUEUE_DESC queue_desc = {};
            queue_desc.Flags                    = D3D12_COMMAND_QUEUE_FLAG_NONE;

            // graphics queue
            queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            SP_ASSERT_MSG(d3d12_utility::error::check(RHI_Context::device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queues::graphics))),
                "Failed to create graphics queue");

            // compute queue
            queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
            SP_ASSERT_MSG(d3d12_utility::error::check(RHI_Context::device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queues::compute))),
                "Failed to create compute queue");

            // copy queue
            queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
            SP_ASSERT_MSG(d3d12_utility::error::check(RHI_Context::device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queues::copy))),
                "Failed to create copy queue");
        }

        // create command allocators
        {
            SP_ASSERT_MSG(d3d12_utility::error::check(RHI_Context::device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&queues::allocator_graphics))),
                "Failed to create graphics command allocator");

            SP_ASSERT_MSG(d3d12_utility::error::check(RHI_Context::device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&queues::allocator_compute))),
                "Failed to create compute command allocator");

            SP_ASSERT_MSG(d3d12_utility::error::check(RHI_Context::device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&queues::allocator_copy))),
                "Failed to create copy command allocator");
        }

        // create fence for synchronization
        {
            SP_ASSERT_MSG(d3d12_utility::error::check(RHI_Context::device->CreateFence(
                0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&queues::fence_graphics))),
                "Failed to create fence");

            queues::fence_value_graphics = 1;
            queues::fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            SP_ASSERT_MSG(queues::fence_event != nullptr, "Failed to create fence event");
        }

        // create descriptor heaps
        {
            // rtv heap
            D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
            rtv_heap_desc.NumDescriptors = 100;
            rtv_heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            rtv_heap_desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            SP_ASSERT_MSG(d3d12_utility::error::check(RHI_Context::device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&descriptors::heap_rtv))),
                "Failed to create RTV descriptor heap");

            // dsv heap
            D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {};
            dsv_heap_desc.NumDescriptors = 100;
            dsv_heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            dsv_heap_desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            SP_ASSERT_MSG(d3d12_utility::error::check(RHI_Context::device->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(&descriptors::heap_dsv))),
                "Failed to create DSV descriptor heap");

            // cbv/srv/uav heap (shader visible for bindless)
            D3D12_DESCRIPTOR_HEAP_DESC cbv_srv_uav_heap_desc = {};
            cbv_srv_uav_heap_desc.NumDescriptors = 10000;
            cbv_srv_uav_heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            cbv_srv_uav_heap_desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            SP_ASSERT_MSG(d3d12_utility::error::check(RHI_Context::device->CreateDescriptorHeap(&cbv_srv_uav_heap_desc, IID_PPV_ARGS(&descriptors::heap_cbv_srv_uav))),
                "Failed to create CBV/SRV/UAV descriptor heap");

            // get descriptor sizes
            descriptors::rtv_descriptor_size       = RHI_Context::device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            descriptors::dsv_descriptor_size       = RHI_Context::device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
            descriptors::cbv_srv_uav_descriptor_size = RHI_Context::device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }

        // create rhi queue wrappers
        queues::regular[static_cast<uint32_t>(RHI_Queue_Type::Graphics)] = make_shared<RHI_Queue>(RHI_Queue_Type::Graphics, "graphics");
        queues::regular[static_cast<uint32_t>(RHI_Queue_Type::Compute)]  = make_shared<RHI_Queue>(RHI_Queue_Type::Compute,  "compute");
        queues::regular[static_cast<uint32_t>(RHI_Queue_Type::Copy)]     = make_shared<RHI_Queue>(RHI_Queue_Type::Copy,     "copy");

        SP_LOG_INFO("DirectX 12.0 initialized successfully");
    }

    void RHI_Device::Tick(const uint64_t frame_count)
    {
        // nothing needed for d3d12 tick
    }

    void RHI_Device::Destroy()
    {
        QueueWaitAll();

        // destroy queues and allocators
        queues::destroy();

        // destroy descriptor heaps
        descriptors::destroy();

        // destroy command queues
        if (queues::graphics) { queues::graphics->Release(); queues::graphics = nullptr; }
        if (queues::compute) { queues::compute->Release(); queues::compute = nullptr; }
        if (queues::copy) { queues::copy->Release(); queues::copy = nullptr; }

        // destroy device
        if (RHI_Context::device)
        {
            RHI_Context::device->Release();
            RHI_Context::device = nullptr;
        }
    }

    void RHI_Device::QueueWaitAll(const bool flush)
    {
        // wait for graphics queue
        if (queues::graphics && queues::fence_graphics)
        {
            const uint64_t fence_value = queues::fence_value_graphics;
            queues::graphics->Signal(queues::fence_graphics, fence_value);
            queues::fence_value_graphics++;

            if (queues::fence_graphics->GetCompletedValue() < fence_value)
            {
                queues::fence_graphics->SetEventOnCompletion(fence_value, queues::fence_event);
                WaitForSingleObject(queues::fence_event, INFINITE);
            }
        }
    }

    uint32_t RHI_Device::GetQueueIndex(const RHI_Queue_Type type)
    {
        // d3d12 doesn't use queue indices like vulkan
        return 0;
    }

    RHI_Queue* RHI_Device::GetQueue(const RHI_Queue_Type type)
    {
        if (type == RHI_Queue_Type::Graphics)
            return queues::regular[static_cast<uint32_t>(RHI_Queue_Type::Graphics)].get();

        if (type == RHI_Queue_Type::Compute)
            return queues::regular[static_cast<uint32_t>(RHI_Queue_Type::Compute)].get();

        if (type == RHI_Queue_Type::Copy)
            return queues::regular[static_cast<uint32_t>(RHI_Queue_Type::Copy)].get();

        return nullptr;
    }

    void* RHI_Device::GetQueueRhiResource(const RHI_Queue_Type type)
    {
        if (type == RHI_Queue_Type::Graphics)
            return queues::graphics;

        if (type == RHI_Queue_Type::Compute)
            return queues::compute;

        if (type == RHI_Queue_Type::Copy)
            return queues::copy;

        return nullptr;
    }

    void RHI_Device::DeletionQueueAdd(const RHI_Resource_Type resource_type, void* resource)
    {
        // todo: implement deletion queue for d3d12
    }

    void RHI_Device::DeletionQueueParse()
    {
        // todo: implement deletion queue for d3d12
    }

    bool RHI_Device::DeletionQueueNeedsToParse()
    {
        return false;
    }

    void RHI_Device::UpdateBindlessResources(
        array<RHI_Texture*, rhi_max_array_size>* material_textures,
        RHI_Buffer* material_parameters,
        RHI_Buffer* light_parameters,
        const std::array<std::shared_ptr<RHI_Sampler>, static_cast<uint32_t>(Renderer_Sampler::Max)>* samplers,
        RHI_Buffer* aabbs,
        RHI_Buffer* draw_data
    )
    {
        // todo: implement bindless resources for d3d12
    }

    void* RHI_Device::MemoryGetMappedDataFromBuffer(void* resource)
    {
        // d3d12 buffers handle their own mapping
        return nullptr;
    }

    void RHI_Device::MemoryBufferCreate(void*& resource, const uint64_t size, uint32_t flags_usage, uint32_t flags_memory, const void* data, const char* name)
    {
        // d3d12 buffers are created directly in RHI_Buffer
    }

    void RHI_Device::MemoryBufferDestroy(void*& resource)
    {
        if (resource)
        {
            static_cast<ID3D12Resource*>(resource)->Release();
            resource = nullptr;
        }
    }

    void RHI_Device::MemoryTextureCreate(RHI_Texture* texture)
    {
        // d3d12 textures are created directly in RHI_Texture
    }

    void RHI_Device::MemoryTextureDestroy(void*& resource)
    {
        if (resource)
        {
            static_cast<ID3D12Resource*>(resource)->Release();
            resource = nullptr;
        }
    }

    void RHI_Device::MemoryMap(void* resource, void*& mapped_data)
    {
        if (resource)
        {
            ID3D12Resource* d3d_resource = static_cast<ID3D12Resource*>(resource);
            D3D12_RANGE read_range = { 0, 0 };
            d3d_resource->Map(0, &read_range, &mapped_data);
        }
    }

    void RHI_Device::MemoryUnmap(void* resource)
    {
        if (resource)
        {
            static_cast<ID3D12Resource*>(resource)->Unmap(0, nullptr);
        }
    }

    uint64_t RHI_Device::MemoryGetAllocatedMb()
    {
        return 0;
    }

    uint64_t RHI_Device::MemoryGetAvailableMb()
    {
        return 0;
    }

    uint64_t RHI_Device::MemoryGetTotalMb()
    {
        return 0;
    }

    void RHI_Device::GetOrCreatePipeline(RHI_PipelineState& pso, RHI_Pipeline*& pipeline, RHI_DescriptorSetLayout*& descriptor_set_layout)
    {
        pso.Prepare();

        lock_guard<mutex> lock(descriptors::pipeline_mutex);

        // d3d12 doesn't use descriptor set layouts like vulkan
        descriptor_set_layout = nullptr;

        // if no pipeline exists, create one
        uint64_t hash = pso.GetHash();
        auto it = descriptors::pipelines.find(hash);
        if (it == descriptors::pipelines.end())
        {
            it = descriptors::pipelines.emplace(make_pair(hash, make_shared<RHI_Pipeline>(pso, descriptor_set_layout))).first;
        }

        pipeline = it->second.get();
    }

    uint32_t RHI_Device::GetPipelineCount()
    {
        return static_cast<uint32_t>(descriptors::pipelines.size());
    }

    void RHI_Device::SetResourceName(void* resource, const RHI_Resource_Type resource_type, const char* name)
    {
        if (resource && name)
        {
            d3d12_utility::debug::set_name(resource, name);
        }
    }

    void RHI_Device::MarkerBegin(RHI_CommandList* cmd_list, const char* name, const math::Vector4& color)
    {
        // todo: implement pix markers for d3d12
    }

    void RHI_Device::MarkerEnd(RHI_CommandList* cmd_list)
    {
        // todo: implement pix markers for d3d12
    }

    void RHI_Device::SetVariableRateShading(const RHI_CommandList* cmd_list, const bool enabled)
    {
        // todo: implement vrs for d3d12
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
        // todo: implement descriptor set allocation for d3d12
    }

    unordered_map<uint64_t, RHI_DescriptorSet>& RHI_Device::GetDescriptorSets()
    {
        static unordered_map<uint64_t, RHI_DescriptorSet> descriptors;
        return descriptors;
    }

    uint64_t RHI_Device::GetBufferDeviceAddress(void* buffer)
    {
        if (buffer)
        {
            return static_cast<ID3D12Resource*>(buffer)->GetGPUVirtualAddress();
        }
        return 0;
    }
}

// expose descriptor heap access for other d3d12 files
namespace spartan::d3d12_descriptors
{
    ID3D12DescriptorHeap* GetRtvHeap() { return descriptors::heap_rtv; }
    ID3D12DescriptorHeap* GetDsvHeap() { return descriptors::heap_dsv; }
    ID3D12DescriptorHeap* GetCbvSrvUavHeap() { return descriptors::heap_cbv_srv_uav; }

    uint32_t GetRtvDescriptorSize() { return descriptors::rtv_descriptor_size; }
    uint32_t GetDsvDescriptorSize() { return descriptors::dsv_descriptor_size; }
    uint32_t GetCbvSrvUavDescriptorSize() { return descriptors::cbv_srv_uav_descriptor_size; }

    D3D12_CPU_DESCRIPTOR_HANDLE GetRtvHandle(uint32_t index)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = descriptors::heap_rtv->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += index * descriptors::rtv_descriptor_size;
        return handle;
    }

    uint32_t AllocateRtv()
    {
        return descriptors::rtv_offset++;
    }

    uint32_t AllocateDsv()
    {
        return descriptors::dsv_offset++;
    }

    uint32_t AllocateCbvSrvUav()
    {
        return descriptors::cbv_srv_uav_offset++;
    }

    ID3D12CommandAllocator* GetGraphicsAllocator() { return queues::allocator_graphics; }
    ID3D12Fence* GetGraphicsFence() { return queues::fence_graphics; }
    uint64_t& GetGraphicsFenceValue() { return queues::fence_value_graphics; }
    HANDLE GetFenceEvent() { return queues::fence_event; }
}
