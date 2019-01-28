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

namespace D3D11_Common
{ 
	inline D3D11_DEPTH_STENCIL_DESC Desc_DepthDisabled()
	{
		D3D11_DEPTH_STENCIL_DESC dsDesc;
		dsDesc.DepthEnable						= false;
		dsDesc.DepthWriteMask					= D3D11_DEPTH_WRITE_MASK_ZERO;
		dsDesc.DepthFunc						= D3D11_COMPARISON_LESS_EQUAL;
		dsDesc.StencilEnable					= false;
		dsDesc.StencilReadMask					= D3D11_DEFAULT_STENCIL_READ_MASK;
		dsDesc.StencilWriteMask					= D3D11_DEFAULT_STENCIL_WRITE_MASK;
		dsDesc.FrontFace.StencilDepthFailOp		= D3D11_STENCIL_OP_KEEP;
		dsDesc.FrontFace.StencilFailOp			= D3D11_STENCIL_OP_KEEP;
		dsDesc.FrontFace.StencilPassOp			= D3D11_STENCIL_OP_REPLACE;
		dsDesc.FrontFace.StencilFunc			= D3D11_COMPARISON_ALWAYS;
		dsDesc.BackFace							= dsDesc.FrontFace;
	
		return dsDesc;
	}
	
	inline D3D11_DEPTH_STENCIL_DESC Desc_DepthEnabled()
	{
		D3D11_DEPTH_STENCIL_DESC desc;
		desc.DepthEnable					= true;
		desc.DepthWriteMask					= D3D11_DEPTH_WRITE_MASK_ALL;
		desc.DepthFunc						= D3D11_COMPARISON_LESS_EQUAL;
		desc.StencilEnable					= false;
		desc.StencilReadMask				= D3D11_DEFAULT_STENCIL_READ_MASK;
		desc.StencilWriteMask				= D3D11_DEFAULT_STENCIL_WRITE_MASK;
		desc.FrontFace.StencilDepthFailOp	= D3D11_STENCIL_OP_KEEP;
		desc.FrontFace.StencilFailOp		= D3D11_STENCIL_OP_KEEP;
		desc.FrontFace.StencilPassOp		= D3D11_STENCIL_OP_REPLACE;
		desc.FrontFace.StencilFunc			= D3D11_COMPARISON_ALWAYS;
		desc.BackFace						= desc.FrontFace;
	
		return desc;
	}
	
	inline D3D11_DEPTH_STENCIL_DESC Desc_DepthReverseEnabled()
	{
		D3D11_DEPTH_STENCIL_DESC dsDesc;
		dsDesc.DepthEnable					= true;
		dsDesc.DepthWriteMask				= D3D11_DEPTH_WRITE_MASK_ALL;
		dsDesc.DepthFunc					= D3D11_COMPARISON_GREATER_EQUAL;
		dsDesc.StencilEnable				= false;
		dsDesc.StencilReadMask				= D3D11_DEFAULT_STENCIL_READ_MASK;
		dsDesc.StencilWriteMask				= D3D11_DEFAULT_STENCIL_WRITE_MASK;
		dsDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		dsDesc.FrontFace.StencilFailOp		= D3D11_STENCIL_OP_KEEP;
		dsDesc.FrontFace.StencilPassOp		= D3D11_STENCIL_OP_REPLACE;
		dsDesc.FrontFace.StencilFunc		= D3D11_COMPARISON_ALWAYS;
		dsDesc.BackFace						= dsDesc.FrontFace;
	
		return dsDesc;
	}
	
	inline D3D11_RASTERIZER_DESC Desc_RasterizerCullNone()
	{
		D3D11_RASTERIZER_DESC rastDesc;
	
		rastDesc.AntialiasedLineEnable		= true;
		rastDesc.CullMode					= D3D11_CULL_NONE;
		rastDesc.DepthBias					= 0;
		rastDesc.DepthBiasClamp				= 0.0f;
		rastDesc.DepthClipEnable			= true;
		rastDesc.FillMode					= D3D11_FILL_SOLID;
		rastDesc.FrontCounterClockwise		= false;
		rastDesc.MultisampleEnable			= false;
		rastDesc.ScissorEnable				= false;
		rastDesc.SlopeScaledDepthBias		= 0;
	
		return rastDesc;
	}
	
	inline D3D11_RASTERIZER_DESC Desc_RasterizerCullFront()
	{
		D3D11_RASTERIZER_DESC rastDesc;
	
		rastDesc.AntialiasedLineEnable	= true;
		rastDesc.CullMode				= D3D11_CULL_FRONT;
		rastDesc.DepthBias				= 0;
		rastDesc.DepthBiasClamp			= 0.0f;
		rastDesc.DepthClipEnable		= true;
		rastDesc.FillMode				= D3D11_FILL_SOLID;
		rastDesc.FrontCounterClockwise	= false;
		rastDesc.MultisampleEnable		= false;
		rastDesc.ScissorEnable			= false;
		rastDesc.SlopeScaledDepthBias	= 0;
	
		return rastDesc;
	}
	
