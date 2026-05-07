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
#include "../RHI_Buffer.h"
#include "../RHI_Texture.h"
#include "../RHI_Sampler.h"
#include <wrl/client.h>
#include "../RHI_Queue.h"
#include "../Core/Debugging.h"
#include <unordered_map>
#include <mutex>
#include <atomic>
//================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace validation
    {
        ID3D12InfoQueue*  info_queue       = nullptr;
        ID3D12InfoQueue1* info_queue1      = nullptr;
        DWORD             callback_cookie  = 0;

        static void __stdcall message_callback(
            D3D12_MESSAGE_CATEGORY,
            D3D12_MESSAGE_SEVERITY severity,
            D3D12_MESSAGE_ID,
            LPCSTR description,
            void*)
        {
            switch (severity)
            {
                case D3D12_MESSAGE_SEVERITY_CORRUPTION:
                case D3D12_MESSAGE_SEVERITY_ERROR:
                    SP_LOG_ERROR("D3D12: %s", description);
                    break;
                case D3D12_MESSAGE_SEVERITY_WARNING:
                    SP_LOG_WARNING("D3D12: %s", description);
                    break;
                case D3D12_MESSAGE_SEVERITY_INFO:
                case D3D12_MESSAGE_SEVERITY_MESSAGE:
                default:
                    SP_LOG_INFO("D3D12: %s", description);
                    break;
            }
        }

        void initialize()
        {
            if (!Debugging::IsValidationLayerEnabled())
                return;

            if (!d3d12_utility::error::check(RHI_Context::device->QueryInterface(IID_PPV_ARGS(&info_queue))))
                return;

            info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR,      TRUE);
            info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING,    FALSE);

            // suppress noisy or false-positive ids that aren't actionable
            D3D12_MESSAGE_ID deny_ids[] =
            {
                D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
                D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
                D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
                D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
            };

            D3D12_INFO_QUEUE_FILTER filter = {};
            filter.DenyList.NumIDs         = static_cast<UINT>(sizeof(deny_ids) / sizeof(deny_ids[0]));
            filter.DenyList.pIDList        = deny_ids;
            info_queue->AddStorageFilterEntries(&filter);

            // prefer a callback (info queue 1) so messages flow into the engine log,
            // fall back to the basic info queue when the runtime doesn't support it
            if (SUCCEEDED(RHI_Context::device->QueryInterface(IID_PPV_ARGS(&info_queue1))))
            {
                if (!d3d12_utility::error::check(info_queue1->RegisterMessageCallback(
                    &message_callback,
                    D3D12_MESSAGE_CALLBACK_FLAG_NONE,
                    nullptr,
                    &callback_cookie)))
                {
                    info_queue1->Release();
                    info_queue1     = nullptr;
                    callback_cookie = 0;
                }
            }

            SP_LOG_INFO("D3D12 info queue active (break-on-error enabled)");
        }

        void destroy()
        {
            if (info_queue1)
            {
                if (callback_cookie != 0)
                {
                    info_queue1->UnregisterMessageCallback(callback_cookie);
                    callback_cookie = 0;
                }
                info_queue1->Release();
                info_queue1 = nullptr;
            }

            if (info_queue)
            {
                info_queue->Release();
                info_queue = nullptr;
            }
        }
    }

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
        // heap sizes
        constexpr uint32_t rtv_heap_size            = 512;
        constexpr uint32_t dsv_heap_size            = 256;
        constexpr uint32_t cbv_srv_uav_heap_size    = 100000;
        constexpr uint32_t sampler_heap_size        = 512;

        // bindless zone sizes within cbv_srv_uav heap
        constexpr uint32_t bindless_textures_count  = 16384;
        constexpr uint32_t bindless_buffers_count   = 16; // material, light, aabb, draw_data, geometry verts/indices/instances etc.

        // bindless zone sizes within sampler heap
        constexpr uint32_t bindless_samplers_compare_count = 64;
        constexpr uint32_t bindless_samplers_count         = 64;

        // descriptor heaps
        ID3D12DescriptorHeap* heap_rtv         = nullptr;
        ID3D12DescriptorHeap* heap_dsv         = nullptr;
        ID3D12DescriptorHeap* heap_cbv_srv_uav = nullptr;
        ID3D12DescriptorHeap* heap_sampler     = nullptr;

        // non-shader-visible cpu staging heaps for copying descriptors into the visible ring
        ID3D12DescriptorHeap* heap_cbv_srv_uav_cpu = nullptr;
        ID3D12DescriptorHeap* heap_sampler_cpu     = nullptr;

        uint32_t rtv_descriptor_size         = 0;
        uint32_t dsv_descriptor_size         = 0;
        uint32_t cbv_srv_uav_descriptor_size = 0;
        uint32_t sampler_descriptor_size     = 0;

        // current allocation offsets
        atomic<uint32_t> rtv_offset             = 0;
        atomic<uint32_t> dsv_offset             = 0;
        atomic<uint32_t> cbv_srv_uav_cpu_offset = 0;
        atomic<uint32_t> sampler_cpu_offset     = 0;

        // zone bases inside the shader-visible heaps
        uint32_t zone_bindless_textures_base   = 0;
        uint32_t zone_bindless_buffers_base    = 0;
        uint32_t zone_ring_base                = 0;
        uint32_t zone_ring_size                = 0;
        atomic<uint32_t> zone_ring_offset      = 0;

        uint32_t zone_samplers_compare_base    = 0;
        uint32_t zone_samplers_base            = 0;

        // pipeline cache
        unordered_map<uint64_t, shared_ptr<RHI_Pipeline>> pipelines;
        mutex pipeline_mutex;

        void destroy()
        {
            pipelines.clear();
            if (heap_rtv)             { heap_rtv->Release();             heap_rtv = nullptr; }
            if (heap_dsv)             { heap_dsv->Release();             heap_dsv = nullptr; }
            if (heap_cbv_srv_uav)     { heap_cbv_srv_uav->Release();     heap_cbv_srv_uav = nullptr; }
            if (heap_sampler)         { heap_sampler->Release();         heap_sampler = nullptr; }
            if (heap_cbv_srv_uav_cpu) { heap_cbv_srv_uav_cpu->Release(); heap_cbv_srv_uav_cpu = nullptr; }
            if (heap_sampler_cpu)     { heap_sampler_cpu->Release();     heap_sampler_cpu = nullptr; }
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
                IDXGIAdapter1* adapter1 = nullptr;
                DXGI_ADAPTER_DESC1 adapter_desc1 = {};
                
                if (SUCCEEDED(display_adapter->QueryInterface(IID_PPV_ARGS(&adapter1))))
                {
                    adapter1->GetDesc1(&adapter_desc1);
                    adapter1->Release();
                }
                else
                {
                    DXGI_ADAPTER_DESC adapter_desc;
                    if (FAILED(display_adapter->GetDesc(&adapter_desc)))
                    {
                        SP_LOG_ERROR("Failed to get adapter description");
                        continue;
                    }
                    wcscpy_s(adapter_desc1.Description, adapter_desc.Description);
                    adapter_desc1.VendorId = adapter_desc.VendorId;
                    adapter_desc1.DedicatedVideoMemory = adapter_desc.DedicatedVideoMemory;
                    adapter_desc1.Flags = 0;
                }

                char name[128];
                auto def_char = ' ';
                WideCharToMultiByte(CP_ACP, 0, adapter_desc1.Description, -1, name, 128, &def_char, nullptr);

                RHI_PhysicalDevice_Type device_type = RHI_PhysicalDevice_Type::Discrete;
                if (adapter_desc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                {
                    device_type = RHI_PhysicalDevice_Type::Cpu;
                }
                else if (adapter_desc1.DedicatedVideoMemory == 0)
                {
                    device_type = RHI_PhysicalDevice_Type::Integrated;
                }

                RHI_Device::PhysicalDeviceRegister(RHI_PhysicalDevice
                (
                    12 << 22,
                    0,
                    nullptr,
                    adapter_desc1.VendorId,
                    device_type,
                    &name[0],
                    static_cast<uint64_t>(adapter_desc1.DedicatedVideoMemory),
                    static_cast<void*>(display_adapter))
                );
            }
        }
        
        void select_primary()
        {
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
        m_max_push_constant_size     = 256;

        device_physical::detect_all();
        device_physical::select_primary();

        UINT dxgi_factory_flags = 0;
        if (Debugging::IsValidationLayerEnabled())
        {
            Microsoft::WRL::ComPtr<ID3D12Debug1> debug_interface;
            if (d3d12_utility::error::check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_interface))))
            {
                debug_interface->EnableDebugLayer();
                dxgi_factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
                SP_LOG_INFO("D3D12 debug layer enabled");

                // gpu-based validation requires the debug layer and adds significant overhead,
                // so it is gated on its own flag and only enabled when explicitly requested
                if (Debugging::IsGpuAssistedValidationEnabled())
                {
                    debug_interface->SetEnableGPUBasedValidation(TRUE);
                    debug_interface->SetEnableSynchronizedCommandQueueValidation(TRUE);
                    SP_LOG_INFO("D3D12 gpu-based validation enabled");
                }
            }
            else
            {
                SP_LOG_WARNING("D3D12 debug layer requested but unavailable, install the graphics tools optional feature in windows");
            }
        }

        Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
        SP_ASSERT_MSG(d3d12_utility::error::check(CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&factory))), "Failed to create dxgi factory");

        D3D_FEATURE_LEVEL minimum_feature_level = D3D_FEATURE_LEVEL_12_0;
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        {
            for (UINT adapter_index = 0; DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(adapter_index, &adapter); ++adapter_index)
            {
                DXGI_ADAPTER_DESC1 desc;
                adapter->GetDesc1(&desc);

                if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                    continue;

                if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), minimum_feature_level, _uuidof(ID3D12Device), nullptr)))
                    break;
            }
        }

        SP_ASSERT_MSG(d3d12_utility::error::check(D3D12CreateDevice(
            adapter.Get(),
            minimum_feature_level,
            IID_PPV_ARGS(&RHI_Context::device)
        )), "Failed to create device");

        // hook up the info queue so debug layer messages flow into the engine log
        validation::initialize();

        // create command queues
        {
            D3D12_COMMAND_QUEUE_DESC queue_desc = {};
            queue_desc.Flags                    = D3D12_COMMAND_QUEUE_FLAG_NONE;

            queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            SP_ASSERT_MSG(d3d12_utility::error::check(RHI_Context::device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queues::graphics))),
                "Failed to create graphics queue");

            queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
            SP_ASSERT_MSG(d3d12_utility::error::check(RHI_Context::device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queues::compute))),
                "Failed to create compute queue");

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
            // rtv heap (cpu-only)
            D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
            rtv_heap_desc.NumDescriptors = descriptors::rtv_heap_size;
            rtv_heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            rtv_heap_desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            SP_ASSERT_MSG(d3d12_utility::error::check(RHI_Context::device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&descriptors::heap_rtv))),
                "Failed to create RTV descriptor heap");

            // dsv heap (cpu-only)
            D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {};
            dsv_heap_desc.NumDescriptors = descriptors::dsv_heap_size;
            dsv_heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            dsv_heap_desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            SP_ASSERT_MSG(d3d12_utility::error::check(RHI_Context::device->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(&descriptors::heap_dsv))),
                "Failed to create DSV descriptor heap");

            // cbv/srv/uav heap (shader-visible) - layout: bindless_textures | bindless_buffers | ring
            D3D12_DESCRIPTOR_HEAP_DESC cbv_srv_uav_heap_desc = {};
            cbv_srv_uav_heap_desc.NumDescriptors = descriptors::cbv_srv_uav_heap_size;
            cbv_srv_uav_heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            cbv_srv_uav_heap_desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            SP_ASSERT_MSG(d3d12_utility::error::check(RHI_Context::device->CreateDescriptorHeap(&cbv_srv_uav_heap_desc, IID_PPV_ARGS(&descriptors::heap_cbv_srv_uav))),
                "Failed to create CBV/SRV/UAV descriptor heap");

            // sampler heap (shader-visible) - layout: samplers_compare | samplers
            D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_desc = {};
            sampler_heap_desc.NumDescriptors = descriptors::sampler_heap_size;
            sampler_heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
            sampler_heap_desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            SP_ASSERT_MSG(d3d12_utility::error::check(RHI_Context::device->CreateDescriptorHeap(&sampler_heap_desc, IID_PPV_ARGS(&descriptors::heap_sampler))),
                "Failed to create sampler descriptor heap");

            // cpu-only staging heaps for sources used by CopyDescriptors
            D3D12_DESCRIPTOR_HEAP_DESC cpu_cbv_heap_desc = {};
            cpu_cbv_heap_desc.NumDescriptors = 100000;
            cpu_cbv_heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            cpu_cbv_heap_desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            SP_ASSERT_MSG(d3d12_utility::error::check(RHI_Context::device->CreateDescriptorHeap(&cpu_cbv_heap_desc, IID_PPV_ARGS(&descriptors::heap_cbv_srv_uav_cpu))),
                "Failed to create CPU staging CBV/SRV/UAV heap");

            D3D12_DESCRIPTOR_HEAP_DESC cpu_sampler_heap_desc = {};
            cpu_sampler_heap_desc.NumDescriptors = 1024;
            cpu_sampler_heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
            cpu_sampler_heap_desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            SP_ASSERT_MSG(d3d12_utility::error::check(RHI_Context::device->CreateDescriptorHeap(&cpu_sampler_heap_desc, IID_PPV_ARGS(&descriptors::heap_sampler_cpu))),
                "Failed to create CPU staging sampler heap");

            // get descriptor sizes
            descriptors::rtv_descriptor_size         = RHI_Context::device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            descriptors::dsv_descriptor_size         = RHI_Context::device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
            descriptors::cbv_srv_uav_descriptor_size = RHI_Context::device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            descriptors::sampler_descriptor_size     = RHI_Context::device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

            // compute zone bases (cbv/srv/uav)
            descriptors::zone_bindless_textures_base = 0;
            descriptors::zone_bindless_buffers_base  = descriptors::zone_bindless_textures_base + descriptors::bindless_textures_count;
            descriptors::zone_ring_base              = descriptors::zone_bindless_buffers_base  + descriptors::bindless_buffers_count;
            descriptors::zone_ring_size              = descriptors::cbv_srv_uav_heap_size - descriptors::zone_ring_base;
            descriptors::zone_ring_offset            = 0;

            // compute zone bases (samplers)
            descriptors::zone_samplers_compare_base  = 0;
            descriptors::zone_samplers_base          = descriptors::zone_samplers_compare_base + descriptors::bindless_samplers_compare_count;
        }

        // create rhi queue wrappers
        queues::regular[static_cast<uint32_t>(RHI_Queue_Type::Graphics)] = make_shared<RHI_Queue>(RHI_Queue_Type::Graphics, "graphics");
        queues::regular[static_cast<uint32_t>(RHI_Queue_Type::Compute)]  = make_shared<RHI_Queue>(RHI_Queue_Type::Compute,  "compute");
        queues::regular[static_cast<uint32_t>(RHI_Queue_Type::Copy)]     = make_shared<RHI_Queue>(RHI_Queue_Type::Copy,     "copy");

        SP_LOG_INFO("DirectX 12.0 initialized successfully");
    }

    void RHI_Device::Tick(const uint64_t frame_count)
    {
        // wrap the per-frame ring on each tick to avoid unbounded growth
        descriptors::zone_ring_offset.store(0);
    }

    void RHI_Device::Destroy()
    {
        QueueWaitAll();
        queues::destroy();
        descriptors::destroy();

        if (queues::graphics) { queues::graphics->Release(); queues::graphics = nullptr; }
        if (queues::compute)  { queues::compute->Release();  queues::compute  = nullptr; }
        if (queues::copy)     { queues::copy->Release();     queues::copy     = nullptr; }

        // release info queue before the device, since it holds a reference to it
        validation::destroy();

        if (RHI_Context::device)
        {
            RHI_Context::device->Release();
            RHI_Context::device = nullptr;
        }
    }

    void RHI_Device::QueueWaitAll(const bool flush)
    {
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

    uint32_t RHI_Device::GetQueueIndex(const RHI_Queue_Type type) { return 0; }

    RHI_Queue* RHI_Device::GetQueue(const RHI_Queue_Type type)
    {
        if (type == RHI_Queue_Type::Graphics) return queues::regular[static_cast<uint32_t>(RHI_Queue_Type::Graphics)].get();
        if (type == RHI_Queue_Type::Compute)  return queues::regular[static_cast<uint32_t>(RHI_Queue_Type::Compute)].get();
        if (type == RHI_Queue_Type::Copy)     return queues::regular[static_cast<uint32_t>(RHI_Queue_Type::Copy)].get();
        return nullptr;
    }

    void* RHI_Device::GetQueueRhiResource(const RHI_Queue_Type type)
    {
        if (type == RHI_Queue_Type::Graphics) return queues::graphics;
        if (type == RHI_Queue_Type::Compute)  return queues::compute;
        if (type == RHI_Queue_Type::Copy)     return queues::copy;
        return nullptr;
    }

    void RHI_Device::DeletionQueueAdd(const RHI_Resource_Type resource_type, void* resource)      { /* todo */ }
    void RHI_Device::DeletionQueueParse()                                                         { /* todo */ }
    void RHI_Device::DeletionQueueFlush()                                                         { /* todo */ }
    bool RHI_Device::DeletionQueueNeedsToParse()                                                  { return false; }
}

