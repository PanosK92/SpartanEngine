/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES ==================
#include "pch.h"
#include "RHI_Implementation.h"
//=============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    // API specific
#   if defined(API_GRAPHICS_D3D12)
        ID3D12Device* RHI_Context::device;
    #elif defined(API_GRAPHICS_VULKAN)
        VkInstance RHI_Context::instance;
        VkPhysicalDevice RHI_Context::device_physical;
        VkDevice RHI_Context::device;
        vector<VkValidationFeatureEnableEXT> RHI_Context::validation_extensions;
        vector<const char*> RHI_Context::extensions_instance;
        vector<const char*> RHI_Context::validation_layers;
        vector<const char*> RHI_Context::extensions_device;
    #endif
    
    // API agnostic
    string RHI_Context::api_version_str;
    string RHI_Context::api_type_str;
    RHI_Api_Type RHI_Context::api_type = RHI_Api_Type::Undefined;
    bool RHI_Context::validation;
    bool RHI_Context::gpu_markers;
    bool RHI_Context::gpu_profiling;
    bool RHI_Context::renderdoc;

    void RHI_Context::Initialize()
    {
        #if defined(API_GRAPHICS_D3D12)
            api_type            = RHI_Api_Type::D3d12;
            api_type_str        = "D3D12";
            device              = nullptr;
        #elif defined(API_GRAPHICS_VULKAN)
            api_type            = RHI_Api_Type::Vulkan;
            api_type_str        = "Vulkan";
            instance            = nullptr;
            device_physical     = nullptr;
            device              = nullptr;
            extensions_instance = { "VK_KHR_surface", "VK_KHR_win32_surface", "VK_EXT_swapchain_colorspace" };
            validation_layers   = { "VK_LAYER_KHRONOS_validation" };
            extensions_device   = { "VK_KHR_swapchain", "VK_EXT_memory_budget", "VK_EXT_depth_clip_enable" };
            // hardware capability viewer: https://vulkan.gpuinfo.org/
        #endif
        
        #ifdef DEBUG
            validation    = true;
            gpu_markers   = true;
            gpu_profiling = true;
            renderdoc     = false;
        #else
            validation    = false;
            gpu_markers   = false;
            gpu_profiling = true;
            renderdoc     = false;
        #endif
    } 
}
