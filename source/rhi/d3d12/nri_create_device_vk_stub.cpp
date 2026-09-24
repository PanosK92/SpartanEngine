/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =======================
#include "pch.h"
SP_WARNINGS_OFF
#include "NRI.h"
#include "Extensions/NRIWrapperVK.h"
SP_WARNINGS_ON
//==================================

// static nri.lib / nri_validation reference vk helpers, d3d12 does not link nri_vk (it needs vma)
namespace nri
{
    struct DeviceBase;

    QueryType GetQueryTypeVK(uint32_t)
    {
        return QueryType::TIMESTAMP;
    }

    uint32_t NRIFormatToVKFormat(Format)
    {
        return 0; // VK_FORMAT_UNDEFINED
    }
}

nri::Result CreateDeviceVK(const nri::DeviceCreationDesc&, const nri::DeviceCreationVKDesc&, nri::DeviceBase*&)
{
    return nri::Result::UNSUPPORTED;
}