// expose descriptor heap access for other d3d12 files - declared here, fully implemented below
namespace spartan::d3d12_descriptors
{
    ID3D12DescriptorHeap* GetRtvHeap()         { return spartan::descriptors::heap_rtv; }
    ID3D12DescriptorHeap* GetDsvHeap()         { return spartan::descriptors::heap_dsv; }
    ID3D12DescriptorHeap* GetCbvSrvUavHeap()   { return spartan::descriptors::heap_cbv_srv_uav; }
    ID3D12DescriptorHeap* GetSamplerHeap()     { return spartan::descriptors::heap_sampler; }
    ID3D12DescriptorHeap* GetCbvSrvUavHeapCpu(){ return spartan::descriptors::heap_cbv_srv_uav_cpu; }
    ID3D12DescriptorHeap* GetSamplerHeapCpu()  { return spartan::descriptors::heap_sampler_cpu; }

    uint32_t GetRtvDescriptorSize()         { return spartan::descriptors::rtv_descriptor_size; }
    uint32_t GetDsvDescriptorSize()         { return spartan::descriptors::dsv_descriptor_size; }
    uint32_t GetCbvSrvUavDescriptorSize()   { return spartan::descriptors::cbv_srv_uav_descriptor_size; }
    uint32_t GetSamplerDescriptorSize()     { return spartan::descriptors::sampler_descriptor_size; }

