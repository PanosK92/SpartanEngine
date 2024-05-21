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

//= INCLUDES ======================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_BlendState.h"
#include "../RHI_RasterizerState.h"
#include "../RHI_Shader.h"
#include "../RHI_InputLayout.h"
#include <wrl/client.h>
#include "../RHI_Queue.h"
#include "../Profiling/Profiler.h"
//=================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    namespace
    {
        // queues
        void* m_queue_graphics = nullptr;
        void* m_queue_compute  = nullptr;
        void* m_queue_copy     = nullptr;

        vector<shared_ptr<RHI_Queue>> queues;
    }

    void RHI_Device::Initialize()
    {
        SP_ERROR_WINDOW("The D3D12 backend is not finished, use it only if your goal is to work on it.");

        // Detect device limits
        m_max_texture_1d_dimension   = D3D12_REQ_TEXTURE1D_U_DIMENSION;
        m_max_texture_2d_dimension   = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        m_max_texture_3d_dimension   = D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
        m_max_texture_cube_dimension = D3D12_REQ_TEXTURECUBE_DIMENSION;
        m_max_texture_array_layers   = D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;

        // Find a physical device
        PhysicalDeviceDetect();
        PhysicalDeviceSelectPrimary();

        // Debug layer
        UINT dxgi_factory_flags = 0;
        if (Profiler::IsValidationLayerEnabled())
        {
            Microsoft::WRL::ComPtr<ID3D12Debug1> debug_interface;
            if (d3d12_utility::error::check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_interface))))
            {
                debug_interface->EnableDebugLayer();
                debug_interface->SetEnableGPUBasedValidation(true);
                dxgi_factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
            }
        }

        // Factory
        Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
        SP_ASSERT_MSG(d3d12_utility::error::check(CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&factory))), "Failed to created dxgi factory");

        // Adapter
        D3D_FEATURE_LEVEL minimum_feature_level = D3D_FEATURE_LEVEL_12_0;
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        {
            for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(adapterIndex, &adapter); ++adapterIndex)
            {
                DXGI_ADAPTER_DESC1 desc;
                adapter->GetDesc1(&desc);

                // Skip Basic Render Driver adapter.
                if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                    continue;

                // Check to see if the adapter supports Direct3D 12, but don't create the actual device yet.
                SP_ASSERT_MSG(d3d12_utility::error::check(D3D12CreateDevice
                (
                    adapter.Get(),
                    minimum_feature_level,
                    _uuidof(ID3D12Device),
                    nullptr
                )), "Failed to create device");
            }
        }

        // Device
        SP_ASSERT_MSG(d3d12_utility::error::check(D3D12CreateDevice
        (
            adapter.Get(),
            minimum_feature_level,
            IID_PPV_ARGS(&RHI_Context::device)
        )), "Failed to create device");

        // Create a graphics, compute and a copy queue.
        {
            D3D12_COMMAND_QUEUE_DESC queue_desc = {};
            queue_desc.Flags                    = D3D12_COMMAND_QUEUE_FLAG_NONE;

            queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            d3d12_utility::error::check(RHI_Context::device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(reinterpret_cast<ID3D12CommandQueue**>(&m_queue_graphics))));

            queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
            d3d12_utility::error::check(RHI_Context::device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(reinterpret_cast<ID3D12CommandQueue**>(&m_queue_compute))));

            queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
            d3d12_utility::error::check(RHI_Context::device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(reinterpret_cast<ID3D12CommandQueue**>(&m_queue_copy))));
        }

        // Create command list allocator
        SP_ASSERT_MSG
        (
            /*d3d12_utility::error::check
            (
                m_rhi_context->device->CreateCommandAllocator
                (
                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                    IID_PPV_ARGS(reinterpret_cast<ID3D12CommandAllocator**>(&m_cmd_pools))
                )
            )*/false,
            "Failed to create command allocator"
         );

        // Log feature level
        std::string level = "12.0";
        Settings::RegisterThirdPartyLib("DirectX", level, "https://en.wikipedia.org/wiki/DirectX");
        SP_LOG_INFO("DirectX %s", level.c_str());
    }

    void RHI_Device::Tick(const uint64_t frame_count)
    {

    }

    void RHI_Device::Destroy()
    {
        SP_ASSERT(m_queue_graphics != nullptr);

        // Command queues
        d3d12_utility::release<ID3D12CommandQueue>(m_queue_graphics);
        d3d12_utility::release<ID3D12CommandQueue>(m_queue_compute);
        d3d12_utility::release<ID3D12CommandQueue>(m_queue_copy);

        // Command allocator
        //d3d12_utility::release<ID3D12CommandAllocator>(m_cmd_pool_graphics);

        QueueWaitAll();

       RHI_Context::device->Release();
       RHI_Context::device = nullptr;
    }

    void RHI_Device::PhysicalDeviceDetect()
    {
        // Create DirectX graphics interface factory
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

        // Get all available adapters
        vector<IDXGIAdapter*> adapters = get_available_adapters(factory);
        factory->Release();
        factory = nullptr;
        SP_ASSERT(adapters.size() > 0);

        // Save all available adapters
        DXGI_ADAPTER_DESC adapter_desc;
        for (IDXGIAdapter* display_adapter : adapters)
        {
            if (FAILED(display_adapter->GetDesc(&adapter_desc)))
            {
                SP_LOG_ERROR("Failed to get adapter description");
                continue;
            }

            // Of course it wouldn't be simple, lets convert the device name
            char name[128];
            auto def_char = ' ';
            WideCharToMultiByte(CP_ACP, 0, adapter_desc.Description, -1, name, 128, &def_char, nullptr);

            PhysicalDeviceRegister(PhysicalDevice
            (
                11 << 22,                                                 // api version
                0,                                                        // driver version
                adapter_desc.VendorId,                                    // vendor id
                RHI_PhysicalDevice_Type::Max,                             // type
                &name[0],                                                 // name
                static_cast<uint64_t>(adapter_desc.DedicatedVideoMemory), // memory
                static_cast<void*>(display_adapter))                      // data
            );
        }
    }

    void RHI_Device::PhysicalDeviceSelectPrimary()
    {
        // Get the first available device
        PhysicalDeviceSetPrimary(0);
    }

    void RHI_Device::QueueWaitAll()
    {

    }

    RHI_Queue* RHI_Device::GetQueue(const RHI_Queue_Type type)
    {
        return nullptr;
    }

    void RHI_Device::DeletionQueueAdd(const RHI_Resource_Type resource_type, void* resource)
    {

    }

    void RHI_Device::DeletionQueueParse()
    {

    }

    bool RHI_Device::DeletionQueueNeedsToParse()
    {
        return false;
    }

    void RHI_Device::UpdateBindlessResources(const array<shared_ptr<RHI_Sampler>, static_cast<uint32_t>(Renderer_Sampler::Max)>* samplers, array<RHI_Texture*, rhi_max_array_size>* textures)
    {

    }

    uint32_t RHI_Device::MemoryGetUsageMb()
    {
        return 0;
    }

    uint32_t RHI_Device::MemoryGetBudgetMb()
    {
        return 0;
    }

    uint32_t RHI_Device::GetPipelineCount()
    {
        return 0;
    }
}
