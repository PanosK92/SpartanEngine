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

#pragma once

//= INCLUDES ======================
#include "../Core/Spartan_Object.h"
#include <mutex>
#include <memory>
#include "../Display/DisplayMode.h"
#include "RHI_PhysicalDevice.h"
//=================================

namespace Spartan
{
    class SPARTAN_CLASS RHI_Device : public Spartan_Object
    {
    public:
        RHI_Device(Context* context);
        ~RHI_Device();

        // Physical device
        void RegisterPhysicalDevice(const PhysicalDevice& physical_device);    
        const PhysicalDevice* GetPrimaryPhysicalDevice();
        void SetPrimaryPhysicalDevice(const uint32_t index);
        const std::vector<PhysicalDevice>& GetPhysicalDevices() const { return m_physical_devices; }

        // Queue
        bool Queue_Present(void* swapchain_view, uint32_t* image_index, RHI_Semaphore* wait_semaphore = nullptr) const;
        bool Queue_Submit(const RHI_Queue_Type type, void* cmd_buffer, RHI_Semaphore* wait_semaphore = nullptr, RHI_Semaphore* signal_semaphore = nullptr, RHI_Fence* signal_fence = nullptr, const uint32_t wait_flags = 0) const;
        bool Queue_Wait(const RHI_Queue_Type type) const;
        bool Queue_WaitAll() const;
        void* Queue_Get(const RHI_Queue_Type type) const;
        uint32_t Queue_Index(const RHI_Queue_Type type) const;

        // Misc
        bool ValidateResolution(const uint32_t width, const uint32_t height) const;
        auto IsInitialized()                const { return m_initialized; }
        RHI_Context* GetContextRhi()        const { return m_rhi_context.get(); }
        Context* GetContext()               const { return m_context; }
        uint32_t GetEnabledGraphicsStages() const { return m_enabled_graphics_shader_stages; }

    private:    
        std::vector<PhysicalDevice> m_physical_devices;
        uint32_t m_physical_device_index            = 0;     
        uint32_t m_enabled_graphics_shader_stages   = 0;
        bool m_initialized                          = false;
        mutable std::mutex m_queue_mutex;
        std::shared_ptr<RHI_Context> m_rhi_context;
    };
}
