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

//= INCLUDES =====================
#include "pch.h"
#include "RHI_OpenImageDenoise.h"
#include "RHI_Device.h"
#include "RHI_Texture.h"
SP_WARNINGS_OFF
#include <OpenImageDenoise/oidn.h>
SP_WARNINGS_ON
//================================

namespace spartan
{
    namespace
    {
        OIDNDevice device = nullptr;
        OIDNFilter filter = nullptr;
    }

    void RHI_OpenImageDenoise::Initialize()
    {
        // create a device
        OIDNDeviceType device_type = RHI_Device::GetPrimaryPhysicalDevice()->IsNvidia() ? OIDN_DEVICE_TYPE_CUDA : OIDN_DEVICE_TYPE_HIP;
        OIDNDevice oid_device          = oidnNewDevice(device_type);
        oidnCommitDevice(oid_device);

        // check if the device supports OIDN_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32
        uint32_t external_memory_types = 0;
        external_memory_types = oidnGetDeviceUInt(oid_device, "externalMemoryTypes");
        if ((external_memory_types & OIDN_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32) == 0)
        {
            //SP_LOG_ERROR("The selected device does not support OIDN_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32 external memory type.");
        }

        // create a generic ray tracing filter
        filter = oidnNewFilter(oid_device, "RT");

        // set denoiser parameters
        oidnSetFilterBool(filter, "hdr", true);
        oidnCommitFilter(filter);
    }

    void RHI_OpenImageDenoise::Shutdown()
    {
        oidnReleaseFilter(filter);
        oidnReleaseDevice(device);
    }

    void RHI_OpenImageDenoise::Denoise(RHI_Texture* texture)
    {
        SP_ASSERT(texture->HasExternalMemory());

        // create buffer from texture
        OIDNBuffer buffer = oidnNewSharedBufferFromWin32Handle(
            device,
            OIDNExternalMemoryTypeFlag::OIDN_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32,
            texture->GetExternalMemoryHandle(),
            texture->GetObjectName().c_str(),
            texture->GetWidth() * texture->GetHeight() * texture->GetBytesPerPixel()
        );

        // execute the denoising filter
        oidnSetFilterImage(filter, "texture", buffer, OIDN_FORMAT_FLOAT4, texture->GetWidth(), texture->GetHeight(), 0, 0, 0);
        oidnExecuteFilter(filter);

        // release the OIDNBuffer
        oidnReleaseBuffer(buffer);

        // check for errors
        const char* error_message;
        if (oidnGetDeviceError(device, &error_message) != OIDN_ERROR_NONE)
        {
            SP_LOG_ERROR("%s", error_message);
        }
    }
}