    uint32_t GetBindlessTexturesBase()  { return spartan::descriptors::zone_bindless_textures_base; }
    uint32_t GetBindlessTexturesCount() { return spartan::descriptors::bindless_textures_count; }
    uint32_t GetBindlessBuffersBase()   { return spartan::descriptors::zone_bindless_buffers_base; }
    uint32_t GetBindlessBuffersCount()  { return spartan::descriptors::bindless_buffers_count; }
    uint32_t GetSamplersCompareBase()   { return spartan::descriptors::zone_samplers_compare_base; }
    uint32_t GetSamplersCompareCount()  { return spartan::descriptors::bindless_samplers_compare_count; }
    uint32_t GetSamplersBase()          { return spartan::descriptors::zone_samplers_base; }
    uint32_t GetSamplersCount()         { return spartan::descriptors::bindless_samplers_count; }

    // bindless slot indices inside the bindless_buffers zone
    namespace bindless_buffer_slot
    {
        constexpr uint32_t material_parameters  = 0;
        constexpr uint32_t light_parameters     = 1;
        constexpr uint32_t aabbs                = 2;
        constexpr uint32_t draw_data            = 3;
        constexpr uint32_t geometry_vertices    = 4;
        constexpr uint32_t geometry_indices     = 5;
        constexpr uint32_t geometry_instances   = 6;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetRtvHandle(uint32_t index)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = spartan::descriptors::heap_rtv->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += index * spartan::descriptors::rtv_descriptor_size;
        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetDsvHandle(uint32_t index)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = spartan::descriptors::heap_dsv->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += index * spartan::descriptors::dsv_descriptor_size;
        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetCbvSrvUavCpuHandle(uint32_t index)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = spartan::descriptors::heap_cbv_srv_uav_cpu->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += index * spartan::descriptors::cbv_srv_uav_descriptor_size;
        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetSamplerCpuHandle(uint32_t index)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = spartan::descriptors::heap_sampler_cpu->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += index * spartan::descriptors::sampler_descriptor_size;
        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetCbvSrvUavGpuVisibleCpuHandle(uint32_t index)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = spartan::descriptors::heap_cbv_srv_uav->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += index * spartan::descriptors::cbv_srv_uav_descriptor_size;
        return handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetCbvSrvUavGpuHandle(uint32_t index)
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle = spartan::descriptors::heap_cbv_srv_uav->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += index * spartan::descriptors::cbv_srv_uav_descriptor_size;
        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetSamplerGpuVisibleCpuHandle(uint32_t index)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = spartan::descriptors::heap_sampler->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += index * spartan::descriptors::sampler_descriptor_size;
        return handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetSamplerGpuHandle(uint32_t index)
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle = spartan::descriptors::heap_sampler->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += index * spartan::descriptors::sampler_descriptor_size;
        return handle;
    }

