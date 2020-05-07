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
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include "RHI_Definition.h"
#include "../Core/Spartan_Object.h"
//=================================

namespace Spartan
{
    class Context;
	namespace Math
	{ 
		class Vector4;
		class Rectangle;
	}

	struct DisplayMode
	{
		DisplayMode() = default;
		DisplayMode(const uint32_t width, const uint32_t height, const uint32_t numerator, const uint32_t denominator)
		{
			this->width			= width;
			this->height		= height;
			this->numerator     = numerator;
            this->denominator   = denominator;
            this->hz  = static_cast<double>(numerator) / static_cast<double>(denominator);
		}

        bool operator ==(const DisplayMode& rhs) const
        {
            return width == rhs.width && height == rhs.height && hz == rhs.hz;
        }

		uint32_t width		    = 0;
		uint32_t height		    = 0;   
        uint32_t numerator      = 0;
        uint32_t denominator    = 0;
        double hz               = 0;
	};

	class PhysicalDevice
	{
    public:
		PhysicalDevice(const uint32_t api_version, const uint32_t driver_version, const uint32_t vendor_id, const RHI_PhysicalDevice_Type type, const char* name, const uint32_t memory, void* data)
		{
            this->vendor_id         = vendor_id;
            this->type              = type;
			this->name		        = name;
			this->memory	        = memory / 1024 / 1024; // mb
			this->data		        = data;
            this->api_version       = decode_driver_version(api_version);
            this->driver_version    = decode_driver_version(driver_version);
		}

        /*
            0x10DE - Nvidia
            0x8086 - Intel
            0x1002 - Amd
            0x13B5 - ARM
            0x5143 - Qualcomm
            0x1010 - ImgTec
            
        */
		bool IsNvidia()     const { return vendor_id == 0x10DE || name.find("Nvidia") != std::string::npos; }
		bool IsAmd()        const { return vendor_id == 0x1002 || vendor_id == 0x1022 || name.find("Amd") != std::string::npos; }
		bool IsIntel()      const { return vendor_id == 0x8086 || vendor_id == 0x163C || vendor_id == 0x8087 || name.find("Intel") != std::string::npos;}
        bool IsArm()        const { return vendor_id == 0x13B5 || name.find("Arm,") != std::string::npos; }
        bool IsQualcomm()   const { return vendor_id == 0x5143 || name.find("Qualcomm") != std::string::npos; }

        const std::string& GetName()    const { return name; }
        uint32_t GetMemory()            const { return memory; }
        void* GetData()                 const { return data; }

    private:
        std::string decode_driver_version(const uint32_t version)
        {
            char buffer[256];
            
            if (IsNvidia())
            {
                sprintf_s
                (
                    buffer,
                    "%d.%d.%d.%d",
                    (version >> 22) & 0x3ff,
                    (version >> 14) & 0x0ff,
                    (version >> 6) & 0x0ff,
                    (version) & 0x003f
                );
                
            }
            else if(IsIntel())
            {
                sprintf_s
                (
                    buffer,
                    "%d.%d",
                    (version >> 14),
                    (version) & 0x3fff
                );
            }
            else // Use Vulkan version conventions if vendor mapping is not available
            {
                sprintf_s
                (
                    buffer,
                    "%d.%d.%d",
                    (version >> 22),
                    (version >> 12) & 0x3ff,
                    version & 0xfff
                );
            }

            return buffer;
        }

        std::string api_version         = "Unknown"; // version of Vulkan supported by the device
        std::string driver_version      = "Unknown"; // vendor-specified version of the driver.
		uint32_t vendor_id	            = 0; // unique identifier of the vendor
        RHI_PhysicalDevice_Type type    = RHI_PhysicalDevice_Unknown;
        std::string name                = "Unknown";
		uint32_t memory		            = 0;
		void* data                      = nullptr;
	};

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

        // Display mode
        void RegisterDisplayMode(const DisplayMode& display_mode);
        const DisplayMode& GetActiveDisplayMode() const { return m_display_mode_active; }
        void SetActiveDisplayMode(const DisplayMode& display_mode) { m_display_mode_active = display_mode; }
        const std::vector<DisplayMode>& GetDisplayModes() const { return m_display_modes; }
        bool ValidateResolution(const uint32_t width, const uint32_t height) const;

        // Queue
        bool Queue_Present(void* swapchain_view, uint32_t* image_index) const;
        bool Queue_Submit(const RHI_Queue_Type type, void* cmd_buffer, void* wait_semaphore = nullptr, void* wait_fence = nullptr, const uint32_t wait_flags = 0) const;
        bool Queue_Wait(const RHI_Queue_Type type) const;
        bool Queue_WaitAll() const;
        void* Queue_Get(const RHI_Queue_Type type) const;
        uint32_t Queue_Index(const RHI_Queue_Type type) const;

        // Misc
		auto IsInitialized()                const { return m_initialized; }
        RHI_Context* GetContextRhi()	    const { return m_rhi_context.get(); }
        Context* GetContext()               const { return m_context; }
        uint32_t GetEnabledGraphicsStages() const { return m_enabled_graphics_shader_stages; }

	private:	
		std::vector<PhysicalDevice> m_physical_devices;
        std::vector<DisplayMode> m_display_modes;
        DisplayMode m_display_mode_active;
        uint32_t m_physical_device_index            = 0;     
        uint32_t m_enabled_graphics_shader_stages   = 0;
        bool m_initialized                          = false;
        mutable std::mutex m_queue_mutex;
        std::shared_ptr<RHI_Context> m_rhi_context;
	};
}
