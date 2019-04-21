/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= IMPLEMENTATION ===============
#include "../RHI_Implementation.h"
#ifdef API_GRAPHICS_D3D11
//================================

//= INCLUDES =====================
#include "../RHI_Device.h"
#include <vector>
#include <wrl/client.h>
#include "../../Logging/Log.h"
#include "../../Core/EngineDefs.h"
//================================

namespace Spartan::D3D11_Common
{ 
	inline const char* dxgi_error_to_string(const HRESULT error_code)
	{
		switch (error_code)
		{
			case DXGI_ERROR_DEVICE_HUNG:                    return "DXGI_ERROR_DEVICE_HUNG";					// The application's device failed due to badly formed commands sent by the application. This is an design-time issue that should be investigated and fixed.
			case DXGI_ERROR_DEVICE_REMOVED:                 return "DXGI_ERROR_DEVICE_REMOVED";					// The video card has been physically removed from the system, or a driver upgrade for the video card has occurred. The application should destroy and recreate the device. For help debugging the problem, call ID3D10Device::GetDeviceRemovedReason.
			case DXGI_ERROR_DEVICE_RESET:                   return "DXGI_ERROR_DEVICE_RESET";					// The device failed due to a badly formed command. This is a run-time issue; The application should destroy and recreate the device.
			case DXGI_ERROR_DRIVER_INTERNAL_ERROR:          return "DXGI_ERROR_DRIVER_INTERNAL_ERROR";			// The driver encountered a problem and was put into the device removed state.
			case DXGI_ERROR_FRAME_STATISTICS_DISJOINT:      return "DXGI_ERROR_FRAME_STATISTICS_DISJOINT";		// An event (for example, a power cycle) interrupted the gathering of presentation statistics.
			case DXGI_ERROR_GRAPHICS_VIDPN_SOURCE_IN_USE:   return "DXGI_ERROR_GRAPHICS_VIDPN_SOURCE_IN_USE";	// The application attempted to acquire exclusive ownership of an output, but failed because some other application (or device within the application) already acquired ownership.
			case DXGI_ERROR_INVALID_CALL:                   return "DXGI_ERROR_INVALID_CALL";					// The application provided invalid parameter data; this must be debugged and fixed before the application is released.
			case DXGI_ERROR_MORE_DATA:                      return "DXGI_ERROR_MORE_DATA";						// The buffer supplied by the application is not big enough to hold the requested data.
			case DXGI_ERROR_NONEXCLUSIVE:                   return "DXGI_ERROR_NONEXCLUSIVE";					// A global counter resource is in use, and the Direct3D device can't currently use the counter resource.
			case DXGI_ERROR_NOT_CURRENTLY_AVAILABLE:        return "DXGI_ERROR_NOT_CURRENTLY_AVAILABLE";		// The resource or request is not currently available, but it might become available later.
			case DXGI_ERROR_NOT_FOUND:                      return "DXGI_ERROR_NOT_FOUND";						// When calling IDXGIObject::GetPrivateData, the GUID passed in is not recognized as one previously passed to IDXGIObject::SetPrivateData or IDXGIObject::SetPrivateDataInterface. When calling IDXGIFentityy::EnumAdapters or IDXGIAdapter::EnumOutputs, the enumerated ordinal is out of range.
			case DXGI_ERROR_REMOTE_CLIENT_DISCONNECTED:     return "DXGI_ERROR_REMOTE_CLIENT_DISCONNECTED";		// Reserved
			case DXGI_ERROR_REMOTE_OUTOFMEMORY:             return "DXGI_ERROR_REMOTE_OUTOFMEMORY";				// Reserved
			case DXGI_ERROR_WAS_STILL_DRAWING:              return "DXGI_ERROR_WAS_STILL_DRAWING";				// The GPU was busy at the moment when a call was made to perform an operation, and did not execute or schedule the operation.
			case DXGI_ERROR_UNSUPPORTED:                    return "DXGI_ERROR_UNSUPPORTED";					// The requested functionality is not supported by the device or the driver.
			case DXGI_ERROR_ACCESS_LOST:                    return "DXGI_ERROR_ACCESS_LOST";					// The desktop duplication interface is invalid. The desktop duplication interface typically becomes invalid when a different type of image is displayed on the desktop.
			case DXGI_ERROR_WAIT_TIMEOUT:                   return "DXGI_ERROR_WAIT_TIMEOUT";					// The time-out interval elapsed before the next desktop frame was available.
			case DXGI_ERROR_SESSION_DISCONNECTED:           return "DXGI_ERROR_SESSION_DISCONNECTED";			// The Remote Desktop Services session is currently disconnected.
			case DXGI_ERROR_RESTRICT_TO_OUTPUT_STALE:       return "DXGI_ERROR_RESTRICT_TO_OUTPUT_STALE";		// The DXGI output (monitor) to which the swap chain content was restricted is now disconnected or changed.
			case DXGI_ERROR_CANNOT_PROTECT_CONTENT:         return "DXGI_ERROR_CANNOT_PROTECT_CONTENT";			// DXGI can't provide content protection on the swap chain. This error is typically caused by an older driver, or when you use a swap chain that is incompatible with content protection.
			case DXGI_ERROR_ACCESS_DENIED:                  return "DXGI_ERROR_ACCESS_DENIED";					// You tried to use a resource to which you did not have the required access privileges. This error is most typically caused when you write to a shared resource with read-only access.
			case DXGI_ERROR_NAME_ALREADY_EXISTS:            return "DXGI_ERROR_NAME_ALREADY_EXISTS";			// The supplied name of a resource in a call to IDXGIResource1::CreateSharedHandle is already associated with some other resource.
			case DXGI_ERROR_SDK_COMPONENT_MISSING:          return "DXGI_ERROR_SDK_COMPONENT_MISSING";			// The operation depends on an SDK component that is missing or mismatched.
			case E_INVALIDARG:								return "E_INVALIDARG";								// One or more arguments are invalid.
		}
	
		return "Unknown error code";
	}