    uint32_t AllocateRtv()             { return spartan::descriptors::rtv_offset.fetch_add(1); }
    uint32_t AllocateDsv()             { return spartan::descriptors::dsv_offset.fetch_add(1); }
    uint32_t AllocateCbvSrvUavCpu()    { return spartan::descriptors::cbv_srv_uav_cpu_offset.fetch_add(1); }
    uint32_t AllocateSamplerCpu()      { return spartan::descriptors::sampler_cpu_offset.fetch_add(1); }

    // ring allocator inside the shader-visible cbv/srv/uav heap; wraps once full
    uint32_t AllocateRing(uint32_t count)
    {
        if (count == 0) return spartan::descriptors::zone_ring_base;
        const uint32_t ring_size = spartan::descriptors::zone_ring_size;
        uint32_t prev = spartan::descriptors::zone_ring_offset.fetch_add(count);
        if (prev + count > ring_size)
        {
            // wrap (simple, racey but acceptable: gpu may still be reading old content; ring is huge enough)
            spartan::descriptors::zone_ring_offset.store(count);
            prev = 0;
        }
        return spartan::descriptors::zone_ring_base + prev;
    }

    ID3D12CommandAllocator* GetGraphicsAllocator() { return spartan::queues::allocator_graphics; }
    ID3D12Fence*            GetGraphicsFence()     { return spartan::queues::fence_graphics; }
    uint64_t&               GetGraphicsFenceValue(){ return spartan::queues::fence_value_graphics; }
    HANDLE                  GetFenceEvent()        { return spartan::queues::fence_event; }
}

