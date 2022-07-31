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
        // Detect device limits
        m_max_texture_1d_dimension   = D3D11_REQ_TEXTURE1D_U_DIMENSION;
        m_max_texture_2d_dimension   = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        m_max_texture_3d_dimension   = D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
        m_max_texture_cube_dimension = D3D11_REQ_TEXTURECUBE_DIMENSION;
        m_max_texture_array_layers   = D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;

        m_context                           = context;
        m_rhi_context                       = make_shared<RHI_Context>();
        d3d11_utility::globals::rhi_context = m_rhi_context.get();
        d3d11_utility::globals::rhi_device  = this;
        const bool multithread_protection   = true;

        // Find a physical device
        {
            if (!DetectPhysicalDevices())
            {
                LOG_ERROR("Failed to detect any devices");
                return;
            }

            if (!SelectPrimaryPhysicalDevice())
            {
                LOG_ERROR("Failed to detect any devices");
                return;
            }
        }

        const PhysicalDevice* physical_device = GetPrimaryPhysicalDevice();

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
                D3D_FEATURE_LEVEL_11_1
            };

            // Save API version
            m_rhi_context->api_version_str = "11.1";

            IDXGIAdapter* adapter       = static_cast<IDXGIAdapter*>(physical_device->GetData());
            D3D_DRIVER_TYPE driver_type = adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;

            auto create_device = [this, &adapter, &driver_type, &device_flags, &feature_levels]()
            {
                ID3D11Device* temp_device = nullptr;
                ID3D11DeviceContext* temp_context = nullptr;

                const HRESULT result = D3D11CreateDevice(
                    adapter,                                  // pAdapter: If nullptr, the default adapter will be used
                    driver_type,                              // DriverType
                    nullptr,                                  // HMODULE: nullptr because DriverType = D3D_DRIVER_TYPE_HARDWARE
                    device_flags,                             // Flags
                    feature_levels.data(),                    // pFeatureLevels
                    static_cast<UINT>(feature_levels.size()), // FeatureLevels
                    D3D11_SDK_VERSION,                        // SDKVersion
                    &temp_device,                             // ppDevice
                    nullptr,                                  // pFeatureLevel
                    &temp_context                             // ppImmediateContext
                );

                if (SUCCEEDED(result))
                {
                    // Query old device for newer interface.
                    if (!d3d11_utility::error_check(temp_device->QueryInterface(__uuidof(ID3D11Device5), (void**)&m_rhi_context->device)))
                        return E_FAIL;

                    // Release old device.
                    temp_device->Release();
                    temp_device = nullptr;

                    // Query old device context for newer interface.
                    if (!d3d11_utility::error_check(temp_context->QueryInterface(__uuidof(ID3D11DeviceContext4), (void**)&m_rhi_context->device_context)))
                        return E_FAIL;

                    // Release old context.
                    temp_context->Release();
                    temp_context = nullptr;
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
        if (Settings* settings = m_context->GetSubsystem<Settings>())
        {
            settings->RegisterThirdPartyLib("DirectX", "11.1", "https://www.microsoft.com/en-us/download/details.aspx?id=17431");
            LOG_INFO("DirectX 11.1");
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
    }

    RHI_Device::~RHI_Device()
    {
        m_rhi_context->device_context->Release();
        m_rhi_context->device_context = nullptr;

        m_rhi_context->device->Release();
        m_rhi_context->device = nullptr;

        m_rhi_context->annotation->Release();
        m_rhi_context->annotation = nullptr;
    }

    bool RHI_Device::DetectPhysicalDevices()
    {
        // Create DirectX graphics interface factory
        IDXGIFactory1* factory;
        const auto result = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        if (FAILED(result))
        {
            LOG_ERROR("Failed to create a DirectX graphics interface factory, %s.", d3d11_utility::dxgi_error_to_string(result));
            return false;
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
        if (adapters.empty())
        {
            LOG_ERROR("Couldn't find any adapters");
            return false;
        }

        // Save all available adapters
        DXGI_ADAPTER_DESC adapter_desc;
        for (IDXGIAdapter* display_adapter : adapters)
        {
            if (FAILED(display_adapter->GetDesc(&adapter_desc)))
            {
                LOG_ERROR("Failed to get adapter description");
                continue;
            }

            // Of course it wouldn't be simple, lets convert the device name
            char name[128];
            auto def_char = ' ';
            WideCharToMultiByte(CP_ACP, 0, adapter_desc.Description, -1, name, 128, &def_char, nullptr);

            RegisterPhysicalDevice(PhysicalDevice
            (
                11 << 22,                                                 // api version
                0,                                                        // driver version
                adapter_desc.VendorId,                                    // vendor id
                RHI_PhysicalDevice_Type::Undefined,                       // type
                &name[0],                                                 // name
                static_cast<uint64_t>(adapter_desc.DedicatedVideoMemory), // memory
                static_cast<void*>(display_adapter))                      // data
            );
        }

        return true;
    }

    bool RHI_Device::SelectPrimaryPhysicalDevice()
    {
        for (uint32_t device_index = 0; device_index < m_physical_devices.size(); device_index++)
        {
            // Adapters are ordered by memory (descending), so stop on the first success
            if (DetectDisplayModes(&m_physical_devices[device_index], RHI_Format_R8G8B8A8_Unorm)) // TODO: Format should be determined based on what the swap chain supports.
            {
                SetPrimaryPhysicalDevice(device_index);
                return true;
            }
            else
            {
                LOG_ERROR("Failed to get display modes for \"%s\".", m_physical_devices[device_index].GetName().c_str());
            }
        }

        // If we failed to detect any display modes but we have at least one adapter, use it.
        if (m_physical_devices.size() != 0)
        {
            LOG_ERROR("Failed to detect display modes for all physical devices, falling back to first available.");
            SetPrimaryPhysicalDevice(0);
            return true;
        }

        return false;
    }

    bool RHI_Device::DetectDisplayModes(const PhysicalDevice* physical_device, const RHI_Format format)
    {
        bool result = false;

        IDXGIAdapter* adapter = static_cast<IDXGIAdapter*>(physical_device->GetData());

        // Enumerate the primary adapter output (monitor).
        IDXGIOutput* adapter_output = nullptr;
        if (d3d11_utility::error_check(adapter->EnumOutputs(0, &adapter_output)))
        {
            // Get supported display mode count
            UINT display_mode_count = 0;
            if (d3d11_utility::error_check(adapter_output->GetDisplayModeList(d3d11_format[format], DXGI_ENUM_MODES_INTERLACED, &display_mode_count, nullptr)))
            {
                // Get display modes
                vector<DXGI_MODE_DESC> display_modes;
                display_modes.resize(display_mode_count);
                if (d3d11_utility::error_check(adapter_output->GetDisplayModeList(d3d11_format[format], DXGI_ENUM_MODES_INTERLACED, &display_mode_count, &display_modes[0])))
                {
                    // Save all the display modes
                    for (const DXGI_MODE_DESC& mode : display_modes)
                    {
                        bool update_fps_limit_to_highest_hz = true;
                        Display::RegisterDisplayMode(DisplayMode(mode.Width, mode.Height, mode.RefreshRate.Numerator, mode.RefreshRate.Denominator), update_fps_limit_to_highest_hz, m_context);
                        result = true;
                    }
                }
            }

            adapter_output->Release();
        }

        return result;
    }

    void RHI_Device::QueuePresent(void* swapchain_view, uint32_t* image_index, std::vector<RHI_Semaphore*>& wait_semaphores) const
    {

    }

    void RHI_Device::QueueSubmit(const RHI_Queue_Type type, const uint32_t wait_flags, void* cmd_buffer, RHI_Semaphore* wait_semaphore /*= nullptr*/, RHI_Semaphore* signal_semaphore /*= nullptr*/, RHI_Fence* signal_fence /*= nullptr*/) const
    {

    }

    bool RHI_Device::QueueWait(const RHI_Queue_Type type) const
    {
        m_rhi_context->device_context->Flush();
        return true;
    }

    void RHI_Device::QueryCreate(void** query, const RHI_Query_Type type)
    {
        SP_ASSERT(*query == nullptr);

        D3D11_QUERY_DESC desc = {};
        desc.Query            = (type == RHI_Query_Type::Timestamp_Disjoint) ? D3D11_QUERY_TIMESTAMP_DISJOINT : D3D11_QUERY_TIMESTAMP;
        desc.MiscFlags        = 0;

        SP_ASSERT(SUCCEEDED(m_rhi_context->device->CreateQuery(&desc, reinterpret_cast<ID3D11Query**>(query))));
    }

    void RHI_Device::QueryRelease(void*& query)
    {
        SP_ASSERT(query != nullptr);

        static_cast<ID3D11Query*>(query)->Release();
        query = nullptr;
    }

    void RHI_Device::QueryBegin(void* query)
    {
        SP_ASSERT(query != nullptr);

        m_rhi_context->device_context->Begin(static_cast<ID3D11Query*>(query));
    }

    void RHI_Device::QueryEnd(void* query)
    {
        SP_ASSERT(query != nullptr);

        m_rhi_context->device_context->End(static_cast<ID3D11Query*>(query));
    }

    void RHI_Device::QueryGetData(void* query)
    {
        SP_ASSERT(query != nullptr);

        // Check whether timestamps were disjoint during the last frame
        D3D10_QUERY_DATA_TIMESTAMP_DISJOINT disjoint_data = {};
        while (m_rhi_context->device_context->GetData(static_cast<ID3D11Query*>(query), &disjoint_data, sizeof(disjoint_data), 0) != S_OK);

        m_timestamp_period = static_cast<float>(disjoint_data.Frequency);
    }
}