	inline void DetectAdapters(RHI_Device* device)
	{
		// Create DirectX graphics interface factory
		IDXGIFactory1* factory;
		const auto result = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
		if (FAILED(result))
		{
			LOGF_ERROR("Failed to create a DirectX graphics interface factory, %s.", dxgi_error_to_string(result));
			return;
		}

		const auto get_available_adapters = [](IDXGIFactory1* factory)
		{
			unsigned int i = 0;
			IDXGIAdapter* adapter;
			std::vector<IDXGIAdapter*> adapters;
			while (factory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND)
			{
				adapters.emplace_back(adapter);
				++i;
			}

			return adapters;
		};

		// Get all available adapters
		auto adapters = get_available_adapters(factory);
		safe_release(factory);
		if (adapters.empty())
		{
			LOG_ERROR("Couldn't find any adapters");
			return;
		}

		// Save all available adapters
		DXGI_ADAPTER_DESC adapter_desc;
		for (auto display_adapter : adapters)
		{
			if (FAILED(display_adapter->GetDesc(&adapter_desc)))
			{
				LOG_ERROR("Failed to get adapter description");
				continue;
			}

			const auto memory_mb = static_cast<unsigned int>(adapter_desc.DedicatedVideoMemory / 1024 / 1024);
			char name[128];
			auto def_char = ' ';
			WideCharToMultiByte(CP_ACP, 0, adapter_desc.Description, -1, name, 128, &def_char, nullptr);

			device->AddAdapter(name, memory_mb, adapter_desc.VendorId, static_cast<void*>(display_adapter));
		}

		// DISPLAY MODES
		const auto get_display_modes = [device](IDXGIAdapter* adapter, RHI_Format format)
		{
			// Enumerate the primary adapter output (monitor).
			IDXGIOutput* adapter_output;
			bool result = SUCCEEDED(adapter->EnumOutputs(0, &adapter_output));
			if (result)
			{
				// Get supported display mode count
				UINT display_mode_count;
				result = SUCCEEDED(adapter_output->GetDisplayModeList(d3d11_format[format], DXGI_ENUM_MODES_INTERLACED, &display_mode_count, nullptr));
				if (result)
				{
					// Get display modes
					std::vector<DXGI_MODE_DESC> display_modes;
					display_modes.resize(display_mode_count);
					result = SUCCEEDED(adapter_output->GetDisplayModeList(d3d11_format[format], DXGI_ENUM_MODES_INTERLACED, &display_mode_count, &display_modes[0]));
					if (result)
					{
						// Save all the display modes
						for (const auto& mode : display_modes)
						{
							device->AddDisplayMode(mode.Width, mode.Height, mode.RefreshRate.Numerator, mode.RefreshRate.Denominator);
						}
					}
				}
				adapter_output->Release();
			}

			return result;
		};

		// Get display modes and set primary adapter
		for (const auto& display_adapter : device->GetAdapters())
		{
			const auto adapter = static_cast<IDXGIAdapter*>(display_adapter.data);

			// Adapters are ordered by memory (descending), so stop on the first success
			auto format = Format_R8G8B8A8_UNORM; // TODO: This must come from the swapchain
			if (get_display_modes(adapter, format))
			{
				device->SetPrimaryAdapter(&display_adapter);
				break;
			}
			else
			{
				LOGF_ERROR("Failed to get display modes for \"%s\".", display_adapter.name.c_str());
			}
		}

		// If we failed to detect any display modes but we have at least one adapter, use it.
		if (!device->GetPrimaryAdapter() && device->GetAdapters().size() != 0)
		{
			auto& adapter = device->GetAdapters().front();
			LOGF_ERROR("Failed to detect display modes for all adapters, using %s, unexpected results may occur.", adapter.name.c_str());
			device->SetPrimaryAdapter(&adapter);
		}
	}