	inline D3D11_RASTERIZER_DESC Desc_RasterizerCullBack()
	{
		D3D11_RASTERIZER_DESC rastDesc;
	
		rastDesc.AntialiasedLineEnable	= true;
		rastDesc.CullMode				= D3D11_CULL_BACK;
		rastDesc.DepthBias				= 0;
		rastDesc.DepthBiasClamp			= 0.0f;
		rastDesc.DepthClipEnable		= true;
		rastDesc.FillMode				= D3D11_FILL_SOLID;
		rastDesc.FrontCounterClockwise	= false;
		rastDesc.MultisampleEnable		= false;
		rastDesc.ScissorEnable			= false;
		rastDesc.SlopeScaledDepthBias	= 0;
	
		return rastDesc;
	}
	
	
	inline D3D11_BLEND_DESC Desc_BlendDisabled()
	{
		D3D11_BLEND_DESC blendDesc;
		blendDesc.AlphaToCoverageEnable		= false;
		blendDesc.IndependentBlendEnable	= false;
		for (UINT i = 0; i < 8; ++i)
		{
			blendDesc.RenderTarget[i].BlendEnable			= false;
			blendDesc.RenderTarget[i].BlendOp				= D3D11_BLEND_OP_ADD;
			blendDesc.RenderTarget[i].BlendOpAlpha			= D3D11_BLEND_OP_ADD;
			blendDesc.RenderTarget[i].DestBlend				= D3D11_BLEND_INV_SRC_ALPHA;
			blendDesc.RenderTarget[i].DestBlendAlpha		= D3D11_BLEND_ONE;
			blendDesc.RenderTarget[i].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			blendDesc.RenderTarget[i].SrcBlend				= D3D11_BLEND_SRC_ALPHA;
			blendDesc.RenderTarget[i].SrcBlendAlpha			= D3D11_BLEND_ONE;
		}
	
		return blendDesc;
	}
	
	inline D3D11_BLEND_DESC Desc_BlendAlpha()
	{
		D3D11_BLEND_DESC blendDesc;
		blendDesc.AlphaToCoverageEnable		= false;
		blendDesc.IndependentBlendEnable	= true;
		for (UINT i = 0; i < 8; ++i)
		{
			blendDesc.RenderTarget[i].BlendEnable			= true;
			blendDesc.RenderTarget[i].BlendOp				= D3D11_BLEND_OP_ADD;
			blendDesc.RenderTarget[i].BlendOpAlpha			= D3D11_BLEND_OP_ADD;
			blendDesc.RenderTarget[i].DestBlend				= D3D11_BLEND_INV_SRC_ALPHA;
			blendDesc.RenderTarget[i].DestBlendAlpha		= D3D11_BLEND_ONE;
			blendDesc.RenderTarget[i].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			blendDesc.RenderTarget[i].SrcBlend				= D3D11_BLEND_SRC_ALPHA;
			blendDesc.RenderTarget[i].SrcBlendAlpha			= D3D11_BLEND_ONE;
		}
	
		blendDesc.RenderTarget[0].BlendEnable = true;
	
		return blendDesc;
	}
	
	inline D3D11_BLEND_DESC Desc_BlendColorWriteDisabled()
	{
		D3D11_BLEND_DESC blendDesc;
		blendDesc.AlphaToCoverageEnable		= false;
		blendDesc.IndependentBlendEnable	= false;
		for (UINT i = 0; i < 8; ++i)
		{
			blendDesc.RenderTarget[i].BlendEnable			= false;
			blendDesc.RenderTarget[i].BlendOp				= D3D11_BLEND_OP_ADD;
			blendDesc.RenderTarget[i].BlendOpAlpha			= D3D11_BLEND_OP_ADD;
			blendDesc.RenderTarget[i].DestBlend				= D3D11_BLEND_INV_SRC_ALPHA;
			blendDesc.RenderTarget[i].DestBlendAlpha		= D3D11_BLEND_ONE;
			blendDesc.RenderTarget[i].RenderTargetWriteMask = 0;
			blendDesc.RenderTarget[i].SrcBlend				= D3D11_BLEND_SRC_ALPHA;
			blendDesc.RenderTarget[i].SrcBlendAlpha			= D3D11_BLEND_ONE;
		}
	
		return blendDesc;
	}
	
