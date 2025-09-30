/*
Copyright(c) 2015-2025 Panos Karabelas

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

//= INCLUDES ===============
#include "RHI_Definitions.h"
#include <cstring>
//==========================

namespace spartan
{
    class RHI_PhysicalDevice
    {
    public:
        RHI_PhysicalDevice(const uint32_t api_version, const uint32_t driver_version, const char* driver_info, const uint32_t vendor_id, const RHI_PhysicalDevice_Type type, const char* name, const uint64_t memory, void* data)
        {
            this->vendor_id = vendor_id;
            this->type      = type;
            this->memory    = static_cast<uint32_t>(memory / 1024 / 1024); // mb
            this->data      = data;

            // copy strings into local buffers (no heap allocations)
            strncpy_s(this->name, sizeof(this->name), name ? name : "Unknown", _TRUNCATE);
            strncpy_s(this->vendor_name, sizeof(this->vendor_name), get_vendor_name(), _TRUNCATE);
            strncpy_s(this->api_version, sizeof(this->api_version), decode_api_version(api_version), _TRUNCATE);
            strncpy_s(this->driver_version, sizeof(this->driver_version), decode_driver_version(driver_version, driver_info), _TRUNCATE);
        }

        bool IsNvidia() const
        {
            return vendor_id == 0x10DE ||
                   strstr(name, "Nvidia") != nullptr ||
                   strstr(name, "nvidia") != nullptr;
        }
        
        bool IsAmd() const
        {
            return vendor_id == 0x1002 || vendor_id == 0x1022 ||
                   strstr(name, "AMD") != nullptr ||
                   strstr(name, "amd") != nullptr;
        }
        
        bool IsIntel() const
        {
            return vendor_id == 0x8086 || vendor_id == 0x163C || vendor_id == 0x8087 ||
                   strstr(name, "Intel") != nullptr ||
                   strstr(name, "intel") != nullptr;
        }
        
        bool IsArm() const
        {
            return vendor_id == 0x13B5 ||
                   strstr(name, "Arm") != nullptr ||
                   strstr(name, "arm") != nullptr;
        }
        
        bool IsQualcomm() const
        {
            return vendor_id == 0x5143 ||
                   strstr(name, "Qualcomm") != nullptr ||
                   strstr(name, "qualcomm") != nullptr;
        }

        bool IsBelowMinimumRequirements() const
        {
            // note: we don't prevent the user from running the engine, we just show them a warning window

            const uint32_t min_memory_mb           = 4096; // minimum memory in MB, 4GB in this case
            const RHI_PhysicalDevice_Type min_type = RHI_PhysicalDevice_Type::Discrete;
            const bool is_old                      = 
                // NVIDIA GPUs older than or including 1000 series
                strstr(name, "GeForce GTX 10") != nullptr ||
                strstr(name, "GeForce GTX 9")  != nullptr ||
                strstr(name, "GeForce GTX 7")  != nullptr ||
                strstr(name, "GeForce GTX 6")  != nullptr ||
                // AMD GPUs older than or including R9, RX 400/500 series
                strstr(name, "Radeon R9")      != nullptr ||
                strstr(name, "Radeon RX 4")    != nullptr ||
                strstr(name, "Radeon RX 5")    != nullptr ||
                strstr(name, "Radeon HD")      != nullptr;
        
            return memory < min_memory_mb || type != min_type || is_old;
        }

        const char* GetName()             const { return name; }
        const char* GetDriverVersion()    const { return driver_version; }
        const char* GetApiVersion()       const { return api_version; }
        const char* GetVendorName()       const { return vendor_name; }
        uint32_t GetMemory()              const { return memory; }
        void* GetData()                   const { return data; }
        RHI_PhysicalDevice_Type GetType() const { return type; }

    private:
        const char* get_vendor_name()
        {
            if (IsNvidia())   return "Nvidia";
            if (IsAmd())      return "Amd";
            if (IsIntel())    return "Intel";
            if (IsArm())      return "Arm";
            if (IsQualcomm()) return "Qualcomm";

            return "Unknown";
        }

        const char* decode_api_version(const uint32_t version);
        const char* decode_driver_version(const uint32_t version, const char* driver_info);

        // fixed-size buffers (stack, no heap allocations)
        static const size_t buffer_size  = 128;
        char api_version[buffer_size]    = "N/A";                        // vulkan/directx/opengl api version supported
        char driver_version[buffer_size] = "N/A";                        // gpu driver version provided by vendor
        char vendor_name[buffer_size]    = "N/A";                        // gpu vendor name (e.g., nvidia, amd)
        char name[buffer_size]           = "N/A";                        // gpu device name/model
        uint32_t vendor_id               = 0;                            // vendor unique id
        RHI_PhysicalDevice_Type type     = RHI_PhysicalDevice_Type::Max; // type of device (discrete, integrated, etc.)
        uint32_t memory                  = 0;                            // total device memory in mb
        void* data                       = nullptr;                      // pointer to device-specific extra data
    };
}