	// Determines whether tearing support is available for fullscreen borderless windows.
	inline bool CheckTearingSupport(RHI_Device* device)
	{
		// Rather than create the 1.5 factory interface directly, we create the 1.4
		// interface and query for the 1.5 interface. This will enable the graphics
		// debugging tools which might not support the 1.5 factory interface
		Microsoft::WRL::ComPtr<IDXGIFactory4> factory4;
		HRESULT resut		= CreateDXGIFactory1(IID_PPV_ARGS(&factory4));
		BOOL allowTearing	= FALSE;
		if (SUCCEEDED(resut))
		{
			Microsoft::WRL::ComPtr<IDXGIFactory5> factory5;
			resut = factory4.As(&factory5);
			if (SUCCEEDED(resut))
			{
				resut = factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
			}
		}

		bool fullscreen_borderless_support	= SUCCEEDED(resut) && allowTearing;
		bool vendor_support					= !device->GetPrimaryAdapter()->IsIntel(); // Intel, bad

		return fullscreen_borderless_support && vendor_support;
	}

	namespace swap_chain
	{
		inline unsigned int flag_filter(RHI_Device* device, unsigned long flags)
		{
			// If SwapChain_Allow_Tearing was requested
			if (flags & SwapChain_Allow_Tearing)
			{
				// Check if the adapter supports it, if not, disable it (tends to fail with Intel adapters)
				if (D3D11_Common::CheckTearingSupport(device))
				{
					flags &= ~SwapChain_Allow_Tearing;
				}
				else
				{
					LOG_WARNING("SwapChain_Allow_Tearing was requested but it's not supported by the adapter.");
				}
			}

			return flags;
		}

		inline UINT flag_to_d3d11(unsigned long flags)
		{
			UINT d3d11_flags = 0;

			d3d11_flags |= flags & SwapChain_Allow_Mode_Switch	? DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH : 0;
			d3d11_flags |= flags & SwapChain_Allow_Tearing		? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

			return d3d11_flags;
		}

		inline DXGI_SWAP_EFFECT swap_effect_filter(RHI_Device* device, RHI_Swap_Effect swap_effect)
		{
			#if !defined(_WIN32_WINNT_WIN10)
			if (swap_effect == Swap_Flip_Discard)
			{
				LOG_WARNING("Swap_Flip_Discard was requested but it's only support in Windows 10, using Swap_Discard instead.");
				swap_effect = Swap_Discard;
			}
			#endif

			if (swap_effect == Swap_Flip_Discard && device->GetPrimaryAdapter()->IsIntel())
			{
				LOG_WARNING("Swap_Flip_Discard was requested but it's not supported by Intel adapters, using Swap_Discard instead.");
				swap_effect = Swap_Discard;
			}

			return d3d11_swap_effect[swap_effect];
		}
	}
}

#endif