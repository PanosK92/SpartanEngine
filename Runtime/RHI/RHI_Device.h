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

//= INCLUDES ==================
#include <string>
#include <vector>
#include <memory>
#include "RHI_Definition.h"
#include "../Core/EngineDefs.h"
#include <mutex>
//=============================

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
            this->refresh_rate  = static_cast<double>(numerator) / static_cast<double>(denominator);
		}

        DisplayMode(const uint32_t width, const uint32_t height, const double refresh_rate)
        {
            this->width         = width;
            this->height        = height;
            this->refresh_rate  = refresh_rate;
        }

		uint32_t width		    = 0;
		uint32_t height		    = 0;   
        uint32_t numerator      = 0;
        uint32_t denominator    = 0;
        double refresh_rate     = 0;
	};

	struct PhysicalDevice
	{
		PhysicalDevice(const uint32_t api_version, const uint32_t driver_version, const uint32_t vendor_id, const RHI_PhysicalDevice_Type type, std::string& name, const uint32_t memory, void* data)
		{
            this->api_version       = api_version;
            this->driver_version    = driver_version;
            this->vendor_id         = vendor_id;
            this->type              = type;
			this->name		        = name;
			this->memory	        = memory;
			this->data		        = data;
		}

        /*
            0x10DE - Nvidia
            0x8086 - Intel
            0x1002 - Amd
            0x13B5 - ARM
            0x5143 - Qualcomm
            0x1010 - ImgTec
            
        */
		bool IsNvidia() const	{ return vendor_id == 0x10DE || name.find("Nvidia") != std::string::npos; }
		bool IsAmd() const		{ return vendor_id == 0x1002 || vendor_id == 0x1022 || name.find("Amd") != std::string::npos; }
		bool IsIntel() const	{ return vendor_id == 0x8086 || vendor_id == 0x163C || vendor_id == 0x8087 || name.find("Intel") != std::string::npos;}
        bool IsArm() const      { return vendor_id == 0x13B5 || name.find("Arm,") != std::string::npos; }
        bool IsQualcomm() const { return vendor_id == 0x5143 || name.find("Qualcomm") != std::string::npos; }

        uint32_t api_version            = 0; // version of Vulkan supported by the device
        uint32_t driver_version         = 0; // vendor-specified version of the driver.
		uint32_t vendor_id	            = 0; // unique identifier of the vendor
        RHI_PhysicalDevice_Type type    = RHI_PhysicalDevice_Unknown;
        std::string name                = "Unknown";
		uint32_t memory		            = 0;
		void* data                      = nullptr;
	};

	class SPARTAN_CLASS RHI_Device
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
        const DisplayMode* GetPrimaryDisplayMode();
        bool ValidateResolution(const uint32_t width, const uint32_t height);

        // Queue
        bool Queue_Submit(const RHI_Queue_Type type, void* cmd_buffer, void* wait_semaphore = nullptr, void* wait_fence = nullptr, const uint32_t wait_flags = 0);
        bool Queue_Wait(const RHI_Queue_Type type);
        bool Queue_WaitAll();
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
        uint32_t m_physical_device_index            = 0;
        uint32_t m_display_mode_index               = 0;
        uint32_t m_enabled_graphics_shader_stages   = 0;
        bool m_initialized                          = false;
        Context* m_context                          = nullptr;
        std::mutex m_mutex_submit;
        std::mutex m_mutex_wait;  
        std::shared_ptr<RHI_Context> m_rhi_context;
	};
}
