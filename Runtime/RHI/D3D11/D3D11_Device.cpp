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
        d3d11_utility::globals::rhi_context = m_rhi_context.get();
        d3d11_utility::globals::rhi_device  = this;
        const bool multithread_protection   = true;

        // Detect adapters
        d3d11_utility::DetectAdapters();

        // Resource limits
        m_rhi_context->rhi_max_texture_dimension_2d = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;

        const PhysicalDevice* physical_device = GetPrimaryPhysicalDevice();
        if (!physical_device)
        {
            LOG_ERROR("Failed to detect any devices");
            return;
        }

        // Create device
        {
            // Flags
            UINT device_flags = 0;
            // Enable debug layer
            if (m_rhi_context->debug)
            {
                device_flags |= D3D11_CREATE_DEVICE_DEBUG;
            }

            // The order of the feature levels that we'll try to create a device with
            vector<D3D_FEATURE_LEVEL> feature_levels =
            {
                D3D_FEATURE_LEVEL_11_1,
                D3D_FEATURE_LEVEL_11_0,
                D3D_FEATURE_LEVEL_10_1,
                D3D_FEATURE_LEVEL_10_0,
                D3D_FEATURE_LEVEL_9_3,
                D3D_FEATURE_LEVEL_9_2,
                D3D_FEATURE_LEVEL_9_1
            };

            auto adapter = static_cast<IDXGIAdapter*>(physical_device->GetData());
            auto driver_type = adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;

            auto create_device = [this, &adapter, &driver_type, &device_flags, &feature_levels]()
            {
                ID3D11Device* temp_device = nullptr;
                ID3D11DeviceContext* temp_context = nullptr;

                const HRESULT result = D3D11CreateDevice(
                    adapter,                                    // pAdapter: If nullptr, the default adapter will be used
                    driver_type,                                // DriverType
                    nullptr,                                    // HMODULE: nullptr because DriverType = D3D_DRIVER_TYPE_HARDWARE
                    device_flags,                                // Flags
                    feature_levels.data(),                        // pFeatureLevels
                    static_cast<UINT>(feature_levels.size()),    // FeatureLevels
                    D3D11_SDK_VERSION,                            // SDKVersion
                    &temp_device,                                // ppDevice
                    nullptr,                                    // pFeatureLevel
                    &temp_context                                // ppImmediateContext
                );

                if (SUCCEEDED(result))
                {
                    // Query old device for newer interface.
                    if (!d3d11_utility::error_check(temp_device->QueryInterface(__uuidof(ID3D11Device5), (void**)&m_rhi_context->device)))
                        return E_FAIL;

                    // Release old device.
                    d3d11_utility::release(temp_device);

                    // Query old device context for newer interface.
                    if (!d3d11_utility::error_check(temp_context->QueryInterface(__uuidof(ID3D11DeviceContext4), (void**)&m_rhi_context->device_context)))
                        return E_FAIL;

                    // Release old context.
                    d3d11_utility::release(temp_context);
                }

                return result;
            };

            // Create Direct3D device and Direct3D device context.
            auto result = create_device();

            // Using the D3D11_CREATE_DEVICE_DEBUG flag, requires the SDK to be installed, so try again without it
            if (result == DXGI_ERROR_SDK_COMPONENT_MISSING)
            {
                LOG_WARNING("Failed to create device with D3D11_CREATE_DEVICE_DEBUG flags as it requires the DirectX SDK to be installed. Attempting to create a device without it.");
                device_flags &= ~D3D11_CREATE_DEVICE_DEBUG;
                result = create_device();
            }

            if (FAILED(result))
            {
                LOG_ERROR("Failed to create device, %s.", d3d11_utility::dxgi_error_to_string(result));
                return;
            }
        }

        // Log feature level
        {
            auto log_feature_level = [this](const std::string& level)
            {
                auto settings = m_context->GetSubsystem<Settings>();
                settings->RegisterThirdPartyLib("DirectX", level, "https://www.microsoft.com/en-us/download/details.aspx?id=17431");
                LOG_INFO("DirectX %s", level.c_str());
            };

            switch (m_rhi_context->device->GetFeatureLevel())
            {
                case D3D_FEATURE_LEVEL_9_1:
                    log_feature_level("9.1");
                    break;

                case D3D_FEATURE_LEVEL_9_2:
                    log_feature_level("9.2");
                    break;

                case D3D_FEATURE_LEVEL_9_3:
                    log_feature_level("9.3");
                    break;

                case D3D_FEATURE_LEVEL_10_0:
                    log_feature_level("10.0");
                    break;

                case D3D_FEATURE_LEVEL_10_1:
                    log_feature_level("10.1");
                    break;

                case D3D_FEATURE_LEVEL_11_0:
                    log_feature_level("11.0");
                    break;

                case D3D_FEATURE_LEVEL_11_1:
                    log_feature_level("11.1");
                    break;
                case D3D_FEATURE_LEVEL_12_0: break;
                case D3D_FEATURE_LEVEL_12_1: break;
                default: ;
            }
        }

        // Multi-thread protection
        if (multithread_protection)
        {
            ID3D11Multithread* multithread = nullptr;
            if (SUCCEEDED(m_rhi_context->device_context->QueryInterface(__uuidof(ID3D11Multithread), reinterpret_cast<void**>(&multithread))))
            {        
                multithread->SetMultithreadProtected(TRUE);
                multithread->Release();
            }
            else 
            {
                LOG_ERROR("Failed to enable multi-threaded protection");
            }
        }

        // Annotations
        if (m_rhi_context->debug)
        {
            const auto result = m_rhi_context->device_context->QueryInterface(IID_PPV_ARGS(&m_rhi_context->annotation));
            if (FAILED(result))
            {
                LOG_ERROR("Failed to create ID3DUserDefinedAnnotation for event reporting, %s.", d3d11_utility::dxgi_error_to_string(result));
                return;
            }
        }

        m_initialized = true;
    }

    RHI_Device::~RHI_Device()
    {
        d3d11_utility::release(m_rhi_context->device_context);
        d3d11_utility::release(m_rhi_context->device);
        d3d11_utility::release(m_rhi_context->annotation);
    }

    bool RHI_Device::Queue_Submit(const RHI_Queue_Type type, void* cmd_buffer, RHI_Semaphore* wait_semaphore /*= nullptr*/, RHI_Semaphore* signal_semaphore /*= nullptr*/, RHI_Fence* wait_fence /*= nullptr*/, uint32_t wait_flags /*= 0*/) const
    {
        return true;
    }

    bool RHI_Device::Queue_Wait(const RHI_Queue_Type type) const
    {
        m_rhi_context->device_context->Flush();
        return true;
    }
}
