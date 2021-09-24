/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "Spartan.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_BlendState.h"
#include "../RHI_RasterizerState.h"
#include "../RHI_Shader.h"
#include "../RHI_InputLayout.h"
#include <wrl.h>
//=================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    RHI_Device::RHI_Device(Context* context)
    {
        m_context                           = context;
        m_rhi_context                       = make_shared<RHI_Context>();
        d3d12_utility::globals::rhi_context = m_rhi_context.get();
        d3d12_utility::globals::rhi_device  = this;

        // Debug layer
        UINT dxgi_factory_flags = 0;
        if (m_rhi_context->debug)
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
        if (!d3d12_utility::error::check(CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&factory))))
        {
            LOG_ERROR("Failed to created dxgi factory");
            return;
        }

        D3D_FEATURE_LEVEL minimum_feature_level = D3D_FEATURE_LEVEL_12_0;

        // Adapter
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
                if (!d3d12_utility::error::check(D3D12CreateDevice(adapter.Get(), minimum_feature_level, _uuidof(ID3D12Device), nullptr)))
                    break;
            }

        }

        // Device
        if (!d3d12_utility::error::check(D3D12CreateDevice(adapter.Get(), minimum_feature_level, IID_PPV_ARGS(&m_rhi_context->device))))
        {
            LOG_ERROR("Failed to created device");
            return;
        }

        // Create a graphics, compute and a copy queue.
        {
            D3D12_COMMAND_QUEUE_DESC queue_desc = {};
            queue_desc.Flags                    = D3D12_COMMAND_QUEUE_FLAG_NONE;

            queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            d3d12_utility::error::check(m_rhi_context->device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(reinterpret_cast<ID3D12CommandQueue**>(&m_rhi_context->queue_graphics))));

            queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
            d3d12_utility::error::check(m_rhi_context->device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(reinterpret_cast<ID3D12CommandQueue**>(&m_rhi_context->queue_compute))));

            queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
            d3d12_utility::error::check(m_rhi_context->device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(reinterpret_cast<ID3D12CommandQueue**>(&m_rhi_context->queue_copy))));
        }

        // Create command list allocator
        d3d12_utility::error::check(m_rhi_context->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(reinterpret_cast<ID3D12CommandAllocator**>(&m_cmd_pool_graphics))));

        // Log feature level
        if (Settings* settings = m_context->GetSubsystem<Settings>())
        {
            std::string level = "12.0";
            settings->RegisterThirdPartyLib("DirectX", level, "https://www.microsoft.com/en-us/download/details.aspx?id=17431");
            LOG_INFO("DirectX %s", level.c_str());
        }

        m_initialized = true;
    }

    RHI_Device::~RHI_Device()
    {
        SP_ASSERT(m_rhi_context != nullptr);
        SP_ASSERT(m_rhi_context->queue_graphics != nullptr);

        // Command queues
        d3d12_utility::release<ID3D12CommandQueue>(m_rhi_context->queue_graphics);
        d3d12_utility::release<ID3D12CommandQueue>(m_rhi_context->queue_compute);
        d3d12_utility::release<ID3D12CommandQueue>(m_rhi_context->queue_copy);

        // Command allocator
        d3d12_utility::release<ID3D12CommandAllocator>(m_cmd_pool_graphics);

        if (Queue_WaitAll())
        {
            m_rhi_context->device->Release();
            m_rhi_context->device = nullptr;
        }
    }

    bool RHI_Device::Queue_Submit(const RHI_Queue_Type type, const uint32_t wait_flags, void* cmd_buffer, RHI_Semaphore* wait_semaphore /*= nullptr*/, RHI_Semaphore* signal_semaphore /*= nullptr*/, RHI_Fence* signal_fence /*= nullptr*/) const
    {
        return true;
    }

    bool RHI_Device::Queue_Wait(const RHI_Queue_Type type) const
    {
        return true;
    }
}
