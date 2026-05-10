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
#include "../Rendering/Renderer_Definitions.h"
#include "D3D12_Internal.h"
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <vector>
//================================

// pix3blob marker encoding constants, hard-coded so we don't need the winpixeventruntime header pack
// PIX, Renderdoc and the d3d12 debug layer all accept these via ID3D12GraphicsCommandList::BeginEvent etc.
#ifndef PIX_EVENT_ANSI_VERSION
#define PIX_EVENT_ANSI_VERSION 1
#endif
#ifndef PIX_COLOR
#define PIX_COLOR(r, g, b) static_cast<UINT64>(0xff000000u | (static_cast<UINT32>(r) << 16) | (static_cast<UINT32>(g) << 8) | static_cast<UINT32>(b))
#endif

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace validation
    {
        ID3D12InfoQueue*  info_queue      = nullptr;
        ID3D12InfoQueue1* info_queue1     = nullptr;
        DWORD             callback_cookie = 0;

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
                // imgui emits draws with fully-clipped (zero-area) scissor rects for hidden ui elements,
                // d3d12 correctly draws nothing in that case so the warning is informational only
                D3D12_MESSAGE_ID_DRAW_EMPTY_SCISSOR_RECTANGLE,
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

        // per-queue fences for queue-side wait/signal, separate from the rhi sync primitives
        // (fence_graphics is reused as the "everything finished" fence by QueueWaitAll)
        ID3D12Fence* fence_graphics = nullptr;
        ID3D12Fence* fence_compute  = nullptr;
        ID3D12Fence* fence_copy     = nullptr;
        uint64_t fence_value_graphics = 0;
        uint64_t fence_value_compute  = 0;
        uint64_t fence_value_copy     = 0;
        HANDLE fence_event = nullptr;

        ID3D12CommandQueue* get_d3d_queue(RHI_Queue_Type type)
        {
            if (type == RHI_Queue_Type::Graphics) return graphics;
            if (type == RHI_Queue_Type::Compute)  return compute;
            if (type == RHI_Queue_Type::Copy)     return copy;
            return nullptr;
        }

        ID3D12Fence* get_fence(RHI_Queue_Type type)
        {
            if (type == RHI_Queue_Type::Graphics) return fence_graphics;
            if (type == RHI_Queue_Type::Compute)  return fence_compute;
            if (type == RHI_Queue_Type::Copy)     return fence_copy;
            return nullptr;
        }

        uint64_t& get_fence_value(RHI_Queue_Type type)
        {
            if (type == RHI_Queue_Type::Compute)  return fence_value_compute;
            if (type == RHI_Queue_Type::Copy)     return fence_value_copy;
            return fence_value_graphics;
        }

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

            if (fence_graphics) { fence_graphics->Release(); fence_graphics = nullptr; }
            if (fence_compute)  { fence_compute->Release();  fence_compute  = nullptr; }
            if (fence_copy)     { fence_copy->Release();     fence_copy     = nullptr; }

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

    // deferred resource destruction queue, mirrors Vulkan_Device behaviour, age in frames
    namespace deletion
    {
        struct Entry { RHI_Resource_Type type; void* resource; uint64_t frame; };
        std::vector<Entry> queue;
        uint64_t           frame = 0;
        std::mutex         mutex;

        // per-resource destructor switch, called when an entry is old enough to retire
        void destroy_resource(RHI_Resource_Type type, void* resource);
    }

    // pipeline library, populated on first GetPipelineCache call
    namespace pipeline_library
    {
        ID3D12PipelineLibrary* lib = nullptr;
        std::vector<uint8_t>   data;
        std::once_flag         init_flag;
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
        atomic<uint32_t> rtv_offset                       = 0;
        atomic<uint32_t> dsv_offset                       = 0;
        atomic<uint32_t> cbv_srv_uav_cpu_offset           = 0; // monotonic, used by static allocations (texture init etc)
        atomic<uint32_t> cbv_srv_uav_cpu_transient_offset = 0; // wraps inside the transient zone, used by per-frame transient views
        atomic<uint32_t> sampler_cpu_offset               = 0;

        // cpu staging heap layout (cbv/srv/uav)
        // [0, cpu_static_size)                            static views (texture/buffer creation)
        // [cpu_static_size, cpu_static_size + cpu_transient_size) transient views (set_texture mip views, set_acceleration_structure, set_buffer_uav)
        constexpr uint32_t cbv_srv_uav_cpu_static_size    = 100000;
        constexpr uint32_t cbv_srv_uav_cpu_transient_size = 200000;
        constexpr uint32_t cbv_srv_uav_cpu_total_size     = cbv_srv_uav_cpu_static_size + cbv_srv_uav_cpu_transient_size;

        // sampler cpu heap free list, populated when samplers are destroyed so the slot can be reused
        std::vector<uint32_t> sampler_free_list;
        std::mutex            sampler_free_list_mutex;

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
        // detect device limits, the rt and vrs caps below are queried from the d3d12 device
        m_max_texture_1d_dimension   = D3D12_REQ_TEXTURE1D_U_DIMENSION;
        m_max_texture_2d_dimension   = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        m_max_texture_3d_dimension   = D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
        m_max_texture_cube_dimension = D3D12_REQ_TEXTURECUBE_DIMENSION;
        m_max_texture_array_layers   = D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;
        m_max_push_constant_size     = 256;

        // alignment requirements, fixed by the d3d12 spec
        m_min_uniform_buffer_offset_alignment      = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;       // 256
        m_min_storage_buffer_offset_alignment      = 16;                                                  // structured buffers, conservative
        m_min_acceleration_buffer_offset_alignment = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT; // 256
        m_optimal_buffer_copy_offset_alignment     = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;              // 512

        // dxr shader binding table sizes, fixed by the d3d12 spec
        m_shader_group_handle_size      = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;            // 32
        m_shader_group_handle_alignment = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT;    // 32
        m_shader_group_base_alignment   = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;     // 64

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

        // query feature support for ray tracing, vrs, and timestamp period
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
            if (SUCCEEDED(RHI_Context::device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))))
            {
                m_is_ray_tracing_supported = (options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0);
                if (m_is_ray_tracing_supported)
                {
                    SP_LOG_INFO("DXR raytracing tier %d supported", static_cast<int>(options5.RaytracingTier));
                }
            }

            D3D12_FEATURE_DATA_D3D12_OPTIONS6 options6 = {};
            if (SUCCEEDED(RHI_Context::device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &options6, sizeof(options6))))
            {
                // tier 2 supports per-image vrs (RSSetShadingRateImage), tier 1 only per-draw, we need image vrs for parity
                m_is_shading_rate_supported       = (options6.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2);
                m_max_shading_rate_texel_size_x   = options6.ShadingRateImageTileSize;
                m_max_shading_rate_texel_size_y   = options6.ShadingRateImageTileSize;
                if (m_is_shading_rate_supported)
                {
                    SP_LOG_INFO("Variable rate shading tier %d supported (tile size %u)", static_cast<int>(options6.VariableShadingRateTier), options6.ShadingRateImageTileSize);
                }
            }

            // xess always reports as supported on d3d12 since the runtime falls back to a pass-through path on unsupported gpus
            m_xess_supported = true;
        }

        // queue timestamp period: d3d12 uses GetTimestampFrequency on the queue (ticks/second)
        {
            // query graphics queue's timestamp frequency when the queue is created below
            // for now, leave m_timestamp_period 0 and patch it after queue creation
        }

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

            // sample the graphics queue's timestamp frequency, used by RHI_CommandList::GetTimestampResult
            // d3d12 returns ticks-per-second, vulkan-style m_timestamp_period is nanoseconds-per-tick
            UINT64 ticks_per_second = 0;
            if (SUCCEEDED(queues::graphics->GetTimestampFrequency(&ticks_per_second)) && ticks_per_second > 0)
            {
                m_timestamp_period = 1e9f / static_cast<float>(ticks_per_second);
            }
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

        // create fences for synchronization (one per queue so wait/signal across queues can be ordered)
        {
            SP_ASSERT_MSG(d3d12_utility::error::check(RHI_Context::device->CreateFence(
                0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&queues::fence_graphics))),
                "Failed to create graphics fence");

            SP_ASSERT_MSG(d3d12_utility::error::check(RHI_Context::device->CreateFence(
                0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&queues::fence_compute))),
                "Failed to create compute fence");

            SP_ASSERT_MSG(d3d12_utility::error::check(RHI_Context::device->CreateFence(
                0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&queues::fence_copy))),
                "Failed to create copy fence");

            queues::fence_value_graphics = 1;
            queues::fence_value_compute  = 1;
            queues::fence_value_copy     = 1;
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
            cpu_cbv_heap_desc.NumDescriptors = descriptors::cbv_srv_uav_cpu_total_size;
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

        // retire any deletion queue entries old enough to be safe (gpu has finished using them)
        DeletionQueueParse();
    }

    void RHI_Device::Destroy()
    {
        QueueWaitAll();

        // flush the pipeline library to disk (best-effort, ignored on failure)
        if (pipeline_library::lib)
        {
            pipeline_library::lib->Release();
            pipeline_library::lib = nullptr;
        }

        // force-retire any pending deletions, the gpu is guaranteed idle here
        deletion::frame += renderer_draw_data_buffer_count + 2;
        DeletionQueueParse();

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
        // first ensure all rhi command lists per queue are fully submitted/waited
        if (flush)
        {
            for (uint32_t i = 0; i < static_cast<uint32_t>(RHI_Queue_Type::Max); i++)
            {
                if (queues::regular[i])
                    queues::regular[i]->Wait(true);
            }
        }
        else
        {
            for (uint32_t i = 0; i < static_cast<uint32_t>(RHI_Queue_Type::Max); i++)
            {
                if (queues::regular[i])
                    queues::regular[i]->Wait(false);
            }
        }

        // then signal+wait on each queue's per-queue fence as a final guard
        const RHI_Queue_Type queue_types[] = { RHI_Queue_Type::Graphics, RHI_Queue_Type::Compute, RHI_Queue_Type::Copy };
        for (RHI_Queue_Type type : queue_types)
        {
            ID3D12CommandQueue* q = queues::get_d3d_queue(type);
            ID3D12Fence*        f = queues::get_fence(type);
            if (!q || !f) continue;

            uint64_t& fv = queues::get_fence_value(type);
            const uint64_t target = fv++;
            q->Signal(f, target);

            if (f->GetCompletedValue() < target)
            {
                f->SetEventOnCompletion(target, queues::fence_event);
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

    void deletion::destroy_resource(RHI_Resource_Type type, void* resource)
    {
        if (!resource) return;

        switch (type)
        {
            case RHI_Resource_Type::Buffer:
            case RHI_Resource_Type::Image:
            {
                // both are ID3D12Resource, also evict from the global state tracker
                ID3D12Resource* d3d_res = static_cast<ID3D12Resource*>(resource);
                d3d12_state::RemoveState(d3d_res);
                d3d_res->Release();
                break;
            }
            case RHI_Resource_Type::Shader:
            {
                // shader bytecode is owned by IDxcBlob held inside RHI_Shader, so not released here
                break;
            }
            case RHI_Resource_Type::Sampler:
            {
                // d3d12 samplers live in the cpu staging heap, the entry encodes (index << 1 | 1) so we can recover the slot
                uintptr_t encoded = reinterpret_cast<uintptr_t>(resource);
                if (encoded & 0x1ull)
                {
                    uint32_t index = static_cast<uint32_t>(encoded >> 1);
                    d3d12_descriptors::FreeSamplerCpu(index);
                }
                break;
            }
            case RHI_Resource_Type::Pipeline:
            {
                static_cast<ID3D12PipelineState*>(resource)->Release();
                break;
            }
            case RHI_Resource_Type::PipelineLayout:
            {
                // ID3D12RootSignature
                static_cast<IUnknown*>(resource)->Release();
                break;
            }
            case RHI_Resource_Type::DescriptorSetLayout:
            {
                // d3d12 dsl m_rhi_resource is a stable hash token, not a com object, drop without release
                break;
            }
            case RHI_Resource_Type::AccelerationStructure:
            {
                // dxr acceleration structures live inside an ID3D12Resource buffer, the result buffer enqueue handles release
                break;
            }
            case RHI_Resource_Type::Fence:
            case RHI_Resource_Type::Semaphore:
            {
                static_cast<ID3D12Fence*>(resource)->Release();
                break;
            }
            default:
                // best-effort: anything that came in as an IUnknown can be Release()-d, otherwise ignore
                break;
        }
    }

    void RHI_Device::DeletionQueueAdd(const RHI_Resource_Type resource_type, void* resource)
    {
        if (!resource)
            return;

        std::lock_guard<std::mutex> guard(deletion::mutex);
        deletion::queue.push_back({ resource_type, resource, deletion::frame });
    }

    void RHI_Device::DeletionQueueParse()
    {
        std::lock_guard<std::mutex> guard(deletion::mutex);

        deletion::frame++;

        // resources older than (renderer_draw_data_buffer_count + 1) frames are guaranteed to be no longer in flight
        const uint64_t safe_age = renderer_draw_data_buffer_count + 1;

        auto it = deletion::queue.begin();
        while (it != deletion::queue.end())
        {
            if ((deletion::frame - it->frame) < safe_age)
            {
                ++it;
                continue;
            }

            deletion::destroy_resource(it->type, it->resource);
            it = deletion::queue.erase(it);
        }
    }

    void RHI_Device::DeletionQueueFlush()
    {
        {
            std::lock_guard<std::mutex> guard(deletion::mutex);
            for (auto& entry : deletion::queue)
                entry.frame = 0;
        }
        DeletionQueueParse();
    }

    bool RHI_Device::DeletionQueueNeedsToParse()
    {
        std::lock_guard<std::mutex> guard(deletion::mutex);

        if (deletion::queue.empty())
            return false;

        const uint64_t safe_age = renderer_draw_data_buffer_count + 1;
        for (const auto& entry : deletion::queue)
        {
            if ((deletion::frame + 1 - entry.frame) >= safe_age)
                return true;
        }

        return false;
    }
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

    // monotonic allocator for static cpu staging descriptors, used by texture/buffer init
    // never wraps, sized large enough to hold all long-lived views
    uint32_t AllocateCbvSrvUavCpu()
    {
        uint32_t idx = spartan::descriptors::cbv_srv_uav_cpu_offset.fetch_add(1);
        SP_ASSERT_MSG(idx < spartan::descriptors::cbv_srv_uav_cpu_static_size,
            "Static cpu staging heap exhausted, increase cbv_srv_uav_cpu_static_size");
        return idx;
    }

    // ring allocator for transient cpu staging descriptors, wraps inside the dedicated transient zone
    // safe to wrap because transient views are consumed by CopyDescriptorsSimple before any reuse becomes observable
    uint32_t AllocateCbvSrvUavCpuTransient()
    {
        uint32_t offset = spartan::descriptors::cbv_srv_uav_cpu_transient_offset.fetch_add(1);
        return spartan::descriptors::cbv_srv_uav_cpu_static_size + (offset % spartan::descriptors::cbv_srv_uav_cpu_transient_size);
    }
    uint32_t AllocateSamplerCpu()
    {
        // reuse a freed slot first, then bump the offset
        std::lock_guard<std::mutex> lock(spartan::descriptors::sampler_free_list_mutex);
        if (!spartan::descriptors::sampler_free_list.empty())
        {
            uint32_t idx = spartan::descriptors::sampler_free_list.back();
            spartan::descriptors::sampler_free_list.pop_back();
            return idx;
        }
        return spartan::descriptors::sampler_cpu_offset.fetch_add(1);
    }

    void FreeSamplerCpu(uint32_t index)
    {
        std::lock_guard<std::mutex> lock(spartan::descriptors::sampler_free_list_mutex);
        spartan::descriptors::sampler_free_list.push_back(index);
    }

    uint32_t SamplerHandleToIndex(SIZE_T handle_ptr)
    {
        if (!spartan::descriptors::heap_sampler_cpu) return UINT32_MAX;
        SIZE_T base = spartan::descriptors::heap_sampler_cpu->GetCPUDescriptorHandleForHeapStart().ptr;
        if (handle_ptr < base) return UINT32_MAX;
        return static_cast<uint32_t>((handle_ptr - base) / spartan::descriptors::sampler_descriptor_size);
    }

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

    static bool query_video_memory_info(DXGI_QUERY_VIDEO_MEMORY_INFO& out)
    {
        // walk back to the adapter via dxgi factory so memory budget is in sync with dxgi state
        Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))))
            return false;

        const LUID adapter_luid = RHI_Context::device->GetAdapterLuid();
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        if (FAILED(factory->EnumAdapterByLuid(adapter_luid, IID_PPV_ARGS(&adapter))))
            return false;

        Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3;
        if (FAILED(adapter.As(&adapter3)))
            return false;

        return SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &out));
    }

    uint64_t RHI_Device::MemoryGetAllocatedMb()
    {
        DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
        return query_video_memory_info(info) ? (info.CurrentUsage / (1024ull * 1024ull)) : 0;
    }

    uint64_t RHI_Device::MemoryGetAvailableMb()
    {
        DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
        if (!query_video_memory_info(info))
            return 0;
        return (info.Budget > info.CurrentUsage) ? ((info.Budget - info.CurrentUsage) / (1024ull * 1024ull)) : 0;
    }

    uint64_t RHI_Device::MemoryGetTotalMb()
    {
        DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
        return query_video_memory_info(info) ? (info.Budget / (1024ull * 1024ull)) : 0;
    }

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
        // create a small empty pipeline library on first request, callers use it as an opaque handle
        // d3d12 requires the library to be created from a serialized blob, an empty blob is valid
        std::call_once(pipeline_library::init_flag, []()
        {
            Microsoft::WRL::ComPtr<ID3D12Device1> device1;
            if (FAILED(RHI_Context::device->QueryInterface(IID_PPV_ARGS(&device1))))
                return;

            HRESULT hr = device1->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&pipeline_library::lib));
            if (FAILED(hr))
            {
                // unsupported, drivers without pipeline library support fail this with ERROR_UNSUPPORTED
                pipeline_library::lib = nullptr;
            }
        });

        return pipeline_library::lib;
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
        if (!Debugging::IsGpuMarkingEnabled() || !cmd_list || !name)
            return;

        ID3D12GraphicsCommandList* d3d_cmd_list = static_cast<ID3D12GraphicsCommandList*>(cmd_list->GetRhiResource());
        if (!d3d_cmd_list)
            return;

        // PIX_EVENT_ANSI_VERSION encoding: data is a null-terminated ansi string
        const size_t length = strnlen_s(name, 256);
        d3d_cmd_list->BeginEvent(PIX_EVENT_ANSI_VERSION, name, static_cast<UINT>(length + 1));
    }

    void RHI_Device::MarkerEnd(RHI_CommandList* cmd_list)
    {
        if (!Debugging::IsGpuMarkingEnabled() || !cmd_list)
            return;

        ID3D12GraphicsCommandList* d3d_cmd_list = static_cast<ID3D12GraphicsCommandList*>(cmd_list->GetRhiResource());
        if (!d3d_cmd_list)
            return;

        d3d_cmd_list->EndEvent();
    }

    void RHI_Device::SetVariableRateShading(const RHI_CommandList* cmd_list, const bool enabled)
    {
        if (!m_is_shading_rate_supported || !cmd_list)
            return;

        // command list 5 exposes RSSetShadingRate / RSSetShadingRateImage, query for it on first use
        ID3D12GraphicsCommandList* d3d_cmd_list = static_cast<ID3D12GraphicsCommandList*>(cmd_list->GetRhiResource());
        if (!d3d_cmd_list)
            return;

        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList5> cmd_list5;
        if (FAILED(d3d_cmd_list->QueryInterface(IID_PPV_ARGS(&cmd_list5))))
            return;

        if (enabled)
        {
            // combiner: PASSTHROUGH for the per-draw rate (x), OVERRIDE for the per-image (y) so the image always wins
            const D3D12_SHADING_RATE_COMBINER combiners[2] = {
                D3D12_SHADING_RATE_COMBINER_PASSTHROUGH,
                D3D12_SHADING_RATE_COMBINER_OVERRIDE
            };
            cmd_list5->RSSetShadingRate(D3D12_SHADING_RATE_1X1, combiners);

            // the image binding requires a per-pso vrs input texture, set in Renderer_Passes via pso.vrs_input_texture
            // we rely on the command list path to call cmd_list5->RSSetShadingRateImage with the texture's resource at pass-bind time
        }
        else
        {
            cmd_list5->RSSetShadingRate(D3D12_SHADING_RATE_1X1, nullptr);
            cmd_list5->RSSetShadingRateImage(nullptr);
        }
    }

    void* RHI_Device::GetDescriptorSet(const RHI_Device_Bindless_Resource resource_type) { return nullptr; }
    void* RHI_Device::GetDescriptorSetLayout(const RHI_Device_Bindless_Resource resource_type) { return nullptr; }

    // map an rhi descriptor to its d3d12 root parameter slot, used by callers that want to know
    // which root slot a binding maps to in the unified bindless root signature; mirrors the layout
    // baked into create_root_signature_bindless above
    uint32_t RHI_Device::GetDescriptorType(const RHI_Descriptor& descriptor)
    {
        switch (descriptor.type)
        {
            case RHI_Descriptor_Type::ConstantBuffer:        return 0; // CBV b0
            case RHI_Descriptor_Type::PushConstantBuffer:    return 1; // 32-bit constants b1
            case RHI_Descriptor_Type::Image:                 return 2; // SRV table space0
            case RHI_Descriptor_Type::TextureStorage:        return 3; // UAV table space0
            case RHI_Descriptor_Type::StructuredBuffer:      return 2; // SRV table space0 (structured buffers are SRVs)
            case RHI_Descriptor_Type::AccelerationStructure: return 2; // SRV table space0 (tlas srv)
            default:                                         return 0;
        }
    }

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
