/*
Copyright(c) 2016-2024 Panos Karabelas

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
//==========================

namespace Spartan
{
    class PhysicalDevice
    {
    public:
        PhysicalDevice(const uint32_t api_version, const uint32_t driver_version, const uint32_t vendor_id, const RHI_PhysicalDevice_Type type, const char* name, const uint64_t memory, void* data)
        {
            this->vendor_id      = vendor_id;
            this->vendor_name    = get_vendor_name();
            this->type           = type;
            this->name           = name;
            this->memory         = static_cast<uint32_t>(memory / 1024 / 1024); // mb
            this->data           = data;
            this->api_version    = decode_api_version(api_version);
            this->driver_version = decode_driver_version(driver_version);
        }

        bool IsNvidia()   const { return vendor_id == 0x10DE ||                                               name.find("Nvidia")   != std::string::npos; }
        bool IsAmd()      const { return vendor_id == 0x1002 || vendor_id == 0x1022 ||                        name.find("Amd")      != std::string::npos; }
        bool IsIntel()    const { return vendor_id == 0x8086 || vendor_id == 0x163C || vendor_id == 0x8087 || name.find("Intel")    != std::string::npos;}
        bool IsArm()      const { return vendor_id == 0x13B5 ||                                               name.find("Arm,")     != std::string::npos; }
        bool IsQualcomm() const { return vendor_id == 0x5143 ||                                               name.find("Qualcomm") != std::string::npos; }

        bool IsBelowMinimumRequirments()
        {
            // minimum requirements
            const uint32_t min_memory_mb           = 4096; // minimum memory in MB, 4GB in this case
            const RHI_PhysicalDevice_Type min_type = RHI_PhysicalDevice_Type::Discrete;

            return memory < min_memory_mb || type != min_type;
        }

        const std::string& GetName()          const { return name; }
        const std::string& GetDriverVersion() const { return driver_version; }
        const std::string& GetApiVersion()    const { return api_version; }
        const std::string& GetVendorName()    const { return vendor_name; }
        uint32_t GetMemory()                  const { return memory; }
        void* GetData()                       const { return data; }
        RHI_PhysicalDevice_Type GetType()     const { return type; }

    private:
        const char* get_vendor_name()
        {
            if (IsNvidia())
                return "Nvidia";

            if (IsAmd())
               return "Amd";

            if (IsIntel())
                return "Intel";

            if (IsArm())
             return "Arm";

            if (IsQualcomm())
                return "Qualcomm";

            return "Unknown";
        }

        std::string decode_api_version(const uint32_t version);
        std::string decode_driver_version(const uint32_t version);

        std::string api_version      = "Unknown"; // version of api supported by the device
        std::string driver_version   = "Unknown"; // vendor-specified version of the driver
        uint32_t vendor_id           = 0;         // unique identifier of the vendor
        std::string vendor_name      = "Unknown";
        RHI_PhysicalDevice_Type type = RHI_PhysicalDevice_Type::Max;
        std::string name             = "Unknown";
        uint32_t memory              = 0;
        void* data                   = nullptr;
    };
}