	inline const char* DxgiErrorToString(HRESULT errorCode)
	{
		switch (errorCode)
		{
			case DXGI_ERROR_DEVICE_HUNG:                    return "DXGI_ERROR_DEVICE_HUNG";                  // The application's device failed due to badly formed commands sent by the application. This is an design-time issue that should be investigated and fixed.
			case DXGI_ERROR_DEVICE_REMOVED:                 return "DXGI_ERROR_DEVICE_REMOVED";               // The video card has been physically removed from the system, or a driver upgrade for the video card has occurred. The application should destroy and recreate the device. For help debugging the problem, call ID3D10Device::GetDeviceRemovedReason.
			case DXGI_ERROR_DEVICE_RESET:                   return "DXGI_ERROR_DEVICE_RESET";                 // The device failed due to a badly formed command. This is a run-time issue; The application should destroy and recreate the device.
			case DXGI_ERROR_DRIVER_INTERNAL_ERROR:          return "DXGI_ERROR_DRIVER_INTERNAL_ERROR";        // The driver encountered a problem and was put into the device removed state.
			case DXGI_ERROR_FRAME_STATISTICS_DISJOINT:      return "DXGI_ERROR_FRAME_STATISTICS_DISJOINT";    // An event (for example, a power cycle) interrupted the gathering of presentation statistics.
			case DXGI_ERROR_GRAPHICS_VIDPN_SOURCE_IN_USE:   return "DXGI_ERROR_GRAPHICS_VIDPN_SOURCE_IN_USE"; // The application attempted to acquire exclusive ownership of an output, but failed because some other application (or device within the application) already acquired ownership.
			case DXGI_ERROR_INVALID_CALL:                   return "DXGI_ERROR_INVALID_CALL";                 // The application provided invalid parameter data; this must be debugged and fixed before the application is released.
			case DXGI_ERROR_MORE_DATA:                      return "DXGI_ERROR_MORE_DATA";                    // The buffer supplied by the application is not big enough to hold the requested data.
			case DXGI_ERROR_NONEXCLUSIVE:                   return "DXGI_ERROR_NONEXCLUSIVE";                 // A global counter resource is in use, and the Direct3D device can't currently use the counter resource.
			case DXGI_ERROR_NOT_CURRENTLY_AVAILABLE:        return "DXGI_ERROR_NOT_CURRENTLY_AVAILABLE";      // The resource or request is not currently available, but it might become available later.
			case DXGI_ERROR_NOT_FOUND:                      return "DXGI_ERROR_NOT_FOUND";                    // When calling IDXGIObject::GetPrivateData, the GUID passed in is not recognized as one previously passed to IDXGIObject::SetPrivateData or IDXGIObject::SetPrivateDataInterface. When calling IDXGIFactory::EnumAdapters or IDXGIAdapter::EnumOutputs, the enumerated ordinal is out of range.
			case DXGI_ERROR_REMOTE_CLIENT_DISCONNECTED:     return "DXGI_ERROR_REMOTE_CLIENT_DISCONNECTED";   // Reserved
			case DXGI_ERROR_REMOTE_OUTOFMEMORY:             return "DXGI_ERROR_REMOTE_OUTOFMEMORY";           // Reserved
			case DXGI_ERROR_WAS_STILL_DRAWING:              return "DXGI_ERROR_WAS_STILL_DRAWING";            // The GPU was busy at the moment when a call was made to perform an operation, and did not execute or schedule the operation.
			case DXGI_ERROR_UNSUPPORTED:                    return "DXGI_ERROR_UNSUPPORTED";                  // The requested functionality is not supported by the device or the driver.
			case DXGI_ERROR_ACCESS_LOST:                    return "DXGI_ERROR_ACCESS_LOST";                  // The desktop duplication interface is invalid. The desktop duplication interface typically becomes invalid when a different type of image is displayed on the desktop.
			case DXGI_ERROR_WAIT_TIMEOUT:                   return "DXGI_ERROR_WAIT_TIMEOUT";                 // The time-out interval elapsed before the next desktop frame was available.
			case DXGI_ERROR_SESSION_DISCONNECTED:           return "DXGI_ERROR_SESSION_DISCONNECTED";         // The Remote Desktop Services session is currently disconnected.
			case DXGI_ERROR_RESTRICT_TO_OUTPUT_STALE:       return "DXGI_ERROR_RESTRICT_TO_OUTPUT_STALE";     // The DXGI output (monitor) to which the swap chain content was restricted is now disconnected or changed.
			case DXGI_ERROR_CANNOT_PROTECT_CONTENT:         return "DXGI_ERROR_CANNOT_PROTECT_CONTENT";       // DXGI can't provide content protection on the swap chain. This error is typically caused by an older driver, or when you use a swap chain that is incompatible with content protection.
			case DXGI_ERROR_ACCESS_DENIED:                  return "DXGI_ERROR_ACCESS_DENIED";                // You tried to use a resource to which you did not have the required access privileges. This error is most typically caused when you write to a shared resource with read-only access.
			case DXGI_ERROR_NAME_ALREADY_EXISTS:            return "DXGI_ERROR_NAME_ALREADY_EXISTS";          // The supplied name of a resource in a call to IDXGIResource1::CreateSharedHandle is already associated with some other resource.
			case DXGI_ERROR_SDK_COMPONENT_MISSING:          return "DXGI_ERROR_SDK_COMPONENT_MISSING";        // The operation depends on an SDK component that is missing or mismatched.
		}
	
		return "Unknown error code";
	}
}