namespace spartan
{
    void RHI_Device::UpdateBindlessMaterials(array<RHI_Texture*, rhi_max_array_size>* textures, RHI_Buffer* parameters)
    {
        // copy each texture's srv into the bindless_textures zone
        if (textures)
        {
            const uint32_t base    = d3d12_descriptors::GetBindlessTexturesBase();
            const uint32_t cap     = d3d12_descriptors::GetBindlessTexturesCount();
            const uint32_t count   = static_cast<uint32_t>(textures->size());
            const uint32_t to_copy = (count < cap) ? count : cap;

            for (uint32_t i = 0; i < to_copy; i++)
            {
                RHI_Texture* tex = (*textures)[i];
                if (!tex || !tex->GetRhiSrv())
                    continue;

                // tex->GetRhiSrv() stores the cpu handle ptr of the source srv (see D3D12_Texture)
                D3D12_CPU_DESCRIPTOR_HANDLE src;
                src.ptr = reinterpret_cast<SIZE_T>(tex->GetRhiSrv());

                D3D12_CPU_DESCRIPTOR_HANDLE dst = d3d12_descriptors::GetCbvSrvUavGpuVisibleCpuHandle(base + i);
                RHI_Context::device->CopyDescriptorsSimple(1, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }
        }

        // write the material parameters structured-buffer srv at its bindless slot
        if (parameters && parameters->GetRhiResource())
        {
            const uint32_t slot = d3d12_descriptors::GetBindlessBuffersBase() + d3d12_descriptors::bindless_buffer_slot::material_parameters;

            D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
            srv_desc.Format                  = DXGI_FORMAT_UNKNOWN;
            srv_desc.ViewDimension           = D3D12_SRV_DIMENSION_BUFFER;
            srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv_desc.Buffer.NumElements      = parameters->GetElementCount();
            srv_desc.Buffer.StructureByteStride = parameters->GetStride();

            D3D12_CPU_DESCRIPTOR_HANDLE dst = d3d12_descriptors::GetCbvSrvUavGpuVisibleCpuHandle(slot);
            RHI_Context::device->CreateShaderResourceView(static_cast<ID3D12Resource*>(parameters->GetRhiResource()), &srv_desc, dst);
        }
    }

    static void write_bindless_structured_srv(uint32_t slot, RHI_Buffer* buffer)
    {
        if (!buffer || !buffer->GetRhiResource())
            return;

        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Format                     = DXGI_FORMAT_UNKNOWN;
        srv_desc.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
        srv_desc.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Buffer.NumElements         = buffer->GetElementCount();
        srv_desc.Buffer.StructureByteStride = buffer->GetStride();

        const uint32_t descriptor_index = d3d12_descriptors::GetBindlessBuffersBase() + slot;
        D3D12_CPU_DESCRIPTOR_HANDLE dst  = d3d12_descriptors::GetCbvSrvUavGpuVisibleCpuHandle(descriptor_index);
        RHI_Context::device->CreateShaderResourceView(static_cast<ID3D12Resource*>(buffer->GetRhiResource()), &srv_desc, dst);
    }

    void RHI_Device::UpdateBindlessLights(RHI_Buffer* parameters)
    {
        write_bindless_structured_srv(d3d12_descriptors::bindless_buffer_slot::light_parameters, parameters);
    }

    void RHI_Device::UpdateBindlessSamplers(const std::array<std::shared_ptr<RHI_Sampler>, static_cast<uint32_t>(Renderer_Sampler::Max)>* samplers)
    {
        if (!samplers)
            return;

        const uint32_t base_compare = d3d12_descriptors::GetSamplersCompareBase();
        const uint32_t base_sampler = d3d12_descriptors::GetSamplersBase();

        // first sampler is comparison sampler (per renderer convention) - copy into compare zone
        // others go into the regular sampler zone
        for (uint32_t i = 0; i < samplers->size(); i++)
        {
            const auto& s = (*samplers)[i];
            if (!s || !s->GetRhiResource())
                continue;

            D3D12_CPU_DESCRIPTOR_HANDLE src;
            src.ptr = reinterpret_cast<SIZE_T>(s->GetRhiResource());

            // index 0 is compare (Compare_depth in the enum), rest go into samplers
            uint32_t dst_index = (i == 0) ? base_compare : (base_sampler + (i - 1));
            D3D12_CPU_DESCRIPTOR_HANDLE dst = d3d12_descriptors::GetSamplerGpuVisibleCpuHandle(dst_index);
            RHI_Context::device->CopyDescriptorsSimple(1, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        }
    }

    void RHI_Device::UpdateBindlessAABBs(RHI_Buffer* buffer)             { write_bindless_structured_srv(d3d12_descriptors::bindless_buffer_slot::aabbs, buffer); }
    void RHI_Device::UpdateBindlessDrawData(RHI_Buffer* buffer)          { write_bindless_structured_srv(d3d12_descriptors::bindless_buffer_slot::draw_data, buffer); }
    void RHI_Device::UpdateBindlessGeometryVertices(RHI_Buffer* buffer)  { write_bindless_structured_srv(d3d12_descriptors::bindless_buffer_slot::geometry_vertices, buffer); }
    void RHI_Device::UpdateBindlessGeometryIndices(RHI_Buffer* buffer)   { write_bindless_structured_srv(d3d12_descriptors::bindless_buffer_slot::geometry_indices, buffer); }
    void RHI_Device::UpdateBindlessInstances(RHI_Buffer* buffer)         { write_bindless_structured_srv(d3d12_descriptors::bindless_buffer_slot::geometry_instances, buffer); }

    void* RHI_Device::MemoryGetMappedDataFromBuffer(void* resource)
    {
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

    uint64_t RHI_Device::MemoryGetAllocatedMb() { return 0; }
    uint64_t RHI_Device::MemoryGetAvailableMb() { return 0; }
    uint64_t RHI_Device::MemoryGetTotalMb()     { return 0; }

    // staging buffer pool - simple acquire/release wrapping committed upload buffers
    namespace staging
    {
        struct Entry { ID3D12Resource* res = nullptr; uint64_t size = 0; bool in_use = false; };
        vector<Entry> pool;
        mutex mutex_pool;
    }

    void* RHI_Device::StagingBufferAcquire(uint64_t size)
    {
        lock_guard<mutex> lock(staging::mutex_pool);

        // find a free entry that fits
        for (auto& e : staging::pool)
        {
            if (!e.in_use && e.size >= size)
            {
                e.in_use = true;
                return e.res;
            }
        }

        // create a new upload buffer
        D3D12_HEAP_PROPERTIES props = {};
        props.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width              = size;
        desc.Height             = 1;
        desc.DepthOrArraySize   = 1;
        desc.MipLevels          = 1;
        desc.Format             = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count   = 1;
        desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ID3D12Resource* res = nullptr;
        if (FAILED(RHI_Context::device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res))))
            return nullptr;

        staging::pool.push_back({ res, size, true });
        return res;
    }

    void RHI_Device::StagingBufferRelease(void* buffer)
    {
        lock_guard<mutex> lock(staging::mutex_pool);
        for (auto& e : staging::pool)
        {
            if (e.res == buffer)
            {
                e.in_use = false;
                return;
            }
        }
    }

    void RHI_Device::StagingBufferPoolDestroy()
    {
        lock_guard<mutex> lock(staging::mutex_pool);
        for (auto& e : staging::pool)
        {
            if (e.res) e.res->Release();
        }
        staging::pool.clear();
    }

    void RHI_Device::GetOrCreatePipeline(RHI_PipelineState& pso, RHI_Pipeline*& pipeline, RHI_DescriptorSetLayout*& descriptor_set_layout)
    {
        pso.Prepare();

        lock_guard<mutex> lock(descriptors::pipeline_mutex);

        descriptor_set_layout = nullptr;

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

    void* RHI_Device::GetPipelineCache()
    {
        return nullptr;
    }

    void RHI_Device::SetResourceName(void* resource, const RHI_Resource_Type resource_type, const char* name)
    {
        if (resource && name)
        {
            d3d12_utility::debug::set_name(resource, name);
        }
    }

    void RHI_Device::MarkerBegin(RHI_CommandList* cmd_list, const char* name, const math::Vector4& color) { }
    void RHI_Device::MarkerEnd(RHI_CommandList* cmd_list) { }
    void RHI_Device::SetVariableRateShading(const RHI_CommandList* cmd_list, const bool enabled) { }
    void* RHI_Device::GetDescriptorSet(const RHI_Device_Bindless_Resource resource_type) { return nullptr; }
    void* RHI_Device::GetDescriptorSetLayout(const RHI_Device_Bindless_Resource resource_type) { return nullptr; }
    uint32_t RHI_Device::GetDescriptorType(const RHI_Descriptor& descriptor) { return 0; }

    void RHI_Device::AllocateDescriptorSet(void*& resource, RHI_DescriptorSetLayout* descriptor_set_layout, const vector<RHI_DescriptorWithBinding>& descriptors)
    {
        // d3d12 doesn't use this api
    }

    unordered_map<uint64_t, RHI_DescriptorSet>& RHI_Device::GetDescriptorSets()
    {
        static unordered_map<uint64_t, RHI_DescriptorSet> descriptors;
        return descriptors;
    }

    uint64_t RHI_Device::GetDescriptorSetFrame() { return 0; }

    uint64_t RHI_Device::GetBufferDeviceAddress(void* buffer)
    {
        if (buffer)
        {
            return static_cast<ID3D12Resource*>(buffer)->GetGPUVirtualAddress();
        }
        return 0;
    }
}
