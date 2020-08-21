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

//= INCLUDES ==============
#include "RHI_Definition.h"
//=========================

namespace Spartan
{
    class PhysicalDevice
    {
    public:
        PhysicalDevice(const uint32_t api_version, const uint32_t driver_version, const uint32_t vendor_id, const RHI_PhysicalDevice_Type type, const char* name, const uint32_t memory, void* data)
        {
            this->vendor_id         = vendor_id;
            this->type              = type;
            this->name                = name;
            this->memory            = memory / 1024 / 1024; // mb
            this->data                = data;
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

        const std::string& GetName()            const { return name; }
        const std::string& GetDriverVersion()   const { return driver_version; }
        const std::string& GetApiVersion()      const { return api_version; }
        uint32_t GetMemory()                    const { return memory; }
        void* GetData()                         const { return data; }

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

        std::string api_version         = "Unknown"; // version of api supported by the device
        std::string driver_version      = "Unknown"; // vendor-specified version of the driver.
        uint32_t vendor_id                = 0; // unique identifier of the vendor
        RHI_PhysicalDevice_Type type    = RHI_PhysicalDevice_Unknown;
        std::string name                = "Unknown";
        uint32_t memory                    = 0;
        void* data                      = nullptr;
    };
}
